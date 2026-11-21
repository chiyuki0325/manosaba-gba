#!/usr/bin/env python3
"""Render rough bell comparisons from the project's existing XM samples."""
from pathlib import Path
import struct
import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'gba' / 'build-generated' / 'sfx' / 'bell_study'
RATE = 16000


def sample_from_xm(path, instrument, index):
    data = path.read_bytes()
    assert data[:17] == b'Extended Module: '
    pos = 60 + struct.unpack_from('<I', data, 60)[0]
    patterns, instruments = struct.unpack_from('<HH', data, 70)
    for _ in range(patterns):
        pos += struct.unpack_from('<I', data, pos)[0] + struct.unpack_from('<H', data, pos + 7)[0]
    for _ in range(instruments):
        size = struct.unpack_from('<I', data, pos)[0]
        name = data[pos + 4:pos + 26].split(b'\0')[0].decode('utf-8', 'replace')
        count = struct.unpack_from('<H', data, pos + 27)[0]
        if not count:
            pos += size
            continue
        header_size = struct.unpack_from('<I', data, pos + 29)[0]
        headers = pos + size
        sample_pos = headers + count * header_size
        for sample_index in range(count):
            header = headers + sample_index * header_size
            length = struct.unpack_from('<I', data, header)[0]
            kind = data[header + 14]
            if name.startswith(instrument) and sample_index == index:
                bits = 16 if kind & 16 else 8
                dtype = '<i2' if bits == 16 else 'i1'
                unsigned = '<u2' if bits == 16 else 'u1'
                delta = np.frombuffer(data[sample_pos:sample_pos + length], dtype=dtype)
                return (np.cumsum(delta.astype(np.int64)) % (1 << bits)).astype(unsigned).view(dtype).astype(float) / (1 << (bits - 1))
            sample_pos += length
        pos = sample_pos
    raise ValueError(f'Instrument {instrument} sample {index} not found in {path}')


def render(sample, base_hz, notes, duration, max_note=None, overlap=None):
    out = np.zeros(round(RATE * duration))
    for index, (time, hz, level) in enumerate(notes):
        # XM samples have a nominal C-4 rate of 8363 Hz.  Frequencies here
        # match spectral peaks, not XM note/finetune metadata.
        step = 8363 * (hz / base_hz) / RATE
        length = int((len(sample) - 1) / step)
        if max_note:
            length = min(length, int(max_note * RATE))
        if overlap is not None:
            if index + 1 < len(notes):
                interval = notes[index + 1][0] - time
                end = notes[index + 1][0] + interval * overlap
            else:
                interval = time - notes[index - 1][0]
                end = time + interval * (1 + overlap)
            # A new third voice must always cut the oldest one.
            if index + 2 < len(notes):
                end = min(end, notes[index + 2][0])
            length = min(length, max(0, round((end - time) * RATE)))
        wave = np.interp(np.arange(length) * step, np.arange(len(sample)), sample)
        # Short fade prevents clicks when limiting a bell hit's tail.
        fade = min(160, len(wave))
        wave[-fade:] *= np.linspace(1, 0, fade)
        start = round(time * RATE)
        length = min(len(wave), len(out) - start)
        out[start:start + length] += level * wave[:length]
    peak = np.max(np.abs(out))
    if peak:
        out *= .8 / peak
    return out


def main():
    OUT.mkdir(exist_ok=True)
    candidates = {
        'glockenspiel': (sample_from_xm(ROOT/'gba/audio/song004.xm', 'Bell-like', 0), 629),
        'vibraphone': (sample_from_xm(ROOT/'gba/audio/bgm011.xm', 'Vibraphone m', 1), 559),
    }
    # Approximate strikes/pitches obtained from inspection of spectral peaks.
    # They are not a validated transcription of the source audio.
    school = [(0,761,1),(1,961,1),(2,855,1),(3,571,1),(5,761,1),(6,855,1),(7,961,1),(8,761,1)]
    bell = [(0,523,1),(2.95,523,1)]
    crossing = [(0.12 + .407*i,824, min(1, (i+1)/3, (16-i)/4)) for i in range(16)]
    for name, (sample, base) in candidates.items():
        for title, notes, duration, maximum in [('school',school,10.1,None),('bell',bell,6.13,None),('crossing',crossing,6.82,.4)]:
            if title == 'school' and name == 'vibraphone':
                notes = [(time, hz / 2, level) for time, hz, level in notes]
                audio = render(sample, base, notes, duration, maximum, overlap=1/3)
            else:
                audio = render(sample, base, notes, duration, maximum)
            sf.write(OUT/f'{title}_{name}.wav', audio, RATE, subtype='PCM_16')
    print(f'Wrote 6 rough comparison WAVs to {OUT}')


if __name__ == '__main__':
    main()
