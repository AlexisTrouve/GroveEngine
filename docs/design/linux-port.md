# Linux port — handoff / resume point

Resume-from-here for making GroveEngine a **first-class Linux platform** (build · dev · debug · compile ·
**play**, GPU included). This doc is the map: what works, the one blocker left, how to resume, and the box +
branch it lives on. **Status: IN PROGRESS — the engine already BUILDS + RUNS on Linux (core + GPU module);
one runtime blocker remains before a rendered frame comes out headless.**

---

## The one-line state

`grove_impl` **and** `libBgfxRenderer.so` **compile + link on Linux**; core/headless tests RUN (worldcheck,
mapview, blobs — 123 assertions green); the viewer BUILDS and bgfx INITS with the **OpenGL** backend on a
headless VPS. The only thing between here and a rendered `--poster` on Linux is the **GL context version**:
bgfx makes an **OpenGL 2.1** context under Mesa llvmpipe, but the (multi-backend, correctly-selected) GLSL
shaders need **1.30+** → `Failed to compile shader`. On a **real GPU** this should just render.

> ⚠️ The old note "GPU modules don't compile on Linux" (memory/handoffs) is **DEBUNKED** — measured false.

---

## Where it lives

- **Branch `linux-port`** (off `master` @ the last Linux commit). All *unfinished* GPU-render work continues
  here; `master` stays the stable Windows-first line. **`master` is Windows-green (140/140 ctests)** — the Linux
  commits already on master are Windows-safe (`#ifdef`-guarded / additive) and the deps-vendoring is a real fix,
  so they were kept there; only the churny GL-context work is branch-isolated until proven, then merges.
- **The Linux box = VPS142** (no Linux desktop yet). Debian 13 (trixie), 8 cores / 31 GB / 176 GB free, **headless**.
  - SSH: `debian@100.85.89.83` (**Tailscale** — the public IP's `:22` is firewalled). Passwordless sudo.
  - It's a **prod VPS** (ai.etheryale.com) → only additive dev packages were installed (`cmake ninja-build
    libsdl2-dev libsdl2-mixer-dev libgl1-mesa-dev libegl1-mesa-dev libglu1-mesa-dev libx11-dev libxext-dev
    libxrandr-dev libxi-dev zlib1g-dev`; `xvfb` + `libgl1-mesa-dri`/llvmpipe already present).
  - Repo at `~/groveengine`, builds into `build-linux/`. Interactive **play** needs a real display → deferred to
    a future GPU desktop; VPS142 covers build/compile/debug + **headless** GPU (Xvfb + Mesa software GL).

---

## What's DONE (committed on master, Windows-safe, validated on VPS142)

| Piece | Commit | Note |
|---|---|---|
| **Clean-clone build** | `d28dec0` | ~26 test targets hardcoded `deps/bgfx/…/miniz.c` + `deps/nlohmann_json/single_include`, but `deps/` is GITIGNORED (FetchContent's copies) → a fresh clone (Linux **or a new Windows checkout**) failed to CONFIGURE. Fixed by vendoring miniz + nlohmann git-tracked under `third_party/`, via `GROVE_MINIZ_DIR`/`GROVE_NLOHMANN_DIR`. A real portability bug, not Linux-specific. |
| **GPU module compiles on Linux** | — | `libBgfxRenderer.so`, bgfx-from-source, **zero compile errors**. The platform-data groundwork (nwh/ndt) was already there. |
| **OpenGL backend** | `769ae54` | `BgfxDevice::init`: `RendererType::Direct3D11` on Windows / `OpenGL` on Linux (`#ifdef _WIN32`). |
| **Cross-platform native handles** | `769ae54` `806141a` | `SdlNativeHandle.{h,cpp}` — the **.cpp is the ONLY TU including `<SDL_syswm.h>`** (on Linux it drags X11 macros `None`/`Status`/`BlendMode`→`KBLedMode` that COLLIDE with `RHITypes.h`). HWND on Win / X11 Window + Display* on Linux. |
| **Cross-platform CMake** | `578e63f` | moved `test_mapview_viewer` out of the `if(WIN32 AND …)` visual block into `if(GROVE_BUILD_BGFX_RENDERER AND SDL2_AVAILABLE)`. It now BUILDS on Linux. |

Everything else headless already ran green on Linux (the core + mapview S0/S1 + validators + IIO/blobs).

---

## THE BLOCKER — resume here

Running the poster headless on VPS142:

```bash
cd ~/groveengine
cmake -B build-linux -G Ninja -DGROVE_BUILD_BGFX_RENDERER=ON -DGROVE_BUILD_UI_MODULE=ON \
      -DGROVE_BUILD_INPUT_MODULE=ON -DGROVE_BUILD_SOUND_MODULE=ON
cmake --build build-linux --target test_mapview_viewer -j8
# a .world fixture is at /tmp/gv_lin (256², from /tmp/genworld — a tiny inline generator)
SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
  xvfb-run -a -s "-screen 0 1280x1024x24" \
  ./build-linux/tests/test_mapview_viewer --load /tmp/gv_lin --poster /tmp/poster_linux.png --ppc 4
```

→ `Initializing BgfxRenderer: … backend=opengl` ✓ then `GPU: OpenGL 2.1` then
`BGFX FATAL 0x1: Failed to compile shader … preprocessor error: unexpected HASH_TOKEN`.

**Diagnosis:** the shader variants are fine — `Shaders/*.bin.h` carry `glsl`/`essl`/`spirv`/`dx11`, and
`ShaderManager.cpp` selects `glsl` on GL correctly. The problem is the **GL context is 2.1** (GLSL 1.20), so the
GLSL 1.30+ shaders' `#version 130` is rejected. `MESA_GL_VERSION_OVERRIDE=3.3` did **not** lift it — **bgfx
creates its own GL context** (likely a 2.1 compat profile), so the Mesa override is bypassed.

**Leads (in order of "real fix"):**
1. **Force bgfx to request a GL 3.3+ context.** bgfx's GLX/EGL context creation — find where it picks the GL
   version and raise it (or request a core profile ≥ 3.3). This is the proper correction and fixes both
   llvmpipe and real drivers. Start in `_deps/bgfx-src/bgfx/src/glcontext_glx.cpp` / `renderer_gl.cpp`.
2. **zink** (`MESA_LOADER_DRIVER_OVERRIDE=zink`, GL-on-Vulkan via lavapipe) or a newer Mesa that exposes GL 4.x
   in software — a runtime/env workaround, no code.
3. **Accept it renders on a REAL GPU** (shaders + GL selection are correct; only llvmpipe's default caps it) and
   treat build + init + headless-up-to-shaders as the "server" proof, with interactive render proven later on a
   GPU desktop.

---

## After the blocker — remaining Linux TODO (smaller)

- **Port the other GPU test mains** to `SdlNativeHandle.h` (they still read `wmi.info.win.window` directly →
  WIN32-gated): `test_mapview_viewer_e2e` first (so the poster regression lock runs on Linux too), then the UI/
  capture visual tests as needed. Move each into the cross-platform CMake block once clean.
- **`--poster` on a headless server** is the actual product use (generate map PNGs off-box) — it lands the moment
  the GL context is sorted.
- Fold `modules/` into the sanitizer sweep ([[quality-hardening]]) — now unblocked (modules compile on Linux).

---

## Doctrine notes for whoever resumes

- Work on **`linux-port`** (`git checkout linux-port`); keep `master` green on Windows. Verify **both** sides
  before merging any renderer-touching change (build Windows + run the VPS build).
- `sed -i` mangles line endings on this repo — use the Edit tool; `git checkout -- <file>` if churn happened.
- Adding an `IDataNode` virtual: **append** it (vtable grows, slots stay) — ABI-safe for not-rebuilt hot-load `.so`.
- The memory file `linux-port` mirrors this doc (survives across sessions).
