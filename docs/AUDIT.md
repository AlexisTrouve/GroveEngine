# GroveEngine — Audit 2026-06-15

Audit adversarial des 3 sous-systèmes (2D renderer, UIModule, eventbus IIO + lifecycle),
mené par 3 auditeurs en lecture seule puis re-vérifié sur le code. Statut tenu à jour au fil
des fixes.

Légende statut : ✅ corrigé (commit) · 🔧 ouvert (roadmap) · ✅(test) verrouillé par un test.

## Findings

| # | Sév | Sous-système | Problème | Statut |
|---|---|---|---|---|
| 1 | 🔴 CRIT | 2D | Double-free shader : `"color"`/`"debug"` aliasent le même handle, `shutdown()` le `destroy()` 2× | ✅ `fcfcd56` — dédup, locké par `ShaderManagerUnit` |
| 2 | 🟠 HIGH | IIO | `enforceQueueLimits()` jamais appelée → queues non bornées → OOM, backpressure morte | ✅ `4843c11` — rebranchée, locké par `IIOBackpressure` |
| 3 | 🟠 HIGH | 2D | DebugPass overflow : buffer `MAX_DEBUG_LINES*2` verts, écrit `lines*2 + rects*8` sans clamp | ✅ `49b1175` — clamp+warn, locké par `DebugPassUnit` |
| — | 🟠 HIGH | 2D | **FrameAllocator** : alignait l'offset, pas l'adresse absolue → `allocate(_,32/64)` rendait du 16-aligné (crash SIMD) | ✅ `fcfcd56` — adresse absolue, locké par `FrameAllocatorUnit` |
| — | — | Tous | 6 tests (unit RHI + scene_collector) **écrits mais jamais enregistrés** dans ctest → 0 couverture | ✅ `fcfcd56`/`49b1175` — enregistrés (dont `SceneCollectorTest` couvre `debug:rect`/`text`) |
| 4 | 🟠 HIGH | 2D | Layer/depth pas honoré au submit (depth=0 partout) : ordre = ordre CPU intra-pass, pas le champ `layer` | 🔧 ouvert |
| 5 | 🔴 CRIT | UI | Clavier mort : UIModule subscribe `"input:keyboard"`, InputModule publie `"input:keyboard:key"`/`:text` → ne matche pas | 🟡 **partiel** `2f5234d` — chemin **texte** corrigé (subscribe `input:keyboard:text`) + locké par E2E `UIClickAssert` ; touches spéciales (mapping scancode→keycode) + UTF-8 (C2) restants |
| 6 | 🟠 HIGH | UI | Clics ratés en layout : `absX/absY` (hit-test) pas recalculés après `UILayout` | 🔧 ouvert (candidat rewrite) |
| 7 | 🟠 HIGH | IIO | 3 matchers de patterns incohérents (regex IntraIO ≠ lambda manager ≠ TopicTree) → mis-routing / swallow | 🟡 **partiel** `c15d812` — swallow (`.*` terminal) corrigé+locké ; unification complète (single-`*` cross-segment, lambda freq) restante |
| 8 | 🟡 MED | IIO | `unload()` purge les subs côté IntraIO seulement — routage fantôme + fuite lente au reload | ✅ `f10f050` — `clearInstanceSubscriptions`, locké |
| 9 | 🟡 MED | IIO | `routeMessage` `managerMutex` exclusif + `instancePatterns[]` insère sur le chemin chaud | 🟡 **partiel** `f205e4c` — `operator[]`→`.find` ; passage `shared_lock` (perf) différé (à valider TSAN) |
| 10 | 🟠 HIGH | Threaded | BUG D réel : `queryModule` appelle `process()` depuis le thread appelant pendant le worker → data race, juste loggée | 🔧 ouvert (ThreadedModuleSystem à considérer "expérimental/non-safe", pas "Phase 2 Complete") |

## Verdicts par sous-système

- **Eventbus (IIO)** : 🟡 pub/sub/fan-out/patterns OK et testés ; **backpressure réparée** (#2) ; restent matchers incohérents (#7), routage fantôme au reload (#8), lock exclusif (#9).
- **2D renderer** : 🟡 double-free (#1) + overflow (#3) + alignement réparés et **lockés par tests headless** ; reste l'ordre layer/depth (#4). `executeCommandBuffer` (couche bgfx réelle) et la correction **pixel** restent non testés (pas de screenshot-diff).
- **UI (UIModule)** : 🔴 toujours "n'existe pas" — clavier cassé (#5), hit-test cassé en layout (#6), ScrollPanel cassé, **zéro E2E**. Harness E2E faisable (input via IIO, events observables) ; ScrollPanel + couplage layout/absX = **rewrite**, pas retrofit.
- **Reload ceiling (~70-100 reloads/process)** : **architectural** (tables d'exception DW2/SJLJ + fragmentation adressage ; les orphan DLLs, eux, sont bien gérés). Non réparable chirurgicalement — nécessite host out-of-process ou stratégie no-unload.

## Roadmap restante

**Tier 2 (correctness)** — ✅ livré : #8 (`unload` purge le manager), #9-partiel (`.find`), #7-partiel (swallow `.*` terminal). Restes : #7 unification complète des matchers (source unique de vérité — single-`*` + lambda freq), #9 `shared_lock` (perf, à valider TSAN). #5 déplacé en Tier 3 (cf. ci-dessous).

**Tier 3 (gros/stratégique)** : ✅ **harness E2E UI amorcé** (`IT_016/UIClickAssert` — clic bouton + saisie textinput prouvés ; #5-texte corrigé). Restes : autres widgets (checkbox toggle, slider — cf. audit H2 drag cassé), rework ScrollPanel/layout #6 + mapping scancode des touches spéciales (#5 suite) · #4 `layer`→clé de tri submit · #10 `queryModule` via IIO + statut doc ThreadedModuleSystem · plafond reload (host out-of-process) · E2E pixel screenshot-diff · #7 unification matchers + #9 shared_lock (TSAN).

## Note testabilité (doctrine)
**Harness E2E UI établi** (`IT_016`) : inject input via IIO → assert events `ui:*`, headless,
sans fenêtre. Couvre désormais : clic bouton (`ui:click`/`ui:action`) + saisie textinput
(`ui:text_changed`). Restent non prouvés : checkbox/slider/scroll, touches spéciales, et le
**rendu pixel** (le renderer n'asserte aucun pixel — seuls les commandes/packets le sont, y
compris `debug:rect`/`text`).
