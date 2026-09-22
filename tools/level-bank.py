#!/usr/bin/env python3
"""
Trim the factory bank's line levels until nothing clips and the whole thing
sits at one level. Measured with the same gated loudness the bank check uses,
because a fixed window over a short pluck reports a brief patch as a quiet one.

    python3 tools/level-bank.py
"""
import re, subprocess, sys, statistics, math, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC  = ROOT / 'src' / 'pd_presets.c'

def build_and_measure():
    subprocess.run(['clang', '-O2', '-I', 'src', '-o', '/tmp/bankcheck',
                    'tools/pd_bankcheck.c', 'src/pd_presets.c', 'src/pd_voice.c',
                    'src/pd_osc.c', 'src/pd_env.c', '-lm'],
                   cwd=ROOT, check=True)
    out = subprocess.run(['/tmp/bankcheck'], cwd=ROOT, capture_output=True, text=True).stdout
    rows = []
    for line in out.splitlines():
        m = re.match(r'^(\S.{0,15})\s+(\w+)\s+([\d.]+)\s+([\d.]+)\s+(\d+)\s+([\d.]+)x', line)
        if m:
            rows.append({'name': m.group(1).strip(), 'peak': float(m.group(3)),
                         'loud': float(m.group(4))})
    return rows

def apply_gains(gains):
    """gains: {preset name: linear factor} applied to both lines' level fields"""
    text = SRC.read_text()
    lines = text.split('\n')
    for i, line in enumerate(lines):
        m = re.match(r'\s*\{ "([^"]+)", "[^"]+",', line)
        if not m: continue
        g = gains.get(m.group(1))
        if not g or abs(g - 1.0) < 0.005: continue
        # the level is the fifth field of each line block: ..., detune, LEVEL, {
        def fix(mm):
            v = float(mm.group(1)) * g
            return '%.4f, {' % min(1.0, max(0.0, v))
        lines[i] = re.sub(r'([\d.]+), \{', fix, line)
    SRC.write_text('\n'.join(lines))

TARGET_PEAK = 0.82
for round_ in range(7):
    rows = build_and_measure()
    if not rows:
        sys.exit('could not read the bank check output')
    med = statistics.median(r['loud'] for r in rows)
    hot = [r for r in rows if r['peak'] > 0.95]
    spread = 20 * math.log10(max(r['loud'] for r in rows) / min(r['loud'] for r in rows))
    within = sum(1 for r in rows if abs(20 * math.log10(r['loud'] / med)) <= 3.0)
    print('round %d: %d clipping, spread %.1f dB, %d of %d within 3 dB'
          % (round_, len(hot), spread, within, len(rows)))
    if not hot and within >= len(rows) - 2:
        break

    gains = {}
    for r in rows:
        g = 1.0
        # never let a preset reach the limiter: what it does there is not what
        # it was designed to do
        if r['peak'] > TARGET_PEAK:
            g *= TARGET_PEAK / r['peak']
        db = 20 * math.log10(med / max(r['loud'], 1e-9))
        if abs(db) > 1.2:
            g *= 10 ** (max(-4.0, min(4.0, db)) / 20.0 * 0.6)
        if r['peak'] * g > TARGET_PEAK:
            g = TARGET_PEAK / r['peak']
        if abs(g - 1.0) > 0.005:
            gains[r['name']] = g
    if not gains:
        break
    apply_gains(gains)

rows = build_and_measure()
med = statistics.median(r['loud'] for r in rows)
print('\nfinal: peak %.3f max, spread %.1f dB, %d of %d within 3 dB'
      % (max(r['peak'] for r in rows),
         20 * math.log10(max(r['loud'] for r in rows) / min(r['loud'] for r in rows)),
         sum(1 for r in rows if abs(20 * math.log10(r['loud'] / med)) <= 3.0), len(rows)))
