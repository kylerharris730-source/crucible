#include "audio.h"

#define SOUND_NAME(id, path, cooldown) #id,
static const char* cueNames[SFX_COUNT] = { SOUND_CUES(SOUND_NAME) };
#undef SOUND_NAME
const char* audioCueName(SoundId id) {
    return id >= 0 && id < SFX_COUNT ? cueNames[id] : "unknown sound";
}

#ifdef __EMSCRIPTEN__
/* A fresh browser visit is silent until the player opts in. */
static bool muted = true;
#else
static bool muted = false;
#endif
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

/* ============================================================================
   One device, mixed in software.

   Every sound used to open a waveOut device of its own and close it when it
   finished. Those two calls go through the whole Windows audio stack and the
   sound card's driver, and they are slow: measured on the development
   machine, opening took 16.5 ms on average and up to 53 ms, closing about
   10 ms -- a whole frame or more each, on the game's own thread. Nothing
   else stood out, because on that machine it was one dropped frame at a
   time. On another machine with a slower audio driver (a USB or Bluetooth
   headset, a vendor "enhancement" layer, a device that must resample 22 kHz)
   it was reported as the game stuttering every few seconds in single player:
   the ambient wind or drip plays every eight seconds standing still, and
   hitched once when it started and again when it ended.

   So the device is opened ONCE, at audioInit, and fed by a thread of its own
   that mixes every playing voice into a small ring of buffers. Playing a
   sound on the game's thread is now a copy into a voice slot under a lock.
   No driver call happens on the game's thread after startup.

   Win32 threads and a critical section rather than std::thread: the test
   suite still builds this file with a GCC whose C++11 threading is missing,
   like the lane pool in world.cpp.
   ========================================================================== */

struct Sample { std::vector<int16_t> pcm; };
struct Voice {
    std::vector<int16_t> pcm;      /* stereo, interleaved, gain and pan applied */
    size_t at;                     /* next frame to mix                         */
    SoundId id;
    bool active;
};

static const int OUT_RATE = 22050;
static const int VOICES = 12;
/* Four buffers of 368 frames (~16.7 ms each): about 67 ms queued ahead. Short
   enough that a hit is heard with its flash, long enough that the mixer
   thread can be late by three buffers before anything is audible. */
static const int MIX_BUFFERS = 4;
static const int MIX_FRAMES = 368;

static Sample samples[SFX_COUNT];
static Voice voices[VOICES];
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

/* The device and its mixer. `g_voiceLock` guards voices[]: the game thread
   starts and steals voices, the mixer thread advances and retires them. */
static HWAVEOUT g_out = 0;
static WAVEHDR g_hdr[MIX_BUFFERS];
static int16_t g_buf[MIX_BUFFERS][MIX_FRAMES * 2];
static HANDLE g_mixEvent = 0, g_mixThread = 0;
static volatile LONG g_mixStop = 0;
static CRITICAL_SECTION g_voiceLock;

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

/* Sum every active voice into one buffer. Clamped rather than scaled: the
   old one-device-per-voice design let Windows sum them the same way, so the
   mix sounds as it did. */
static void mixInto(int16_t* out) {
    static int32_t acc[MIX_FRAMES * 2];
    memset(acc, 0, sizeof(acc));
    EnterCriticalSection(&g_voiceLock);
    for (int i = 0; i < VOICES; ++i) {
        Voice& v = voices[i];
        if (!v.active) continue;
        const size_t frames = v.pcm.size() / 2;
        size_t n = frames - v.at;
        if (n > (size_t)MIX_FRAMES) n = MIX_FRAMES;
        const int16_t* src = &v.pcm[v.at * 2];
        for (size_t k = 0; k < n * 2; ++k) acc[k] += src[k];
        v.at += n;
        if (v.at >= frames) v.active = false;   /* pcm kept: reused by the next sound */
    }
    LeaveCriticalSection(&g_voiceLock);
    for (int k = 0; k < MIX_FRAMES * 2; ++k)
        out[k] = (int16_t)(acc[k] > 32767 ? 32767 : acc[k] < -32768 ? -32768 : acc[k]);
}

static DWORD WINAPI mixerMain(LPVOID) {
    /* Above the sim's lane threads, which can have every core busy: a mixer
       that misses its turn is a click, where a late frame is only a frame. */
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    while (!g_mixStop) {
        for (int b = 0; b < MIX_BUFFERS; ++b) {
            if (!(g_hdr[b].dwFlags & WHDR_DONE)) continue;
            mixInto(g_buf[b]);
            g_hdr[b].dwFlags &= ~WHDR_DONE;
            waveOutWrite(g_out, &g_hdr[b], sizeof(g_hdr[b]));
        }
        /* The device signals the event as each buffer finishes; the timeout
           only matters if a signal is ever missed. */
        WaitForSingleObject(g_mixEvent, 50);
    }
    return 0;
}

static bool openDevice() {
    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = OUT_RATE;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = OUT_RATE * 4;
    g_mixEvent = CreateEventA(0, FALSE, FALSE, 0);
    if (!g_mixEvent) return false;
    if (waveOutOpen(&g_out, WAVE_MAPPER, &format, (DWORD_PTR)g_mixEvent, 0,
                    CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(g_mixEvent); g_mixEvent = 0; g_out = 0;
        return false;
    }
    for (int b = 0; b < MIX_BUFFERS; ++b) {
        memset(&g_hdr[b], 0, sizeof(g_hdr[b]));
        memset(g_buf[b], 0, sizeof(g_buf[b]));
        g_hdr[b].lpData = (LPSTR)g_buf[b];
        g_hdr[b].dwBufferLength = sizeof(g_buf[b]);
        waveOutPrepareHeader(g_out, &g_hdr[b], sizeof(g_hdr[b]));
        /* Marked done so the mixer's first pass fills and queues all four. */
        g_hdr[b].dwFlags |= WHDR_DONE;
    }
    g_mixStop = 0;
    g_mixThread = CreateThread(0, 0, mixerMain, 0, 0, 0);
    if (!g_mixThread) {
        for (int b = 0; b < MIX_BUFFERS; ++b) waveOutUnprepareHeader(g_out, &g_hdr[b], sizeof(g_hdr[b]));
        waveOutClose(g_out); g_out = 0;
        CloseHandle(g_mixEvent); g_mixEvent = 0;
        return false;
    }
    return true;
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
    InitializeCriticalSection(&g_voiceLock);
    /* No device -- none installed, or it is busy -- means a silent game, not
       a broken one: every play() below checks `ready`. */
    ready = openDevice();
    if (!ready) DeleteCriticalSection(&g_voiceLock);
}

/* Nothing to do per frame any more: finished voices are retired by the mixer.
   Kept so the game loop's call stays valid. */
void audioUpdate() {}

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
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;

    /* Rendered outside the lock -- resampling a long cue is the only real
       work here, and the mixer should never wait on it. */
    const std::vector<int16_t>& src = samples[id].pcm;
    /* Vary only the ordinary mining scrape; the hard-material ding stays
       recognizable. Resampling keeps the output device at a fixed 22050 Hz. */
    const double pitch = id == SFX_MINE ? miningPlaybackPitch() : 1.0;
    const size_t frames = (size_t)ceil((double)src.size() / pitch);
    std::vector<int16_t> pcm(frames * 2);
    const float left = pan > 0.0f ? 1.0f - pan : 1.0f;
    const float right = pan < 0.0f ? 1.0f + pan : 1.0f;
    for (size_t i = 0; i < frames; ++i) {
        const double position = (double)i * pitch;
        const size_t at = (size_t)position < src.size() ? (size_t)position : src.size() - 1;
        float sample = (float)src[at];
        if (at + 1 < src.size())
            sample += (float)((src[at + 1] - sample) * (position - at));
        pcm[i * 2] = (int16_t)(sample * gain * left);
        pcm[i * 2 + 1] = (int16_t)(sample * gain * right);
    }

    EnterCriticalSection(&g_voiceLock);
    Voice* v = 0;
    for (int i = 0; i < VOICES; ++i) if (!voices[i].active) { v = &voices[i]; break; }
    if (!v) {
        /* All twelve busy: steal the least important, if this one outranks
           it. Same rule as before; stealing is now free. */
        int weakest = -1;
        for (int i = 0; i < VOICES; ++i)
            if (priority(id) > priority(voices[i].id) &&
                (weakest < 0 || priority(voices[i].id) < priority(voices[weakest].id)))
                weakest = i;
        if (weakest >= 0) v = &voices[weakest];
    }
    if (v) {
        v->pcm.swap(pcm);
        v->at = 0;
        v->id = id;
        v->active = true;
    }
    LeaveCriticalSection(&g_voiceLock);
    if (!v) return;
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
    ready = false;
    InterlockedExchange(&g_mixStop, 1);
    SetEvent(g_mixEvent);
    WaitForSingleObject(g_mixThread, 1000);
    CloseHandle(g_mixThread); g_mixThread = 0;
    waveOutReset(g_out);
    for (int b = 0; b < MIX_BUFFERS; ++b) waveOutUnprepareHeader(g_out, &g_hdr[b], sizeof(g_hdr[b]));
    waveOutClose(g_out); g_out = 0;
    CloseHandle(g_mixEvent); g_mixEvent = 0;
    DeleteCriticalSection(&g_voiceLock);
}

/* Called from the saved settings before audioInit, too -- which is why
   silencing the voices waits for `ready`. */
void audioSetMuted(bool value) {
    if (muted == value) return;
    muted = value;
    if (!muted || !ready) return;
    EnterCriticalSection(&g_voiceLock);
    for (int i = 0; i < VOICES; ++i) voices[i].active = false;
    LeaveCriticalSection(&g_voiceLock);
}

#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <math.h>
#include <stdint.h>
#include "audio_embedded.h"

#define SOUND_COOLDOWN(id, path, cooldown) cooldown,
static const int cooldownMs[SFX_COUNT] = { SOUND_CUES(SOUND_COOLDOWN) };
#undef SOUND_COOLDOWN

static bool ready = false;
static double lastPlay[SFX_COUNT];
static bool played[SFX_COUNT];
static float listenerX, listenerY;
static uint32_t miningPitchState = 0xC2A65u;

/* Keep the sound bank in the wasm: there are no loose asset requests to race
   the first menu click, and the same embedded WAVs ship in the Windows build. */
EM_JS(int, webAudioInit, (), {
    const Audio = window.AudioContext || window.webkitAudioContext;
    if (!Audio) return 0;
    try {
        const ctx = new Audio();
        const bank = window.__cinderliftAudio = {
            ctx: ctx, samples: [], voices: [], muted: true
        };
        const resume = () => {
            if (ctx.state === 'suspended') ctx.resume().catch(() => {});
        };
        /* SDL dispatches the click to the game on a later frame. Resume in
           the DOM gesture itself, before the game handles the mute button. */
        document.addEventListener('pointerdown', resume, true);
        document.addEventListener('keydown', resume, true);
        document.addEventListener('touchend', resume, true);
        bank.resume = resume;
        return 1;
    } catch (e) {
        console.warn('Cinderlift audio unavailable:', e);
        return 0;
    }
});

EM_JS(int, webAudioRegister, (int id, const unsigned char* ptr, int size), {
    const bank = window.__cinderliftAudio;
    if (!bank || size < 44) return 0;
    const bytes = HEAPU8.subarray(ptr, ptr + size);
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const fourcc = (at, value) =>
        bytes[at] === value.charCodeAt(0) && bytes[at + 1] === value.charCodeAt(1) &&
        bytes[at + 2] === value.charCodeAt(2) && bytes[at + 3] === value.charCodeAt(3);
    if (!fourcc(0, 'RIFF') || !fourcc(8, 'WAVE')) return 0;
    let formatOk = false, dataAt = -1, dataSize = 0;
    for (let at = 12; at + 8 <= size;) {
        const len = view.getUint32(at + 4, true);
        const start = at + 8;
        if (len > size - start) return 0;
        if (fourcc(at, 'fmt ') && len >= 16)
            formatOk = view.getUint16(start, true) === 1 &&
                view.getUint16(start + 2, true) === 1 &&
                view.getUint32(start + 4, true) === 22050 &&
                view.getUint16(start + 14, true) === 16;
        if (fourcc(at, 'data')) { dataAt = start; dataSize = len; }
        at = start + len + (len & 1);
    }
    if (!formatOk || dataAt < 0 || !dataSize || (dataSize & 1)) return 0;
    const count = dataSize / 2;
    const buffer = bank.ctx.createBuffer(1, count, 22050);
    const channel = buffer.getChannelData(0);
    for (let i = 0; i < count; ++i)
        channel[i] = view.getInt16(dataAt + i * 2, true) / 32768;
    bank.samples[id] = buffer;
    return 1;
});

EM_JS(int, webAudioPlay, (int id, float gain, float pan, float rate, int priority), {
    const bank = window.__cinderliftAudio;
    if (!bank || bank.muted || bank.ctx.state !== 'running' || !bank.samples[id]) return 0;
    if (bank.voices.length >= 12) {
        let weakest = -1;
        for (let i = 0; i < bank.voices.length; ++i)
            if (bank.voices[i].priority < priority &&
                (weakest < 0 || bank.voices[i].priority < bank.voices[weakest].priority))
                weakest = i;
        if (weakest < 0) return 0;
        const stolen = bank.voices.splice(weakest, 1)[0];
        stolen.source.stop();
    }
    const ctx = bank.ctx;
    const source = ctx.createBufferSource();
    source.buffer = bank.samples[id];
    source.playbackRate.value = rate;
    const left = ctx.createGain(), right = ctx.createGain();
    left.gain.value = gain * (pan > 0 ? 1 - pan : 1);
    right.gain.value = gain * (pan < 0 ? 1 + pan : 1);
    const merger = ctx.createChannelMerger(2);
    source.connect(left); source.connect(right);
    left.connect(merger, 0, 0); right.connect(merger, 0, 1);
    merger.connect(ctx.destination);
    const voice = { source: source, priority: priority };
    bank.voices.push(voice);
    source.onended = () => {
        const at = bank.voices.indexOf(voice);
        if (at >= 0) bank.voices.splice(at, 1);
        source.disconnect(); left.disconnect(); right.disconnect(); merger.disconnect();
    };
    source.start();
    return 1;
});

EM_JS(void, webAudioMute, (int value), {
    const hint = document.getElementById('soundHint');
    if (hint) hint.textContent = value ? 'Sound: off (Esc menu)' : 'Sound: on (Esc menu)';
    const bank = window.__cinderliftAudio;
    if (bank) {
        bank.muted = !!value;
        if (value) {
            for (const voice of bank.voices) voice.source.stop();
            bank.voices.length = 0;
        } else {
            bank.resume();
        }
    }
    try { localStorage.setItem('cinderlift.sound_muted', value ? '1' : '0'); }
    catch (e) { /* Site data may be disabled; the current session still works. */ }
});

EM_JS(void, webAudioShutdown, (), {
    const bank = window.__cinderliftAudio;
    if (!bank) return;
    for (const voice of bank.voices) voice.source.stop();
    bank.voices.length = 0;
    bank.ctx.close().catch(() => {});
    window.__cinderliftAudio = null;
});

void audioInit() {
    if (ready) return;
    ready = webAudioInit() != 0;
    if (!ready) return;
    webAudioMute(muted ? 1 : 0);
    for (int i = 0; i < SFX_COUNT && i < EMBEDDED_SFX_COUNT; ++i)
        webAudioRegister(i, EMBEDDED_SFX[i].data, EMBEDDED_SFX[i].size);
    miningPitchState ^= (uint32_t)emscripten_get_now();
    if (!miningPitchState) miningPitchState = 0xC2A65u;
}
void audioUpdate() {}
void audioShutdown() { if (ready) { webAudioShutdown(); ready = false; } }
void audioSetMuted(bool value) {
    if (muted == value) return;
    muted = value;
    webAudioMute(value ? 1 : 0);
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
    if (muted || !ready || id < 0 || id >= SFX_COUNT) return;
    const double now = emscripten_get_now();
    if (played[id] && now - lastPlay[id] < cooldownMs[id]) return;
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    const float pitch = id == SFX_MINE ? miningPlaybackPitch() : 1.0f;
    if (!webAudioPlay(id, gain, pan, pitch, priority(id))) return;
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

#else
void audioInit() {}
void audioUpdate() {}
void audioShutdown() {}
void audioSetMuted(bool value) { muted = value; }
void audioPlay(SoundId, float) {}
void audioSetListener(float, float) {}
void audioPlayAt(SoundId, float, float, float) {}
#endif
