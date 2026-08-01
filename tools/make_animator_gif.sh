#!/usr/bin/env bash
# Assemble la sequence PNG de capture_animator_demo en un GIF.
#
# POURQUOI un script separe plutot que l'encodage dans le C++ : encoder un GIF n'apprend rien sur le
# moteur, et ffmpeg fait un bien meilleur travail de palette qu'un encodeur maison. La demo produit
# des PNG (le format que le moteur sait deja ecrire, cf. PngCapture.h), le reste est de l'outillage.
#
# Prerequis : ffmpeg sur le PATH (deja requis par VideoModule pour le decodage MP4 reel).
# Usage, depuis la racine du projet :
#     ./build/tests/capture_animator_demo.exe build/animator_frames
#     tools/make_animator_gif.sh build/animator_frames blog/animator_fade.gif
set -euo pipefail

FRAMES_DIR="${1:-build/animator_frames}"
OUT="${2:-blog/animator_fade.gif}"
FPS="${3:-25}"

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "ffmpeg introuvable sur le PATH -- requis pour l'assemblage." >&2
    exit 1
fi
if [ -z "$(ls -A "$FRAMES_DIR"/*.png 2>/dev/null)" ]; then
    echo "aucun PNG dans $FRAMES_DIR -- lancer capture_animator_demo d'abord." >&2
    exit 1
fi

# Palette en deux passes : une palette globale generee sur toute la sequence, puis appliquee avec
# tramage. En une passe, ffmpeg retombe sur la palette web 216 couleurs et le degrade du fond
# devient une soupe de bandes -- tres visible sur un aplat sombre comme celui de la demo.
PALETTE="$(mktemp -t animpal.XXXXXX.png)"
trap 'rm -f "$PALETTE"' EXIT

ffmpeg -y -loglevel error -framerate 30 -i "$FRAMES_DIR/f%04d.png" \
    -vf "fps=${FPS},scale=640:-1:flags=lanczos,palettegen=stats_mode=diff" "$PALETTE"

ffmpeg -y -loglevel error -framerate 30 -i "$FRAMES_DIR/f%04d.png" -i "$PALETTE" \
    -lavfi "fps=${FPS},scale=640:-1:flags=lanczos[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=3" \
    -loop 0 "$OUT"

echo "ecrit $OUT ($(du -h "$OUT" | cut -f1))"
