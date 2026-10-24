"""M0 analysis: envelope frequency, aliasing, LFSR, DAC/mixing levels. Reads the outputs of the C++ bench."""
import re, sys, numpy as np
from scipy.signal import get_window
OUT = sys.argv[1]
SR = 48000
def load(name, kind):
    if kind == 'f32': return np.fromfile(f"{OUT}/{name}_hq48.f32", dtype=np.float32).astype(float)
    return np.fromfile(f"{OUT}/{name}_{kind}48.i16", dtype=np.int16).astype(float)

def spectrum(x, skip=SR):
    x = x[skip:]  # skips the start-up of the DC filter
    n = 1 << int(np.floor(np.log2(len(x))))
    x = x[:n] - np.mean(x[:n])
    w = get_window('blackmanharris', n)
    X = np.abs(np.fft.rfft(x * w)) / np.sum(w) * 2
    f = np.fft.rfftfreq(n, 1 / SR)
    return f, X

print("== Envelope frequency (shape 8, EP=400, 2 MHz clock) ==")
print(f"datasheet clk/(256*EP) = {2e6/(256*400):.3f} Hz; prototype formula 2e6/(256*32*EP) = {2e6/(256*32*400):.4f} Hz")
for kind in ('crate', 'aabox', 'f32'):
    f, X = spectrum(load('env_saw', kind))
    k = np.argmax(X[5:]) + 5
    print(f"  {kind:6s}: measured fundamental peak = {f[k]:.3f} Hz")
print("== Buzzer (shape $A triangle, EP=30) ==")
print(f"  expected clk/(512*EP) = {2e6/(512*30):.2f} Hz (triangle = 64 steps)")
for kind in ('crate', 'aabox', 'f32'):
    f, X = spectrum(load('buzzer', kind)); k = np.argmax(X[5:]) + 5
    print(f"  {kind:6s}: {f[k]:.2f} Hz")

print("== Aliasing: square TP=12 (10416.7 Hz), 48 kHz output ==")
f0 = 2e6 / (16 * 12)
harm = [h * f0 for h in range(1, 40, 2)]
for kind in ('crate', 'aabox', 'f32'):
    f, X = spectrum(load('sq10k', kind))
    ref = X[np.argmin(abs(f - f0)) - 3: np.argmin(abs(f - f0)) + 4].max()
    mask = (f > 20) & (f < 20000)
    for h in harm:
        if h < SR / 2: mask &= ~(abs(f - h) < 30)
    worst = 20 * np.log10(X[mask].max() / ref)
    fw = f[mask][np.argmax(X[mask])]
    print(f"  {kind:6s}: worst aliased component < 20 kHz = {worst:7.1f} dB (at {fw:8.1f} Hz)")

print("== Noise (17-bit LFSR): period and equivalence of the sequences ==")
def seq_galois(n):
    r = 1; out = []
    for _ in range(n):
        lsb = r & 1; r >>= 1
        if lsb: r ^= 0x12000
        out.append(lsb)
    return np.array(out, dtype=np.int8)
def seq_fib(n, tap):
    r = 1; out = []
    for _ in range(n):
        b = (r ^ (r >> tap)) & 1
        r = (r >> 1) | (b << 16); out.append(b)
    return np.array(out, dtype=np.int8)
def lfsr_period(kind):
    r = 1; start = None
    for i in range(1, 1 << 18):
        if kind == 'galois':
            lsb = r & 1; r >>= 1
            if lsb: r ^= 0x12000
        else:
            b = (r ^ (r >> kind)) & 1; r = (r >> 1) | (b << 16)
        if r == 1: return i
    return None
for name, k in (('crate Galois $12000', 'galois'), ('AtariAudio b0^b2', 2), ('MAME/prototype b0^b3', 3)):
    print(f"  {name:22s} period = {lfsr_period(k)}")
N = 1 << 18
g, a, m = seq_galois(N), seq_fib(N, 2), seq_fib(N, 3)
def same_up_to_shift(x, y, P=131071):
    # looks for a shift where the first 2000 bits match
    target = x[:64].tobytes(); yb = y.tobytes()
    i = yb.find(target)
    while i != -1 and i < P:
        if np.array_equal(x[:4000], y[i:i+4000]): return i
        i = yb.find(target, i + 1)
    return None
print(f"  crate == MAME up to a shift: {same_up_to_shift(g, m)}; crate == reversed MAME: {same_up_to_shift(g, m[::-1].copy())}")
print(f"  crate == AtariAudio up to a shift: {same_up_to_shift(g, a)}; AtariAudio == MAME: {same_up_to_shift(a, m)}; AtariAudio == reversed MAME: {same_up_to_shift(a, m[::-1].copy())}")

print("== Levels: fixed volume 15 against envelope at maximum ==")
LOG = [152,181,215,255,304,362,430,511,608,724,861,1023,1217,1448,1722,2047,2435,2896,3444,4095,4870,5792,6888,8191,9741,11584,13776,16383,19483,23169,27553,32767]
LOG = [v // 3 for v in LOG]
print(f"  crate: vol 15 -> index 30 = {LOG[30]}, envelope max -> index 31 = {LOG[31]}: deviation {20*np.log10(LOG[30]/LOG[31]):.2f} dB")
src = open(sys.argv[2] if len(sys.argv) > 2 else 'ym2149_tables.h').read()
body = src.split('s_ym2149RecordedMixTable[32 * 32 * 32] =')[1]
T = np.array([int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]+', body)][:32768])
print(f"  AtariAudio: vol 15 -> index 31 (2v+1) = envelope max: deviation 0 dB")
one = T[31]; two = T[31 | 31 << 5]; three = T[32767]
print(f"  Measured mixing: 1 channel at max = {one}, 2 channels = {two} ({two/one:.3f}x), 3 channels = {three} ({three/one:.3f}x); linear sum of the crate = 1x / 2x / 3x")
single = [T[i] for i in range(32)]
ideal = [LOG[i] * 3 for i in range(32)]
dev = [20*np.log10(single[i]/single[31]) - 20*np.log10(ideal[i]/ideal[31]) for i in range(2,32)]
print(f"  Single channel, deviation measured DAC - ideal log scale (1.5 dB/step): min {min(dev):+.2f} dB, max {max(dev):+.2f} dB")

print("== Distance crate / AtariAudio on 'tone3' and 'hihat' (48 kHz) ==")
for sc in ('tone3', 'hihat', 'full'):
    f, A = spectrum(load(sc, 'crate')); _, B = spectrum(load(sc, 'aabox'))
    band = (f > 20) & (f < 20000)
    lsd = np.sqrt(np.mean((20*np.log10(A[band] / A[band].max() + 1e-9) - 20*np.log10(B[band] / B[band].max() + 1e-9)) ** 2))
    x = load(sc, 'crate')[SR:]; y = load(sc, 'aabox')[SR:]
    corr = np.corrcoef(x, y)[0, 1]
    rms = 20*np.log10(np.std(x) / np.std(y))
    print(f"  {sc:6s}: log-spectral distance = {lsd:5.1f} dB; correlation = {corr:.3f}; RMS level deviation = {rms:+.2f} dB")
