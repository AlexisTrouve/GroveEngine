#!/usr/bin/env python3
"""Regenerate a shader's .bin.h from its .sc — compile AND assemble, in one command.

POURQUOI cet outil existe : le build ne compile PAS les .sc. Editer un shader ne fait donc
strictement rien tant que son .bin.h n'a pas ete regenere a la main, et la recette shaderc n'etait
ecrite nulle part — il fallait la reconstruire a chaque fois.

POURQUOI compilation ET assemblage dans le MEME script : les deux etapes ont d'abord ete deux
scripts, et un dossier de sortie qui a change entre les deux a fait assembler l'ANCIEN bytecode.
Le shader semblait alors n'avoir aucun effet — un correctif qui a l'air inefficace alors qu'il n'a
jamais tourne. Une seule commande, une seule source de verite.

Usage:
    python tools/regen_shader.py fs_light            # fragment par defaut
    python tools/regen_shader.py vs_light --vertex

⚠️ Le bloc `mtl` est un PLACEHOLDER : pas de backend Metal sur cette chaine. Il est repris tel quel
   depuis le .bin.h existant plutot que recompile.
"""
import argparse
import datetime
import io
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHADERC = os.path.join(ROOT, "build", "_deps", "bgfx-build", "cmake", "bgfx", "shaderc.exe")
SRCDIR = os.path.join(ROOT, "modules", "BgfxRenderer", "Shaders")
INCDIR = os.path.join(ROOT, "build", "_deps", "bgfx-src", "bgfx", "src")

# (suffixe, plateforme, profil) — l'ordre des blocs dans le .bin.h final.
VARIANTS = [("spv", "linux", "spirv"), ("glsl", "linux", "130"), ("dx11", "windows", "s_5_0")]


def extract(text, symbol):
    """Le bloc `static const uint8_t <symbol>[N] = { ... };` complet."""
    m = re.search(r"static const uint8_t %s\[\d+\] =\s*\{.*?\n\};" % re.escape(symbol), text, re.S)
    if not m:
        raise SystemExit("bloc introuvable dans la source : " + symbol)
    return m.group(0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name", help="nom du shader sans extension, ex. fs_light")
    ap.add_argument("--vertex", action="store_true", help="shader de sommets (defaut : fragment)")
    args = ap.parse_args()

    name = args.name
    stype = "vertex" if args.vertex else "fragment"
    src = os.path.join(SRCDIR, name + ".sc")
    dst = os.path.join(SRCDIR, name + ".bin.h")

    for p in (SHADERC, src, dst):
        if not os.path.exists(p):
            raise SystemExit("introuvable : " + p)

    current = io.open(dst, encoding="utf-8").read()
    blocks = {}

    with tempfile.TemporaryDirectory() as tmp:
        for suffix, platform, profile in VARIANTS:
            out = os.path.join(tmp, "%s_%s.bin.h" % (name, suffix))
            cmd = [SHADERC, "-f", src, "-o", out, "--type", stype,
                   "--platform", platform, "-p", profile,
                   "--varyingdef", os.path.join(SRCDIR, "varying.def.sc"),
                   "-i", INCDIR, "--bin2c", "%s_%s" % (name, suffix)]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode != 0 or not os.path.exists(out):
                sys.stderr.write(r.stdout + r.stderr)
                raise SystemExit("shaderc a echoue sur %s/%s" % (platform, profile))
            blocks[suffix] = extract(io.open(out, encoding="utf-8").read(), "%s_%s" % (name, suffix))

    # Metal : conserve verbatim, s'il existait.
    mtl = None
    if ("%s_mtl" % name) in current:
        mtl = extract(current, "%s_mtl" % name)

    today = datetime.date.today().isoformat()
    parts = [
        "// Auto-generated shader bytecode - do NOT edit by hand.",
        "// REGENERATED %s from %s.sc via tools/regen_shader.py" % (today, name),
        "// (shaderc: linux/spirv, linux/130, windows/s_5_0). The `mtl` block is a PLACEHOLDER",
        "// carried over verbatim - no Metal backend on this toolchain.",
        "",
        "// SPIRV (Vulkan)",
        blocks["spv"],
        "",
        "// GLSL (OpenGL)",
        blocks["glsl"],
        "",
    ]
    if mtl:
        parts += ["// Metal", mtl, ""]
    parts += ["// D3D11", blocks["dx11"], ""]

    io.open(dst, "w", encoding="utf-8", newline="\n").write("\n".join(parts))

    print("ecrit", os.path.relpath(dst, ROOT))
    for suffix in ("spv", "glsl", "mtl", "dx11"):
        m = re.search(r"%s_%s\[(\d+)\]" % (re.escape(name), suffix), "\n".join(parts))
        if m:
            print("   %-5s %s octets" % (suffix, m.group(1)))


if __name__ == "__main__":
    main()
