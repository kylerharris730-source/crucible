#include "audio.h"

#define SOUND_NAME(id, path, cooldown) #id,
static const char* cueNames[SFX_COUNT] = { SOUND_CUES(SOUND_NAME) };
#undef SOUND_NAME
const char* audioCueName(SoundId id) {
    return id >= 0 && id < SFX_COUNT ? cueNames[id] : "unknown sound";
}

static bool muted = false;
bool audioMuted() { return muted; }

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <stdint.h>
#include <math.h>

/* Every registered cue travels inside the executable. Loose WAVs in res/sfx
   override them during development. */
#include "audio_embedded.h"

struct Sample { std::vector<int16_t> pcm; };
struct Voice {
    HWAVEOUT out;
    WAVEHDR header;
    std::vector<int16_t> pcm;
    SoundId id;
    bool active;
};
static Sample samples[SFX_COUNT];
static Voice voices[12];
static DWORD lastPlay[SFX_COUNT];
static bool played[SFX_COUNT];
static bool ready;
static float listenerX, listenerY;
static uint32_t miningPitchState = 0xC2A65u;
#define SOUND_PATH(id, path, cooldown) path,
static const char* paths[SFX_COUNT] = { SOUND_CUES(SOUND_PATH) };
#undef SOUND_PATH
#define SOUND_COOLDOWN(id, path, cooldown) cooldown,
static const DWORD cooldownMs[SFX_COUNT] = { SOUND_CUES(SOUND_COOLDOWN) };
#undef SOUND_COOLDOWN

static uint16_t u16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t u32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool decodeWav(const unsigned char* p, size_t n, Sample& sample) {
    if (n < 44 || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) return false;
    bool formatOk = false;
    const unsigned char* data = 0;
    size_t bytes = 0;
    for (size_t at = 12; at + 8 <= n;) {
        const uint32_t len = u32(p + at + 4);
        at += 8;
        if (len > n - at) return false;
        if (!memcmp(p + at - 8, "fmt ", 4) && len >= 16)
            formatOk = u16(p + at) == 1 && u16(p + at + 2) == 1 &&
                       u32(p + at + 4) == 22050 && u16(p + at + 14) == 16;
        if (!memcmp(p + at - 8, "data", 4)) { data = p + at; bytes = len; }
        at += len + (len & 1);
    }
    if (!formatOk || !data || (bytes & 1)) return false;
    sample.pcm.resize(bytes / 2);
    for (size_t i = 0; i < sample.pcm.size(); ++i)
        sample.pcm[i] = (int16_t)u16(data + i * 2);
    return !sample.pcm.empty();
}

static bool loadFile(const char* name, Sample& sample) {
    char path[MAX_PATH];
    const char* roots[] = { "res/sfx/", "../res/sfx/" };
    for (int i = 0; i < 2; ++i) {
        snprintf(path, sizeof(path), "%s%s", roots[i], name);
        FILE* f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        const long length = ftell(f);
        rewind(f);
        if (length < 44 || length > 4 * 1024 * 1024) { fclose(f); continue; }
        std::vector<unsigned char> bytes((size_t)length);
        const bool read = fread(&bytes[0], 1, bytes.size(), f) == bytes.size();
        fclose(f);
        if (read && decodeWav(&bytes[0], bytes.size(), sample)) return true;
    }
    return false;
}

void audioInit() {
    if (ready) return;
    LARGE_INTEGER clock;
    if (QueryPerformanceCounter(&clock))
        miningPitchState ^= (uint32_t)clock.QuadPart ^ (uint32_t)(clock.QuadPart >> 32);
    if (!miningPitchState) miningPitchState = 0xC2A65u;
    for (int i = 0; i < SFX_COUNT; ++i) {
        if (!loadFile(paths[i], samples[i]) && i < EMBEDDED_SFX_COUNT)
            decodeWav(EMBEDDED_SFX[i].data, EMBEDDED_SFX[i].size, samples[i]);
    }
    ready = true;
}

void audioUpdate() {
    for (int i = 0; i < 12; ++i) {
        Voice& v = voices[i];
        if (!v.active || !(v.header.dwFlags & WHDR_DONE)) continue;
        waveOutUnprepareHeader(v.out, &v.header, sizeof(v.header));
        waveOutClose(v.out);
        v.out = 0;
        v.pcm.clear();
        v.active = false;
    }
}

static int priority(SoundId id) {
    if (id == SFX_PLAYER_DAMAGE || id == SFX_PLAYER_DEATH ||
        id == SFX_BOSS_PHASE || id == SFX_BOSS_DEFEAT ||
        id == SFX_ROCKET_IGNITE || id == SFX_VICTORY) return 3;
    if ((id >= SFX_TOOL_FIRE_LIGHT && id <= SFX_MELEE_HIT) ||
        (id >= SFX_BOSS_BROOD_CALL && id <= SFX_BOSS_EFFIGY_CALL) ||
        id == SFX_EXPLOSION) return 2;
    if ((id >= SFX_STEP_EARTH && id <= SFX_STEP_WET) ||
        id == SFX_CAVE_DRIP || id == SFX_SURFACE_WIND ||
        id == SFX_FIRE_CRACKLE || id == SFX_LAVA_BUBBLE) return 0;
    return 1;
}

static float miningPlaybackPitch() {
    miningPitchState ^= miningPitchState << 13;
    miningPitchState ^= miningPitchState >> 17;
    miningPitchState ^= miningPitchState << 5;
    return 0.94f + 0.12f * (float)(miningPitchState & 0xffffu) / 65535.0f;
}

static void play(SoundId id, float gain, float pan) {
    if (muted || !ready || id < 0 || id >= SFX_COUNT || samples[id].pcm.empty()) return;
    const DWORD now = timeGetTime();
    if (played[id] && now - lastPlay[id] < cooldownMs[id]) return;
    audioUpdate();
    Voice* v = 0;
    for (int i = 0; i < 12; ++i) if (!voices[i].active) { v = &voices[i]; break; }
    if (!v) {
        int weakest = -1;
        for (int i = 0; i < 12; ++i)
            if (priority(id) > priority(voices[i].id) &&
                (weakest < 0 || priority(voices[i].id) < priority(voices[weakest].id)))
                weakest = i;
        if (weakest >= 0) {
            v = &voices[weakest];
            waveOutReset(v->out);
            waveOutUnprepareHeader(v->out, &v->header, sizeof(v->header));
            waveOutClose(v->out);
            v->pcm.clear(); v->active = false;
        }
    }
    if (!v) return;
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    const std::vector<int16_t>& src = samples[id].pcm;
    /* Vary only the ordinary mining scrape; the hard-material ding stays
       recognizable. Resampling keeps the output device at a fixed 22050 Hz. */
    const double pitch = id == SFX_MINE ? miningPlaybackPitch() : 1.0;
    const size_t frames = (size_t)ceil((double)src.size() / pitch);
    v->pcm.resize(frames * 2);
    const float left = pan > 0.0f ? 1.0f - pan : 1.0f;
    const float right = pan < 0.0f ? 1.0f + pan : 1.0f;
    for (size_t i = 0; i < frames; ++i) {
        const double position = (double)i * pitch;
        const size_t at = (size_t)position < src.size() ? (size_t)position : src.size() - 1;
        float sample = (float)src[at];
        if (at + 1 < src.size())
            sample += (float)((src[at + 1] - sample) * (position - at));
        v->pcm[i * 2] = (int16_t)(sample * gain * left);
        v->pcm[i * 2 + 1] = (int16_t)(sample * gain * right);
    }
    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = 22050;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = 22050 * 4;
    if (waveOutOpen(&v->out, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        v->pcm.clear(); return;
    }
    memset(&v->header, 0, sizeof(v->header));
    v->header.lpData = (LPSTR)&v->pcm[0];
    v->header.dwBufferLength = (DWORD)(v->pcm.size() * sizeof(int16_t));
    if (waveOutPrepareHeader(v->out, &v->header, sizeof(v->header)) != MMSYSERR_NOERROR) {
        waveOutClose(v->out); v->pcm.clear(); return;
    }
    v->active = true;
    v->id = id;
    if (waveOutWrite(v->out, &v->header, sizeof(v->header)) != MMSYSERR_NOERROR) {
        waveOutUnprepareHeader(v->out, &v->header, sizeof(v->header));
        waveOutClose(v->out); v->pcm.clear(); v->active = false; return;
    }
    lastPlay[id] = now;
    played[id] = true;
}

void audioPlay(SoundId id, float gain) { play(id, gain, 0.0f); }

void audioSetListener(float x, float y) { listenerX = x; listenerY = y; }

void audioPlayAt(SoundId id, float x, float y, float gain) {
    const float dx = x - listenerX, dy = y - listenerY;
    const float distance = sqrtf(dx * dx + dy * dy);
    if (distance >= 360.0f) return;
    const float fade = distance < 64.0f ? 1.0f : (360.0f - distance) / 296.0f;
    float pan = dx / 240.0f;
    if (pan < -0.75f) pan = -0.75f;
    if (pan > 0.75f) pan = 0.75f;
    play(id, gain * fade, pan);
}

void audioShutdown() {
    if (!ready) return;
    for (int i = 0; i < 12; ++i) {
        Voice& v = voices[i];
        if (!v.active) continue;
        waveOutReset(v.out);
        waveOutUnprepareHeader(v.out, &v.header, sizeof(v.header));
        waveOutClose(v.out);
        v.pcm.clear(); v.active = false;
    }
    ready = false;
}

void audioSetMuted(bool value) {
    if (muted == value) return;
    muted = value;
    if (!muted || !ready) return;
    for (int i = 0; i < 12; ++i) {
        Voice& v = voices[i];
        if (!v.active) continue;
        waveOutReset(v.out);
        waveOutUnprepareHeader(v.out, &v.header, sizeof(v.header));
        waveOutClose(v.out);
        v.out = 0;
        v.pcm.clear();
        v.active = false;
    }
}

#else
/* The browser build keeps the event API; its playback backend comes next. */
void audioInit() {}
void audioUpdate() {}
void audioShutdown() {}
void audioSetMuted(bool value) { muted = value; }
void audioPlay(SoundId, float) {}
void audioSetListener(float, float) {}
void audioPlayAt(SoundId, float, float, float) {}
#endif
