#!/usr/bin/env python3
"""Checks tools/make_pictures.py: names, fit/fill, and that the files it
writes are what the console's tpi_check() accepts (via build/test_x86 --tpi).
Skips cleanly when Pillow is missing."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
try:
    from PIL import Image
except ImportError:
    print("make_pictures: skipped (Pillow not installed)")
    sys.exit(0)
import make_pictures as mp

passed = failed = 0


def check(ok, what):
    global passed, failed
    if ok:
        passed += 1
    else:
        failed += 1
        print("FAIL " + what)


check(mp.safe_file_name("Mr. Incredible") == "Mr_ Incredible", "name rule")
check(mp.safe_file_name("Spyro") == "Spyro", "plain name")
check(mp.safe_file_name("Évil-Twin_1") == "_vil-Twin_1", "non-ASCII")
check(len(mp.safe_file_name("x" * 60)) == 37, "37 characters at most")

with tempfile.TemporaryDirectory() as d:
    # A plain test pattern, not any character: a wide gradient, a tall block.
    wide = Image.new("RGB", (400, 100))
    wide.putdata([(x * 255 // 399, y * 2, 200) for y in range(100) for x in range(400)])
    wide.save(os.path.join(d, "Mr. Incredible.png"))
    Image.new("RGB", (50, 200), (255, 0, 0)).save(os.path.join(d, "2.jpg"))
    open(os.path.join(d, "notes.txt"), "w").write("not a picture")
    open(os.path.join(d, "broken.png"), "wb").write(b"\x89PNG broken")
    out = os.path.join(d, "out")
    check(mp.main([d, "dimensions", "--out", out]) == 0, "runs")
    made = sorted(os.listdir(out))
    check(made == ["2.tpi", "Mr_ Incredible.tpi"], "files made: %s" % made)

    w, h, px = mp.read_tpi(open(os.path.join(out, "Mr_ Incredible.tpi"), "rb").read())
    check((w, h) == (128, 38), "size")
    # 400x100 fits as 128x32, centred: 3 see-through rows above and below.
    check(px[0][0] == 0 and px[2 * 128][0] == 0 and px[3 * 128][0] == 255 and px[34 * 128][0] == 255 and px[35 * 128][0] == 0, "fit and centre")
    left, right = px[19 * 128 + 1], px[19 * 128 + 126]
    check(left[1] < 30 and right[1] > 225 and abs(left[3] - 200) < 8, "the pattern's colours")

    w, h, px = mp.read_tpi(open(os.path.join(out, "2.tpi"), "rb").read())
    # 50x200 fits as 10x38 in the middle.
    opaque = [i % 128 for i, p in enumerate(px) if p[0] == 255]
    check(min(opaque) >= 58 and max(opaque) <= 69 and px[19 * 128 + 64][1] > 240 and px[19 * 128 + 64][2] < 16, "tall picture centred")

    out2 = os.path.join(d, "out2")
    mp.main([d, "Dimensions", "--out", out2, "--fill"])
    w, h, px = mp.read_tpi(open(os.path.join(out2, "2.tpi"), "rb").read())
    check(all(p[0] == 255 for p in px), "--fill covers the whole space")

    # The console's own check accepts them.
    tool = os.path.join(HERE, "..", "build", "test_x86")
    if os.path.exists(tool):
        for name in made:
            r = subprocess.run([tool, "--tpi", os.path.join(out, name)], capture_output=True, text=True)
            check(r.returncode == 0 and r.stdout.strip() == "ok 128x38", "tpi_check accepts %s: %s" % (name, r.stdout.strip()))
        bad = os.path.join(d, "bad.tpi")
        open(bad, "wb").write(open(os.path.join(out, "2.tpi"), "rb").read()[:-1])
        r = subprocess.run([tool, "--tpi", bad], capture_output=True, text=True)
        check(r.returncode == 1, "tpi_check refuses a cut-short file")

print("make_pictures: %d passed, %d failed" % (passed, failed))
sys.exit(1 if failed else 0)
