# Scénario 9: Module Dependencies

**Priorité**: ⭐ CRITICAL
**Phase**: 2 (CRITICAL)
**Durée estimée**: ~2 minutes
**Effort implémentation**: ~4-5 heures

---

## 🎯 Objectif

Valider que le système peut gérer des modules avec dépendances explicites, où un module A dépend d'un module B, avec:
- Cascade reload automatique (reload de B → force reload de A)
- Ordre de reload correct (B avant A)
- Protection contre déchargement si dépendance active
- Détection de cycles de dépendances
- Préservation des données partagées entre modules

---

## 📋 Description

### Architecture des Modules de Test

**BaseModule**: Module de base sans dépendances
- Expose des services/données (ex: génère des nombres aléatoires)
- Trackable pour valider les reloads
- Version incrémentale

**DependentModule**: Module qui dépend de BaseModule
- Déclare explicitement BaseModule comme dépendance
- Utilise les services de BaseModule
- Doit être rechargé si BaseModule est rechargé

**IndependentModule**: Module sans dépendances
- Sert de témoin (ne devrait jamais être affecté)
- Permet de valider que seules les bonnes dépendances sont reload

### Setup Initial
1. Charger **BaseModule** (version 1)
   - Expose service: `generateNumber()` → retourne 42
   - Pas de dépendances
2. Charger **DependentModule** (version 1)
   - Dépend de: `["BaseModule"]`
   - Utilise `BaseModule.generateNumber()`
   - Accumule les résultats
3. Charger **IndependentModule** (version 1)
   - Pas de dépendances
   - Témoin indépendant
4. Vérifier que tous les modules sont chargés et fonctionnent

### Phase 1: Cascade Reload (BaseModule → DependentModule)
**Durée**: 30 secondes

1. À t=10s, modifier et reloader **BaseModule** (version 2)
   - `generateNumber()` → retourne maintenant 100
2. Vérifier cascade reload:
   - ✅ BaseModule rechargé automatiquement
   - ✅ DependentModule **forcé** à reloader (cascade)
   - ✅ IndependentModule **non** rechargé (isolé)
3. Vérifier ordre de reload:
   - BaseModule reload **avant** DependentModule
   - État cohérent (pas de références cassées)
4. Vérifier continuité:
   - State de DependentModule préservé
   - Nouvelles valeurs de BaseModule utilisées (100 au lieu de 42)

**Métriques**:
- Cascade reload time: temps total pour recharger B + A
- Dependency resolution time: temps pour identifier dépendants

### Phase 2: Protection contre Déchargement
**Durée**: 10 secondes

1. À t=40s, tenter de **décharger BaseModule**
2. Vérifier rejet:
   - ❌ Déchargement **refusé** (DependentModule en dépend)
   - ⚠️ Erreur claire: "Cannot unload BaseModule: required by DependentModule"
   - ✅ BaseModule reste chargé et fonctionnel
3. Vérifier stabilité:
   - Tous les modules continuent de fonctionner
   - Aucune corruption de state

### Phase 3: Reload Module Dépendant (sans cascade inverse)
**Durée**: 20 secondes

1. À t=50s, modifier et reloader **DependentModule** (version 2)
   - Change comportement interne
   - Garde même dépendance sur BaseModule
2. Vérifier isolation:
   - ✅ DependentModule rechargé
   - ✅ BaseModule **non** rechargé (pas de cascade inverse)
   - ✅ IndependentModule toujours isolé
3. Vérifier connexion:
   - DependentModule peut toujours utiliser BaseModule
   - Pas de références cassées

### Phase 4: Détection de Cycle de Dépendances
**Durée**: 20 secondes

1. À t=70s, créer module **CyclicModule** avec dépendance circulaire:
   - CyclicModuleA dépend de CyclicModuleB
   - CyclicModuleB dépend de CyclicModuleA
2. Tenter de charger les deux modules
3. Vérifier détection:
   - ❌ Chargement **refusé**
   - ⚠️ Erreur claire: "Cyclic dependency detected: A → B → A"
   - ✅ Aucun module partiellement chargé (transactional)
4. Vérifier stabilité:
   - Modules existants non affectés
   - Système reste stable

### Phase 5: Déchargement en Cascade
**Durée**: 20 secondes

1. À t=90s, décharger **DependentModule** (libère dépendance)
2. Vérifier libération:
   - ✅ DependentModule déchargé
   - ✅ BaseModule toujours chargé (encore utilisable)
3. À t=100s, décharger **BaseModule** (maintenant possible)
4. Vérifier succès:
   - ✅ BaseModule déchargé (plus de dépendants)
   - ✅ IndependentModule toujours actif (isolé)

---

## 🏗️ Implémentation

### Extension de IModule.h

```cpp
// include/grove/IModule.h
class IModule {
public:
    // Méthodes existantes
    virtual void initialize(std::shared_ptr<IDataNode> config) = 0;
    virtual void process(float deltaTime) = 0;
    virtual std::shared_ptr<IDataNode> getState() const = 0;
    virtual void setState(std::shared_ptr<IDataNode> state) = 0;
    virtual bool isIdle() const = 0;

    // NOUVELLE: Config hot-reload (Scenario 8)
    virtual bool updateConfig(std::shared_ptr<IDataNode> newConfig) {
        return false;
    }

    // NOUVELLE: Dépendances (Scenario 9)
    virtual std::vector<std::string> getDependencies() const {
        return {};  // Par défaut: aucune dépendance
    }

    // NOUVELLE: Version du module (pour tracking)
    virtual int getVersion() const {
        return 1;  // Par défaut: version 1
    }

    virtual ~IModule() = default;
};
```

### BaseModule Structure

```cpp
// tests/modules/BaseModule.h
#pragma once
#include "grove/IModule.h"
#include <memory>
#include <atomic>

class BaseModule : public IModule {
public:
    void initialize(std::shared_ptr<IDataNode> config) override;
    void process(float deltaTime) override;
    std::shared_ptr<IDataNode> getState() const override;
    void setState(std::shared_ptr<IDataNode> state) override;
    bool isIdle() const override { return true; }

    std::vector<std::string> getDependencies() const override {
        return {};  // Pas de dépendances
    }

    int getVersion() const override { return version_; }

    // Service exposé aux autres modules
    int generateNumber() const;

private:
    int version_ = 1;
    std::atomic<int> processCount_{0};
    int generatedValue_ = 42;  // V1: 42, V2: 100
};

// Module factory
extern "C" IModule* createModule() {
    return new BaseModule();
}

extern "C" void destroyModule(IModule* module) {
    delete module;
}
```

### DependentModule Structure

```cpp
// tests/modules/DependentModule.h
#pragma once
#include "grove/IModule.h"
#include "BaseModule.h"
#include <memory>
#include <vector>

class DependentModule : public IModule {
public:
    void initialize(std::shared_ptr<IDataNode> config) override;
    void process(float deltaTime) override;
    std::shared_ptr<IDataNode> getState() const override;
    void setState(std::shared_ptr<IDataNode> state) override;
    bool isIdle() const override { return true; }

    std::vector<std::string> getDependencies() const override {
        return {"BaseModule"};  // Dépend de BaseModule
    }

    int getVersion() const override { return version_; }

    // Setter pour injecter la référence à BaseModule
    void setBaseModule(BaseModule* baseModule) {
        baseModule_ = baseModule;
    }

private:
    int version_ = 1;
    BaseModule* baseModule_ = nullptr;
    std::vector<int> collectedNumbers_;  // Accumule les valeurs de BaseModule
};

extern "C" IModule* createModule() {
    return new DependentModule();
}

extern "C" void destroyModule(IModule* module) {
    delete module;
}
```

### IndependentModule Structure

```cpp
// tests/modules/IndependentModule.h
#pragma once
#include "grove/IModule.h"
#include <memory>
#include <atomic>

class IndependentModule : public IModule {
public:
    void initialize(std::shared_ptr<IDataNode> config) override;
    void process(float deltaTime) override;
    std::shared_ptr<IDataNode> getState() const override;
    void setState(std::shared_ptr<IDataNode> state) override;
    bool isIdle() const override { return true; }

    std::vector<std::string> getDependencies() const override {
        return {};  // Témoin: aucune dépendance
    }

    int getVersion() const override { return version_; }

private:
    int version_ = 1;
    std::atomic<int> processCount_{0};
};

extern "C" IModule* createModule() {
    return new IndependentModule();
}

extern "C" void destroyModule(IModule* module) {
    delete module;
}
```

### ModuleLoader Extensions

Le `ModuleLoader` doit être étendu pour:

1. **Dependency Resolution**:
   ```cpp
   // Lors du chargement
   std::vector<std::string> resolveDependencies(const std::string& moduleName);
   bool checkCyclicDependencies(const std::string& moduleName, std::set<std::string>& visited);
   ```

2. **Cascade Reload**:
   ```cpp
   // Lors du reload
   std::vector<std::string> findDependents(const std::string& moduleName);
   void reloadWithDependents(const std::string& moduleName);
   ```

3. **Unload Protection**:
   ```cpp
   // Lors du déchargement
   bool canUnload(const std::string& moduleName, std::string& errorMsg);
   ```

### Dependency Graph

```
Phase 1-5:
┌─────────────────┐
│ IndependentModule│  (isolated, never reloaded)
└─────────────────┘

┌──────────────┐
│  BaseModule  │  (no dependencies)
└──────┬───────┘
       │
       │ depends on
       ▼
┌──────────────────┐
│ DependentModule  │  (depends on BaseModule)
└──────────────────┘

Cascade reload:
  BaseModule reload → DependentModule reload (cascade)
  DependentModule reload → BaseModule NOT reloaded (no reverse cascade)

Phase 4 (rejected):
┌────────────────┐        ┌────────────────┐
│ CyclicModuleA  │───────▶│ CyclicModuleB  │
└────────────────┘        └────────┬───────┘
       ▲                           │
       └───────────────────────────┘
                CYCLE! → REJECTED
```

---

## 📊 Métriques Collectées

| Métrique | Description | Seuil |
|----------|-------------|-------|
| **cascade_reload_time_ms** | Temps pour reload B + tous dépendants | < 200ms |
| **dependency_resolution_time_ms** | Temps pour identifier dépendants | < 10ms |
| **cycle_detection_time_ms** | Temps pour détecter cycle | < 50ms |
| **memory_growth_mb** | Croissance mémoire totale | < 5MB |
| **reload_count** | Nombre total de reloads | 3 (Base v2, Dep cascade, Dep v2) |

---

## ✅ Critères de Succès

### MUST PASS
1. ✅ Cascade reload: BaseModule reload → DependentModule reload
2. ✅ Ordre correct: BaseModule avant DependentModule
3. ✅ Isolation: IndependentModule jamais reload
4. ✅ Unload protection: BaseModule non déchargeable si DependentModule actif
5. ✅ Cycle detection: Dépendances circulaires détectées et rejetées
6. ✅ State preservation: State préservé après cascade reload
7. ✅ No reverse cascade: DependentModule reload ne force pas BaseModule reload
8. ✅ Aucun crash durant tout le test

### NICE TO HAVE
1. ✅ Cascade reload en < 100ms
2. ✅ Erreurs explicites avec noms de modules
3. ✅ Support de dépendances multiples (A dépend de B et C)
4. ✅ Visualisation du graphe de dépendances

---

## 🔧 API Nécessaires dans DebugEngine

### Nouvelles méthodes

```cpp
class DebugEngine {
public:
    // Méthodes existantes...

    // NOUVELLES pour Scenario 9
    bool canUnloadModule(const std::string& moduleName, std::string& errorMsg);
    std::vector<std::string> getModuleDependents(const std::string& moduleName);
    std::vector<std::string> getModuleDependencies(const std::string& moduleName);
    bool hasCircularDependencies(const std::string& moduleName);

    // Reload avec cascade
    bool reloadModuleWithDependents(const std::string& moduleName);
};
```

---

## 🐛 Cas d'Erreur Attendus

| Erreur | Cause | Action |
|--------|-------|--------|
| DependentModule non rechargé après BaseModule reload | Cascade pas implémentée | FAIL - implémenter cascade |
| BaseModule rechargé après DependentModule reload | Reverse cascade incorrecte | FAIL - vérifier direction |
| IndependentModule rechargé | Isolation cassée | FAIL - fix isolation |
| BaseModule déchargé alors que DependentModule actif | Protection pas implémentée | FAIL - ajouter check |
| Cycle non détecté | Algorithme DFS manquant | FAIL - implémenter cycle detection |
| Ordre reload incorrect (A avant B) | Topological sort manquant | FAIL - trier deps |
| State corrompu après cascade | Mauvaise gestion | FAIL - préserver state |

---

## 📝 Output Attendu

```
================================================================================
TEST: Module Dependencies
================================================================================

=== Setup: Load modules with dependencies ===
✓ BaseModule loaded (v1, no dependencies)
✓ DependentModule loaded (v1, depends on: BaseModule)
✓ IndependentModule loaded (v1, no dependencies)

Dependency graph:
  IndependentModule → (none)
  BaseModule → (none)
  DependentModule → BaseModule

=== Phase 1: Cascade Reload (30s) ===
Reloading BaseModule...
  → BaseModule reload triggered
  → Cascade reload triggered for DependentModule
✓ BaseModule reloaded: v1 → v2
✓ DependentModule cascade reloaded: v1 → v1 (state preserved)
✓ IndependentModule NOT reloaded (isolated)
✓ generateNumber() changed: 42 → 100
✓ DependentModule using new value: 100

Metrics:
  Cascade reload time:        85ms  ✓
  Dependency resolution time:  4ms  ✓

=== Phase 2: Unload Protection (10s) ===
Attempting to unload BaseModule...
  ✗ Unload rejected: Cannot unload BaseModule: required by DependentModule
✓ BaseModule still loaded and functional
✓ All modules stable

=== Phase 3: Reload Dependent Only (20s) ===
Reloading DependentModule...
✓ DependentModule reloaded: v1 → v2
✓ BaseModule NOT reloaded (no reverse cascade)
✓ IndependentModule still isolated
✓ DependentModule still connected to BaseModule

=== Phase 4: Cyclic Dependency Detection (20s) ===
Attempting to load CyclicModuleA and CyclicModuleB...
  ✗ Load rejected: Cyclic dependency detected: CyclicModuleA → CyclicModuleB → CyclicModuleA
✓ No modules partially loaded
✓ Existing modules unaffected

Metrics:
  Cycle detection time:       12ms  ✓

=== Phase 5: Cascade Unload (20s) ===
Unloading DependentModule...
✓ DependentModule unloaded (dependency released)
✓ BaseModule still loaded

Attempting to unload BaseModule...
✓ BaseModule unload succeeded (no dependents)

Final state:
  IndependentModule: loaded (v1)
  BaseModule: unloaded
  DependentModule: unloaded

================================================================================
METRICS
================================================================================
  Cascade reload time:      85ms         (threshold: < 200ms)  ✓
  Dep resolution time:       4ms         (threshold: < 10ms)   ✓
  Cycle detection time:     12ms         (threshold: < 50ms)   ✓
  Memory growth:          2.1MB         (threshold: < 5MB)    ✓
  Total reloads:              3         (expected: 3)         ✓

================================================================================
ASSERTIONS
================================================================================
  ✓ modules_loaded
  ✓ cascade_reload_triggered
  ✓ reload_order_correct
  ✓ independent_isolated
  ✓ unload_protection_works
  ✓ no_reverse_cascade
  ✓ cycle_detected
  ✓ cascade_unload_works
  ✓ state_preserved
  ✓ no_crashes

Result: ✅ PASSED

================================================================================
```

---

## 📅 Planning

**Jour 10 (3h):**
- Étendre IModule avec `getDependencies()` et `getVersion()`
- Implémenter BaseModule, DependentModule, IndependentModule
- Étendre ModuleLoader avec dependency resolution

**Jour 11 (2h):**
- Implémenter cascade reload dans ModuleLoader
- Implémenter cycle detection (DFS)
- Implémenter unload protection

**Jour 12 (1h):**
- Implémenter test_09_module_dependencies.cpp
- Debug + validation
- Intégration CMake

---

## 🎯 Valeur Ajoutée

Ce test valide une fonctionnalité **critique pour les systèmes complexes** :
- **Modularité avancée** : Permet de construire des architectures avec dépendances claires
- **Sécurité** : Empêche les déchargements dangereux qui casseraient des références
- **Maintenabilité** : Cascade reload automatique garantit cohérence
- **Robustesse** : Détection de cycles évite les deadlocks et états invalides

Dans un moteur de jeu réel, typiquement:
- **PhysicsModule** dépend de **MathModule**
- **RenderModule** dépend de **ResourceModule**
- **GameplayModule** dépend de **PhysicsModule** et **AudioModule**

Le hot-reload de `MathModule` doit automatiquement recharger `PhysicsModule` et tous ses dépendants (`GameplayModule`), dans l'ordre topologique correct.

---

**Prochaine étape**: Implémentation
