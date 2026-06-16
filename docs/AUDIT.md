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
| 5 | 🔴 CRIT | UI | Clavier mort : UIModule subscribe `"input:keyboard"`, InputModule publie `"input:keyboard:key"`/`:text` → ne matche pas | 🟡 **quasi** `2f5234d`+`6ca07a8` — **texte** (subscribe `input:keyboard:text`) + **touches d'édition** (table scancode SDL→keycode dans `input:keyboard:key` : backspace/entrée/suppr/flèches/home/end) corrigés, lockés par `UIClickAssert` ; reste UTF-8/multi-char (C2) |
| 6 | 🟠 HIGH | UI | Clics ratés en layout : `absX/absY` (hit-test ET rendu) pas recalculés après `UILayout` (calculés une fois au load, avant la 1re passe layout) | ✅ `5ddf9b7` — `computeAbsolutePosition()` rappelé après `UILayout::layout()` dans `UIPanel::update` (fix chirurgical, pas de rewrite), locké par `UILayoutHitTestE2E` |
| H2 | 🟠 HIGH | UI | Slider : `ui:value_changed` émis seulement aux fronts press/release → aucun feedback live pendant le drag ; pire, release hors `containsPoint` (bord droit) → valeur finale perdue | ✅ `6590427` — émission centralisée post-`update()` (par id, NaN-grab), locké par `UIWidgetsE2E` |
| 7 | 🟠 HIGH | IIO | 3 matchers de patterns incohérents (regex IntraIO ≠ lambda manager ≠ TopicTree) → mis-routing / swallow | 🟡 **partiel** `c15d812` — swallow (`.*` terminal) corrigé+locké ; unification complète (single-`*` cross-segment, lambda freq) restante |
| 8 | 🟡 MED | IIO | `unload()` purge les subs côté IntraIO seulement — routage fantôme + fuite lente au reload | ✅ `f10f050` — `clearInstanceSubscriptions`, locké |
| 9 | 🟡 MED | IIO | `routeMessage` `managerMutex` exclusif + `instancePatterns[]` insère sur le chemin chaud | 🟡 **partiel** `f205e4c` — `operator[]`→`.find` ; passage `shared_lock` (perf) différé (à valider TSAN) |
| 10 | 🟠 HIGH | Threaded | BUG D réel : `queryModule` appelle `process()` depuis le thread appelant pendant le worker → data race, juste loggée | 🔧 ouvert (ThreadedModuleSystem à considérer "expérimental/non-safe", pas "Phase 2 Complete") |

## Verdicts par sous-système

- **Eventbus (IIO)** : 🟡 pub/sub/fan-out/patterns OK et testés ; **backpressure réparée** (#2) ; restent matchers incohérents (#7), routage fantôme au reload (#8), lock exclusif (#9).
- **2D renderer** : 🟡 double-free (#1) + overflow (#3) + alignement réparés et **lockés par tests headless** ; reste l'ordre layer/depth (#4). `executeCommandBuffer` (couche bgfx réelle) et la correction **pixel** restent non testés (pas de screenshot-diff).
- **UI (UIModule)** : 🟡 sort du "n'existe pas" — **E2E réels** désormais : bouton (clic + miss), textinput (saisie), **checkbox** (toggle + sens), **slider** (clic + drag live), **hit-test en layout vertical**. **#5** (texte + touches d'édition) / **H2** / **#6** corrigés (les layouts non-`absolute` sont enfin cliquables — fix sans rewrite). Restent : ScrollPanel, UTF-8/multi-char clavier (C2), rendu **pixel** non asserté.
- **Reload ceiling (~70-100 reloads/process)** : **architectural** (tables d'exception DW2/SJLJ + fragmentation adressage ; les orphan DLLs, eux, sont bien gérés). Non réparable chirurgicalement — nécessite host out-of-process ou stratégie no-unload.

## Roadmap restante

**Tier 2 (correctness)** — ✅ livré : #8 (`unload` purge le manager), #9-partiel (`.find`), #7-partiel (swallow `.*` terminal). Restes : #7 unification complète des matchers (source unique de vérité — single-`*` + lambda freq), #9 `shared_lock` (perf, à valider TSAN). #5 déplacé en Tier 3 (cf. ci-dessous).

**Tier 3 (gros/stratégique)** : ✅ **harness E2E UI étendu** (`IT_016/UIClickAssert` : clic bouton + saisie textinput ; `IT_017/UIWidgetsE2E` : checkbox toggle + slider clic + **slider drag live** → **H2** ; `IT_018/UILayoutHitTestE2E` : clic sur widget positionné par layout vertical → **#6** ; `IT_016` étendu : touches d'édition → **#5-suite**). #5 (texte + touches d'édition) + H2 + #6 corrigés. Restes : ScrollPanel (rework), UTF-8/multi-char clavier (#5/C2) · #4 `layer`→clé de tri submit · #10 `queryModule` via IIO + statut doc ThreadedModuleSystem · plafond reload (host out-of-process) · E2E pixel screenshot-diff · #7 unification matchers + #9 shared_lock (TSAN).

## Note testabilité (doctrine)
**Harness E2E UI établi** (`IT_016` + `IT_017` + `IT_018`) : inject input via IIO → assert events
`ui:*`, headless, sans fenêtre. Couvre désormais : clic bouton (`ui:click`/`ui:action`), saisie
textinput (`ui:text_changed`), checkbox toggle (`ui:value_changed{checked}`, sens vérifié), slider
clic + **drag live** (`ui:value_changed{value}` à chaque frame du drag), **hit-test sur widget
positionné par layout vertical** (`ui:action` du bon enfant), et **touches d'édition** (backspace
→ texte raccourci, Enter → `ui:text_submit`/action). Restent non prouvés : scroll, UTF-8/multi-char,
et le **rendu pixel** (le renderer n'asserte aucun pixel — seuls les commandes/packets le sont, y
compris `debug:rect`/`text`).
