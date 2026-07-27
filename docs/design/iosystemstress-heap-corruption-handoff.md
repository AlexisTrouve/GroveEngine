# IOSystemStress — corruption de tas intermittente (chasse en cours)

> **Statut** : **NON RÉSOLU**. Hypothèses éliminées par la mesure, suspect reformulé.
> Écrit le 2026-07-27. Reprendre à la §5.

## 1. Le symptôme

`IOSystemStress` (ctest #68, `tests/integration/test_11_io_system.cpp`) sort par intermittence en
**`0xC0000374` = `STATUS_HEAP_CORRUPTION`** sur Windows/MinGW.

**Taux mesuré : ~4 %** (2 échecs sur ~55 exécutions, en série). C'est le chiffre qui compte pour la
suite : **valider un correctif demandera ~75 exécutions propres** pour être crédible à 95 %. Un
« ça passe 10 fois » ne prouvera rien.

⚠️ Ce n'est **pas** l'artefact de parallélisme `ctest -j4` (celui de `StressTest`) : il échoue aussi
en série. Cette confusion a été faite une fois, ne pas la refaire.

## 2. Ce qui est ÉLIMINÉ (par la mesure, pas par la lecture)

| Hypothèse | Verdict | Comment |
|---|---|---|
| Enfilement de la boîte de réception non verrouillé | ❌ | `deliverMessage` prend `operationMutex` (`src/IntraIO.cpp:379`) |
| Drainage non verrouillé | ❌ | `pullAndDispatch` phase 1 sous `operationMutex` |
| `hasMessages()` lisant la deque sans verrou | ❌ | verrouille aussi (`src/IntraIO.cpp:175`) |
| Violation du contrat une-thread-par-instance | ❌ | le garde-fou `ScopedAccessGuard` reste **silencieux sur 30 exécutions** |
| Course de données dans la concurrence supportée | ❌ | **TSan propre**, jusqu'à 32 threads × 1000 msg (6× la pression du test) |
| Capture d'une pile au moment du crash sous Windows | ❌ | `0xC0000374` passe par `RtlFailFast`, qui **contourne** `SetUnhandledExceptionFilter` — `CrashBacktrace.h` ne se déclenche jamais pour cette classe |

## 3. ⚠️ Une conclusion intermédiaire à corriger

En cours de chasse j'ai écrit que le défaut était « localisé dans TEST 6 » parce que le processus
meurt pendant cette phase. **C'est une inférence fausse et il ne faut pas repartir dessus.**

Une corruption de tas est détectée au prochain `free`/`alloc` qui touche les métadonnées abîmées,
**pas là où l'écriture fautive a eu lieu**. TEST 6 alloue des milliers de `JsonDataNode` — c'est
simplement l'endroit du test où le trafic d'allocation est le plus dense, donc le plus probable pour
*heurter* un dégât causé plus tôt. Le crash y apparaît ; rien ne dit qu'il y naît.

## 4. Le suspect reformulé

Le test **charge des modules** (`ProducerModule`, `ConsumerModule`, `BroadcastModule`, `BatchModule`,
`IOStressModule`) avant d'arriver à TEST 6. La **frontière de tas inter-DLL** est le suspect naturel
d'une corruption qui se révèle tard : sous MinGW, hôte et `.dll` peuvent servir des tas distincts, et
un objet alloué d'un côté puis libéré de l'autre corrompt silencieusement les métadonnées.

**Ce repo a exactement ce précédent** : `docs/design/limitstest-segfault-handoff.md` — un
`JsonDataNode` obtenu d'un module et détruit après déchargement de la DLL. La leçon y était
« cross-DLL object lifetime », la même famille.

## 5. Reprendre ici

1. **ASan, pas TSan.** TSan cherche des courses et a déjà répondu non. ASan attrape la corruption
   **au moment de l'écriture fautive** (débordement, use-after-free) — c'est l'outil pour ce symptôme.
2. Il faut le test **complet, modules compris** (le chargement de modules est le suspect), donc
   construire les `.so` sous Linux. Le cœur compile déjà sous Linux (cf. [linux-port], PARKÉ mais le
   build cœur fonctionne).
3. Piste secondaire si ASan est muet : instrumenter les `alloc`/`free` traversant la frontière module
   (qui alloue, qui libère) plutôt que de chercher un débordement.

## 6. L'outil construit pour cette chasse (réutilisable)

`tests/repro/tsan_iio_concurrency.cpp` — un pilote minimal reproduisant **la forme de concurrence de
TEST 6 et rien d'autre** (N publishers ayant chacun son instance → 1 consommateur propriétaire), sans
chargement de module, donc atteignant la phase concurrente immédiatement. Pression réglable par argv.

Recette de compilation (aucun réseau requis, toutes les dépendances sont vendorées) :

```bash
# depuis WSL, à la racine du repo
g++ -std=gnu++17 -g -O1 -fsanitize=thread \
  -Iinclude -Ideps/spdlog/include -Ideps/nlohmann_json/include \
  -Iexternal/StillHammer/topictree/include -Iexternal/StillHammer/logger/include \
  tests/repro/tsan_iio_concurrency.cpp \
  src/IntraIO.cpp src/IntraIOManager.cpp src/JsonDataNode.cpp src/JsonDataValue.cpp \
  src/detail/AccessGuard.cpp external/StillHammer/logger/src/Logger.cpp \
  -o ~/tsanb/repro -lpthread

setarch -R ~/tsanb/repro 16 2000 0     # setarch -R = ASLR off, sinon TSan refuse de démarrer
```

Deux pièges rencontrés, notés pour ne pas les repayer :
- sans `setarch -R` : `FATAL: ThreadSanitizer: unexpected memory mapping` ;
- `/tmp` est **vidé entre deux invocations `wsl -e`** (la VM s'arrête) — compiler vers `~`, et
  enchaîner build+run dans **une seule** invocation.

## 7. Observation secondaire (pas un bug)

À forte pression le pilote montre `published=32000 received=21160` : ~1/3 des messages sont
**volontairement lâchés** par la politique de contre-pression (drop-oldest au-delà de `maxQueueSize`).
Comportement attendu, mentionné ici pour que personne ne le prenne pour une perte accidentelle.
