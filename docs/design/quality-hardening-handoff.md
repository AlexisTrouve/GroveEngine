# Quality Hardening — plan + handoff

**Thesis.** The rigor we applied to the zero-copy IIO work (TSan + ASan/LSan + a measured perf A/B +
a negative-controlled leak check) was **manual and narrow** — run by hand, only on the IIO path. The
quality leap is to make that rigor **systematic** (repeatable, ideally automated), **broad** (the
whole engine, not one subsystem), and to add the **lenses we have never used** (UBSan, static
analysis). This doc is the plan + the resume-from-here state.

---

## Status (2026-06-28) — RESUME HERE

**Phase 1 (sanitizers) ✅ DONE · Phase 2 (clang-tidy) ✅ DONE.** The engine core is now proven clean on
two new axes: **0 UB / 0 memory errors / 0 leaks** under ASan+UBSan (core/logic/IIO/math + the hot-reload
dlopen/dlclose path), and **statically clean** under a curated clang-tidy `src/` pass (only minor real
fixes, no serious bugs). The thesis above is delivered for the core.

**Next:** the `modules/` clang-tidy widen is **DONE** — PART 1 (2026-07-06, 29 Windows-clean files, 3 fixes) +
**PART 2 (2026-07-10, the 22 SAL-tainted/BgfxRenderer files on VPS142's GCC toolchain — 9 fixes incl. a real
SDL2_mixer cross-version portability bug Windows couldn't see; see Phase 2 UPDATE 2026-07-10).** `modules/` is
now fully swept. Remaining: Phase 3 (CI, needs a host decision) / Phase 4 (regression gates), + the `[gpu]`
ASan/UBSan gap (VPS142 has the toolchain now). Detail below.

| Capability | Status |
|---|---|
| Test suite (121 ctest) | ✅ solid, the backbone |
| TSan / Helgrind | ✅ wired (`GROVE_ENABLE_TSAN` / `_HELGRIND`, manual flags, `CMakeLists.txt:11-16, 27-29`); IIO/pool TSan-proven |
| ASan / LSan | ✅ wired (`GROVE_ENABLE_ASAN`, commit `400e62a`); core + hot-reload swept **clean** |
| UBSan | ✅ wired (`GROVE_ENABLE_UBSAN`, `400e62a`); swept **clean**, negative-controlled |
| clang-tidy / static analysis | ✅ `.clang-tidy` curated (`a668239`); `src/` crop fixed. **`modules/` fully swept** — PART 1 (29 non-SAL files on Windows, 3 fixes) + PART 2 (22 SAL-tainted/BgfxRenderer files on VPS142 Linux, 9 fixes incl. an SDL2_mixer cross-version portability bug; Phase 2 UPDATE 2026-07-10) |
| CI (auto on push) | ❌ none — everything still manual (Phase 3) |
| Coverage | ❌ not measured |
| Perf / leak regression gates | ❌ none — benchmarks wall-clock, run by hand (Phase 4) |

**Toolchain reality:** primary build Windows/MinGW (Ninja, `-O3`). MinGW ships no sanitizers → run via
**WSL** ([[tsan-via-wsl-recipe]]; ASan/UBSan recipe in *Phase 1 — what actually worked*). clang-tidy via the
**pip wheel** on Windows + the Ninja compile DB (no admin needed; see Phase 2). These constrain *how*, not *whether*.

---

## Plan (phased by leverage)

### Phase 1 — Sanitizer lenses ✅ DONE (sweep clean) — 2026-06-27

**Result: the core/logic/IIO/math/datatree/topictree subset AND the hot-reload dlopen/dlclose path are
sanitizer-clean.** UBSan = 0 undefined behavior; ASan+LSan = 0 memory errors, 0 leaks — **negative-controlled**
(a deliberate signed overflow IS caught by UBSan; a leaked `JsonDataNode` IS caught by LSan). The hot-reload
coverage came from a follow-up fix (`.dll` hardcoding → portable `.so`/`.dll`, commit `bf7132f`; see side
findings). `GROVE_ENABLE_ASAN`/`_UBSAN` wired (commit `400e62a`). What we actually ran + the gotchas that bit
are in *Phase 1 — what actually worked* below.

Highest yield in *real bugs found*, and it amortizes what we just built. Mirror the existing TSAN
block for two new options, then sweep the suite.

- **1a — first-class CMake options.** Add `GROVE_ENABLE_ASAN` and `GROVE_ENABLE_UBSAN` next to the
  TSAN block (`CMakeLists.txt:11-16` is the template: `option(...)` + `add_compile_options(-fsanitize=…
  -g -O1 -fno-omit-frame-pointer)` + `add_link_options(-fsanitize=…)`). ASan and UBSan are
  **combinable** (`-fsanitize=address,undefined`); TSan is **not** (separate build). For UBSan, add
  `-fno-sanitize-recover=undefined` so a violation **fails the test** instead of just printing.
- **1b — UBSan sweep (do this first; cheapest new signal).** Build the full suite on WSL with
  `GROVE_ENABLE_UBSAN=ON`, run `ctest`. Likely hot spots: the math-heavy code (`grove::camera`,
  `grove::anim`, `EngineClock`), and bit-twiddling (color/UV packing, hashes). Triage each finding
  → **red test → fix → green → commit** (one test per bug, TDD-adversarial doctrine). Signed overflow,
  misaligned loads, invalid shifts/enum values, null deref are the usual crop.
- **1c — ASan/LSan sweep of the WHOLE suite.** We proved the IIO *path* is leak-clean (negative-control
  LSan, today). Now run **all** tests under ASan to find use-after-free / overflow / leaks across the
  whole engine — esp. the hot-reload `.so` lifecycle, the renderer's buffers, the asset cache. Same
  triage loop.
- **Success:** `GROVE_ENABLE_ASAN`/`_UBSAN` exist + documented in the build section; the suite runs
  clean under each (or every finding is fixed and locked by a regression test).
- **Gotchas / constraints:**
  - **GPU tests** (`[gpu]`, bgfx/SDL) need bgfx + SDL2 on the WSL toolchain to build there. The WSL
    sanitizer sweep cleanly covers **core + logic + IIO + module-logic** tests (the majority, and where
    concurrency/memory logic lives). GPU-render sanitizer coverage is a **separate open question**
    (install bgfx/SDL on WSL, or sanitize a `noop`-backend subset). Don't claim "whole engine" until
    this is decided — see Open decisions.
  - The `IntraIOManager` Meyers singleton is torn down by its atexit dtor **before** LSan's check
    (atexit LIFO) → not a false positive. (Confirmed today.)
  - **Verify the verifier**: every sanitizer sweep starts with a *negative control* (inject one
    deliberate fault, confirm the tool flags it) before trusting a clean run. We did this for LSan
    (a leaked `JsonDataNode` → caught at `JsonDataNode.cpp:18`); do the same for UBSan/ASan.

#### Phase 1 — what actually worked (reproducible recipe)

MinGW has no sanitizers → run on WSL. The full-suite Linux build had never been done; this is the
working path (scripts lived in the session scratchpad — re-create if needed):

```bash
# Configure (Unix Makefiles — Ninja isn't on the WSL PATH). SDL find_package SUCCEEDS but its
# imported targets are missing on this WSL → force it off, else the sound demos fail at configure.
cmake -S /mnt/c/.../groveengine -B ~/ubsan_build \
  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON -DCMAKE_DISABLE_FIND_PACKAGE_SDL2_mixer=ON \
  -DGROVE_ENABLE_UBSAN=ON -DGROVE_BUILD_TESTS=ON          # ASan: add -DGROVE_ENABLE_ASAN=ON (they compose)
cmake --build ~/ubsan_build -j4 -- -k                     # Make keep-going is "-k" NOT "-k 0" (Ninja syntax)
UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=1:detect_odr_violation=0 \
  ctest --test-dir ~/ubsan_build --output-on-failure -j2 -E "ChaosMonkey|MemoryLeakHunter|StressTest|ProductionHotReload"
```

- **79 test binaries build** on Linux (only `SDLBackend`/InputModule fails — no SDL.h). **47 run via ctest.**
- **Verify-the-verifier**: standalone neg control `int x=INT_MAX; x+=1;` under `-fsanitize=undefined` →
  `runtime error: signed integer overflow`. Confirms UBSan is live before trusting the clean run.
- **Coverage caveat (be honest):** this is the **core/logic/IIO** subset. NOT covered: GPU/renderer
  tests (no bgfx on WSL), SDL input, the 4 long stress tests (excluded for speed — TSan/ASan-leak
  domain anyway), and the hot-reload tests below.
- **Logs to `$HOME` not `/tmp`** — WSL tmpfs `/tmp` is wiped when the WSL VM idles out between calls.

#### Phase 1 — side findings (test quality, NOT engine bugs → follow-ups)

1. ✅ **FIXED (commit `bf7132f`) — hot-reload `.dll` hardcoding.** `test_09/10/11` hardcoded
   `"./libX.dll"` → failed to load any module on Linux → never sanitized. Added a file-local
   `modPath()` resolving `.so`/`.dll` per platform (mirrors the `#ifdef` idiom in `test_05/07/08`).
   **Result: the hot-reload dlopen/dlclose path is now SANITIZER-CLEAN** — `ModuleDependencies`,
   `MultiVersionCoexistence`, `IOSystemStress` + `ReloadAfterThrow`, `RaceConditionHunter`,
   `ErrorRecovery` all pass under ASan+UBSan on Linux with **0 sanitizer errors**. (24 test module
   `.so`s build fine on Linux.) Windows still 3/3 green. Two tests still fail on Linux for
   **non-sanitizer** reasons, flagged not fixed: **ChaosMonkey** "<10MB RSS growth" assertion (ASan
   inflates RSS ~3× — mechanical false positive; LSan finds no real leak), and **ProductionHotReload**
   "v2 version" assertion (fails on Linux while *every other* reload/multiversion test passes → test-
   specific, not a hot-reload break — worth a look only if Linux becomes a target platform).
2. **`LimitsTest` / `ConfigHotReload`** pass under UBSan-only but fail under ASan+UBSan with **zero
   sanitizer errors** — they load their `.so` fine; the failures are timing/count assertions sensitive
   to the ~2.5× ASan slowdown (LimitsTest is literally the *timeout* test). Confirm on a plain Linux
   build, then relax the timing assertions. Not a memory bug.

### Phase 2 — Static analysis (clang-tidy) ✅ DONE (src/ pass) — 2026-06-28

`.clang-tidy` committed (curated for bug-finding) + the `src/` crop fixed (commit `a668239`).

**Toolchain (no admin needed):** MinGW/Windows has no system clang-tidy and WSL has none either (no
passwordless sudo to `apt install`). Route that worked: the **pip wheel** —
`python -m pip install --user clang-tidy` (lands in `…/Python312/Scripts/clang-tidy.exe`, LLVM 22) —
driven by the **Ninja compile DB** (`cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`):
`clang-tidy -p build src/*.cpp`. Cross-toolchain (MinGW DB + clang frontend) parses our code fine.

**Curation is everything:** raw `misc-*` flooded (≈353 warnings on one file, almost all
`misc-const-correctness`). After disabling the pedantic/noisy checks the `src/` crop was **25 findings**
— all triaged. Fixed the real ones (3× missing `reserve()`, `routeMessage` payload → const ref on the
hot path, `registerInstance` → move, dead `generation`/`filename`/timing locals, qualified
`~DebugEngine` shutdown, anonymized unused params). Curated OFF **with documented rationale** in
`.clang-tidy`: `bugprone-exception-escape` (all 7 hits are destructors → implicitly noexcept → a throw
= a FRANK terminate; wrapping would MASK errors, anti-doctrine), `performance-inefficient-string-concatenation`
(path-building readability), `misc-const-correctness` (flood). 2 intentional empty-catches → `NOLINT` w/ reason.

- **Lesson — verify, don't auto-apply:** clang-tidy flagged `ModuleFactory`'s `fs` alias as *unused*,
  but `fs::listDirectory/isFile` ARE used — a cross-toolchain mis-resolve. Removing it would have broken
  the build. **Skipped (false positive).** Treat every finding as a claim to verify, not a fix to apply.
- **Coverage caveat:** this pass covered `src/` (the engine core). `modules/` (esp. GPU/bgfx) and
  `include/` headers-in-isolation are NOT yet tidy'd — a follow-up (the GPU modules need their compile
  DB entries, which exist in the Windows build). `include/grove/ImGuiUI.h` errors on a missing `imgui.h`
  (optional header, not in the build) — ignore.
- **Open:** a `make tidy` / `run-clang-tidy` wrapper target so it's one command; widen to `modules/`.
- **UPDATE (2026-07-05):** the `modules/` widen is **now unblocked** — the GPU modules (BgfxRenderer) COMPILE +
  LINK on Linux (proven on VPS142; the old "GPU doesn't compile on Linux" belief was false). So the sanitizer +
  clang-tidy sweep of `modules/` can run on a Linux box. See **`docs/design/linux-port.md`** (the Linux port
  chantier, on the `linux-port` branch). NOTE: CI (Phase 3) was **DECLINED** by Alexi — run the rigor locally.
- **UPDATE (2026-07-06) — modules clang-tidy pass, PART 1 (Windows-clean set):** ran clang-tidy over the 33
  non-BgfxRenderer module `.cpp` on the **Windows** compile DB (complete: SDL + bgfx present, unlike WSL).
  **Verify-the-verifier caught a bad verifier BEFORE trusting it:** on any TU whose include chain reaches
  bgfx's `bx/include/compat/mingw/sal.h`, the clang frontend recurses on that shim (`#include "salieri.h"`
  loops — fine under real g++, breaks under clang) → a SAL cascade that **corrupts the std parse**
  (`std::string` loses `.find`/`.empty`, placement-new "not found") → `unused`/`branch-clone` findings that are
  pure **parse garbage** (the variable IS used; the method just failed to resolve). Findings on those TUs were
  discarded. Partition by whether the compile command drags in that shim:
  - **CLEAN (29 files, trustworthy):** all UIModule (Core/Rendering/Widgets), all InputModule (incl. SDLBackend),
    DialogueModule. Swept; every finding verified against the real code.
  - **SAL-TAINTED (4) → Linux pass:** SoundManager (×2, SDL_mixer), VideoModule (×2, popen→windows.h).
  - **EXCLUDED up front → Linux pass:** BgfxRenderer (18; the compat/mingw include path is on the whole target).
    **No silent cap — this is the honest coverage line.**
  - **Real fixes (3), locked by build + the 51-test UI/Input subset + the tidy re-run showing the finding gone**
    (static-analysis hygiene: the tool re-run IS the regression guard, as in the `src/` crop):
    1. `UIRenderer.h` `RenderEntry` — default-initialized `type/x/y/w/h/color/textureId/layer/fontSize` (the
       struct is copied by the change-detection cache before all fields are set → uninitialized read).
    2. `UIModule.cpp:902` — `keyChar` (signed `char`) → `uint32_t` now goes through `unsigned char` first (a byte
       > 127 would sign-extend into a bogus code-point). Latent (the UTF-8/IME path preempts this legacy branch).
    3. `InputModule.cpp:13` — `~InputModule` calls `InputModule::shutdown()` qualified (non-virtual dispatch in
       the dtor; matches the `src/` `~DebugEngine` precedent).
  - **Verified NON-bugs (left as-is, documented so the next pass doesn't re-chase):** 3× `bugprone-branch-clone`
    all INTENTIONAL (Return+KP_Enter→13; tabs/modal both surface-on-press; Enter+Ctrl-A both `return true`).
    3× `misc-use-internal-linkage` are FALSE POSITIVES — `hitTest`/`updateHoverState`/`dispatchMouseButton` are
    cross-TU (forward-declared + called in `UIModule.cpp`); static would break the link (same trap as the `src/`
    `fs`-unused FP). 1× `performance-enum-size` (`IIO.h:12` `IOType`) surfaced 22× = ONE core-header spot
    (ABI-internal, `include/` not `modules/`) → left. 6× `misc-unused-parameters` are `IModule` override sigs → left.
  - **PART 2 (do on VPS142 Linux — the sound toolchain):** rerun clang-tidy for the 4 SAL-tainted + 18 BgfxRenderer
    against a **Linux** compile DB (GCC/glibc — no MSVC SAL, no mingw shim → clean std parse). Same curated
    `.clang-tidy`, same triage. Also sets up the Phase-1 GPU `[gpu]` ASan/UBSan gap on the same box. The sweep
    runner + clean/taint partition detector are in the session scratchpad (`tidy_modules.sh`) — the Linux variant
    drops the BgfxRenderer exclusion and points `-p` at the Linux build.
- **UPDATE (2026-07-10) — PART 2 DONE (the 22 SAL-tainted/BgfxRenderer files, on VPS142).** Recipe: `sudo apt
  install clang-tidy libsdl2-dev libsdl2-mixer-dev` (LLVM 19) on VPS142 (Tailscale `100.85.89.83`); `git fetch
  origin` + `git worktree add --detach /tmp/gt-tidy origin/master` (isolated tree at my HEAD — the VPS's own
  master carries diverged linux-port commits, untouched); `cmake -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
  + the 3 module flags; `run-clang-tidy -p build 'modules/(BgfxRenderer|SoundManager|VideoModule)/.*\.cpp$'`
  (8-core parallel). **29 findings → triaged.** **Real fixes (locked by Windows build + 153-test ctest + the
  clang-tidy re-run showing them gone; the Linux parse SURFACED bugs Windows can't see):**
  1. `SDLMixerBackend.h` — the forward-decl `typedef struct _Mix_Music Mix_Music;` hardcoded SDL's INTERNAL
     struct tag, which **differs across SDL2_mixer versions** (`_Mix_Music` on the MinGW toolchain, `Mix_Music`
     on Debian's newer SDL2_mixer) → a hard **`clang-diagnostic-error`** (conflicting typedef) on Debian + a
     reserved-identifier. Replaced with `#include <SDL_mixer.h>` (only SDL-aware TUs include this header, so no
     new dep). **A genuine cross-version portability bug Windows built by luck** — now compiles on BOTH (proven:
     `ninja SoundManager_static` green on Debian, MinGW build + SoundManagerUnit green on Windows).
  2. `~SDLMixerBackend` + `~FfmpegCliBackend` — both called a **virtual method (`shutdown`/`close`) unqualified
     in the destructor** (analyzer: bypasses virtual dispatch). Qualified both (matches PART 1's `~InputModule`).
     The SDLMixer one was HIDDEN behind the parse error until fix #1 unblocked it.
  3. `SDLMixerBackend.cpp` rounding `(int)(v*MAX+0.5)` → `std::lround` (v is clamped ≥0 so latent-safe; cleared).
  4. 5× `bugprone-implicit-widening-of-multiplication` (FrameAllocator/DebugPass/TilemapPass/RHICommandBuffer/
     BitmapFont) — all bounded-safe (uint8·16, 64² chunks, compile-time const) but hardened with a target-type
     cast so the multiply happens in size_t/uint32/ptrdiff (no int-overflow-then-widen); cleared.
  5. `BitmapFont::loadBMFont` stub — `[[maybe_unused]]` on the two params (future BMFont-loader inputs).
  - **Verified NON-bugs LEFT (documented, same as PART 1):** 7× `misc-unused-parameters` = IModule/pass override
    sigs (newConfigNode/scheduler/frame/cmd/device); 5× `performance-enum-size` = the one `IIO.h:12 IOType`
    core-header spot (ABI-internal, `include/`); 3× `performance-no-int-to-ptr` = the `nativeWindowHandle` casts
    (a window handle IS an int-encoded pointer); 2× `bugprone-branch-clone` = `ShaderManager` backend→shader-set
    select (backends that share a shader set legitimately have identical bodies). **This is the honest coverage
    line — modules/ is now fully swept (Windows-clean set PART 1 + Linux SAL/BgfxRenderer set PART 2).**

### Phase 3 — Automate (CI)

Stops quality from depending on "did I remember to run it". **Needs a host decision** (we push to
gitea + github + bitbucket).

- A Linux workflow: build (engine compiles on Linux) → `ctest` → the ASan/UBSan/TSan sweeps on the
  key targets → clang-tidy. Gate pushes/PRs.
- **Gotcha:** GPU/windowed tests can't run on a headless runner → CI runs the **core/logic/IIO**
  subset (+ `noop`-backend GPU logic), not the visual/windowed tests. Decide the subset explicitly
  and `log` what's skipped (no silent caps).

### Phase 4 — Anti-regression gates (lock the wins)

- ✅ **DONE (2026-07-11) — committed LSan leak gate.** The ephemeral Phase-1 leak sweep is now a permanent
  `tests/regression/test_leak_gate.cpp` (ctest **`LeakGate`**): a FAST, deterministic scenario that loops the
  core allocation-heavy paths (DebugEngine lifecycle + static-module registration/routing via `engine.step`,
  and raw `IntraIOManager` instance churn with shared-payload pub/sub) so LSan flags any per-cycle leak. Plain
  `main()` — the leak *check* is LSan's job (non-zero exit at process exit on any leak); the test only asserts
  the scenario actually ran. **On Windows = a fast smoke test** (MinGW has no sanitizer); **under a Linux
  `-DGROVE_ENABLE_ASAN=ON` build LSan makes it the real gate.** Verified: Windows smoke (exit 0, 8 cycles);
  **VPS142 g++ + `-DGROVE_ENABLE_ASAN=ON` → 0 leaks** (exit 0, no LSan output); **prove-it-bites** — an injected
  `new int[64]` → `LeakSanitizer: 256 byte(s) leaked`, exit 1 (gate would fail). Coverage line (honest): core
  lifecycle + module routing + IntraIO alloc/pub/sub; NOT the dlopen path (covered by the reload ctests under
  ASan) or GPU/SDL. **Gotcha: WSL can't run this** — a fresh WSL build needs `FetchContent` to git-clone
  nlohmann/spdlog/Catch2, but WSL in NAT mode has no proxy → clone fails; run the gate on **VPS142** (network +
  `libasan`) via a `git worktree` at HEAD (mirror of the PART-2 clang-tidy recipe), or a Linux box with network.
- ✅ **DONE (2026-07-11) — committed IIO perf gate.** `tests/regression/test_iio_perf_gate.cpp` (ctest
  **`IIOPerfGate`**) guards the zero-copy **O(1) fan-out** invariant with a **machine-independent RATIO**, not a
  flaky absolute-ns ceiling: it publishes a fat payload to N=1 vs N=32 subscribers and asserts the per-SUBSCRIBER
  cost DROPS below half as fan-out grows (real drop ~25–30×; a reintroduced per-subscriber json copy keeps it
  ~flat → fails). Node built OUTSIDE the timer (isolates publish+route). Verified stable across runs (~25×,
  never near the 2× threshold — no flake) + prove-it-bites (impossible 1000× threshold → the FAIL branch fires).
  ~6.8 s (M=6000). The full A/B `benchmark_iio_zerocopy.cpp` stays the by-hand release tool.
- ✅ **DONE (2026-07-11) — doc-example compile check.** `tests/regression/test_doc_examples.cpp` (ctest
  **`DocExamples`**) mirrors the docs' load-bearing C++ API snippets (IIO pub/sub, DebugEngine hosting + a
  minimal `IModule`, `JsonDataNode`, the `grove::fx` authoring surface) and compiles them against the REAL
  headers — prose isn't compiler-checked, this TU is, so a renamed/removed documented API stops the build and
  flags the doc. The snippets need not RUN (an always-false branch), so it's a fast side-effect-free compile
  gate. Prove-it-bites: swapping in the exact stale `pullMessage()` → a compile error ("no member named
  pullMessage"). Coverage (honest): the core GPU/SDL-free surfaces; NOT `submitSpriteBatch`/`render:*` (need
  the BgfxRenderer link) or the topic wire formats (their own E2Es). **Phase 4 is now complete** (leak gate +
  perf gate + doc-example check); the deferred axes below remain (CI declined, fuzz/coverage, `[gpu]` sanitizer).

---

## Deferred / other axes (named, not scheduled)

- **Fuzzing the JSON parsers** (libFuzzer/AFL on the message + UI-layout parse paths). The bus trusts
  module-published JSON; a malformed payload shouldn't crash the engine. Robustness lever.
- **Coverage** (gcov/llvm-cov) — not a goal in itself, but reveals untested **error paths**.
- **Determinism / replay sink + seq-dedup** — this is the next *feature* in the IO-contract build
  order, tracked in [iio-contract-handoff.md](iio-contract-handoff.md), not here. (Quality-adjacent:
  deterministic replay = reproducible bug repro.)

---

## Recipes & key facts (resume aids)

- **WSL sanitizer recipe** (the pattern): a standalone wrapper CMake `add_subdirectory`s the live repo
  with `GROVE_BUILD_TESTS/MODULES=OFF` (so GPU `find_package` never runs), applies the `-fsanitize`
  flags **globally before** `add_subdirectory` (so `grove_impl` + the harness are both instrumented),
  builds into `~/<san>_build`. TSan needs ASLR off: `setarch $(uname -m) -R ./exe`. Full TSan version:
  [[tsan-via-wsl-recipe]]. **Once Phase 1a lands, this wrapper is obsolete** — sanitizers become a
  flag on the normal WSL build.
- **Today's ASan/LSan harness** (the model for Phase 1c/4): exercised both publish modes, fan-out,
  capture/release, **and undrained-at-teardown** payloads; negative-controlled. Lived in the session
  scratchpad (`scratchpad/asan/`) — **re-create or promote it**, don't assume it persists.
- **MinGW has no sanitizers** → WSL for all of them. Windows/MinGW stays the perf + GPU build.
- **Run sanitizers `-O1 -g -fno-omit-frame-pointer`** (readable stacks, still fast enough).

## Open decisions (for Alexi)

1. **CI host** — GitHub Actions (github remote), gitea CI, or none yet? Determines Phase 3.
2. **UBSan aggressiveness** — fix every finding, or suppress the pedantic ones (e.g. intentional
   unsigned wrap)? Sets the triage bar.
3. **GPU-test sanitizer coverage** — install bgfx/SDL on WSL to sanitize the `[gpu]` tests too, or
   accept core/logic-only sanitizing for now?
4. **Promote the scratchpad harness** into the repo as a permanent leak-check target (Phase 4) — yes/no?

## Key files

- `CMakeLists.txt:11-16` (TSAN option block — the **template** for ASAN/UBSAN), `:27-29` (Helgrind).
- `docs/design/rendering-throughput-handoff.md` — the zero-copy work this hardening follows.
- `docs/design/iio-contract-handoff.md` — the determinism/replay feature track (separate).
- Memory: [[tsan-via-wsl-recipe]], [[rendering-throughput]], [[engine-io-contract]].

**Resume from here (Phases 1+2 done):** two clean increments left, pick by appetite —
- **Widen coverage to `modules/`** (cheap, no new infra): point clang-tidy at `modules/*/` (the Windows
  build already has their compile-DB entries) + get bgfx/SDL onto the WSL toolchain so the `[gpu]`/SDL
  tests join the sanitizer sweep. Closes the one gap: the core is proven clean, the modules aren't yet.
- **Phase 3 (CI)** — the structural win, but it needs Alexi's host decision first (github / gitea / none).
  Once a host is picked, wire a Linux workflow: build → ctest → the ASan/UBSan/TSan sweeps → clang-tidy.

Everything needed to repeat the sanitizer + clang-tidy runs by hand is in the phase sections above
(recipes, gotchas, the pip-wheel + WSL paths). Nothing is blocked except the CI host choice.
