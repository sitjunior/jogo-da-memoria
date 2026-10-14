#!/usr/bin/env python3
"""Gera a música e os efeitos sonoros do jogo (assets/audio/*.wav).

Tudo é sintetizado aqui, então o áudio é original do projeto (sem direitos de terceiros).
Uso: python3 tools/make_audio.py [pasta-de-saída]   (padrão: assets/audio)
"""
import math
import os
import random
import struct
import sys
import wave

SR = 22050
random.seed(7)


def hz(midi):
    return 440.0 * 2 ** ((midi - 69) / 12.0)


class Track:
    def __init__(self, seconds, tail=2.0):
        self.n = int(SR * seconds)
        self.buf = [0.0] * (self.n + int(SR * tail))

    def add(self, start, samples, gain=1.0):
        i0 = int(start * SR)
        for k, v in enumerate(samples):
            j = i0 + k
            if j < len(self.buf):
                self.buf[j] += v * gain

    def folded(self):
        """Dobra a cauda sobre o começo para a música emendar sem estalo."""
        out = self.buf[: self.n]
        for k, v in enumerate(self.buf[self.n:]):
            out[k % self.n] += v
        return out


def marimba(freq, length=0.5):
    n = int(SR * length)
    out = []
    for i in range(n):
        t = i / SR
        env = math.exp(-t * 6.5) * min(1.0, t * 400)
        s = (math.sin(2 * math.pi * freq * t)
             + 0.35 * math.sin(2 * math.pi * freq * 2 * t) * math.exp(-t * 14)
             + 0.12 * math.sin(2 * math.pi * freq * 4 * t) * math.exp(-t * 30))
        out.append(s * env)
    return out


def bass(freq, length=0.55):
    n = int(SR * length)
    out = []
    for i in range(n):
        t = i / SR
        ph = (t * freq) % 1.0
        tri = 4 * abs(ph - 0.5) - 1
        out.append(tri * math.exp(-t * 3.2) * min(1.0, t * 300))
    return out


def pad(freqs, length):
    n = int(SR * length)
    out = []
    for i in range(n):
        t = i / SR
        env = min(1.0, t * 3) * min(1.0, (length - t) * 3)
        out.append(sum(math.sin(2 * math.pi * f * t) for f in freqs) / len(freqs) * env)
    return out


def shaker(length=0.05):
    n = int(SR * length)
    return [(random.random() * 2 - 1) * math.exp(-i / SR * 70) for i in range(n)]


def music():
    bpm = 116
    eighth = 60.0 / bpm / 2
    bars = 16
    t = Track(bars * 8 * eighth)

    chords = {"C": (48, [60, 64, 67]), "Am": (45, [57, 60, 64]),
              "F": (41, [53, 57, 60]), "G": (43, [55, 59, 62])}
    prog = ["C", "Am", "F", "G", "C", "Am", "F", "G",
            "C", "F", "C", "G", "Am", "F", "G", "C"]
    N = None
    melody = [
        [76, 79, 76, 72, 76, 79, 84, 79], [81, 76, 72, 76, 81, 76, 72, N],
        [77, 81, 77, 72, 77, 81, 84, 81], [79, 83, 79, 74, 79, 74, 71, N],
        [76, 79, 76, 72, 76, 79, 84, 79], [81, 84, 81, 76, 81, 76, 72, N],
        [77, 81, 84, 81, 77, 72, 77, 81], [79, 83, 86, 83, 79, 74, 71, N],
        [79, N, 76, N, 72, N, 76, N], [77, N, 81, N, 84, N, 81, N],
        [79, N, 76, N, 72, 76, 79, N], [74, N, 79, N, 83, N, 79, N],
        [81, N, 84, N, 81, N, 76, N], [77, 81, 84, 81, 77, 81, 84, N],
        [83, N, 79, N, 74, 79, 83, N], [84, N, 79, 76, 72, N, N, N],
    ]
    for b in range(bars):
        root, tones = chords[prog[b]]
        t0 = b * 8 * eighth
        t.add(t0, pad([hz(m) for m in tones], 8 * eighth), 0.07)
        t.add(t0, bass(hz(root)), 0.34)
        t.add(t0 + 4 * eighth, bass(hz(root)), 0.26)
        for e in range(8):
            if e % 2 == 1:
                t.add(t0 + e * eighth, shaker(), 0.05)
            note = melody[b][e]
            if note is not None:
                t.add(t0 + e * eighth, marimba(hz(note)), 0.30)
    return t.folded()


def sfx_flip():
    n = int(SR * 0.09)
    return [(math.sin(2 * math.pi * 900 * i / SR) * 0.5
             + (random.random() * 2 - 1) * 0.25) * math.exp(-i / SR * 45) for i in range(n)]


def arpeggio(notes, step, length):
    t = Track(step * len(notes) + length, tail=0.0)
    for k, m in enumerate(notes):
        t.add(k * step, marimba(hz(m), length), 0.6)
    return t.buf


def sfx_wrong():
    n = int(SR * 0.32)
    out = []
    for i in range(n):
        t = i / SR
        f = 330 - 140 * (t / 0.32)
        out.append(math.sin(2 * math.pi * f * t) * math.exp(-t * 7) * 0.7)
    return out


def sfx_click():
    n = int(SR * 0.04)
    return [math.sin(2 * math.pi * 1300 * i / SR) * math.exp(-i / SR * 90) * 0.5 for i in range(n)]


def write(path, samples, peak=0.85):
    top = max(1e-9, max(abs(v) for v in samples))
    scale = peak / top
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, v * scale)) * 32767))
                               for v in samples))


def main(out):
    os.makedirs(out, exist_ok=True)
    write(os.path.join(out, "music.wav"), music(), peak=0.8)
    write(os.path.join(out, "flip.wav"), sfx_flip())
    write(os.path.join(out, "match.wav"), arpeggio([84, 88, 91], 0.09, 0.45))
    write(os.path.join(out, "wrong.wav"), sfx_wrong())
    write(os.path.join(out, "win.wav"), arpeggio([72, 76, 79, 84, 79, 84, 88, 91, 96], 0.11, 0.9))
    write(os.path.join(out, "click.wav"), sfx_click())


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "assets", "audio"))
