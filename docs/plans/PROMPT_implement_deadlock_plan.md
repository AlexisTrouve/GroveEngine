# Prompt : Implémentation du Plan Deadlock Detection & Prevention

**Pour** : Claude Code (successeur) ou développeur qui reprend ce plan
**Plan associé** : [PLAN_deadlock_detection_prevention.md](./PLAN_deadlock_detection_prevention.md)
**Durée estimée** : 15h sur 2 semaines
**Dernière mise à jour** : 2025-01-21

---

## 🎯 Mission

Tu dois implémenter un système complet de détection et prévention des deadlocks pour GroveEngine, un moteur de jeu avec hot-reload de modules dynamiques en C++17.

**Contexte** :
- GroveEngine utilise actuellement `std::mutex` avec `std::lock_guard`
- Pattern : Single mutex par classe (risque faible mais amélioration nécessaire)
- Un deadlock a été identifié dans `test_13_cross_system` (teste encore bloqué)
- Le projet compile sur Linux/WSL2 avec CMake

**Objectif final** :
1. ✅ Détection automatique des deadlocks (TSan + Helgrind)
2. ✅ Prévention des deadlocks multi-mutex (scoped_lock)
3. ✅ Optimisation concurrence read-heavy (shared_mutex)
4. ✅ Tous les tests passent sans deadlock

---

## 📚 Documentation Requise

**IMPORTANT** : Lis ces documents avant de commencer :

1. **Plan complet** : `docs/plans/PLAN_deadlock_detection_prevention.md`
   - Contient TOUTES les modifications ligne par ligne
   - Exemples de code AVANT/APRÈS
   - Commandes exactes à exécuter

2. **Tests d'intégration** : `docs/plans/PLAN_integration_tests_global.md`
   - Liste des 13 tests à valider
   - Seuils de succès

3. **Architecture** : `docs/architecture/`
   - Comprendre IntraIO, IntraIOManager, TopicTree
   - Flux de messages pub/sub

---

## 🗓️ Calendrier d'Implémentation

### Semaine 1 : Détection (5h)

#### Jour 1-2 : ThreadSanitizer (2h)

**Checklist** :
- [ ] Modifier `CMakeLists.txt` (copier le code du plan lignes 21-35)
- [ ] Build avec TSan : `cmake -DGROVE_ENABLE_TSAN=ON -B build-tsan`
- [ ] Exécuter tous les tests : `cd build-tsan && TSAN_OPTIONS="detect_deadlocks=1" ctest -V`
- [ ] Documenter les warnings TSan trouvés (créer `docs/tsan_findings.md`)
- [ ] Fixer tous les lock-order-inversions détectés
- [ ] Re-run tests jusqu'à 0 warning

**Commandes** :
```bash
# 1. Modifier CMakeLists.txt (copier du plan)
nano CMakeLists.txt

# 2. Build
cmake -DGROVE_ENABLE_TSAN=ON -B build-tsan
cmake --build build-tsan -j$(nproc)

# 3. Test
cd build-tsan
TSAN_OPTIONS="detect_deadlocks=1 history_size=7 exitcode=1" ctest --output-on-failure

# 4. Si erreurs, voir logs
cat Testing/Temporary/LastTest.log
```

**Critères de succès** :
- ✅ Build TSan compile sans erreurs
- ✅ `ctest` retourne exit code 0
- ✅ Aucun warning "lock-order-inversion"
- ✅ Aucun warning "data race"

---

#### Jour 3-4 : Helgrind (3h)

**Checklist** :
- [ ] Modifier `CMakeLists.txt` (copier code du plan lignes 84-126)
- [ ] Créer `helgrind.supp` (copier du plan lignes 128-158)
- [ ] Build : `cmake -DGROVE_ENABLE_HELGRIND=ON -B build`
- [ ] Exécuter : `cd build && make helgrind`
- [ ] Analyser `helgrind-full.log`
- [ ] Ajouter suppressions pour false positives
- [ ] Re-run jusqu'à log propre
- [ ] Créer tableau comparatif TSan vs Helgrind

**Commandes** :
```bash
# 1. Créer helgrind.supp (copier du plan)
nano helgrind.supp

# 2. Modifier CMakeLists.txt
nano CMakeLists.txt

# 3. Build
cmake -DGROVE_ENABLE_HELGRIND=ON -B build
cmake --build build

# 4. Run Helgrind (LENT - 10-50x slowdown)
cd build
make helgrind

# 5. Analyser résultats
cat helgrind-full.log | grep -E "(Possible|lock order)" | less

# 6. Compter problèmes
cat helgrind-full.log | grep "Possible data race" | wc -l
```

**Critères de succès** :
- ✅ Target `make helgrind` fonctionne
- ✅ Fichier helgrind.supp avec suppressions
- ✅ 0 lock order violations (après suppressions légitimes)
- ✅ Tableau comparatif TSan vs Helgrind documenté

---

### Semaine 2 : Prévention & Optimisation (10h)

#### Jour 5-7 : std::scoped_lock (4h)

**Checklist** :
- [ ] Rechercher tous les lock_guard multi-mutex : `grep -n "lock_guard" src/*.cpp | sort`
- [ ] Identifier les patterns : lock(mutex1) puis lock(mutex2) dans même fonction
- [ ] Refactorer `IntraIOManager.cpp` lignes 176, 221, 256, 272, 329
- [ ] Remplacer par `std::scoped_lock(mutex1, mutex2)`
- [ ] Créer test unitaire `tests/unit/test_scoped_lock.cpp` (copier du plan)
- [ ] Build et tester : `cmake --build build && ctest`
- [ ] Valider avec TSan : aucun lock-order-inversion

**Exemple de refactoring** :

**AVANT** (src/IntraIOManager.cpp:176) :
```cpp
void IntraIOManager::routeMessage(...) {
    std::lock_guard<std::mutex> lock(managerMutex);
    // ... code ...
    std::lock_guard<std::mutex> batchLock(batchMutex);  // ❌ Risque deadlock
}
```

**APRÈS** :
```cpp
void IntraIOManager::routeMessage(...) {
    std::scoped_lock lock(managerMutex, batchMutex);  // ✅ Safe
    // ... code ...
}
```

**Fichiers à modifier** :
1. `src/IntraIOManager.cpp` (5 endroits - voir plan lignes 213-234)
2. `tests/unit/test_scoped_lock.cpp` (créer nouveau fichier - code dans plan lignes 236-283)
3. `docs/coding_guidelines.md` (créer section - plan lignes 287-324)

**Commandes** :
```bash
# 1. Trouver tous les multi-mutex locks
grep -B3 -A3 "lock_guard" src/IntraIOManager.cpp

# 2. Modifier les fichiers (utilise ton éditeur)
nano src/IntraIOManager.cpp

# 3. Build et test
cmake --build build
ctest --output-on-failure

# 4. Validation TSan
cd build-tsan
cmake --build .
TSAN_OPTIONS="detect_deadlocks=1" ctest -V
```

**Critères de succès** :
- ✅ Tous les lock_guard multi-mutex remplacés
- ✅ Test `test_scoped_lock` passe
- ✅ TSan validation : 0 lock-order-inversion
- ✅ Documentation coding_guidelines.md créée

---

#### Jour 8-10 : std::shared_mutex (6h)

**Checklist - TopicTree** :
- [ ] Modifier `external/StillHammer/topictree/include/topictree/TopicTree.h` ligne 56
- [ ] Changer `std::mutex` → `std::shared_mutex`
- [ ] Refactorer toutes les méthodes (voir plan lignes 406-478)
  - `findSubscribers()` → `std::shared_lock` (READ)
  - `registerSubscriber()` → `std::unique_lock` (WRITE)
  - `unregisterSubscriber()` → `std::unique_lock` (WRITE)
  - `clear()` → `std::unique_lock` (WRITE)
  - `subscriberCount()` → `std::shared_lock` (READ)

**Checklist - IntraIOManager** :
- [ ] Modifier `include/grove/IntraIOManager.h`
- [ ] Split `managerMutex` en 2 : `instancesMutex` (shared) + `statsMutex` (exclusive)
- [ ] Refactorer `src/IntraIOManager.cpp` (voir plan lignes 480-594)
  - `getInstance()` → `std::shared_lock` (READ)
  - `routeMessage()` → `std::shared_lock` pour lookup (CRITICAL!)
  - `createInstance()` → `std::unique_lock` (WRITE)
  - `removeInstance()` → `std::unique_lock` (WRITE)

**Checklist - JsonDataTree (optionnel)** :
- [ ] Modifier `include/grove/JsonDataTree.h` et `src/JsonDataTree.cpp`
- [ ] Changer `std::mutex` → `std::shared_mutex`
- [ ] Refactorer méthodes (voir plan lignes 596-637)

**Checklist - Benchmark** :
- [ ] Créer `tests/benchmarks/benchmark_shared_mutex.cpp` (plan lignes 641-688)
- [ ] Ajouter au CMakeLists.txt (plan lignes 690-698)
- [ ] Build : `cmake --build build`
- [ ] Run : `./build/tests/benchmark_shared_mutex --benchmark_min_time=3s`
- [ ] Créer rapport `docs/performance_reports/shared_mutex_results.md` (plan lignes 718-772)

**Commandes** :
```bash
# 1. Modifier TopicTree.h
nano external/StillHammer/topictree/include/topictree/TopicTree.h

# 2. Modifier IntraIOManager
nano include/grove/IntraIOManager.h
nano src/IntraIOManager.cpp

# 3. Modifier JsonDataTree (optionnel)
nano include/grove/JsonDataTree.h
nano src/JsonDataTree.cpp

# 4. Créer benchmark
mkdir -p tests/benchmarks
nano tests/benchmarks/benchmark_shared_mutex.cpp

# 5. Modifier CMakeLists.txt pour benchmark
nano tests/CMakeLists.txt

# 6. Build
cmake --build build -j$(nproc)

# 7. Run tests
ctest --output-on-failure

# 8. Validation TSan (CRITIQUE!)
cd build-tsan
cmake --build .
TSAN_OPTIONS="detect_deadlocks=1" ctest -V

# 9. Run benchmark
cd ../build
./tests/benchmark_shared_mutex --benchmark_min_time=3s

# 10. Créer rapport de performance
mkdir -p docs/performance_reports
nano docs/performance_reports/shared_mutex_results.md
```

**Résultats attendus du benchmark** :
```
1 thread:  Overhead négligeable (~1%)
4 threads: Speedup 30-50x (mutex: 960ns → shared: 22ns)
8 threads: Speedup 150-250x (mutex: 7680ns → shared: 30ns)
```

**Critères de succès** :
- ✅ TopicTree utilise `shared_mutex`
- ✅ IntraIOManager split mutex (instances shared + stats exclusive)
- ✅ JsonDataTree utilise `shared_mutex` (optionnel)
- ✅ Benchmark créé et exécuté
- ✅ Rapport de performance rempli (gain >50% démontré)
- ✅ TSan validation : 0 data race
- ✅ Tous les tests d'intégration passent

---

## 🔧 Outils et Commandes Essentielles

### Build Variants

```bash
# Normal build
cmake -B build && cmake --build build

# TSan build (détection deadlock)
cmake -DGROVE_ENABLE_TSAN=ON -B build-tsan
cmake --build build-tsan

# Helgrind build
cmake -DGROVE_ENABLE_HELGRIND=ON -B build
cmake --build build
cd build && make helgrind
```

### Tests

```bash
# Tous les tests
ctest --output-on-failure

# Test spécifique
./tests/test_13_cross_system

# Avec TSan
TSAN_OPTIONS="detect_deadlocks=1 history_size=7" ./tests/test_13_cross_system

# Avec timeout (si deadlock)
timeout 30 ./tests/test_13_cross_system || echo "DEADLOCK DETECTED"
```

### Debugging

```bash
# Voir les mutex actifs
ps aux | grep test_13
pstack <PID>  # Si disponible

# Logs TSan détaillés
TSAN_OPTIONS="detect_deadlocks=1 verbosity=2 log_path=tsan.log" ./test_13

# Analyser Helgrind
cat helgrind-full.log | grep -A10 "lock order"
```

---

## ⚠️ Pièges à Éviter

### 1. Test 13 Deadlock (ligne 333)

**Problème identifié** :
```cpp
// test_13_cross_system.cpp:331-333
auto dataRootPtr = tree->getDataRootReadOnly();  // ❌ Raw pointer
dataRootPtr->setChild("player", ...);  // ❌ Modification via read-only!
```

**Fix** :
```cpp
auto dataRoot = tree->getDataRoot();  // ✅ Writable unique_ptr
dataRoot->setChild("player", std::move(player5));
```

### 2. TSan False Positives

Si TSan rapporte des races sur :
- `spdlog` : Ajouter à suppressions (lazy init benign)
- `std::atomic` : Normal, TSan ne comprend pas toujours
- Thread creation/join : Benign

**Ne supprime PAS** :
- Lock-order-inversion : VRAI problème!
- Data race sur variables non-atomic : VRAI problème!

### 3. Helgrind Lenteur

- 10-50x slowdown est NORMAL
- Test complet peut prendre 1-2h
- Utilise `make helgrind-single` pour tester un seul test
- Patience!

### 4. shared_mutex Performance

- **Vérifie le ratio Read/Write** : doit être >10:1
- Si writes fréquents (>30%), shared_mutex est PLUS LENT
- TopicTree : OK (100:1 ratio)
- IntraIOManager : OK (50:1 ratio)
- Stats counters : NON (1:1 ratio) → garde std::mutex

---

## 📋 Checklist Finale de Validation

Avant de considérer le plan terminé, vérifie :

### Phase 1 : Détection
- [ ] CMakeLists.txt avec options GROVE_ENABLE_TSAN et GROVE_ENABLE_HELGRIND
- [ ] Build TSan compile et tests passent (exit code 0)
- [ ] Build Helgrind targets fonctionnent (`make helgrind`)
- [ ] Fichier helgrind.supp avec suppressions appropriées
- [ ] 0 lock-order-inversion dans TSan output
- [ ] 0 data race dans TSan output
- [ ] Documentation TSan vs Helgrind (tableau comparatif)

### Phase 2 : Prévention
- [ ] Tous les lock_guard multi-mutex → scoped_lock
- [ ] Test unitaire test_scoped_lock.cpp passe
- [ ] Documentation coding_guidelines.md mise à jour
- [ ] TSan validation : 0 warning

### Phase 3 : Optimisation
- [ ] TopicTree.h utilise std::shared_mutex
- [ ] IntraIOManager split instancesMutex (shared) + statsMutex (exclusive)
- [ ] JsonDataTree utilise std::shared_mutex (si applicable)
- [ ] Benchmark benchmark_shared_mutex créé et exécuté
- [ ] Rapport de performance avec gains >50% documenté
- [ ] TSan validation : 0 data race
- [ ] Tous les 13 tests d'intégration passent

### Tests Critiques
- [ ] test_01_production_hotreload : PASS
- [ ] test_02_chaos_monkey : PASS
- [ ] test_03_stress_test : PASS (10 min)
- [ ] test_04_race_condition : PASS
- [ ] test_13_cross_system : PASS (fix deadlock ligne 333)

### Documentation
- [ ] docs/tsan_findings.md créé
- [ ] docs/coding_guidelines.md mis à jour
- [ ] docs/performance_reports/shared_mutex_results.md créé
- [ ] README.md des plans mis à jour (statut 🟢)

---

## 🎯 Critères de Succès Final

Le plan est **TERMINÉ** quand :

1. ✅ **Build TSan** : `cmake -DGROVE_ENABLE_TSAN=ON -B build-tsan && cd build-tsan && TSAN_OPTIONS="detect_deadlocks=1" ctest`
   - Exit code: 0
   - Output: "100% tests passed, 0 tests failed"
   - Aucun warning TSan

2. ✅ **Build Helgrind** : `cd build && make helgrind`
   - Exit code: 0
   - helgrind-full.log propre (après suppressions)

3. ✅ **Tests d'intégration** : `ctest --output-on-failure`
   - 13/13 tests passent
   - Dont test_13_cross_system (fix deadlock)

4. ✅ **Benchmark** : `./tests/benchmark_shared_mutex`
   - Speedup >50% démontré (4 threads)
   - Rapport de performance documenté

5. ✅ **Code Review** :
   - Aucun lock_guard multi-mutex restant
   - Tous les read-heavy locks sont shared_lock
   - Tous les write locks sont unique_lock

---

## 📞 En Cas de Problème

### Test 13 deadlock toujours présent

```bash
# 1. Identifier où ça bloque
timeout 10 ./tests/test_13_cross_system
ps aux | grep test_13
pstack <PID>  # Voir les stacks

# 2. Vérifier le fix ligne 333
grep -n "getDataRootReadOnly" tests/integration/test_13_cross_system.cpp
# Doit utiliser getDataRoot() au lieu de getDataRootReadOnly()

# 3. Rebuild et retest
cmake --build build
./tests/test_13_cross_system
```

### TSan trouve encore des lock-order-inversions

```bash
# 1. Voir exactement où
TSAN_OPTIONS="detect_deadlocks=1 history_size=7 second_deadlock_stack=1" ./problematic_test

# 2. Identifier les mutexes (M1, M2)
# 3. Utiliser scoped_lock(M1, M2) partout
# 4. Rebuild et retest
```

### Helgrind timeout

```bash
# 1. Run un seul test
make helgrind-single

# 2. Si trop lent, skip Helgrind
# TSan est suffisant pour validation
```

### Benchmark ne montre pas de gain

```bash
# 1. Vérifier le ratio read/write
# Si writes > 30%, shared_mutex est inutile

# 2. Vérifier que tu as bien utilisé shared_lock pour les reads
grep "shared_lock" src/IntraIOManager.cpp

# 3. Run benchmark avec plus de threads
./benchmark_shared_mutex --benchmark_filter=.*MultiThread.*/threads:8
```

---

## 📝 Rapport Final à Produire

Quand tout est terminé, crée : `docs/plans/REPORT_deadlock_plan_completed.md`

**Template** :
```markdown
# Rapport : Plan Deadlock Detection & Prevention - TERMINÉ

**Date de complétion** : YYYY-MM-DD
**Durée réelle** : Xh (estimé: 15h)
**Statut** : ✅ TERMINÉ

## Résumé

[1-2 paragraphes résumant ce qui a été fait]

## Phase 1 : Détection (5h)

- ✅ ThreadSanitizer intégré (2h réelles)
  - Warnings TSan trouvés : X
  - Fixes appliqués : Y
- ✅ Helgrind intégré (3h réelles)
  - Suppressions ajoutées : Z

## Phase 2 : Prévention (4h)

- ✅ scoped_lock refactoring (4h réelles)
  - Fichiers modifiés : X
  - Lock_guard → scoped_lock : Y endroits

## Phase 3 : Optimisation (6h)

- ✅ shared_mutex implémenté (6h réelles)
  - TopicTree : ✅
  - IntraIOManager : ✅
  - JsonDataTree : ✅/❌
  - Speedup mesuré : Xx (4 threads), Yy (8 threads)

## Tests Validation

| Test | Avant | Après | Statut |
|------|-------|-------|--------|
| test_01 | ✅ | ✅ | OK |
| test_02 | ✅ | ✅ | OK |
| ... | ... | ... | ... |
| test_13 | ❌ Deadlock | ✅ | FIXED |

## Benchmark Results

[Copier les résultats du benchmark]

## Problèmes Rencontrés

1. [Problème 1 et solution]
2. [Problème 2 et solution]

## Leçons Apprises

[Ce qui a été appris pendant l'implémentation]

## Recommandations Futures

1. [Amélioration 1]
2. [Amélioration 2]
```

---

## 🚀 Let's Go!

Tu as maintenant **TOUT** ce qu'il faut :
- ✅ Plan détaillé (30KB de specs)
- ✅ Code AVANT/APRÈS ligne par ligne
- ✅ Commandes exactes
- ✅ Tests de validation
- ✅ Debugging tips
- ✅ Checklist complète

**Commence par la Phase 1.1 (ThreadSanitizer - 2h)** :
```bash
# 1. Ouvre le plan
cat docs/plans/PLAN_deadlock_detection_prevention.md

# 2. Copie le code CMake (lignes 21-35)
nano CMakeLists.txt

# 3. Build
cmake -DGROVE_ENABLE_TSAN=ON -B build-tsan
cmake --build build-tsan

# 4. Test
cd build-tsan
TSAN_OPTIONS="detect_deadlocks=1" ctest -V

# 5. Fix tous les warnings
# 6. Re-test jusqu'à 0 warning
```

**Bonne chance ! 🎯**

---

**Auteur** : Claude Code
**Version** : 1.0
**Pour** : Successeur/Implémenteur du plan deadlock
**Date** : 2025-01-21
