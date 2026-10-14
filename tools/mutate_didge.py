#!/usr/bin/env python3
"""Break the Didgeridoo preset on purpose, one change at a time, and require pd_character to notice.

Each mutant gets its own copy of the sources and its own build, because a build reused across
mutants can pick up a stale object. A mutant that makes pd_character crash or hang counts as caught.

    python3 -I tools/mutate_didge.py <scratch directory>
"""
import os, shutil, subprocess, sys

S = sys.argv[1]
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
gen = open(os.path.join(root, "tools", "gen-presets.py")).read()
i = gen.index(' Preset("Didgeridoo"')
j = gen.index("]\n\nHEADER = '''/*")
didge = gen[i:j]

def mutate(old, new):
    assert didge.count(old) == 1, old
    return gen[:i] + didge.replace(old, new) + gen[j:]

MUTANTS = [
 ("sweep envelope held flat", mutate("wenv=env([70,34,34,34,34,34,40,60],[30,85,30,85,30,85,52,0],6,7)", "wenv=env([70,34,34,34,34,34,40,60],[52,52,52,52,52,52,52,0],6,7)")),
 ("sweep never starts (sustain at step 1)", mutate("[30,85,30,85,30,85,52,0],6,7)", "[30,85,30,85,30,85,52,0],1,7)")),
 ("no drone line", mutate("L('dsine', .32, -3,", "L('dsine', .0, -3,")),
 ("drone swamps the rest", mutate("L('dsine', .32, -3,", "L('dsine', .95, -3,")),
 ("resonant line is a plain saw", mutate("L('rsaw', .52, 0,", "L('saw', .52, 0,")),
 ("only a sine drone", mutate("L('rsaw', .52, 0,", "L('rsaw', .0, 0,").replace("L('sawpulse', .26, +5,", "L('sawpulse', .0, +5,").replace("L('sawpulse', .14, 0,", "L('sawpulse', .0, 0,")),
 ("amplitude decays like a pluck", mutate("L('dsine', .32, -3, wenv=env([40,34,38],[60,56,0],1,2), aenv=env([55,40,40],[99,95,0],1,2))", "L('dsine', .32, -3, wenv=env([40,34,38],[60,56,0],1,2), aenv=env([55,22,40],[99,2,0],1,2))").replace("L('rsaw', .52, 0, wenv=env([70,34,34,34,34,34,40,60],[30,85,30,85,30,85,52,0],6,7),\n     aenv=env([55,40,40],[99,95,0],1,2))", "L('rsaw', .52, 0, wenv=env([70,34,34,34,34,34,40,60],[30,85,30,85,30,85,52,0],6,7),\n     aenv=env([55,22,40],[99,2,0],1,2))").replace("L('sawpulse', .26, +5, wenv=env([40,34,38],[74,70,0],1,2), aenv=env([55,40,40],[99,95,0],1,2))", "L('sawpulse', .26, +5, wenv=env([40,34,38],[74,70,0],1,2), aenv=env([55,22,40],[99,2,0],1,2))").replace("L('sawpulse', .14, 0, wenv=env([40,34,38],[80,70,0],1,2), aenv=env([55,40,40],[99,95,0],1,2),", "L('sawpulse', .14, 0, wenv=env([40,34,38],[80,70,0],1,2), aenv=env([55,22,40],[99,2,0],1,2),")),
 ("attack is a click", mutate("L('rsaw', .52, 0, wenv=env([70,34,34,34,34,34,40,60],[30,85,30,85,30,85,52,0],6,7),\n     aenv=env([55,40,40],[99,95,0],1,2))", "L('rsaw', .52, 0, wenv=env([70,34,34,34,34,34,40,60],[30,85,30,85,30,85,52,0],6,7),\n     aenv=env([99,40,40],[99,95,0],1,2))").replace("L('dsine', .32, -3, wenv=env([40,34,38],[60,56,0],1,2), aenv=env([55,40,40],[99,95,0],1,2))", "L('dsine', .32, -3, wenv=env([40,34,38],[60,56,0],1,2), aenv=env([99,40,40],[99,95,0],1,2))").replace("L('sawpulse', .26, +5, wenv=env([40,34,38],[74,70,0],1,2), aenv=env([55,40,40],[99,95,0],1,2))", "L('sawpulse', .26, +5, wenv=env([40,34,38],[74,70,0],1,2), aenv=env([99,40,40],[99,95,0],1,2))").replace("L('sawpulse', .14, 0, wenv=env([40,34,38],[80,70,0],1,2), aenv=env([55,40,40],[99,95,0],1,2),", "L('sawpulse', .14, 0, wenv=env([40,34,38],[80,70,0],1,2), aenv=env([99,40,40],[99,95,0],1,2),")),
 ("attack is a slow swell", mutate("L('rsaw', .52, 0, wenv=env([70,34,34,34,34,34,40,60],[30,85,30,85,30,85,52,0],6,7),\n     aenv=env([55,40,40],[99,95,0],1,2))", "L('rsaw', .52, 0, wenv=env([70,34,34,34,34,34,40,60],[30,85,30,85,30,85,52,0],6,7),\n     aenv=env([22,40,40],[99,95,0],1,2))").replace("L('dsine', .32, -3, wenv=env([40,34,38],[60,56,0],1,2), aenv=env([55,40,40],[99,95,0],1,2))", "L('dsine', .32, -3, wenv=env([40,34,38],[60,56,0],1,2), aenv=env([22,40,40],[99,95,0],1,2))").replace("L('sawpulse', .26, +5, wenv=env([40,34,38],[74,70,0],1,2), aenv=env([55,40,40],[99,95,0],1,2))", "L('sawpulse', .26, +5, wenv=env([40,34,38],[74,70,0],1,2), aenv=env([22,40,40],[99,95,0],1,2))").replace("L('sawpulse', .14, 0, wenv=env([40,34,38],[80,70,0],1,2), aenv=env([55,40,40],[99,95,0],1,2),", "L('sawpulse', .14, 0, wenv=env([40,34,38],[80,70,0],1,2), aenv=env([22,40,40],[99,95,0],1,2),")),
]

caught = 0
for n, (name, text) in enumerate(MUTANTS):
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
        print("GENERATOR FAIL", name); continue
    b = os.path.join(d, "b")
    cfg = subprocess.run(["cmake", "-S", d, "-B", b, "-DPDSYNTH_BUILD_PLUGIN=OFF", "-DCMAKE_BUILD_TYPE=Release"], capture_output=True)
    blt = subprocess.run(["cmake", "--build", b, "--target", "pd_character", "-j8"], capture_output=True)
    if cfg.returncode or blt.returncode:
        print("BUILD FAIL", name); continue
    try:
        r = subprocess.run([os.path.join(b, "pd_character")], capture_output=True, timeout=120)
        out = r.stdout.decode()
        hit = [l for l in out.splitlines() if l.strip().startswith("Didgeridoo")]
        res = "caught" if (r.returncode != 0 and hit) else "SURVIVED"
        detail = "\n               ".join(h.strip()[:118] for h in hit)
    except subprocess.TimeoutExpired:
        res, detail = "caught (hang)", ""
    caught += res.startswith("caught")
    print(f"{res:14s} {name}\n{'':15s}{detail}")
print(f"{caught}/{len(MUTANTS)} caught")
sys.exit(0 if caught == len(MUTANTS) else 1)
