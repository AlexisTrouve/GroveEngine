#!/usr/bin/env python3
"""Assemble une sequence de frames PNG en un GIF bouclant.

Les sources durables d'un visuel anime sont le PROGRAMME DE CAPTURE et ce script : le GIF se
regenere depuis eux. Meme regle que pour les plaques fixes (voir IMAGES.md).

Usage:
    build/tests/capture_lighting.exe <dir> anim      # ecrit frame_000.png ... frame_NNN.png
    python tools/make_gif.py <dir> blog/13_light_sweep.gif

⚠️ PALETTE PARTAGEE entre toutes les frames. Laisser Pillow en choisir une par frame fait scintiller
   les couleurs d'une image a l'autre — tres visible sur des degrades lisses, et facile a prendre
   pour un defaut du moteur plutot que de l'encodage.

⚠️ TRAMAGE actif par defaut. Un GIF ne porte que 256 couleurs : sans tramage, les degrades d'un halo
   sortent en bandes de contour bien nettes. Le tramage les remplace par du grain, ce qui pese ~50%
   de plus en octets et se lit nettement mieux.
"""
import argparse
import glob
import os

from PIL import Image


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src", help="dossier contenant frame_*.png")
    ap.add_argument("dst", help="chemin du .gif a ecrire")
    ap.add_argument("--frames", type=int, default=32, help="nombre de frames retenues (defaut 32)")
    ap.add_argument("--duration", type=int, default=90, help="ms par frame (defaut 90)")
    ap.add_argument("--scale", type=float, default=1.0, help="facteur de redimensionnement")
    ap.add_argument("--no-dither", action="store_true")
    args = ap.parse_args()

    paths = sorted(glob.glob(os.path.join(args.src, "frame_*.png")))
    if not paths:
        raise SystemExit("aucune frame dans " + args.src)

    # Sous-echantillonnage regulier : la boucle est une sinusoide complete, donc n'importe quel pas
    # regulier boucle encore sans a-coup.
    n = min(args.frames, len(paths))
    idx = [round(i * len(paths) / n) % len(paths) for i in range(n)]

    frames = []
    for i in idx:
        im = Image.open(paths[i]).convert("RGB")
        if args.scale != 1.0:
            im = im.resize((int(im.width * args.scale), int(im.height * args.scale)), Image.LANCZOS)
        frames.append(im)

    w, h = frames[0].size

    # Palette globale : empiler un echantillon de frames et quantifier l'ensemble UNE fois.
    sample = frames[:: max(1, len(frames) // 12)]
    strip = Image.new("RGB", (w, h * len(sample)))
    for i, f in enumerate(sample):
        strip.paste(f, (0, i * h))
    pal = strip.convert("P", palette=Image.Palette.ADAPTIVE, colors=255)

    dither = Image.Dither.NONE if args.no_dither else Image.Dither.FLOYDSTEINBERG
    out = [f.quantize(palette=pal, dither=dither) for f in frames]
    out[0].save(args.dst, save_all=True, append_images=out[1:], loop=0,
                duration=args.duration, optimize=True, disposal=2)

    print("%s  %dx%d  %d frames  %d Ko" % (args.dst, w, h, len(out), os.path.getsize(args.dst) // 1024))


if __name__ == "__main__":
    main()
