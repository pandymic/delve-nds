#!/usr/bin/env python3
"""Imports the third-party music modules (art/third_party/music/) into the
game's soundbank sources (audio/music_*.xm / .it).

Maxmod plays tracker modules straight from the soundbank in main RAM, so
the 16-bit samples of most XM modules are converted to 8-bit (half the
size). The dungeon's track keeps 16 bits: its long quiet passages make the
8-bit noise audible. IT modules are copied as is.

The DS mixes in hardware with no interpolation: a sample played at a low
rate comes out as a coarse staircase, which sounds raspy and crackly (the
Kosmo tracks play their piano and bass samples at 4-16 kHz; desktop players
hide it with sinc interpolation). So every XM sample the patterns play
below MIN_RATE is upsampled here, by a power of two with a Lanczos filter,
and its relative note raised to match: same pitch, smooth waveform.

Deterministic; outputs are committed. Standard library only:
    python3 tools/import_music.py
"""

import math
import os
import struct

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "art", "third_party", "music")
AUDIO = os.path.join(ROOT, "audio")

# (source, output name in audio/, convert 16-bit samples to 8-bit,
#  upsample low-rate samples)
TRACKS = [
    ("kosmo-the-cat/Menu Song.xm", "music_title.xm", True, True),
    ("beyond/beyond_-_pianotune.it", "music_camp.it", False, False),
    ("kosmo-the-cat/Wander in a dungeon.xm", "music_dungeon.xm", False, True),
    ("beyond/bnd-song0.it", "music_sewers.it", False, False),
    # Drozerix's starfield is chip music, meant to buzz.
    ("drozerix/ambient_starfield.xm", "music_caverns.xm", True, False),
    ("jam/crusaders.xm", "music_molten.xm", True, True),
    ("kosmo-the-cat/Evil boss.xm", "music_boss.xm", True, True),
]

# Samples played below this rate (Hz) are upsampled, at most MAX_UPSAMPLE
# times. Tiny chip loops (under CHIP_LEN frames) are left alone.
MIN_RATE = 16000
MAX_UPSAMPLE = 4
CHIP_LEN = 256
LANCZOS_TAPS = 8


def note_rate(note, rel, fine):
    """Playback rate of an XM note (1 = C-0; C-4 plays at 8363 Hz)."""
    return 8363 * 2 ** ((note + rel - 49 + fine / 128) / 12)


def played_notes(d):
    """{instrument (1-based): set of notes the song's patterns play}."""
    hsize = struct.unpack_from("<I", d, 60)[0]
    songlen, _, nch, npat = struct.unpack_from("<HHHH", d, 64)
    order = set(d[80:80 + songlen])
    p = 60 + hsize
    played = {}
    for pat in range(npat):
        plen = struct.unpack_from("<I", d, p)[0]
        rows, psize = struct.unpack_from("<HH", d, p + 5)
        data = d[p + plen:p + plen + psize]
        p += plen + psize
        i = 0
        for _ in range(rows if psize else 0):
            for _ in range(nch):
                b = data[i]
                i += 1
                f = [0] * 5
                if b & 0x80:
                    for k in range(5):
                        if b & (1 << k):
                            f[k] = data[i]
                            i += 1
                else:
                    f = [b] + list(data[i:i + 4])
                    i += 4
                note, ins = f[0], f[1]
                if pat in order and 0 < note < 97 and ins:
                    played.setdefault(ins, set()).add(note)
    return played


def decode(raw, wide):
    """Delta-coded sample bytes to a list of ints (8- or 16-bit range)."""
    out, acc = [], 0
    if wide:
        for (v,) in struct.iter_unpack("<h", raw[:len(raw) // 2 * 2]):
            acc = (acc + v) & 0xFFFF
            out.append(acc - 0x10000 if acc & 0x8000 else acc)
    else:
        for v in raw:
            acc = (acc + v) & 0xFF
            out.append(acc - 0x100 if acc & 0x80 else acc)
    return out


def encode(samples, wide):
    """A list of ints back to delta-coded bytes."""
    out, prev = bytearray(), 0
    for s in samples:
        if wide:
            out += struct.pack("<H", (s - prev) & 0xFFFF)
        else:
            out.append((s - prev) & 0xFF)
        prev = s
    return bytes(out)


def lanczos(t):
    if t == 0:
        return 1.0
    a = math.pi * t
    return math.sin(a) / a * math.sin(a / LANCZOS_TAPS) / (a / LANCZOS_TAPS)


def upsample(s, k, loop):
    """Lanczos interpolation by the integer factor k. Past its end a looped
    sample (loop = (start, length) in frames) reads on around its loop; a
    one-shot reads silence."""
    n = len(s)

    def at(j):
        if j < 0:
            return 0
        if j < n:
            return s[j]
        if loop is None:
            return 0
        return s[loop[0] + (j - loop[0]) % loop[1]]

    kernels = [[lanczos(j - f / k) for j in range(-LANCZOS_TAPS + 1, LANCZOS_TAPS + 1)]
               for f in range(k)]
    out = []
    for i in range(n * k):
        j0, f = divmod(i, k)
        if f == 0:
            out.append(float(s[j0]))
            continue
        acc = 0.0
        for w, j in zip(kernels[f], range(j0 - LANCZOS_TAPS + 1, j0 + LANCZOS_TAPS + 1)):
            acc += w * at(j)
        out.append(acc)
    return out


def convert_xm(data, to8, smooth):
    """Rewrites an XM module: with smooth, low-rate samples upsampled (see
    MIN_RATE); with to8, every 16-bit sample made 8-bit. XM samples are delta-coded;
    lengths and loop points are in bytes. Instruments whose samples are all
    empty (credits written as instrument names) lose their sample headers,
    which mmutil would warn about."""
    d = bytearray(data)
    played = played_notes(d)
    out = bytearray()
    hsize = struct.unpack_from("<I", d, 60)[0]
    npat, nins = struct.unpack_from("<HH", d, 70)
    p = 60 + hsize
    for _ in range(npat):
        plen = struct.unpack_from("<I", d, p)[0]
        psize = struct.unpack_from("<H", d, p + 7)[0]
        p += plen + psize
    out += d[:p]
    for ins in range(1, nins + 1):
        isize = struct.unpack_from("<I", d, p)[0]
        ns = struct.unpack_from("<H", d, p + 27)[0]
        if ns == 0:
            out += d[p:p + isize]
            p += isize
            continue
        shsize = struct.unpack_from("<I", d, p + 29)[0]
        keymap = d[p + 33:p + 33 + 96]
        out += d[p:p + isize]
        q = p + isize
        headers = [bytearray(d[q + k * shsize:q + (k + 1) * shsize])
                   for k in range(ns)]
        q += ns * shsize
        bodies = []
        for si, h in enumerate(headers):
            length, lstart, llen = struct.unpack_from("<III", h, 0)
            fine, typ, rel = struct.unpack_from("<bBxb", h, 13)
            raw = d[q:q + length]
            q += length
            wide = bool(typ & 0x10)
            unit = 2 if wide else 1
            frames = length // unit
            # The lowest note that plays this sample (through the keymap).
            notes = [n for n in played.get(ins, ()) if keymap[n - 1] == si]
            k = 1
            if smooth and notes and frames >= CHIP_LEN:
                rate = note_rate(min(notes), rel, fine)
                while rate * k < MIN_RATE and k < MAX_UPSAMPLE:
                    k *= 2
            if k == 1 and not (wide and to8):
                bodies.append(raw)
                continue
            s = decode(raw, wide)
            if k > 1:
                loop = (lstart // unit, llen // unit) if typ & 3 and llen else None
                s = upsample(s, k, loop)
                rel += 12 * (k.bit_length() - 1)
            if to8 and wide:
                # Round to 8 bits.
                s = [max(-128, min(127, int(math.floor(v / 256 + 0.5)))) for v in s]
                wide = False
            else:
                lim = 32767 if wide else 127
                s = [max(-lim - 1, min(lim, int(math.floor(v + 0.5)))) for v in s]
            new_unit = 2 if wide else 1
            struct.pack_into("<III", h, 0, len(s) * new_unit,
                             lstart // unit * k * new_unit, llen // unit * k * new_unit)
            h[14] = (typ & ~0x10) | (0x10 if wide else 0)
            struct.pack_into("<b", h, 16, rel)
            bodies.append(encode(s, wide))
        if all(len(b) == 0 for b in bodies):
            struct.pack_into("<H", out, len(out) - isize + 27, 0)
            p = q
            continue
        for h in headers:
            out += h
        for b in bodies:
            out += b
        p = q
    out += d[p:]
    return bytes(out)


def main():
    os.makedirs(AUDIO, exist_ok=True)
    total = 0
    for src, name, to8, smooth in TRACKS:
        data = open(os.path.join(SRC, src), "rb").read()
        if name.endswith(".xm"):
            data = convert_xm(data, to8, smooth)
        with open(os.path.join(AUDIO, name), "wb") as f:
            f.write(data)
        total += len(data)
        print("  %-18s %7d bytes  (%s)" % (name, len(data), src))
    print("  music total        %7d bytes" % total)


if __name__ == "__main__":
    main()
