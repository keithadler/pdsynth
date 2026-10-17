#!/usr/bin/env python3
"""Break the octave control on purpose, one change at a time, and require pd_octavecheck to notice.

Each mutant is a fresh copy of the sources built in its own directory, with the JUCE sources reused
from an existing build so it does not fetch them again. A mutant that does not compile is reported,
not counted as caught. One that hangs counts as caught.

    python3 -I tools/mutate_octave.py <scratch directory> <existing build directory>
"""
import os, shutil, subprocess, sys

S, BUILD = sys.argv[1], os.path.abspath(sys.argv[2])
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ed = open(os.path.join(root, "plugin", "PluginEditor.cpp")).read()
M = [
 ("no clamp on the octave", "octaveShift = juce::jlimit(-3, 3, shift);", "octaveShift = shift;"),
 ("clamps to two octaves", "octaveShift = juce::jlimit(-3, 3, shift);", "octaveShift = juce::jlimit(-2, 2, shift);"),
 ("keyboard does not follow", "    keyboard.setAvailableRange(lowest, lowest + 48);\n    keyboard.setLowestVisibleKey(lowest);\n", ""),
 ("keyboard moves a half octave", "const int lowest = 36 + 12 * octaveShift;", "const int lowest = 36 + 6 * octaveShift;"),
 ("keyboard shrinks as it moves", "keyboard.setAvailableRange(lowest, lowest + 48);", "keyboard.setAvailableRange(lowest, lowest + 36);"),
 ("number not updated", "    octValueL.setText(octaveShift > 0 ? \"+\" + juce::String(octaveShift) : juce::String(octaveShift),\n                      juce::dontSendNotification);\n", ""),
 ("number has no plus sign", "octaveShift > 0 ? \"+\" + juce::String(octaveShift) : juce::String(octaveShift)", "juce::String(octaveShift)"),
 ("plus button not disabled at the top", "    octUp.setEnabled(octaveShift < 3);\n", ""),
 ("minus button not disabled at the bottom", "    octDown.setEnabled(octaveShift > -3);\n", ""),
 ("buttons swapped", "octDown.onClick = [this] { setOctave(octaveShift - 1); };", "octDown.onClick = [this] { setOctave(octaveShift + 1); };"),
 ("buttons take keyboard focus", "b->setWantsKeyboardFocus(false);", "b->setWantsKeyboardFocus(true);"),
 ("right arrow no longer moves the picture", "if (k.getKeyCode() == juce::KeyPress::rightKey) { setOctave(octaveShift + 1); return true; }", "if (k.getKeyCode() == juce::KeyPress::rightKey) { octaveShift = juce::jmin(3, octaveShift + 1); return true; }"),
 ("left arrow no longer moves the picture", "if (k.getKeyCode() == juce::KeyPress::leftKey)  { setOctave(octaveShift - 1); return true; }", "if (k.getKeyCode() == juce::KeyPress::leftKey)  { octaveShift = juce::jmax(-3, octaveShift - 1); return true; }"),
 ("starts shifted", "    setOctave(0);\n", "    setOctave(1);\n"),
]
caught = 0
for n, (name, old, new) in enumerate(M):
    assert ed.count(old) == 1, name
    d = os.path.join(S, f"m{n}")
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    for item in ["src", "tools", "tests", "plugin", "CMakeLists.txt", "COPYING", "banks", "docs"]:
        a = os.path.join(root, item)
        shutil.copytree(a, os.path.join(d, item)) if os.path.isdir(a) else shutil.copy(a, d)
    open(os.path.join(d, "plugin", "PluginEditor.cpp"), "w").write(ed.replace(old, new))
    b = os.path.join(d, "b")
    cfg = subprocess.run(["cmake", "-S", d, "-B", b, "-DCMAKE_BUILD_TYPE=Release",
                          "-DFETCHCONTENT_SOURCE_DIR_JUCE=" + os.path.join(BUILD, "_deps", "juce-src"),
                          "-DFETCHCONTENT_SOURCE_DIR_CLAP-JUCE-EXTENSIONS=" + os.path.join(BUILD, "_deps", "clap-juce-extensions-src"),
                          "-DFETCHCONTENT_FULLY_DISCONNECTED=ON", "-DCOPY_PLUGIN_AFTER_BUILD=OFF"], capture_output=True)
    bl = subprocess.run(["cmake", "--build", b, "--target", "pd_octavecheck", "-j8"], capture_output=True)
    if cfg.returncode or bl.returncode:
        print("BUILD FAIL", name, (bl.stderr or cfg.stderr).decode()[-300:]); continue
    exe = subprocess.run(["find", b, "-name", "pd_octavecheck", "-type", "f"], capture_output=True).stdout.decode().split()[0]
    try:
        r = subprocess.run([exe], capture_output=True, timeout=120)
        fails = [l.strip() for l in r.stdout.decode().splitlines() if l.strip().startswith("FAIL")]
        res = "caught" if (r.returncode != 0 and fails) else "SURVIVED"
    except subprocess.TimeoutExpired:
        res, fails = "caught (hang)", []
    caught += res.startswith("caught")
    print(f"{res:14s} {name}")
    for f in fails[:2]:
        print(f"{'':15s}{f[:120]}")
print(f"{caught}/{len(M)} caught")
sys.exit(0 if caught == len(M) else 1)
