# GroveEngine — Session Successor Handoff

**Purpose:** resume work on GroveEngine in a FRESH session with zero prior chat context. Read this top-to-bottom
once; it is the map. Each subsystem points to its own design + handoff doc under `docs/design/` for the detail —
this file does NOT duplicate them, it indexes them and states what is done / open / blocked.

**Last updated:** 2026-07-03. **Branch:** `master`. **Remotes:** gitea (primary) + github + bitbucket (all three
kept in sync — see *Git* below). Working tree should be clean except `Testing/Temporary/CTestCostData.txt`
(a ctest artifact — NEVER stage it).

---

## What GroveEngine is

A **C++17 hot-reload module system** for game engines: dynamic load/unload of modules (.so) with state
preservation across reload, modules communicating **only via IIO topics** (pub/sub). Primary dev toolchain =
**Windows / MinGW** (Ninja, `-O3`); concurrency rigor (sanitizers) runs via **WSL** (MinGW ships none). Two
external consumers static-link the engine at HEAD and each have **their own Claude** — **Drifterra** (a game)
and **Theomen** (procedural worldgen). Stay engine-side; don't reach into their repos.

---

## Status snapshot — what's built

| Subsystem | State | Detail doc |
|---|---|---|
| **Module systems** | ✅ Sequential + Threaded (Phase 2) + ThreadPool/work-stealing (Phase 3), TSan-proven. Cluster (Phase 4, MMO-scale) = planned only | `docs/design/threaded-pool-handoff.md` |
| **IO contract (the spine)** | ✅ **complete for practical purposes** — see below | `docs/design/iio-contract.md` (+ `-handoff`) |
| **Rendering throughput** | ✅ bulk `submitSpriteBatch` (~100k–400k sprites/frame) + IIO zero-copy delivery (`shared_ptr<const>`, fan-out O(N)→O(1)) | `docs/design/rendering-throughput-handoff.md` |
| **BgfxRenderer** | ✅ sprites/text/tilemap(+LOD/fog/anim/multi-layer)/particles/`render:sector`/HUD overlay/runtime textures + streaming **asset system** (registry+cache+LRU, atlases, async decode) | `modules/BgfxRenderer/README.md`, `docs/design/assets.md`, `tilemap-renderer.md` |
| **UIModule** | ✅ full game-UI framework: layout/clipping/z-order/in-app window/tabs/drawers/modal, E2E-tested | `docs/design/ui-framework.md` (+ `-handoff`), `docs/UI_*.md` |
| **InputModule** | ✅ SDL backend (mouse/keyboard/gamepad), `ActionMap` (scancode bindings) | `modules/InputModule/README.md` |
| **SoundManager** | ✅ `sound:*` (SFX/music via SDL_mixer behind `ISoundBackend`) + adaptive music `audio:*` (logic; real stems = content) | CLAUDE.md §SoundManager |
| **Header-only helpers** | ✅ `grove::camera` (zoom/pan/cull), `grove::anim` (2D), `ActionMap`, `ZoneNavigator` | DEVELOPER_GUIDE |
| **grove::mapview** | ✅ **engine-side complete** — generic world-viewer (S0 format → S1 core → S2 viewer `--load` → S3-seam provider → tiling T2/T3 + live 'T' tiling → regions/markers on screen) + **`worldcheck`** (headless `.world` validator). Only remainder = **S3 Theomen adapter (cross-project)** | `docs/design/mapview.md` (+ `-handoff`) |
| **Quality hardening** | 🟡 Phase 1 (ASan/UBSan/TSan) ✅ + Phase 2 (clang-tidy `src/`) ✅ — core swept clean. Phase 3 (CI) + `modules/` sweep OPEN | `docs/design/quality-hardening-handoff.md` |

Test suite: **~137 ctests** (excl. 3 slow: StressTest/MemoryLeakHunter/ChaosMonkey) all green as of this handoff.

---

## The IO contract — freshly completed (the last big chantier)

The engine's communication spine (`docs/design/iio-contract.md` has the doctrine + a per-line ✅/🟡/🔵 ledger).
**All substantive parts are BUILT** (each locked by tests + **WSL TSan-clean**):

- **Async, non-deterministic by default**, carrying the *tools of determinism* (stamp, don't enforce).
- **EngineClock** — fixed timestep, pause/slow-mo/fast-forward, injected read-only into modules (`setClock`).
- **Message envelope** `{source, seq, lamport, tick, simTime, causedBy}` stamped through publish→route→deliver.
  Per-node `LamportClock`. **`causedBy`** populated for a message a handler publishes *in response* (thread-local
  cause id; a module publishing from `process()` is not auto-correlated — documented limit).
- **Zero-copy intra delivery** (`Message::data = shared_ptr<const IDataNode>`).
- **Structured replay sink** (`ReplaySink.h`, tapped in `routeMessage`): opt-in bounded ring, `bySource()` /
  `timeline()` views, **opt-in payload snapshot** (`IDataNode::serialize()`) → a replayable log, not just a timeline.
- **Per-topic backpressure** (`IntraIO::setTopicPolicy`): DropOldest / **Coalesce** (latest-wins) / **Reject**
  (protect a critical); **wildcard patterns** (`render:*`, exact beats pattern); + a **per-inbox drop log**
  (`enableDropLog`/`getRecentDrops` → which/why messages were dropped).

**Remaining IO items are LOW-ROI (recommend NOT building without a concrete need):** exec-order view (needs new
*receive-side* log infra, forensic-only); domain-logger fan-out (spammy at ~100 msg/frame); consumer-side dedup
(no redelivery trigger in the intra transport); manager-level "set a policy on all inboxes" broadcast. Also 🔵
deferred by design: LocalIO/NetworkIO tiers (stubs), live determinism enforcement, vector clocks, lockstep.

> **Note banked:** consumer-side *seq gap-detection* was DROPPED — the sink taps pre-drop and `seq` is
> per-source-across-topics, so gaps are ambiguous; the **drop log** is the direct, false-positive-free answer.

---

## Open work & decisions — pick the next move

1. **CI (auto-run on push) — BLOCKED ON ALEXI'S DECISION: github / gitea / none.** Highest leverage: it makes the
   whole rigor stack (137 ctests + ASan/UBSan/TSan + clang-tidy) automatic instead of manual. Nothing else needed
   — just the host call, then wire it. (`docs/design/quality-hardening-handoff.md` Phase 3.)
2. **Quality — widen the sanitizer + clang-tidy sweep to `modules/` (GPU/bgfx).** The one uncovered area.
   **Caveat:** GPU modules do NOT currently compile on Linux (only `grove_impl` + core are Linux-verified), and
   MinGW has no sanitizers → this likely needs a Linux port of the GPU modules first. Assess before committing.
3. **mapview S3 — Theomen's adapter (cross-project, its Claude).** The engine consumes any `.world` dir today;
   Theomen must WRITE one. A ready-to-paste recipe + prompt is in `docs/design/mapview-handoff.md` ("Theomen:
   write a `.world`"). Theomen's verify loop is now **`worldcheck <dir>`** (headless, deterministic — names the
   exact bad declaration; `--strict`/`--json` for CI), NOT "render + eyeball". Nothing to do engine-side unless
   the contract needs a change.
4. **Low-ROI IO follow-ons** (see above) — only on explicit request.
5. **mapview deferred plug-ins** — S4 timeline scrub, S5 hex/iso/Z-multislice/LOD, palette-LUT. Do when a consumer
   asks (`mapview.md` §8/§10).

---

## Build & test

```bash
# Core only
cmake -B build && cmake --build build -j4
# With modules (SoundManager needs SDL2_mixer; drop its flag if absent)
cmake -B build -DGROVE_BUILD_BGFX_RENDERER=ON -DGROVE_BUILD_UI_MODULE=ON -DGROVE_BUILD_INPUT_MODULE=ON -DGROVE_BUILD_SOUND_MODULE=ON
cmake --build build -j4
# Tests (run the fast suite; the 3 slow ones are StressTest/MemoryLeakHunter/ChaosMonkey)
ctest --test-dir build -E "StressTest|MemoryLeakHunter|ChaosMonkey" -j4
# Headless world-document validator (no GPU) — the deterministic proof a `.world` is well-formed
./build/tests/worldcheck <world-dir>         # exit 0 ok / 1 errors / 2 usage; --strict (warns fail) / --json (CI)
# Visual/interactive demos: run from the PROJECT ROOT (cwd-relative asset paths)
./build/tests/test_mapview_viewer            # drag=pan wheel=zoom H/B/T=lens R=reset  (--load <dir> opens a .world)
#   --shot out.png [--size WxH]   : ONE frame of the whole world at the fit view (capped ~131k cells)
#   --poster out.png [--ppc N]    : the WHOLE map tiled+stitched to one PNG at N px/cell — no ceiling, big map=big PNG
./build/tests/test_ui_showcase
```

**Sanitizers (WSL only — MinGW has none):** a wrapper project + `setarch $(uname -m) -R ./exe` disables ASLR so
TSan runs. Recipe: memory `tsan-via-wsl-recipe` + `docs/design/quality-hardening-handoff.md`. **Re-run WSL TSan
after ANY change to the IIO publish/route/deliver path** (doctrine §10).

---

## How this repo is tracked (important)

- **NOT in ProjectMind.** State lives in `docs/design/*.md` + the file-based **memory** at
  `~/.claude/projects/.../memory/` (index = `MEMORY.md`). Keep BOTH current on every change.
- **TDD is non-negotiable:** red test first → impl → green → commit; a fix = a test that locks the bug; prove a
  test bites (mutate → fail → revert). **UI/render "works" only with an E2E that clicks/renders it** — reading
  code is not proof.
- **Git:** commit+push after each implementation. **No force push** (never on master). Push to all three remotes:
  ```
  git -c http.proxy=http://127.0.0.1:7897 push https://git.etheryale.com/StillHammer/groveengine.git master:master && git update-ref refs/remotes/gitea/master refs/heads/master
  git push github master:master
  GIT_SSH_COMMAND="ssh -o BatchMode=yes" git push origin master:master
  ```
  Commit messages end with the `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>` line (write
  multi-line via a heredoc to `.git/GROVE_COMMIT_MSG.txt` + `git commit -F`, then `rm`). Never stage
  `Testing/Temporary/CTestCostData.txt`.
- **Cross-project boundary:** the engine speaks only generic (`field`/`region`/`chunk`/topics). Drifterra + Theomen
  vocabulary stays in their repos; if engine code starts knowing their words, it stopped being reusable.

---

## Gotchas banked (memory)

- **Shader edit** → a `.sc` change needs a manual `shaderc --bin2c` regen of its `.bin.h` (build won't auto-compile).
- **Visual demos** run from the project root (cwd-relative `../assets`); the exe is locked while its window is open.
- **Never `taskkill /F /IM python.exe`** — kill by PID only.
- **TankModule.h linter bug** — a linter sometimes merges lines 35-36 ("logger not declared"); check if a build fails there.
```
