#!/usr/bin/env python3
"""
Rasterise the UI 9-slice art: assets/textures/ui/src/*.svg -> assets/textures/ui/ui_*.png

WHY THIS EXISTS
---------------
The first batch of frame art was drawn in SVG, exported once, and only the PNGs were committed — so
the art was NOT regenerable: no source, no way to restyle without redrawing from scratch. This makes
the SVG the source of truth and the PNG a build artefact you can rebuild at will.

Both are committed on purpose: the PNGs so the engine and its demos need no Python at build time,
the SVGs so the art can actually be edited.

USAGE
-----
    python tools/gen_ui_art.py            # regenerate every frame
    python tools/gen_ui_art.py panel row  # only these

Needs cairosvg (pip install cairosvg).

9-SLICE CONSTRAINT — read before editing an SVG
-----------------------------------------------
These are nine-patch frames: the four corners draw at native size, the edges and centre STRETCH. Any
detail that must stay undistorted (corner ticks, bevels, studs) has to sit inside the margin the
widget declares as `inset` — 24px at 128px source for this set. Detail placed in the middle band will
smear as soon as the widget is wider or taller than the source.
"""
import os
import sys

SRC = os.path.join("assets", "textures", "ui", "src")
DST = os.path.join("assets", "textures", "ui")
SIZE = 128   # source px; the srcW/srcH a layout declares for these frames


def main() -> int:
    try:
        import cairosvg
    except ImportError:
        print("cairosvg missing - pip install cairosvg", file=sys.stderr)
        return 1

    if not os.path.isdir(SRC):
        print("no source dir: %s (run from the repo root)" % SRC, file=sys.stderr)
        return 1

    wanted = set(a[:-4] if a.endswith(".svg") else a for a in sys.argv[1:])
    names = sorted(f[:-4] for f in os.listdir(SRC) if f.endswith(".svg"))
    if wanted:
        names = [n for n in names if n in wanted]
        if not names:
            print("nothing matched", file=sys.stderr)
            return 1

    for n in names:
        src = os.path.join(SRC, n + ".svg")
        dst = os.path.join(DST, "ui_" + n + ".png")
        cairosvg.svg2png(url=src, write_to=dst, output_width=SIZE, output_height=SIZE)
        print("%s  ->  %s  (%dx%d)" % (src, dst, SIZE, SIZE))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
