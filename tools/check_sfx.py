"""Check that every registered cue can be decoded by the game audio player."""
from pathlib import Path
import re
import struct
import wave

root = Path(__file__).resolve().parents[1]
registry = (root / "src" / "audio.h").read_text(encoding="utf-8")
filenames = re.findall(r'X\(SFX_[A-Z_]+,\s*"([^"]+\.wav)"', registry)
assert len(filenames) == 105 and len(set(filenames)) == len(filenames)
actual = {p.name for p in (root / "res" / "sfx").glob("*.wav")}
assert actual == set(filenames), f"missing={set(filenames)-actual}, extra={actual-set(filenames)}"

for filename in filenames:
    path = root / "res" / "sfx" / filename
    with wave.open(str(path), "rb") as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 22050), filename
        count = wav.getnframes()
        assert 0.04 * 22050 <= count <= 2.0 * 22050, filename
        values = struct.unpack(f"<{count}h", wav.readframes(count))
        peak = max(abs(value) for value in values)
        assert 5000 <= peak < 32767, (filename, peak)
        assert sum(value * value for value in values) / count > 200_000, filename

embedded = (root / "src" / "audio_embedded.h").read_text(encoding="ascii")
assert f"EMBEDDED_SFX_COUNT = {len(filenames)};" in embedded
print(f"Validated {len(filenames)} playable, embedded WAV cues")
