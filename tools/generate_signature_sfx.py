"""Generate Cinderlift's first four procedural sound-effect sketches.

Run with any Python 3 installation; this uses only the standard library.
The palette is deliberately narrow: pulse waves, a little sine resonance,
sample-and-hold noise, stepped pitch, and short envelopes. Each sound is
written as 16-bit mono PCM for easy use by a future game audio mixer.
"""

from __future__ import annotations

import math
import random
import struct
import wave
from pathlib import Path


SAMPLE_RATE = 22050
OUTPUT = Path(__file__).resolve().parents[1] / "res" / "sfx"
TAU = 2.0 * math.pi


def pulse(phase: float, duty: float = 0.35) -> float:
    return 1.0 if phase % 1.0 < duty else -1.0


def decay(time: float, seconds: float) -> float:
    return math.exp(-max(0.0, time) / seconds)


def noise_clock(rng: random.Random, state: list[float], index: int,
                every: int) -> float:
    if index % every == 0:
        state[0] = rng.uniform(-1.0, 1.0)
    return state[0]


def too_hard_to_mine() -> list[float]:
    """A pick glances off material it cannot break, leaving a metal ring."""
    rng = random.Random(0xC1A0C)
    held = [0.0]
    out = []
    phase = 0.0
    low_phase = 0.0
    for i in range(round(0.34 * SAMPLE_RATE)):
        t = i / SAMPLE_RATE
        pitch = 510.0 + 1130.0 * decay(t, 0.047)
        phase += pitch / SAMPLE_RATE
        low_phase += (118.0 - 38.0 * min(t / 0.18, 1.0)) / SAMPLE_RATE

        stone = noise_clock(rng, held, i, 3)
        crack = 0.30 * stone * decay(t, 0.017)
        if t >= 0.061:
            u = t - 0.061
            crack += 0.23 * stone * decay(u, 0.021)

        chip = 0.37 * pulse(phase, 0.23) * decay(t, 0.046)
        ring = (0.19 * math.sin(TAU * 823.0 * t)
                + 0.11 * math.sin(TAU * 1169.0 * t)) * decay(t, 0.095)
        body = 0.10 * math.sin(TAU * low_phase) * decay(t, 0.050)
        out.append(chip + crack + ring + body)
    return out


def mining() -> list[float]:
    """Two soft, sandy scrapes with a rounded low hit at the start."""
    rng = random.Random(0xC2A65)
    out = []
    smooth = rumble_phase = 0.0
    for i in range(round(0.30 * SAMPLE_RATE)):
        t = i / SAMPLE_RATE
        white = rng.uniform(-1.0, 1.0)
        smooth += 0.22 * (white - smooth)
        hiss = 0.24 * white + 0.76 * smooth
        first = max(0.0, math.sin(math.pi * t / 0.135)) ** 1.6 if t < 0.135 else 0.0
        second_t = t - 0.115
        second = (max(0.0, math.sin(math.pi * second_t / 0.155)) ** 1.6
                  if 0.0 < second_t < 0.155 else 0.0)
        rumble_phase += 74.0 / SAMPLE_RATE
        body = 0.035 * math.sin(TAU * rumble_phase) * (first + 0.7 * second)
        hit = (0.13 * math.sin(TAU * 105.0 * t)
               * (1.0 - math.exp(-t / 0.004)) * decay(t, 0.030))
        out.append(hiss * (0.25 * first + 0.20 * second) + body + hit)
    return out


def damage() -> list[float]:
    """A compressed hit followed by two discordant falling chip tones."""
    rng = random.Random(0xD4A6E)
    held = [0.0]
    out = []
    phase_a = phase_b = phase_low = 0.0
    for i in range(round(0.43 * SAMPLE_RATE)):
        t = i / SAMPLE_RATE
        freq = 194.0 + 450.0 * decay(t, 0.115)
        wobble = 1.0 + 0.027 * math.sin(TAU * 19.0 * t)
        phase_a += freq * wobble / SAMPLE_RATE
        phase_b += freq * 1.43 / SAMPLE_RATE
        phase_low += (73.0 + 45.0 * decay(t, 0.045)) / SAMPLE_RATE

        bite = noise_clock(rng, held, i, 4) * (
            0.37 * decay(t, 0.022)
            + (0.13 * decay(t - 0.102, 0.017) if t >= 0.102 else 0.0))
        alarm = (0.31 * pulse(phase_a, 0.31)
                 + 0.15 * pulse(phase_b, 0.18)) * decay(t, 0.125)
        thud = 0.28 * math.sin(TAU * phase_low) * decay(t, 0.083)
        out.append(bite + alarm + thud)
    return out


def machine() -> list[float]:
    """A motor sputters, its gear clicks speed up, then it locks in."""
    rng = random.Random(0x6EA4)
    held = [0.0]
    out = []
    motor_phase = low_phase = 0.0
    gear_times = (0.082, 0.210, 0.316, 0.404, 0.477)
    for i in range(round(0.89 * SAMPLE_RATE)):
        t = i / SAMPLE_RATE
        motor_hz = 88.0 + 57.0 * min(t / 0.48, 1.0)
        motor_hz *= 1.0 + 0.035 * math.sin(TAU * 17.0 * t)
        motor_phase += motor_hz / SAMPLE_RATE
        low_phase += 92.0 / SAMPLE_RATE

        ramp = min(t / 0.075, 1.0)
        release = decay(max(t - 0.61, 0.0), 0.105)
        motor = 0.22 * pulse(motor_phase, 0.43) * ramp * release
        gears = 0.0
        grit = noise_clock(rng, held, i, 5)
        for start in gear_times:
            if start <= t < start + 0.055:
                gears += 0.24 * grit * decay(t - start, 0.012)
        sputter = 0.095 * grit * ramp * release * (0.65 + 0.35 * math.sin(TAU * 23.0 * t))
        if t < 0.018:
            sputter += 0.25 * grit * decay(t, 0.007)
        lock = 0.0
        if t >= 0.62:
            u = t - 0.62
            lock = (0.20 * grit * decay(u, 0.016)
                    + 0.17 * math.sin(TAU * low_phase) * decay(u, 0.045))
        out.append(motor + gears + sputter + lock)
    return out


def write_wav(name: str, samples: list[float], gain: float = 1.0) -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    peak = max(abs(s) for s in samples)
    scale = gain * 0.86 / peak
    fade = round(0.003 * SAMPLE_RATE)
    pcm = bytearray()
    for i, value in enumerate(samples):
        edge = min(1.0, i / fade, (len(samples) - 1 - i) / fade)
        shaped = math.tanh(value * scale) * edge
        pcm.extend(struct.pack("<h", round(32767 * shaped)))
    with wave.open(str(OUTPUT / name), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(pcm)


if __name__ == "__main__":
    for name, synth, gain in (
        ("mining_too_hard.wav", too_hard_to_mine, 1.0),
        ("mining_signature.wav", mining, 0.48),
        ("damage_signature.wav", damage, 1.0),
        ("machine_signature.wav", machine, 0.60),
    ):
        samples = synth()
        write_wav(name, samples, gain)
        print(f"{name}: {len(samples) / SAMPLE_RATE:.2f}s")
