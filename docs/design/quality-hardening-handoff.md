# Quality Hardening — plan + handoff

**Thesis.** The rigor we applied to the zero-copy IIO work (TSan + ASan/LSan + a measured perf A/B +
a negative-controlled leak check) was **manual and narrow** — run by hand, only on the IIO path. The
quality leap is to make that rigor **systematic** (repeatable, ideally automated), **broad** (the
whole engine, not one subsystem), and to add the **lenses we have never used** (UBSan, static
analysis). This doc is the plan + the resume-from-here state.

---

## Where we are (grounded 2026-06-27)

| Capability | Status |
|---|---|
| Test suite (121 ctest) | ✅ solid, the backbone |
| TSan / Helgrind | ⚠️ wired as **manual** CMake flags (`GROVE_ENABLE_TSAN` / `_HELGRIND`, OFF by default), `CMakeLists.txt:11-16, 27-29` |
| ASan / LSan | ❌ **not a build option** — run today via a throwaway WSL wrapper only |
| UBSan | ❌ **never run** — an entire bug class unchecked |
| clang-tidy / static analysis | ❌ none (no `.clang-tidy`) |
| CI (auto on push) | ❌ none — everything is manual, depends on remembering |
| Coverage | ❌ not measured |
| Perf / leak regression gates | ❌ none (benchmarks are wall-clock, run by hand) |

**Toolchain reality:** primary build is Windows/MinGW (Ninja, `-O3`). **MinGW ships no ASan/UBSan/TSan**
→ all sanitizers run via **WSL** (the engine compiles on Linux — see [[tsan-via-wsl-recipe]]). This
constrains *how* the sweeps run (below), not *whether*.

---

## Plan (phased by leverage)

### Phase 1 — Sanitizer lenses ✅ DONE (sweep clean) — 2026-06-27

**Result: the core/logic/IIO/math/datatree/topictree subset is sanitizer-clean.** UBSan = 0 undefined
behavior; ASan+LSan = 0 memory errors, 0 leaks — across 44 passing tests, **negative-controlled** (a
deliberate signed overflow IS caught by UBSan; a leaked `JsonDataNode` IS caught by LSan). `GROVE_ENABLE_ASAN`
/ `_UBSAN` wired (commit `400e62a`). The original plan (below) held; what we actually ran + the gotchas
that bit are captured in *Phase 1 — what actually worked* further down.

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

1. **~5 hot-reload tests hardcode `.dll`** (`ModuleDependencies`, `MultiVersionCoexistence`,
   `IOSystemStress`, …): they `load("./libBaseModule.dll")` etc., so they **fail on Linux** (`cannot
   open shared object file`) and **never get sanitized**. The hot-reload `.so` lifecycle is exactly
   where use-after-free hides → fixing the extension to be platform-conditional **unblocks sanitizer
   coverage of that path** (+ makes them CI-able). High-value follow-up.
2. **`LimitsTest` / `ConfigHotReload`** pass under UBSan-only but fail under ASan+UBSan with **zero
   sanitizer errors** — they load their `.so` fine; the failures are timing/count assertions sensitive
   to the ~2.5× ASan slowdown (LimitsTest is literally the *timeout* test). Confirm on a plain Linux
   build, then relax the timing assertions. Not a memory bug.

### Phase 2 — Static analysis (clang-tidy)

A different bug class (uninit members, missing virtual dtors, API misuse, implicit conversions),
caught *statically* + continuously.

- Add a curated `.clang-tidy` (start narrow: `bugprone-*`, `performance-*`, a `cppcoreguidelines`
  subset; disable the noisy/opinionated ones). Run over `src/ include/ modules/`. Triage; fix
  high-confidence findings, suppress (with reason) the rest.
- Optional `make tidy` target.
- **Success:** `.clang-tidy` committed; a clean-or-triaged run.
- **Gotcha:** blanket-enabling all checks = noise flood → curate the list, expand gradually.

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

**Suggested start:** Phase 1a + 1b (wire `GROVE_ENABLE_UBSAN`, sweep the suite, triage the crop) — it's
one focused session, pure bug-hunting in your TDD-adversarial loop, and it tells us how dirty (or
clean) the engine really is before investing in CI.
