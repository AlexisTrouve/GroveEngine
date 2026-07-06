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

**Next:** the `modules/` clang-tidy widen is **PART 1 done** (2026-07-06 — 29 Windows-clean module files swept,
3 real fixes; see Phase 2 UPDATE). **PART 2 = the SAL-tainted set on Linux** (SoundManager/Video/BgfxRenderer —
Windows clang-tidy can't parse them soundly; run on VPS142's GCC toolchain) — **⚠️ DEFERRED DEBT (Alexi's call
2026-07-06: keep all Linux work parked for now).** NOT technically blocked, just deprioritized; do not surface it
as the active next step. Then Phase 3 (CI, needs a host decision) / Phase 4 (regression gates). Detail below.

| Capability | Status |
|---|---|
| Test suite (121 ctest) | ✅ solid, the backbone |
| TSan / Helgrind | ✅ wired (`GROVE_ENABLE_TSAN` / `_HELGRIND`, manual flags, `CMakeLists.txt:11-16, 27-29`); IIO/pool TSan-proven |
| ASan / LSan | ✅ wired (`GROVE_ENABLE_ASAN`, commit `400e62a`); core + hot-reload swept **clean** |
| UBSan | ✅ wired (`GROVE_ENABLE_UBSAN`, `400e62a`); swept **clean**, negative-controlled |
| clang-tidy / static analysis | ✅ `.clang-tidy` curated (`a668239`); `src/` crop fixed. `modules/` **clean set swept on Windows** (29 non-SAL files, 3 real fixes) — SAL-tainted set (SoundManager/Video/BgfxRenderer) → Linux (see Phase 2 UPDATE 2026-07-06) |
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

### Phase 3 — Automate (CI)

Stops quality from depending on "did I remember to run it". **Needs a host decision** (we push to
gitea + github + bitbucket).

- A Linux workflow: build (engine compiles on Linux) → `ctest` → the ASan/UBSan/TSan sweeps on the
  key targets → clang-tidy. Gate pushes/PRs.
- **Gotcha:** GPU/windowed tests can't run on a headless runner → CI runs the **core/logic/IIO**
  subset (+ `noop`-backend GPU logic), not the visual/windowed tests. Decide the subset explicitly
  and `log` what's skipped (no silent caps).

### Phase 4 — Anti-regression gates (lock the wins)

- Promote today's **LSan leak harness** (currently in the session scratchpad — *ephemeral*) into a
  committed `tests/` target so the leak check survives.
- Turn the **zero-copy perf benchmark** into a soft gate (assert `ns/publish` stays under a ceiling)
  or at least document it as a release-check.
- **Doc-example compile check** — we shipped two stale examples this cycle (`pullMessage()`,
  `setChild(std::move(msg.data))`); a check that extracts + compiles doc code blocks would catch the
  next one. (Doctrine: doc accuracy is paramount.)

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
