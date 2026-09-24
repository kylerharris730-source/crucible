"""Build the complete, deterministic 8-bit SFX sketch library.

Reads the cue registry in src/audio.h. The four hand-tuned signatures remain
owned by generate_signature_sfx.py; every other cue gets a small authored recipe
from a shared pulse/noise palette. No external Python packages are required.
"""
from __future__ import annotations

import math
import random
import re
import struct
import wave
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "res" / "sfx"
RATE = 22050
TAU = 2 * math.pi
SIGNATURES = {"MINE", "MINE_TOO_HARD", "PLAYER_DAMAGE", "MACHINE"}


class Synth:
    def __init__(self, name: str, duration: float):
        self.name = name
        self.data = [0.0] * round(duration * RATE)
        self.rng = random.Random(zlib.crc32(name.encode("ascii")))

    def tone(self, start: float, duration: float, first: float, last: float,
             amp: float = 0.3, shape: str = "pulse", duty: float = 0.33,
             decay: float = 2.8, vibrato: float = 0.0):
        offset = round(start * RATE)
        count = min(round(duration * RATE), len(self.data) - offset)
        if count <= 0:
            return
        phase = 0.0
        for j in range(count):
            u = j / max(1, count - 1)
            hz = first * (last / first) ** u
            if vibrato:
                hz *= 1 + vibrato * math.sin(TAU * 17 * j / RATE)
            phase += hz / RATE
            p = phase % 1.0
            if shape == "sine":
                carrier = math.sin(TAU * phase)
            elif shape == "triangle":
                carrier = 1 - 4 * abs(p - 0.5)
            elif shape == "square":
                carrier = 1.0 if p < 0.5 else -1.0
            else:
                carrier = 1.0 if p < duty else -1.0
            attack = min(1.0, j / (RATE * 0.003))
            env = attack * math.exp(-decay * u) * min(1.0, (count - j) / (RATE * 0.006))
            self.data[offset + j] += amp * carrier * env

    def grit(self, start: float, duration: float, amp: float = 0.3,
             grain: int = 4, decay: float = 4.0, lowpass: float = 0.28):
        offset = round(start * RATE)
        count = min(round(duration * RATE), len(self.data) - offset)
        if count <= 0:
            return
        hold = smooth = 0.0
        for j in range(count):
            if j % grain == 0:
                hold = self.rng.uniform(-1, 1)
            smooth += lowpass * (hold - smooth)
            u = j / max(1, count - 1)
            env = math.exp(-decay * u) * min(1.0, (count - j) / (RATE * 0.005))
            self.data[offset + j] += amp * (0.55 * hold + 0.45 * smooth) * env

    def clicks(self, times: list[float], amp: float = 0.25, low: bool = False):
        for t in times:
            self.grit(t, 0.025 if low else 0.013, amp, 6 if low else 2, 8.0)
            self.tone(t, 0.040 if low else 0.025,
                      110 if low else 620, 70 if low else 280,
                      amp * 0.45, "triangle", decay=5.0)

    def write(self, path: Path, peak: float):
        maximum = max(abs(v) for v in self.data)
        if maximum < 0.001:
            raise ValueError(f"silent cue: {self.name}")
        scale = peak / maximum
        fade = round(RATE * 0.002)
        pcm = bytearray()
        for i, value in enumerate(self.data):
            edge = min(1.0, i / fade, (len(self.data) - 1 - i) / fade)
            pcm += struct.pack("<h", round(32767 * max(-1, min(1, value * scale * edge))))
        with wave.open(str(path), "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(RATE)
            wav.writeframes(pcm)


def build(name: str) -> tuple[Synth, float]:
    seed = zlib.crc32(name.encode("ascii"))
    pitch = 0.88 + (seed % 29) / 100
    if name.startswith("UI_"):
        s = Synth(name, 0.20 if name in {"UI_SAVE", "UI_LOAD", "UI_CRAFT"} else 0.12)
        base = (470 if name in {"UI_ERROR", "UI_BACK"} else 690) * pitch
        direction = 0.74 if name in {"UI_ERROR", "UI_BACK", "UI_PAUSE"} else 1.18
        s.tone(0, 0.075, base, base * direction, 0.28, "pulse", 0.22)
        s.clicks([0], 0.10)
        if name in {"UI_CRAFT", "UI_SAVE", "UI_LOAD"}:
            s.tone(0.075, 0.105, base * direction, base * direction * 1.20,
                   0.20, "triangle")
        if name == "UI_ERROR":
            s.tone(0.027, 0.085, base * 0.71, base * 0.62, 0.16, "square")
        return s, 0.44

    if name.startswith("STEP_"):
        s = Synth(name, 0.17)
        grain = {"STEP_EARTH": 11, "STEP_STONE": 7, "STEP_METAL": 5, "STEP_WET": 14}[name]
        s.grit(0, 0.14, 0.28, grain, 6, lowpass=0.14)
        s.tone(0, 0.07, 85 * pitch, 48 * pitch, 0.24, "sine")
        if name == "STEP_METAL":
            s.tone(0.018, 0.10, 540, 370, 0.055, "sine")
        if name == "STEP_WET":
            s.grit(0.040, 0.10, 0.12, 18, 3, lowpass=0.12)
        # A soft weight transfer keeps the first noise sample from clicking.
        for i in range(min(len(s.data), round(0.016 * RATE))):
            s.data[i] *= i / (0.016 * RATE)
        return s, 0.28

    if name == "PLAYER_LAND":
        s = Synth(name, 0.17)
        s.tone(0, 0.12, 115, 58, 0.28, "sine", decay=4.5)
        s.grit(0, 0.10, 0.15, 10, 6, lowpass=0.14)
        return s, 0.35

    if name.startswith("PLAYER_") or name in {"EQUIP", "THROW"}:
        duration = 0.55 if name in {"PLAYER_DEATH", "PLAYER_RESPAWN"} else 0.26
        s = Synth(name, duration)
        if name in {"PLAYER_JUMP", "PLAYER_RESPAWN", "PLAYER_HEAL"}:
            s.tone(0, duration * 0.8, 185 * pitch, 450 * pitch, 0.27, "pulse", 0.27)
            s.tone(0.035, duration * 0.7, 93 * pitch, 210 * pitch, 0.16, "sine")
        elif name in {"PLAYER_LAND", "PLAYER_FALL_HURT"}:
            s.grit(0, 0.18, 0.38, 5, 4)
            s.tone(0, 0.20, 150, 54, 0.34, "sine")
        elif name == "PLAYER_DEATH":
            for j in range(4):
                s.tone(j * 0.09, 0.18, (420 - j * 64) * pitch,
                       (240 - j * 40) * pitch, 0.23, "pulse", 0.20)
            s.grit(0, 0.16, 0.2, 7)
        elif name in {"PLAYER_BREATH_LOW", "PLAYER_BURN", "PLAYER_FREEZE"}:
            s.tone(0, 0.21, 350 * pitch, 260 * pitch, 0.22, "square")
            s.grit(0, 0.16, 0.12, 7)
        else:
            s.clicks([0], 0.30, name == "THROW")
            s.tone(0.015, 0.18, 380 * pitch, 150 * pitch, 0.20, "triangle")
        return s, 0.62 if name in {"PLAYER_DEATH", "PLAYER_FALL_HURT"} else 0.48

    if name.startswith("MINE_") or name in {"HARVEST", "DEVICE_PICKUP"}:
        s = Synth(name, 0.25)
        s.grit(0, 0.18, 0.44, 7 if name == "MINE_BACKGROUND" else 11, 4)
        s.clicks([0, 0.07], 0.18, True)
        if name == "DEVICE_PICKUP":
            s.tone(0.03, 0.17, 210, 370, 0.20, "triangle")
        return s, 0.55

    if name.startswith("PLACE_") or name.startswith("DOOR_") or name.startswith("CHEST_"):
        s = Synth(name, 0.20)
        s.grit(0, 0.11, 0.34, 12 if name == "PLACE_LIQUID" else 5, 5)
        s.tone(0, 0.11, 200 * pitch, 75 * pitch, 0.23, "triangle")
        if "METAL" in name or name.startswith("DOOR_"):
            s.tone(0.035, 0.13, 730 * pitch, 490 * pitch, 0.16, "sine")
        if name.endswith("CLOSE"):
            s.clicks([0.075], 0.22, True)
        return s, 0.48

    if name == "TOOL_FIRE_LIGHT":
        s = Synth(name, 0.22)
        s.tone(0, 0.17, 410 * pitch, 135 * pitch, 0.30, "triangle", decay=3.5)
        s.tone(0, 0.13, 125, 72, 0.17, "sine", decay=3.0)
        s.grit(0, 0.11, 0.13, 7, 5, lowpass=0.13)
        for i in range(min(len(s.data), round(0.009 * RATE))):
            s.data[i] *= i / (0.009 * RATE)
        return s, 0.43

    if name == "MELEE_SWING":
        s = Synth(name, 0.16)
        held = low = high = 0.0
        for i in range(len(s.data)):
            u = i / max(1, len(s.data) - 1)
            if i % 4 == 0:
                held = s.rng.uniform(-1.0, 1.0)
            low += 0.055 * (held - low)
            high += 0.25 * (held - high)
            envelope = math.sin(math.pi * u) ** 2
            s.data[i] += (high - low) * envelope * 0.17
        # A short blade flick gives the swing a defined attack without a long whoosh.
        s.tone(0.005, 0.105, 390, 190, 0.24, "triangle", decay=3.8)
        s.grit(0.020, 0.045, 0.06, 7, 5, lowpass=0.16)
        return s, 0.40

    if name == "PROJECTILE_WALL":
        s = Synth(name, 0.16)
        s.tone(0, 0.12, 230, 85, 0.30, "triangle", decay=4.0)
        s.grit(0, 0.09, 0.18, 7, 6, lowpass=0.16)
        return s, 0.38

    if name == "PROJECTILE_FLESH":
        s = Synth(name, 0.16)
        s.tone(0, 0.13, 150, 68, 0.28, "sine", decay=3.8)
        s.grit(0, 0.10, 0.17, 9, 5, lowpass=0.15)
        return s, 0.38

    if name == "MELEE_HIT":
        s = Synth(name, 0.18)
        s.tone(0, 0.14, 260, 95, 0.28, "triangle", decay=3.5)
        s.grit(0, 0.10, 0.20, 6, 5, lowpass=0.18)
        return s, 0.43

    if name == "ENEMY_HIT":
        s = Synth(name, 0.17)
        s.tone(0, 0.13, 195, 88, 0.26, "triangle", decay=4.0)
        s.grit(0, 0.09, 0.17, 7, 6, lowpass=0.16)
        return s, 0.38

    if name == "FIRE_CRACKLE":
        s = Synth(name, 0.42)
        held = low = mid = 0.0
        for i in range(len(s.data)):
            t = i / RATE
            if i % 4 == 0:
                held = s.rng.uniform(-1.0, 1.0)
            low += 0.025 * (held - low)
            mid += 0.19 * (held - mid)
            edge = min(1.0, t / 0.035, (0.42 - t) / 0.055)
            # Gentle crackle swells in a thin sizzle; no hard-edged sample clicks.
            crackle = 0.0
            for start in (0.045, 0.145, 0.250, 0.345):
                u = (t - start) / 0.065
                if 0.0 < u < 1.0:
                    crackle += 0.16 * math.sin(math.pi * u) ** 2
            s.data[i] += edge * (0.13 + crackle) * (mid - low)
        return s, 0.40

    if name.startswith("TOOL_") or name.startswith("PROJECTILE_") or name.startswith("MELEE_"):
        long = name == "TOOL_BEAM_START" or name == "PROJECTILE_BURST"
        s = Synth(name, 0.38 if long else 0.24)
        heavy = name in {"TOOL_FIRE_HEAVY", "PROJECTILE_BURST", "MELEE_HIT"}
        s.tone(0, 0.25 if long else 0.16, (420 if heavy else 680) * pitch,
               (85 if heavy else 200) * pitch, 0.34, "pulse", 0.18)
        s.grit(0, 0.15 if heavy else 0.085, 0.38 if heavy else 0.25,
               3 if heavy else 2)
        if name in {"PROJECTILE_WALL", "MELEE_HIT"}:
            s.tone(0.02, 0.17, 870 * pitch, 650 * pitch, 0.16, "sine")
        if name == "PROJECTILE_FLESH":
            s.grit(0.025, 0.13, 0.3, 9)
        return s, 0.69 if heavy else 0.56

    if name.startswith("ENEMY_") or name in {"BEE_BUZZ", "COAL_BEE_BUZZ"}:
        flying = name in {"ENEMY_MOTH", "ENEMY_BAT", "ENEMY_WISP",
                          "ENEMY_STOOPER", "ENEMY_EMBERWING", "BEE_BUZZ", "COAL_BEE_BUZZ"}
        heavy = name in {"ENEMY_HUSK", "ENEMY_SHAMBLER", "ENEMY_THRESHER",
                         "ENEMY_SLAGMAW", "ENEMY_ASHHOUND"}
        s = Synth(name, 0.32 if flying else 0.37)
        base = (350 if flying else 125 if heavy else 220) * pitch
        s.tone(0, 0.29, base, base * (1.5 if flying else 0.68),
               0.25, "pulse", 0.16 if flying else 0.41, vibrato=0.025)
        s.tone(0.03, 0.26, base * (1.79 if flying else 0.57), base * 0.76,
               0.17, "triangle")
        s.grit(0, 0.19, 0.24 if heavy else 0.14, 7 if heavy else 4)
        if name in {"ENEMY_SHOT", "ENEMY_HIT", "ENEMY_DEATH"}:
            s.grit(0, 0.20, 0.30, 3)
        return s, 0.48

    if name.startswith("BOSS_"):
        s = Synth(name, 0.95 if name in {"BOSS_PHASE", "BOSS_DEFEAT"} else 0.66)
        base = (115 + seed % 80) * pitch
        for j in range(3):
            start = j * 0.11
            s.tone(start, 0.34, base * (1.0 + 0.23 * j), base * (0.65 + 0.11 * j),
                   0.27, "pulse", 0.21)
            s.grit(start, 0.08, 0.18, 5)
        if name == "BOSS_DEFEAT":
            s.tone(0.37, 0.45, 390, 130, 0.27, "triangle")
        return s, 0.73

    if name in {"CLOCK_PULSE", "SENSOR_TRIP", "SPARK", "CIRCUIT_SWITCH",
                "PIPE_TRANSFER", "PLACER_CYCLE", "MINER_CYCLE", "SPOUT_CYCLE",
                "DRAIN_CYCLE", "STATION_CRAFT", "HIVE_RELEASE"}:
        s = Synth(name, 0.30)
        s.clicks([0, 0.085] if name in {"PIPE_TRANSFER", "MINER_CYCLE", "PLACER_CYCLE"} else [0],
                 0.28, name not in {"SPARK", "SENSOR_TRIP"})
        base = (85 if name in {"MINER_CYCLE", "DRAIN_CYCLE"} else 190) * pitch
        s.tone(0.01, 0.22, base, base * (1.25 if name in {"STATION_CRAFT", "HIVE_RELEASE"} else 0.76),
               0.20, "pulse", 0.39)
        s.grit(0.01, 0.12, 0.18, 8)
        return s, 0.48

    if name.startswith("ROCKET_") or name == "VICTORY":
        length = 1.5 if name in {"ROCKET_IGNITE", "ROCKET_ASCENT", "VICTORY"} else 0.60
        s = Synth(name, length)
        if name == "ROCKET_IGNITE":
            s.grit(0, 1.28, 0.40, 9, 0.8)
            s.tone(0.04, 1.28, 42, 115, 0.34, "pulse", 0.48)
            s.clicks([0, 0.11, 0.23], 0.35, True)
        elif name == "ROCKET_ASCENT":
            s.tone(0, 1.35, 95, 260, 0.32, "pulse", 0.48)
            s.grit(0, 1.20, 0.28, 8, 1.2)
        elif name == "VICTORY":
            for j, note in enumerate((220, 277, 330, 440)):
                s.tone(j * 0.20, 0.54, note, note, 0.24, "triangle", decay=1.5)
        else:
            s.tone(0, 0.40, 310 * pitch, 210 * pitch if name == "ROCKET_ABORT" else 440 * pitch,
                   0.27, "square")
            s.clicks([0, 0.20] if name == "ROCKET_COUNTDOWN" else [0], 0.24)
        return s, 0.74

    # Physical world: noise texture establishes material, bass gives weight.
    s = Synth(name, 0.45 if name != "SURFACE_WIND" else 0.65)
    if name in {"CAVE_DRIP", "WATER_SPLASH", "LAVA_BUBBLE"}:
        s.tone(0, 0.25, 380 * pitch, 110 * pitch, 0.25, "sine")
        s.grit(0.02, 0.22, 0.20, 12)
    elif name == "SURFACE_WIND":
        s.grit(0, 0.55, 0.22, 18, 1.6, 0.08)
    else:
        s.grit(0, 0.30, 0.42, 3 if name in {"EXPLOSION", "SPARK"} else 7, 3)
        s.tone(0, 0.25, 250 * pitch, 55 * pitch, 0.24, "triangle")
    return s, 0.68 if name == "EXPLOSION" else 0.40


def main():
    registry = (ROOT / "src" / "audio.h").read_text(encoding="utf-8")
    cues = re.findall(r'X\(SFX_([A-Z_]+),\s*"([^"]+\.wav)"', registry)
    if len(cues) < 100:
        raise ValueError("cue registry parse failed")
    OUT.mkdir(parents=True, exist_ok=True)
    for name, filename in cues:
        if name in SIGNATURES:
            continue
        sound, level = build(name)
        sound.write(OUT / filename, level)
    print(f"Generated {len(cues) - len(SIGNATURES)} cues in {OUT}")


if __name__ == "__main__":
    main()
