#!/usr/bin/env python3
"""Generates Delve's sound effects into audio/, and, with --songs,
its original synthesized music into art/generated/music/.

The game plays CC0 / public-domain tracker music instead
(tools/import_music.py); the songs here are kept as an option.

Everything here is original and synthesized from scratch, so it is covered
by the project's own licence (no third-party audio).

- Sound effects: 8-bit mono WAVs at 16 kHz, built from oscillators, filtered
  noise and envelopes.
- Music: five 6-channel ProTracker MODs ("6CHN", supported by mmutil) with
  synthesized instruments: a chorused pad, a plucked string (Karplus-Strong),
  a bell, a pulse lead, a sub bass and a small drum kit.
    menu.mod    "Descent"     A minor, 84 BPM: pluck arpeggios, bell melody
    camp.mod    "Embers"      F major, 76 BPM: finger-picked, warm
    dungeon.mod "Depths"      D minor, 128 BPM: driving bass and drums
    sewers.mod  "Undercroft"  E phrygian, 100 BPM: drips and a slow lead
    boss.mod    "Warlord"     C minor, 150 BPM: fast and heavy

Each song is written as bars of chords plus melody strings; the tracks are
laid out on one timeline and cut into 64-row patterns. A copy of the lead,
three rows late and quieter, gives the classic tracker echo.

Deterministic; the outputs are committed.

Usage: python3 tools/make_audio.py   (standard library only)
"""

import math
import os
import random
import struct
import wave

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
AUDIO = os.path.join(ROOT, "audio")
RATE = 16000

# ---------------------------------------------------------------------------
# Sound effect building blocks (lists of floats in [-1, 1] at RATE)
# ---------------------------------------------------------------------------


def wave_at(shape, p):
    if shape == "sine":
        return math.sin(2 * math.pi * p)
    if shape == "square":
        return 1.0 if p < 0.5 else -1.0
    if shape == "pulse":
        return 1.0 if p < 0.25 else -1.0
    if shape == "tri":
        return 4 * abs(p - 0.5) - 1
    return 2 * p - 1   # saw


def tone(dur, f0, f1=None, shape="sine", curve=1.0, vib=0.0, vib_hz=0.0):
    """A tone gliding from f0 to f1 (t ** curve), with optional vibrato."""
    n = int(RATE * dur)
    f1 = f0 if f1 is None else f1
    out, ph = [], 0.0
    for i in range(n):
        t = i / n
        f = f0 + (f1 - f0) * t ** curve
        if vib:
            f *= 1 + vib * math.sin(2 * math.pi * vib_hz * i / RATE)
        ph += f / RATE
        out.append(wave_at(shape, ph % 1.0))
    return out


def noise(dur, seed):
    rnd = random.Random(seed)
    return [rnd.random() * 2 - 1 for _ in range(int(RATE * dur))]


def lowpass(x, cutoff):
    """One-pole low-pass; cutoff in Hz, or a function of t in [0, 1]."""
    out, y, n = [], 0.0, len(x)
    for i, v in enumerate(x):
        fc = cutoff(i / n) if callable(cutoff) else cutoff
        a = 1 - math.exp(-2 * math.pi * fc / RATE)
        y += a * (v - y)
        out.append(y)
    return out


def highpass(x, cutoff):
    return [a - b for a, b in zip(x, lowpass(x, cutoff))]


def env(x, attack=0.004, power=1.5, hold=0.0):
    """Linear attack, optional hold, then (1 - t) ** power decay."""
    n = len(x)
    na, nh = max(1, int(RATE * attack)), int(RATE * hold)
    out = []
    for i, v in enumerate(x):
        if i < na:
            g = i / na
        elif i < na + nh:
            g = 1.0
        else:
            g = (1 - (i - na - nh) / max(1, n - na - nh)) ** power
        out.append(v * g)
    return out


def tremolo(x, hz, depth):
    return [v * (1 - depth * (0.5 + 0.5 * math.sin(2 * math.pi * hz * i / RATE)))
            for i, v in enumerate(x)]


def gain(x, g):
    return [v * g for v in x]


def mix(*parts):
    """parts: lists, or (list, offset_seconds)."""
    items = [(p, 0.0) if isinstance(p, list) else p for p in parts]
    n = max(len(p) + int(RATE * o) for p, o in items)
    out = [0.0] * n
    for p, o in items:
        s = int(RATE * o)
        for i, v in enumerate(p):
            out[s + i] += v
    return out


def echo(x, delay, feedback, repeats):
    d = int(RATE * delay)
    out = x + [0.0] * (d * repeats)
    for r in range(1, repeats + 1):
        g = feedback ** r
        for i, v in enumerate(x):
            out[i + d * r] += v * g
    return out


def normalize(x, peak=0.92):
    m = max(abs(v) for v in x) or 1.0
    return [v * peak / m for v in x]


def write_wav(name, samples, peak=0.92):
    x = normalize(samples, peak)
    # Fade the last 4 ms so nothing ends on a click.
    k = min(len(x), int(RATE * 0.004))
    for i in range(k):
        x[len(x) - k + i] *= 1 - (i + 1) / k
    data = bytes(max(0, min(255, int(round(128 + v * 127)))) for v in x)
    with wave.open(os.path.join(AUDIO, name + ".wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(RATE)
        w.writeframes(data)


def notes_seq(freqs, step, shape, length, vol=1.0):
    """Notes starting every `step` seconds, each `length` long."""
    return mix(*[(gain(env(tone(length, f, shape=shape), 0.003, 2.0), vol),
                  i * step) for i, f in enumerate(freqs)])


def bell_tone(f, dur, seed=0):
    """Inharmonic partials, each decaying at its own rate."""
    parts = [(1.0, 1.0, 1.0), (2.76, 0.5, 1.8), (5.4, 0.3, 2.6),
             (8.93, 0.15, 3.5)]
    return mix(*[gain(env(tone(dur, f * r), 0.002, p), a)
                 for r, a, p in parts])


def make_sfx():
    # Menus.
    write_wav("select", mix(env(tone(0.05, 1320, shape="tri"), 0.002, 2),
                            (gain(env(tone(0.06, 1760, shape="tri"), 0.002, 2),
                                  0.7), 0.035)), 0.6)
    # The sword: a band-passed whoosh that rises then falls.
    write_wav("swing", env(lowpass(highpass(noise(0.15, 2), 500),
                                   lambda t: 900 + 3800 * math.sin(math.pi * t)),
                           0.03, 1.2), 0.75)
    # A blade landing: a low thump, a click and a mid "thwack".
    write_wav("hit", mix(env(tone(0.12, 220, 55, curve=0.5), 0.001, 2),
                         gain(env(highpass(noise(0.025, 3), 2000), 0.0005, 2),
                              0.6),
                         gain(env(lowpass(noise(0.08, 31), 1400), 0.001, 2.5),
                              0.7)))
    write_wav("hurt", mix(env(tone(0.26, 520, 170, "square", 0.6, 0.05, 30),
                              0.002, 1.4),
                          gain(env(lowpass(noise(0.12, 4), 2500), 0.001, 2),
                               0.5)), 0.8)
    write_wav("dash", env(lowpass(highpass(noise(0.22, 5), 300),
                                  lambda t: 600 + 5000 * t * (1 - t) * 4),
                          0.06, 1.3), 0.75)
    # A stone door grinding open, with a low thud.
    write_wav("door", mix(gain(tremolo(env(lowpass(noise(0.55, 6), 500),
                                           0.05, 1.0), 17, 0.6), 1.0),
                          gain(env(tone(0.35, 90, 45, "tri"), 0.002, 2), 0.9),
                          (gain(env(tone(0.12, 70, 40), 0.001, 2), 0.8), 0.4)))
    write_wav("pickup", echo(notes_seq([1047, 1319, 1568, 2093], 0.055, "tri",
                                       0.09), 0.09, 0.35, 2), 0.7)
    # Death: a falling minor line over a rumble.
    write_wav("death", mix(notes_seq([659, 523, 440, 330], 0.16, "square",
                                     0.3, 0.6),
                           gain(env(lowpass(noise(1.1, 7), 300), 0.05, 1.2),
                                0.9),
                           gain(env(tone(1.1, 110, 40), 0.01, 1.2), 0.6)))
    # A fireball: a breathy "fwoom" with a falling core.
    write_wav("shoot", mix(env(lowpass(noise(0.2, 8), lambda t: 3000 - 2400 * t),
                               0.01, 1.5),
                           gain(env(tone(0.16, 700, 200, "tri"), 0.002, 1.8),
                                0.5)), 0.75)
    write_wav("pop", mix(env(tone(0.09, 260, 900, curve=0.5), 0.001, 1.5),
                         gain(env(lowpass(noise(0.16, 9), 1800), 0.001, 2), 0.8)),
              0.8)
    write_wav("throw", mix(env(lowpass(highpass(noise(0.11, 10), 1500),
                                       lambda t: 7000 - 4000 * t), 0.01, 1.4),
                           gain(env(tone(0.09, 1500, 700, "tri"), 0.002, 2),
                                0.3)), 0.6)
    tink = mix(*[gain(env(tone(0.16, f), 0.0005, p), a)
                 for f, a, p in ((2637, 1.0, 3), (3770, 0.6, 4),
                                 (5120, 0.35, 5))])
    write_wav("tink", tink, 0.6)
    write_wav("blast", mix(env(tone(0.7, 160, 32, curve=0.4), 0.002, 1.5),
                           gain(env(lowpass(noise(0.7, 11),
                                            lambda t: 4000 * (1 - t) + 200),
                                    0.004, 1.6), 0.9),
                           gain(env(tone(0.5, 300, 1500, "tri", 0.5), 0.2, 1),
                                0.25)))
    write_wav("charged", echo(notes_seq([784, 1047, 1319, 1568, 2093], 0.045,
                                        "tri", 0.12), 0.08, 0.4, 3), 0.65)
    # Pottery: a crack and a scatter of shards.
    rnd = random.Random(12)
    shards = [(gain(env(highpass(noise(0.03, 120 + k), 3000), 0.0005, 2),
                    0.3 + rnd.random() * 0.4), 0.02 + rnd.random() * 0.2)
              for k in range(9)]
    write_wav("shatter", mix(env(highpass(noise(0.12, 13), 1200), 0.001, 2.5),
                             *shards), 0.75)
    write_wav("wood", mix(env(lowpass(noise(0.18, 14), 1600), 0.001, 2.2),
                          gain(env(tone(0.15, 210, 120, "tri"), 0.001, 2), 0.9),
                          (gain(env(lowpass(noise(0.08, 15), 900), 0.001, 2),
                                0.5), 0.07)))
    write_wav("explode", mix(env(tone(0.9, 110, 26, curve=0.4), 0.002, 1.3),
                             gain(env(lowpass(noise(1.0, 16),
                                              lambda t: 3500 * (1 - t) ** 2 + 150),
                                      0.003, 1.4), 1.1),
                             *[(gain(env(highpass(noise(0.02, 200 + k), 2500),
                                         0.0005, 2), 0.25), 0.15 + k * 0.07)
                               for k in range(6)]))
    write_wav("coin", mix(env(tone(0.06, 1976, shape="square"), 0.001, 1),
                          (env(tone(0.22, 2637, shape="square"), 0.001, 2),
                           0.055)), 0.45)
    write_wav("fall", env(tone(0.8, 900, 110, "tri", 0.5, 0.03, 9), 0.01, 1.3),
              0.7)
    write_wav("spikes", mix(env(highpass(noise(0.1, 17), 2500), 0.0005, 2),
                            gain(tink[: int(RATE * 0.1)], 0.5)), 0.7)

    # Bosses, the camp.
    # The Warlord's roar: a growling saw with a throaty tremolo.
    write_wav("roar", mix(env(lowpass(tremolo(tone(0.75, 95, 70, "saw", 1, 0.04,
                                                   7), 23, 0.5), 900),
                              0.08, 1.2, 0.2),
                          gain(env(lowpass(noise(0.75, 18), 1200), 0.1, 1.5),
                               0.5)))
    # A heavy body hitting stone, with rubble.
    write_wav("slam", mix(env(tone(0.5, 80, 28, curve=0.4), 0.001, 1.4),
                          gain(env(lowpass(noise(0.5, 19), 700), 0.001, 1.8),
                               1.0),
                          *[(gain(env(lowpass(noise(0.04, 300 + k), 2500),
                                      0.001, 2), 0.35), 0.08 + k * 0.05)
                            for k in range(5)]))
    # The Slime King landing: wet, with a wobbling bubble.
    write_wav("splat", mix(env(lowpass(noise(0.3, 20), lambda t: 2500 - 2000 * t),
                               0.002, 1.8),
                           gain(env(tone(0.35, 320, 90, "sine", 0.6, 0.15, 18),
                                    0.005, 1.5), 0.9),
                           gain(env(tone(0.3, 70, 35), 0.001, 1.5), 0.8)))
    # Victory: a bright major arpeggio and a held chord.
    fan = notes_seq([523, 659, 784], 0.1, "square", 0.12, 0.6)
    chord = mix(*[gain(env(tone(0.7, f, shape=s), 0.01, 1.5, 0.15), 0.4)
                  for f, s in ((1047, "square"), (1319, "tri"),
                               (1568, "tri"), (523, "tri"))])
    write_wav("fanfare", echo(mix(fan, (chord, 0.3)), 0.12, 0.3, 2), 0.75)
    # Stepping through the portal: a rising shimmer.
    write_wav("portal", mix(env(tremolo(tone(0.8, 180, 1400, "tri", 2.0), 14,
                                        0.6), 0.2, 1.2),
                            gain(env(lowpass(highpass(noise(0.8, 21), 800),
                                             lambda t: 1000 + 6000 * t),
                                     0.3, 1.5), 0.6)), 0.75)
    write_wav("chime", echo(mix(bell_tone(1047, 0.6),
                                (gain(bell_tone(1568, 0.5), 0.6), 0.08)),
                            0.1, 0.3, 2), 0.6)

    # Feedback sounds.
    # Menus, back: the select blip falling instead of rising.
    write_wav("back", mix(env(tone(0.05, 1320, shape="tri"), 0.002, 2),
                          (gain(env(tone(0.06, 990, shape="tri"), 0.002, 2),
                                0.7), 0.035)), 0.55)
    # Can't do that (shrine: too poor, locked): a low double buzz.
    buzz = env(lowpass(tone(0.08, 150, 140, "square"), 1800), 0.002, 1.2, 0.03)
    write_wav("deny", mix(buzz, (buzz, 0.1)), 0.6)
    # An action with nothing left (no dash charge, dagger or blast): a dry
    # click.
    write_wav("empty", mix(env(highpass(noise(0.03, 31), 2000), 0.0005, 3),
                           gain(env(tone(0.04, 420, 300, "square"), 0.001, 3),
                                0.4)), 0.45)
    # One heart left: a soft lub-dub.
    thump = env(lowpass(tone(0.12, 70, 45, curve=0.5), 400), 0.004, 2)
    write_wav("heartbeat", mix(thump, (gain(thump, 0.7), 0.16)), 0.7)
    # An upgrade bought: coins tumbling into a chime.
    coins = mix(*[(gain(env(tone(0.06, f, shape="square"), 0.001, 1.2), 0.35),
                   0.05 * i) for i, f in enumerate([1568, 1976, 1760, 2349])])
    write_wav("buy", echo(mix(coins, (bell_tone(1319, 0.5), 0.2)),
                          0.09, 0.25, 2), 0.65)


# ---------------------------------------------------------------------------
# Instruments (signed 8-bit samples for the MODs)
# ---------------------------------------------------------------------------

PAL_CLOCK = 7093789.2
C2_RATE = PAL_CLOCK / (2 * 428)     # MOD note C-2 plays a sample at this rate
C3_RATE = PAL_CLOCK / (2 * 214)
CYCLE = 32                          # samples per cycle of the pitched voices

# Amiga periods, octaves 1-3 (ProTracker "C-1" .. "B-3").
PERIODS = [856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
           428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
           214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113]


def s8(values, amp=110):
    m = max(abs(v) for v in values) or 1.0
    return bytes((max(-128, min(127, int(round(v * amp / m)))) & 0xFF)
                 for v in values)


class Instrument:
    """A MOD sample. base_midi is the pitch MOD note C-1 plays (None for
    drums, always triggered at C-3)."""

    def __init__(self, name, data, loop=None, base_midi=None):
        self.name, self.data, self.loop, self.base_midi = name, data, loop, base_midi


def inst_pulse():
    # 25% pulse, slightly rounded so it is not too harsh.
    raw = [1.0 if i < CYCLE // 4 else -0.45 for i in range(CYCLE)]
    sm = [(raw[i - 1] + 2 * raw[i] + raw[(i + 1) % CYCLE]) / 4
          for i in range(CYCLE)]
    return Instrument("pulse", s8(sm, 90), (0, CYCLE), 48)


def inst_sub():
    # Triangle plus a little square, 64 samples: an octave lower.
    n = CYCLE * 2
    v = [(4 * abs(i / n - 0.5) - 1) * 0.8 + (0.2 if i < n // 2 else -0.2)
         for i in range(n)]
    return Instrument("sub bass", s8(v, 120), (0, n), 36)


def inst_pad():
    # Two saws, 63 and 65 cycles per 2048 samples: a slow chorus around the
    # 32-sample pitch. Low-passed for warmth.
    n = CYCLE * 64
    v = []
    for i in range(n):
        a = (i * 63 / n) % 1.0
        b = (i * 65 / n) % 1.0
        v.append((2 * a - 1) + (2 * b - 1))
    y, out = 0.0, []
    for k in range(2):           # two passes so the loop seam is settled
        out = []
        for s in v:
            y += 0.35 * (s - y)
            out.append(y)
    return Instrument("chorus pad", s8(out, 70), (0, n), 48)


def inst_pluck(seconds=0.7, seed=3):
    # Karplus-Strong. The averaging filter adds half a sample of delay, so a
    # 63-sample line sounds a period of 63.5: run it at the rate that makes
    # that one 32-sample cycle at C2_RATE, then resample to C2_RATE.
    rate = C2_RATE * 63.5 / CYCLE
    rnd = random.Random(seed)
    buf = [rnd.random() * 2 - 1 for _ in range(63)]
    raw = []
    for i in range(int(rate * seconds) + 2):
        a = buf[i % 63]
        b = buf[(i + 1) % 63]
        buf[i % 63] = 0.998 * 0.5 * (a + b)
        raw.append(a)
    out = []
    for i in range(int(C2_RATE * seconds)):
        x = i * rate / C2_RATE
        k = int(x)
        out.append(raw[k] + (raw[k + 1] - raw[k]) * (x - k))
    return Instrument("pluck", s8(out, 115), None, 48)


def inst_bell(seconds=0.9):
    f = C2_RATE / CYCLE
    n = int(C2_RATE * seconds)
    parts = [(1.0, 1.0, 2.2), (2.0, 0.35, 4.0), (2.76, 0.3, 5.0),
             (5.4, 0.12, 8.0)]
    v = [sum(a * math.sin(2 * math.pi * f * r * i / C2_RATE)
             * math.exp(-p * i / n) for r, a, p in parts) for i in range(n)]
    return Instrument("bell", s8(v, 110), None, 48)


def inst_kick():
    n, ph, v = int(C3_RATE * 0.22), 0.0, []
    for i in range(n):
        t = i / n
        ph += (45 + 140 * (1 - t) ** 4) / C3_RATE
        v.append(math.sin(2 * math.pi * ph) * (1 - t) ** 1.6
                 + (random.Random(i).random() - 0.5) * 0.3 * max(0, 1 - t * 30))
    return Instrument("kick", s8(v, 125))


def inst_snare():
    n, rnd, v, y = int(C3_RATE * 0.18), random.Random(5), [], 0.0
    for i in range(n):
        t = i / n
        w = rnd.random() * 2 - 1
        y += 0.6 * (w - y)
        body = math.sin(2 * math.pi * 190 * i / C3_RATE) * (1 - t) ** 6
        v.append((y * 0.8 * (1 - t) ** 2.2) + body * 0.6)
    return Instrument("snare", s8(v, 120))


def inst_hat():
    n, rnd, v, prev = int(C3_RATE * 0.05), random.Random(7), [], 0.0
    for i in range(n):
        w = rnd.random() * 2 - 1
        v.append((w - prev) * (1 - i / n) ** 3)   # high-passed
        prev = w
    return Instrument("hat", s8(v, 100))


# ---------------------------------------------------------------------------
# Composition helpers
# ---------------------------------------------------------------------------

ROWS_PER_BAR = 16
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
FLATS = {"Db": "C#", "Eb": "D#", "Gb": "F#", "Ab": "G#", "Bb": "A#"}

CHORDS = {   # quality -> intervals
    "": (0, 4, 7), "m": (0, 3, 7), "7": (0, 4, 10),
}


def midi(name):
    """'A4' / 'C#5' / 'Bb4' -> MIDI number (C4 = 60)."""
    pitch, octave = name[:-1], int(name[-1])
    pitch = FLATS.get(pitch, pitch)
    return 12 * (octave + 1) + NOTE_NAMES.index(pitch)


def chord(sym):
    """'Am' -> (root pitch class, intervals)."""
    root = sym[:2] if len(sym) > 1 and sym[1] in "#b" else sym[:1]
    quality = sym[len(root):]
    root = FLATS.get(root, root)
    return NOTE_NAMES.index(root), CHORDS[quality]


def near(pc, low):
    """The lowest MIDI note of pitch class pc at or above `low`."""
    m = low + (pc - low) % 12
    return m


class Song:
    def __init__(self, title, tempo, channels=6):
        self.title, self.tempo, self.channels = title, tempo, channels
        self.instruments = []
        self.events = {}            # (row, channel) -> [period, inst, fx, param]
        self.rows = 0

    def inst(self, instrument):
        self.instruments.append(instrument)
        return len(self.instruments)          # 1-based sample number

    def put(self, row, ch, inst=0, midi_note=None, effect=0, param=0):
        period = 0
        if midi_note is not None:
            i = self.instruments[inst - 1]
            idx = 24 if i.base_midi is None else midi_note - i.base_midi
            assert 0 <= idx < len(PERIODS), (self.title, i.name, midi_note)
            period = PERIODS[idx]
        cell = self.events.get((row, ch), [0, 0, 0, 0])
        if period:
            cell[0], cell[1] = period, inst
        if effect or param:
            cell[2], cell[3] = effect, param
        self.events[(row, ch)] = cell
        self.rows = max(self.rows, row + 1)

    def note(self, row, ch, inst, m, vol):
        self.put(row, ch, inst, m, 0xC, vol)

    def cut(self, row, ch):
        if (row, ch) not in self.events:
            self.put(row, ch, effect=0xC, param=0)

    def write(self, name):
        bars = (self.rows + 63) // 64
        n_pat = bars
        # Tempo goes on row 0 of the first free effect slot.
        for ch in range(self.channels):
            cell = self.events.get((0, ch), [0, 0, 0, 0])
            if cell[2] == 0 and cell[3] == 0:
                cell[2], cell[3] = 0xF, self.tempo
                self.events[(0, ch)] = cell
                break
        else:
            raise ValueError("no free slot for the tempo")
        out = bytearray(self.title.encode()[:20].ljust(20, b"\0"))
        for k in range(31):
            if k < len(self.instruments):
                i = self.instruments[k]
                ls, ll = i.loop if i.loop else (0, 2)
                out += i.name.encode()[:22].ljust(22, b"\0")
                out += struct.pack(">HBBHH", len(i.data) // 2, 0, 64, ls // 2,
                                   ll // 2 if i.loop else 1)
            else:
                out += b"\0" * 22 + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
        out += bytes([n_pat, 127])
        out += bytes(list(range(n_pat)) + [0] * (128 - n_pat))
        out += ("%dCHN" % self.channels).encode()
        for p in range(n_pat):
            for r in range(64):
                for ch in range(self.channels):
                    per, smp, eff, par = self.events.get((p * 64 + r, ch),
                                                         [0, 0, 0, 0])
                    out += bytes([(smp & 0xF0) | (per >> 8), per & 0xFF,
                                  ((smp & 0x0F) << 4) | eff, par])
        for i in self.instruments:
            data = i.data if len(i.data) % 2 == 0 else i.data + b"\0"
            out += data
        with open(os.path.join(AUDIO_SONGS, name + ".mod"), "wb") as f:
            f.write(out)
        return len(out)


def bar_row(bar, step=0):
    return bar * ROWS_PER_BAR + step


def melody(song, ch, inst, start_bar, text, vol, vib=0x24, echo_ch=None,
           echo_vol=None, echo_delay=3, sustain=True):
    """text: 'E5:6 D5:2 r:4 ...' (note or r for rest : length in rows).
    Long notes get vibrato after a few rows. echo_ch repeats the line
    echo_delay rows later, quieter."""
    row = bar_row(start_bar)
    for tok in text.split():
        name, length = tok.split(":")
        length = int(length)
        if name == "r":
            if sustain:
                song.cut(row, ch)
                if echo_ch is not None:
                    song.cut(row + echo_delay, echo_ch)
        else:
            m = midi(name)
            song.note(row, ch, inst, m, vol)
            if echo_ch is not None:
                song.note(row + echo_delay, echo_ch, inst, m, echo_vol)
            if sustain and vib and length >= 6:
                for k in range(4, length):
                    song.put(row + k, ch, effect=0x4, param=vib)
        row += length
    return row


def drums(song, ch, kit, start_bar, bars, pattern, vols=None):
    """pattern: 16 characters per bar, K kick, S snare, h hat, H loud hat."""
    vols = vols or {"K": 60, "S": 48, "h": 14, "H": 24}
    inst = {"K": kit[0], "S": kit[1], "h": kit[2], "H": kit[2]}
    for b in range(bars):
        for step, c in enumerate(pattern):
            if c in inst:
                song.note(bar_row(start_bar + b, step), ch, inst[c], 60,
                          vols[c])


def bassline(song, ch, inst, start_bar, chords, rhythm, vol, low=36):
    """rhythm: list of (step, interval, octave_up) per bar."""
    for b, sym in enumerate(chords):
        pc, iv = chord(sym)
        root = near(pc, low)
        for step, deg, up in rhythm:
            song.note(bar_row(start_bar + b, step), ch, inst,
                      root + iv[deg] + 12 * up, vol)


def arpeggio(song, ch, inst, start_bar, chords, shape, vol, low=48, every=2):
    """shape: chord-degree indices (0-2, +3 = an octave up) played every
    `every` rows."""
    for b, sym in enumerate(chords):
        pc, iv = chord(sym)
        root = near(pc, low)
        for k, deg in enumerate(shape):
            row = bar_row(start_bar + b, k * every)
            if row >= bar_row(start_bar + b + 1):
                break
            song.note(row, ch, inst, root + iv[deg % 3] + 12 * (deg // 3),
                      vol)


def pad(song, ch, inst, start_bar, chords, vol, low=55, degree=1, vib=0x13):
    """One sustained chord tone per bar (by default the third) with a slow
    vibrato."""
    for b, sym in enumerate(chords):
        pc, iv = chord(sym)
        m = near((pc + iv[degree]) % 12, low)
        r0 = bar_row(start_bar + b)
        song.note(r0, ch, inst, m, vol)
        for k in range(3, ROWS_PER_BAR):
            song.put(r0 + k, ch, effect=0x4, param=vib)


def stabs(song, ch, inst, start_bar, chords, steps, vol, low=55, length=3):
    """Chord stabs: the root, then the chord as a fast arpeggio (0xy)."""
    for b, sym in enumerate(chords):
        pc, iv = chord(sym)
        root = near(pc, low)
        for s in steps:
            r = bar_row(start_bar + b, s)
            song.note(r, ch, inst, root, vol)
            for k in range(1, length):
                song.put(r + k, ch, effect=0x0, param=(iv[1] << 4) | iv[2])
            song.cut(r + length, ch)


# ---------------------------------------------------------------------------
# Songs
# ---------------------------------------------------------------------------

# Channel roles in every song.
BASS, DRUM, LEAD, ECHO, HARM, ARP = range(6)


def menu_song():
    s = Song("Delve - Descent", 84)
    sub, padi, pluck, bell = (s.inst(inst_sub()), s.inst(inst_pad()),
                              s.inst(inst_pluck()), s.inst(inst_bell()))
    kick, snare, hat = s.inst(inst_kick()), s.inst(inst_snare()), s.inst(inst_hat())
    kit = (kick, snare, hat)
    A = ["Am", "F", "C", "G"]
    B = ["Am", "F", "E", "E"]
    C = ["Dm", "Am", "F", "E"]
    form = [(A, None), (A, "m1"), (B, "m2"), (C, "m3"), (A, "m1d"),
            (B, "m2d")]
    tunes = {
        "m1": "E5:6 D5:2 C5:4 B4:4 A4:8 C5:4 F5:4 E5:6 G5:2 E5:4 D5:4 D5:12 r:4",
        "m2": "A4:4 C5:4 E5:4 A5:4 G5:6 F5:2 E5:4 C5:4 B4:8 G#4:4 B4:4 E5:12 r:4",
        "m3": "F5:6 E5:2 D5:8 C5:6 B4:2 A4:8 A4:4 C5:4 F5:4 E5:4 E5:8 D5:4 B4:4",
    }
    arp = [0, 2, 3, 4, 5, 4, 3, 2]
    for sec, (chords, tune) in enumerate(form):
        b0 = sec * 4
        arpeggio(s, ARP, pluck, b0, chords, arp, 34, low=48)
        pad(s, HARM, padi, b0, chords, 26 if tune else 32, low=55)
        bassline(s, BASS, sub, b0, chords,
                 [(0, 0, 0), (8, 0, 0), (12, 2, 0)], 46 if tune else 40)
        if tune:
            melody(s, LEAD, bell, b0, tunes[tune[:2]], 40, vib=0,
                   echo_ch=ECHO, echo_vol=16, sustain=False)
        if tune and tune.endswith("d") or tune == "m3":
            drums(s, DRUM, kit, b0, 4, "K.....h.K.h...h.",
                  {"K": 40, "S": 30, "h": 10, "H": 16})
    return s.write("menu")


def camp_song():
    s = Song("Delve - Embers", 76)
    sub, padi, pluck, bell = (s.inst(inst_sub()), s.inst(inst_pad()),
                              s.inst(inst_pluck(0.9, 11)), s.inst(inst_bell()))
    hat = s.inst(inst_hat())
    P1 = ["F", "C", "Dm", "Bb"]
    P2 = ["F", "C", "Bb", "C"]
    P3 = ["Dm", "Bb", "F", "C"]
    P4 = ["Gm", "Bb", "C", "C7"]
    tunes = {
        1: "A4:4 C5:4 F5:8 E5:4 D5:4 C5:8 D5:4 F5:4 A5:4 G5:4 F5:12 r:4",
        2: "C5:4 A4:4 C5:4 F5:4 G5:8 E5:4 C5:4 D5:6 C5:2 Bb4:8 C5:12 r:4",
        3: "r:8 A4:4 D5:4 F5:8 D5:8 C5:8 A4:8 G4:12 r:4",
        4: "Bb4:4 D5:4 G5:8 F5:4 D5:4 Bb4:8 C5:4 E5:4 G5:4 Bb5:4 "
           "E5:8 G5:8",
    }
    form = [(P1, None), (P1, 1), (P2, 2), (P3, 3), (P4, 4)]
    # Finger-picking: bass note, then the strings above it.
    pick = [0, 2, 3, 2, 4, 2, 3, 2]
    for sec, (chords, tune) in enumerate(form):
        b0 = sec * 4
        arpeggio(s, ARP, pluck, b0, chords, pick, 36, low=48)
        bassline(s, BASS, sub, b0, chords, [(0, 0, 0), (8, 2, 0)], 40,
                 low=41)
        if sec:
            pad(s, HARM, padi, b0, chords, 16, low=57)
            drums(s, DRUM, (hat, hat, hat), b0, 4, "..h...h...h..hh.",
                  {"h": 9})
        if tune:
            melody(s, LEAD, bell, b0, tunes[tune], 38, vib=0, echo_ch=ECHO,
                   echo_vol=14, echo_delay=4, sustain=False)
    return s.write("camp")


def dungeon_song():
    s = Song("Delve - Depths", 128)
    sub, padi, pluck, lead = (s.inst(inst_sub()), s.inst(inst_pad()),
                              s.inst(inst_pluck(0.35, 21)), s.inst(inst_pulse()))
    kit = (s.inst(inst_kick()), s.inst(inst_snare()), s.inst(inst_hat()))
    A1 = ["Dm", "Dm", "Bb", "C"]
    A2 = ["Dm", "Dm", "Bb", "A"]
    B = ["Gm", "Bb", "Dm", "A"]
    D = ["Gm", "A", "Dm", "Dm"]
    tunes = {
        "a1": "D5:6 F5:2 A5:4 G5:2 F5:2 E5:4 F5:4 D5:8 "
              "F5:6 D5:2 Bb4:4 D5:4 E5:6 G5:2 C5:8",
        "a2": "D5:6 F5:2 A5:6 G5:2 F5:4 E5:4 D5:4 C5:4 "
              "D5:6 Bb4:2 F5:8 E5:4 C#5:4 A4:8",
        "b": "G5:6 F5:2 D5:4 Bb4:4 D5:6 F5:2 Bb5:8 A5:6 G5:2 F5:4 D5:4 "
             "E5:4 G5:4 C#5:4 E5:4",
        "d": "G5:16 A5:8 E5:8 F5:16 D5:12 r:4",
    }
    form = [(A1, None), (A1, "a1"), (A2, "a2"), (B, "b"), (D, "d")]
    groove = "K...S..hK.K.S..h"
    riff = [0, 0, 3, 0, 1, 0, 2, 0]
    for sec, (chords, tune) in enumerate(form):
        b0 = sec * 4
        bassline(s, BASS, sub, b0, chords,
                 [(k * 2, 0, k % 2) for k in range(8)], 46)
        arpeggio(s, ARP, pluck, b0, chords, riff, 26 if tune else 34, low=50)
        if tune == "d":
            drums(s, DRUM, kit, b0, 4, "K.......S.......")
            pad(s, HARM, padi, b0, chords, 26, low=57, degree=0)
        else:
            drums(s, DRUM, kit, b0, 4, groove)
            if sec:
                stabs(s, HARM, padi, b0, chords, [2, 10], 20, low=57)
        if tune:
            melody(s, LEAD, lead, b0, tunes[tune], 34, echo_ch=ECHO,
                   echo_vol=14)
    return s.write("dungeon")


def sewers_song():
    s = Song("Delve - Undercroft", 100)
    sub, padi, bell, lead = (s.inst(inst_sub()), s.inst(inst_pad()),
                             s.inst(inst_bell(0.6)), s.inst(inst_pulse()))
    kit = (s.inst(inst_kick()), s.inst(inst_snare()), s.inst(inst_hat()))
    P = ["Em", "F", "Em", "F"]
    Q = ["Am", "F", "E", "E"]
    R = ["Am", "C", "B", "B"]
    tunes = {
        1: "E5:8 B4:4 G4:4 A4:8 C5:4 F5:4 E5:4 G5:4 B5:8 A5:6 G5:2 F5:8",
        2: "E5:8 C5:4 A4:4 F5:8 A5:4 F5:4 G#5:8 F5:4 E5:4 E5:16",
        3: "A4:4 C5:4 E5:4 A5:4 G5:8 E5:4 C5:4 D#5:8 F#5:4 B4:4 B4:12 r:4",
    }
    form = [(P, None), (P, 1), (Q, 2), (P, 1), (R, 3)]
    scale = [midi(n) for n in ("E5", "F5", "G5", "A5", "B5", "C5", "D5")]
    rnd = random.Random(42)
    for sec, (chords, tune) in enumerate(form):
        b0 = sec * 4
        # A slow pulse: root on the beat, quieter octave off it.
        bassline(s, BASS, sub, b0, chords,
                 [(k * 2, 0, 0) for k in range(8)], 36, low=40)
        pad(s, HARM, padi, b0, chords, 20, low=52, degree=0, vib=0x24)
        # Drips: a few high bell notes on random off-beats.
        for b in range(4):
            for step in sorted(rnd.sample(range(1, 16, 2), 3)):
                s.note(bar_row(b0 + b, step), ARP, bell, rnd.choice(scale),
                       14 + rnd.randrange(12))
        if sec:
            drums(s, DRUM, kit, b0, 4, "K..h..h.K.h.S..h",
                  {"K": 44, "S": 30, "h": 10, "H": 16})
        if tune:
            melody(s, LEAD, lead, b0, tunes[tune], 26, vib=0x35,
                   echo_ch=ECHO, echo_vol=11, echo_delay=6)
    return s.write("sewers")


def caverns_song():
    """The Crystal Caverns: slow, wide and glassy; bells drip like
    water off crystal."""
    s = Song("Delve - Hollow", 88)
    sub, padi, bell, pluck, lead = (s.inst(inst_sub()), s.inst(inst_pad()),
                                    s.inst(inst_bell(1.2)),
                                    s.inst(inst_pluck(0.9, 7)),
                                    s.inst(inst_pulse()))
    kit = (s.inst(inst_kick()), s.inst(inst_snare()), s.inst(inst_hat()))
    P = ["Dm", "G", "Dm", "C"]
    Q = ["Bb", "C", "Dm", "Dm"]
    R = ["F", "G", "Am", "Dm"]
    tunes = {
        1: "D5:8 F5:4 A5:4 G5:8 B4:8 D5:4 E5:4 F5:8 E5:4 C5:4 D5:8",
        2: "A5:8 G5:4 F5:4 E5:8 D5:4 C5:4 D5:6 F5:2 A5:8 G5:12 r:4",
        3: "F5:4 A5:4 A5:8 B5:8 G5:8 A5:4 E5:4 C5:8 D5:16",
    }
    form = [(P, None), (P, 1), (Q, 2), (P, 1), (R, 3)]
    scale = [midi(n) for n in ("D5", "E5", "F5", "A5", "C5", "G5")]
    rnd = random.Random(2721)
    for sec, (chords, tune) in enumerate(form):
        b0 = sec * 4
        bassline(s, BASS, sub, b0, chords, [(0, 0, 0), (10, 0, 0)], 34, low=38)
        pad(s, HARM, padi, b0, chords, 18, low=50, degree=1, vib=0x23)
        arpeggio(s, ARP, pluck, b0, chords, [0, 2, 4, 1, 3, 2, 4, 5], 18,
                 low=50, every=2)
        # Crystal drips on the lead's off-beats while it is quiet.
        if not tune:
            for b in range(4):
                for step in sorted(rnd.sample(range(1, 16, 2), 2)):
                    s.note(bar_row(b0 + b, step), LEAD, bell,
                           rnd.choice(scale), 12 + rnd.randrange(10))
        if sec:
            drums(s, DRUM, kit, b0, 4, "K.......K..h....",
                  {"K": 38, "S": 24, "h": 9, "H": 14})
        if tune:
            melody(s, LEAD, lead, b0, tunes[tune], 22, vib=0x24,
                   echo_ch=ECHO, echo_vol=10, echo_delay=9)
    return s.write("caverns")


def molten_song():
    """The Molten Depths: heavy and driving, E Phrygian dominant
    (the raised third gives it its heat)."""
    s = Song("Delve - Crucible", 138)
    sub, padi, pluck, lead = (s.inst(inst_sub()), s.inst(inst_pad()),
                              s.inst(inst_pluck(0.3, 19)), s.inst(inst_pulse()))
    kit = (s.inst(inst_kick()), s.inst(inst_snare()), s.inst(inst_hat()))
    P = ["E", "F", "E", "Dm"]
    Q = ["Am", "F", "E", "E"]
    R = ["F", "G", "Am", "E"]
    tunes = {
        1: "E5:4 F5:4 G#5:8 A5:4 G#5:4 F5:8 E5:6 F5:2 G#5:4 B5:4 A5:8 G#5:8",
        2: "B4:8 C5:4 D5:4 E5:8 F5:4 E5:4 D5:6 C5:2 B4:8 E5:16",
        3: "A5:4 G#5:4 F5:8 E5:4 F5:4 G#5:8 A5:8 B5:8 G#5:16",
    }
    form = [(P, None), (P, 1), (Q, 2), (R, 3), (P, 1)]
    chug = [0, 0, 1, 0, 0, 2, 0, 3]
    for sec, (chords, tune) in enumerate(form):
        b0 = sec * 4
        bassline(s, BASS, sub, b0, chords,
                 [(k * 2, 0, 1 if k in (3, 6) else 0) for k in range(8)], 38,
                 low=38)
        arpeggio(s, ARP, pluck, b0, chords, chug, 26, low=50)
        drums(s, DRUM, kit, b0, 4, "K..KS..KK.K.S..h" if sec else "K...K...K...K.KK",
              {"K": 44, "S": 34, "h": 10, "H": 16})
        stabs(s, HARM, padi, b0, chords, [0, 10], 18, low=55, length=3)
        if tune:
            melody(s, LEAD, lead, b0, tunes[tune], 28, vib=0x35,
                   echo_ch=ECHO, echo_vol=12, echo_delay=6)
    return s.write("molten")


def boss_song():
    s = Song("Delve - Warlord", 150)
    sub, padi, pluck, lead = (s.inst(inst_sub()), s.inst(inst_pad()),
                              s.inst(inst_pluck(0.25, 31)), s.inst(inst_pulse()))
    kit = (s.inst(inst_kick()), s.inst(inst_snare()), s.inst(inst_hat()))
    I = ["Cm", "Cm", "Ab", "Bb"]
    J = ["Cm", "Cm", "Ab", "G"]
    K = ["Fm", "Ab", "Bb", "G"]
    tunes = {
        1: "C5:4 Eb5:4 G5:4 Eb5:4 F5:2 Eb5:2 D5:4 C5:8 "
           "C5:4 Eb5:4 Ab5:8 G5:4 F5:4 D5:8",
        2: "G5:6 Ab5:2 G5:4 Eb5:4 C5:6 D5:2 Eb5:8 "
           "Eb5:4 F5:4 Ab5:4 G5:4 B4:8 D5:4 G5:4",
        3: "F5:4 Ab5:4 C5:4 F5:4 Eb5:6 C5:2 Ab4:8 "
           "Bb4:4 D5:4 F5:4 Bb5:4 G5:8 F5:4 D5:4",
    }
    form = [(I, None), (I, 1), (J, 2), (K, 3), (J, 2)]
    chug = [0, 0, 3, 0, 0, 1, 0, 2]
    for sec, (chords, tune) in enumerate(form):
        b0 = sec * 4
        bassline(s, BASS, sub, b0, chords,
                 [(k * 2, 0, 1 if k in (3, 7) else 0) for k in range(8)], 50)
        arpeggio(s, ARP, pluck, b0, chords, chug, 30, low=48)
        drums(s, DRUM, kit, b0, 4, "K.hKS.hKK.hKS.hS" if sec else "K.h.K.h.K.h.KKKK")
        stabs(s, HARM, padi, b0, chords, [0, 6, 12], 22, low=55, length=2)
        if tune:
            melody(s, LEAD, lead, b0, tunes[tune], 36, vib=0x46, echo_ch=ECHO,
                   echo_vol=14)
    return s.write("boss")


AUDIO_SONGS = None   # set by main() for --songs

SONGS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                         "art", "generated", "music")


def main():
    import sys
    os.makedirs(AUDIO, exist_ok=True)
    # An early placeholder track and the synthesized songs no longer
    # belong in the soundbank.
    for old in ("game.mod", "menu.mod", "camp.mod", "dungeon.mod",
                "sewers.mod", "boss.mod", "caverns.mod", "molten.mod"):
        path = os.path.join(AUDIO, old)
        if os.path.exists(path):
            os.remove(path)
    make_sfx()
    print("sound effects written to", os.path.normpath(AUDIO))
    if "--songs" in sys.argv:
        global AUDIO_SONGS
        AUDIO_SONGS = SONGS_DIR
        os.makedirs(SONGS_DIR, exist_ok=True)
        for fn in (menu_song, camp_song, dungeon_song, sewers_song, boss_song,
                   caverns_song, molten_song):
            size = fn()
            print("  %-14s %6d bytes" % (fn.__name__, size))
        print("songs written to", os.path.normpath(SONGS_DIR))


if __name__ == "__main__":
    main()
