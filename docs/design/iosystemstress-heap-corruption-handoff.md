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
| Débordement / use-after-free détectable | ❌ | **ASan propre sur 40 exécutions du test COMPLET** (modules `.so` inclus) sous Linux |
| Copie dupliquée du moteur dans chaque DLL | ❌ | `nm --defined-only` sur `libProducerModule.dll` : **0** définition de `IntraIOManager::getInstance` et de `IntraIO::publish` — le module les IMPORTE de l'exe |
| Tas séparés entre l'hôte et les modules | ❌ | exe et DLL importent **tous deux** `libstdc++-6.dll` et `api-ms-win-crt-heap-l1-1-0.dll` — runtime et tas partagés |
| Objets détruits après déchargement de la DLL | ❌ (côté test) | le test détruit l'`IntraIO` du module **avant** `FreeLibrary` (`test_11_io_system.cpp:101-107`) |

## 3. ⚠️ Une conclusion intermédiaire à corriger

En cours de chasse j'ai écrit que le défaut était « localisé dans TEST 6 » parce que le processus
meurt pendant cette phase. **C'est une inférence fausse et il ne faut pas repartir dessus.**

Une corruption de tas est détectée au prochain `free`/`alloc` qui touche les métadonnées abîmées,
**pas là où l'écriture fautive a eu lieu**. TEST 6 alloue des milliers de `JsonDataNode` — c'est
simplement l'endroit du test où le trafic d'allocation est le plus dense, donc le plus probable pour
*heurter* un dégât causé plus tôt. Le crash y apparaît ; rien ne dit qu'il y naît.

## 4. Suspects successifs — et pourquoi ils sont tombés

⚠️ **Deuxième correction à mon propre raisonnement.** J'ai d'abord désigné la **frontière de tas
inter-DLL** (précédent `limitstest-segfault-handoff.md`, même famille). Vérification faite, elle
n'existe pas dans ce build : le module n'embarque aucune copie du moteur (il l'importe de l'exe) et
les deux partagent `libstdc++-6.dll` + le tas UCRT. **Hypothèse séduisante, historiquement fondée,
et fausse ici.** Ne pas la reprendre sans re-vérifier les imports.

### Ce qui reste, et l'indice différentiel le plus fort

**Linux est propre, Windows corrompt.** La différence structurelle la plus lourde entre les deux, une
fois les tas écartés, est le **déchargement de bibliothèque** : `FreeLibrary` démappe réellement le
code sous Windows, alors que la `dlclose` de la glibc **ne démappe très souvent PAS**. Un accès à du
code ou à une vtable appartenant à un module déchargé serait donc **invisible sous Linux par
construction** — ce qui expliquerait d'un coup les 40 exécutions ASan propres.

Le test se protège du cas évident (il détruit l'`IntraIO` du module avant `FreeLibrary`), mais le
**fil de flush par lots du manager tourne en parallèle**. Une fenêtre de course entre ce fil et la
destruction/le déchargement collerait au profil : rare (~4 %), dépendante du timing, invisible sous
Linux. **C'est la piste à instrumenter en premier.**

## 5. Reprendre ici

**Les sanitizers Linux sont épuisés** : TSan et ASan ont tous deux répondu non, sur le test complet.
Continuer à les relancer ne produira rien de neuf.

1. **Instrumenter la fenêtre déchargement × fil de flush**, côté Windows : tracer l'ordre exact
   (dernière livraison du fil de flush vers l'instance du module ↔ destruction de l'instance ↔
   `FreeLibrary`) sur ~100 exécutions, et chercher les runs où l'ordre s'inverse. C'est du log
   horodaté, pas un sanitizer — et c'est ce que le symptôme réclame.
2. Si l'ordre est toujours correct : suspecter la **destruction statique en fin de processus**
   (le singleton `IntraIOManager` et son fil, dont `~IntraIOManager` gère déjà un cas délicat
   documenté à `IntraIOManager.cpp:87-94`).
3. **Ne PAS** relancer TSan/ASan sur Linux en espérant mieux, et **ne pas** repartir sur les tas
   séparés : les deux sont mesurés et clos.

**Coût/bénéfice** : le taux de 4 % rend chaque itération longue (~75 exécutions par vérification).
Décider explicitement si cette chasse vaut une session dédiée, ou si le test doit être marqué connu
instable en attendant, plutôt que de la poursuivre par petites touches.

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
