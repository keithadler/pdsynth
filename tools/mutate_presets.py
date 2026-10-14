#!/usr/bin/env python3
"""Break a preset on purpose, one change at a time, and require pd_character to notice.

Each mutant gets its own copy of the sources and its own build, because a build reused across
mutants can pick up a stale object. A mutant that makes pd_character crash or hang counts as caught,
and every failed check is listed so it is clear each one failed for the reason it was written for.

    python3 -I tools/mutate_presets.py <scratch directory> [preset name ...]
"""
import os, shutil, subprocess, sys

S = sys.argv[1]
only = set(sys.argv[2:])
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
gen = open(os.path.join(root, "tools", "gen-presets.py")).read()
END = "]\n\nHEADER = '''/*"

def preset_text(name):
    i = gen.index(' Preset("%s"' % name)
    j = gen.find("\n Preset(", i + 1)
    k = gen.index(END)
    j = k if j < 0 or j > k else j + 1
    return i, j

def mutate(name, *edits):
    """edits: (old, new) pairs applied inside one preset, each old text must be present"""
    i, j = preset_text(name)
    text = gen[i:j]
    for old, new in edits:
        assert old in text, (name, old)
        text = text.replace(old, new)
    return gen[:i] + text + gen[j:]

AE = "aenv=env([55,40,40],[99,95,0],1,2)"
D = "Didgeridoo"
W = "Tin Whistle"
MUTANTS = [
 (D, "sweep envelope held flat", mutate(D, ("[30,85,30,85,30,85,52,0],6,7)", "[52,52,52,52,52,52,52,0],6,7)"))),
 (D, "sweep never starts (sustain at step 1)", mutate(D, ("[30,85,30,85,30,85,52,0],6,7)", "[30,85,30,85,30,85,52,0],1,7)"))),
 (D, "no drone line", mutate(D, ("L('dsine', .32, -3,", "L('dsine', .0, -3,"))),
 (D, "drone swamps the rest", mutate(D, ("L('dsine', .32, -3,", "L('dsine', .95, -3,"))),
 (D, "resonant line is a plain saw", mutate(D, ("L('rsaw', .52, 0,", "L('saw', .52, 0,"))),
 (D, "only a sine drone", mutate(D, ("L('rsaw', .52, 0,", "L('rsaw', .0, 0,"), ("L('sawpulse', .26, +5,", "L('sawpulse', .0, +5,"), ("L('sawpulse', .14, 0,", "L('sawpulse', .0, 0,"))),
 (D, "amplitude decays like a pluck", mutate(D, (AE, "aenv=env([55,22,40],[99,2,0],1,2)"))),
 (D, "attack is a click", mutate(D, (AE, "aenv=env([99,40,40],[99,95,0],1,2)"))),
 (D, "attack is a slow swell", mutate(D, (AE, "aenv=env([22,40,40],[99,95,0],1,2)"))),

 (W, "no chiff", mutate(W, ("L('sawpulse', .18, 0, octave=2,", "L('sawpulse', .0, 0, octave=2,"))),
 (W, "chiff never stops", mutate(W, ("aenv=env([99,52,40],[99,4,0],1,2)", "aenv=env([99,52,40],[99,90,0],1,2)"))),
 (W, "no scoop", mutate(W, ("pdepth=0.5,", "pdepth=0.0,"))),
 (W, "scoop too deep", mutate(W, ("pdepth=0.5,", "pdepth=3.0,"))),
 (W, "pitch comes down instead of up", mutate(W, ("penv=env([99,38,80],[45,99,95],1,2)", "penv=env([99,38,80],[99,45,95],1,2)"))),
 (W, "tone is a bent sawtooth, a buzz", mutate(W, ("L('dsine', .62, 0, wenv=env([58,42,46],[16,12,0]", "L('saw', .62, 0, wenv=env([58,42,46],[99,99,0]"))),
 (W, "decays like a pluck", mutate(W, ("aenv=env([52,42,46],[99,94,0],1,2),\n     pdepth", "aenv=env([52,42,46],[99,3,0],1,2),\n     pdepth"))),
 (W, "no air in the held tone", mutate(W, ("L('sawpulse', .16, 0, octave=2,", "L('sawpulse', .0, 0, octave=2,"))),
 (W, "a hiss, not a breath", mutate(W, ("L('sawpulse', .16, 0, octave=2,", "L('sawpulse', 1.0, 0, octave=2,"))),
]

caught = total = 0
for n, (pname, name, text) in enumerate(MUTANTS):
    if only and pname not in only:
        continue
    total += 1
    d = os.path.join(S, f"m{n}")
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    for item in ["src", "tools", "tests", "CMakeLists.txt", "COPYING", "banks", "docs", "plugin"]:
        a = os.path.join(root, item)
        if os.path.isdir(a):
            shutil.copytree(a, os.path.join(d, item))
        elif os.path.exists(a):
            shutil.copy(a, d)
    open(os.path.join(d, "tools", "gen-presets.py"), "w").write(text)
    if subprocess.run(["python3", "-I", os.path.join(d, "tools", "gen-presets.py")], capture_output=True).returncode:
        print("GENERATOR FAIL", pname, name); continue
    b = os.path.join(d, "b")
    cfg = subprocess.run(["cmake", "-S", d, "-B", b, "-DPDSYNTH_BUILD_PLUGIN=OFF", "-DCMAKE_BUILD_TYPE=Release"], capture_output=True)
    blt = subprocess.run(["cmake", "--build", b, "--target", "pd_character", "-j8"], capture_output=True)
    if cfg.returncode or blt.returncode:
        print("BUILD FAIL", pname, name); continue
    try:
        env = dict(os.environ, PD_CHARACTER_VERBOSE="1")
        r = subprocess.run([os.path.join(b, "pd_character")], capture_output=True, timeout=180, env=env)
        lines = [l.strip() for l in r.stdout.decode().splitlines() if pname in l]
        failed = [l for l in lines if l.startswith("FAIL")]
        res = "caught" if (r.returncode != 0 and failed) else "SURVIVED"
        # a caught mutant lists what failed; a survivor lists every measurement, to show why it passed
        detail = "\n               ".join(l[:130] for l in (failed if failed else lines))
    except subprocess.TimeoutExpired:
        res, detail = "caught (hang)", ""
    caught += res.startswith("caught")
    print(f"{res:14s} [{pname}] {name}\n{'':15s}{detail}")
print(f"{caught}/{total} caught")
sys.exit(0 if caught == total else 1)
