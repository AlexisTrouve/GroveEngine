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
| 5 | 🔴 CRIT | UI | Clavier mort : UIModule subscribe `"input:keyboard"`, InputModule publie `"input:keyboard:key"` → ne matche pas | 🔧 ouvert |
| 6 | 🟠 HIGH | UI | Clics ratés en layout : `absX/absY` (hit-test) pas recalculés après `UILayout` | 🔧 ouvert (candidat rewrite) |
| 7 | 🟠 HIGH | IIO | 3 matchers de patterns incohérents (regex IntraIO ≠ lambda manager ≠ TopicTree) → mis-routing | 🔧 ouvert |
| 8 | 🟡 MED | IIO | `unload()` purge les subs côté IntraIO seulement — TopicTree/`instancePatterns` du manager gardent l'instance → routage fantôme + fuite lente au reload | 🔧 ouvert |
| 9 | 🟡 MED | IIO | `routeMessage` prend `managerMutex` exclusif (defeat shared_mutex) + `instancePatterns[]` insère sur le chemin chaud | 🔧 ouvert |
| 10 | 🟠 HIGH | Threaded | BUG D réel : `queryModule` appelle `process()` depuis le thread appelant pendant le worker → data race, juste loggée | 🔧 ouvert (ThreadedModuleSystem à considérer "expérimental/non-safe", pas "Phase 2 Complete") |

## Verdicts par sous-système

- **Eventbus (IIO)** : 🟡 pub/sub/fan-out/patterns OK et testés ; **backpressure réparée** (#2) ; restent matchers incohérents (#7), routage fantôme au reload (#8), lock exclusif (#9).
- **2D renderer** : 🟡 double-free (#1) + overflow (#3) + alignement réparés et **lockés par tests headless** ; reste l'ordre layer/depth (#4). `executeCommandBuffer` (couche bgfx réelle) et la correction **pixel** restent non testés (pas de screenshot-diff).
- **UI (UIModule)** : 🔴 toujours "n'existe pas" — clavier cassé (#5), hit-test cassé en layout (#6), ScrollPanel cassé, **zéro E2E**. Harness E2E faisable (input via IIO, events observables) ; ScrollPanel + couplage layout/absX = **rewrite**, pas retrofit.
- **Reload ceiling (~70-100 reloads/process)** : **architectural** (tables d'exception DW2/SJLJ + fragmentation adressage ; les orphan DLLs, eux, sont bien gérés). Non réparable chirurgicalement — nécessite host out-of-process ou stratégie no-unload.

## Roadmap restante

**Tier 2 (correctness)** : #5 topic clavier UI · #7 unifier les matchers (source unique de vérité) · #8 `unload` purge aussi le manager · #9 `routeMessage` en `shared_lock` + `.find`.

**Tier 3 (gros/stratégique)** : harness E2E UI + rework ScrollPanel/layout (#6) · #4 mapper `layer` → clé de tri submit · #10 router `queryModule` via IIO + corriger le statut doc du ThreadedModuleSystem · plafond reload (host out-of-process) · E2E pixel screenshot-diff.

## Note testabilité (doctrine)
"Une UI sans test E2E qui clique réellement = non vérifiée." L'UIModule n'a toujours aucun test
d'interaction → traiter comme non prouvé. Le renderer n'asserte aucun pixel → le rendu visuel
reste non vérifié (seuls les commandes/packets le sont, désormais y compris `debug:rect`/`text`).
