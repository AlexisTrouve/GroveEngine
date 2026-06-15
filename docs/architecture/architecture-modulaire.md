# Architecture Modulaire - Warfactory

## Concept Révolutionnaire

L'architecture modulaire Warfactory transforme le développement de jeux complexes en découpant le système par **sous-système**, une approche optimisée pour Claude Code. Un module = un SOUS-SYSTÈME important (ex. système d'IA, simulation RTS, système colonie, rendu) ; il peut être GROS et contenir plusieurs classes — c'est voulu. La granularité est par sous-système / responsabilité, PAS par nombre de lignes.

## Core Interface Architecture

### Architecture Fondamentale

```cpp
ICoordinationModule → Orchestrateur global système
IEngine            → Orchestration locale
IModuleSystem      → Stratégies d'exécution
IModule            → Logique métier pure
IIO                → Communication et transport
ITaskScheduler     → Délégation de tâches
```

### IEngine - Orchestration

**Responsabilité** : Coordination générale du système et évolution performance

```cpp
class IEngine {
public:
    virtual void initialize() = 0;
    virtual void update(float deltaTime) = 0;
    virtual void shutdown() = 0;
    virtual void setModuleSystem(std::unique_ptr<IModuleSystem>) = 0;
};
```

**Implémentations disponibles :**
- **DebugEngine** : Développement et test (step-by-step, verbose logging)
- **HighPerfEngine** : Production optimisée (threading, memory management)
- **DataOrientedEngine** : Scale massive (SIMD, cluster distribution)

### IModuleSystem - Stratégies d'Exécution

**Responsabilité** : Détermine comment et quand les modules s'exécutent

```cpp
class IModuleSystem {
public:
    virtual void registerModule(const std::string& name, std::unique_ptr<IModule>) = 0;
    virtual void processModules(float deltaTime) = 0;
    virtual void setIOLayer(std::unique_ptr<IIO>) = 0;
    virtual json queryModule(const std::string& name, const json& input) = 0;
};
```

**Stratégies d'exécution :**
- **SequentialModuleSystem** : Debug/test (1 module à la fois)
- **ThreadedModuleSystem** : Chaque module dans son thread
- **MultithreadedModuleSystem** : Pool de threads pour tasks
- **ClusterModuleSystem** : Distribution sur plusieurs machines

### IModule - Logique Métier Pure

**Responsabilité** : Logique de jeu spécialisée sans infrastructure

```cpp
class IModule {
public:
    virtual json process(const json& input) = 0;     // PURE FUNCTION
    virtual void initialize(const json& config) = 0; // Configuration
    virtual void shutdown() = 0;                     // Cleanup

    // Hot-reload support
    virtual json getState() = 0;                     // Save state
    virtual void setState(const json& state) = 0;    // Restore state
};
```

**Contraintes strictes :**
- **Un module = un sous-système** (granularité par responsabilité, pas par nombre de lignes) ; on découpe seulement quand un module mêle DEUX sous-systèmes distincts
- **Aucune dépendance infrastructure** (threading, network, etc.)
- **JSON in/out uniquement** pour communication
- **Logic métier pure** sans effets de bord

### IIO - Communication

**Responsabilité** : Abstraction transport entre modules

```cpp
class IIO {
public:
    virtual json send(const std::string& target, const json& message) = 0;
    virtual json receive(const std::string& source) = 0;
    virtual void broadcast(const json& message) = 0;
};
```

**Implémentations transport :**
- **IntraIO** : Appel direct (même processus)
- **LocalIO** : Named pipes/sockets (même machine)
- **NetworkIO** : TCP/WebSocket (réseau)

## Modules Spécialisés

### ProductionModule (Un Seul Sous-Système)

**Particularité** : Belt+Inserter+Factory forment UN sous-système de production unique — ils DOIVENT cohabiter dans le même module pour la coordination frame-perfect

```cpp
class ProductionModule : public IModule {
    // Un seul sous-système : Belt+Inserter+Factory restent ensemble
    // (granularité par responsabilité, pas par lignes — on ne découpe pas
    //  un sous-système cohérent)
    // Raison: ISocket overhead >1ms inacceptable pour 60 FPS

    Belt beltSystem;
    Inserter inserterSystem;
    Factory factorySystem;

public:
    json process(const json& input) override {
        // Frame-perfect coordination required
        auto beltData = beltSystem.update(input);
        auto inserterData = inserterSystem.update(beltData);
        auto factoryData = factorySystem.update(inserterData);

        return factoryData;
    }
};
```

### TankModule

```cpp
class TankModule : public IModule {
    // Targeting: 60Hz
    // Movement: 30Hz
    // Tactical: 1Hz
    // Analytics: 0.1Hz

public:
    json process(const json& input) override {
        auto context = getCurrentContext(input);

        if (shouldUpdateTargeting(context)) {
            return processTargeting(input);  // 60Hz
        }

        if (shouldUpdateMovement(context)) {
            return processMovement(input);   // 30Hz
        }

        if (shouldUpdateTactical(context)) {
            return processTactical(input);   // 1Hz
        }

        return processAnalytics(input);      // 0.1Hz
    }
};
```

### EconomyModule

```cpp
class EconomyModule : public IModule {
    // Economic cycles: 0.01-0.1Hz

public:
    json process(const json& input) override {
        auto marketData = input["market"];

        // Slow economic simulation
        auto priceUpdates = calculatePriceDiscovery(marketData);
        auto supplyDemand = updateSupplyDemand(marketData);
        auto transportOptim = optimizeTransportCosts(marketData);

        return {
            {"prices", priceUpdates},
            {"supply_demand", supplyDemand},
            {"transport", transportOptim}
        };
    }
};
```

### LogisticModule

```cpp
class LogisticModule : public IModule {
    // Variable frequency: 50ms → 1000ms

public:
    json process(const json& input) override {
        auto context = input["context"];

        if (context["urgent"]) {
            return processRealTimeTransport(input);  // 50ms
        }

        return processPlanning(input);               // 1000ms
    }
};
```

## Isolation et Communication

### Règles d'Isolation Strictes

**War Module Isolation :**
```cpp
// ✅ CORRECT - War assets via LogisticModule
LogisticModule → TurretSupply → Ammunition
LogisticModule → VehicleSupply → Fuel/Parts

// ❌ FORBIDDEN - Direct factory interaction
ProductionModule → TankModule  // ZERO interaction
FactoryInserter → Turret       // NO direct supply
```

**Supply Chain Architecture :**
```cpp
// ✅ CORRECT - Unidirectional flow
ProductionModule ↔ LogisticModule  // Export/Import only
LogisticModule ↔ WarModule        // Supply war assets

// ❌ FORBIDDEN - Any direct war interaction
ProductionModule ↔ TankModule     // ZERO interaction
ProductionModule ↔ TurretModule   // ZERO interaction
```

### Communication JSON

**Standard Message Format :**
```json
{
  "timestamp": 1234567890,
  "source": "TankModule",
  "target": "LogisticModule",
  "action": "request_supply",
  "data": {
    "item": "ammunition",
    "quantity": 100,
    "priority": "high"
  }
}
```

**Response Format :**
```json
{
  "timestamp": 1234567891,
  "source": "LogisticModule",
  "target": "TankModule",
  "status": "completed",
  "data": {
    "delivered": 100,
    "eta": "30s",
    "cost": 50.0
  }
}
```

## Hot-Reload Architecture

### State Preservation

```cpp
class TankModule : public IModule {
private:
    json persistentState;

public:
    json getState() override {
        return {
            {"position", currentPosition},
            {"health", currentHealth},
            {"ammunition", ammunitionCount},
            {"target", currentTarget}
        };
    }

    void setState(const json& state) override {
        currentPosition = state["position"];
        currentHealth = state["health"];
        ammunitionCount = state["ammunition"];
        currentTarget = state["target"];
    }
};
```

### Hot-Reload Workflow

```cpp
class ModuleLoader {
    void reloadModule(const std::string& modulePath) {
        // 1. Save state
        auto state = currentModule->getState();

        // 2. Unload old module
        unloadModule(modulePath);

        // 3. Load new module
        auto newModule = loadModule(modulePath);

        // 4. Restore state
        newModule->setState(state);

        // 5. Continue execution
        registerModule(newModule);
    }
};
```

## Claude Code Development

### Workflow Optimisé

```bash
# 1. Claude travaille dans contexte isolé
cd modules/tank/
# Context: CLAUDE.md (50 lignes) + TankModule.cpp (200 lignes) + IModule.h (30 lignes)
# Total: 280 lignes vs 50K+ dans architecture monolithique

# 2. Development cycle ultra-rapide
edit("src/TankModule.cpp")  # Modification logique pure
cmake . && make tank-module # Build autonome (5 secondes)
./build/tank-module        # Test standalone

# 3. Hot-reload dans jeu principal
# Engine détecte changement → Reload automatique → Game continue
```

### Parallel Development

```bash
# Instance Claude A - Tank Logic
cd modules/tank/
# Context: 200 lignes tank behavior

# Instance Claude B - Economy Logic
cd modules/economy/
# Context: 250 lignes market simulation

# Instance Claude C - Factory Logic
cd modules/factory/
# Context: 300 lignes production optimization

# Zero conflicts, parallel commits, modular architecture
```

## Évolution Progressive

### Phase 1 : Prototype (Debug)
```cpp
DebugEngine + SequentialModuleSystem + IntraIO
→ Développement ultra-rapide, Claude Code 100% focus logique
→ Step-by-step debugging, verbose logging
→ Validation concepts sans complexité infrastructure
```

### Phase 2 : Optimization (Threading)
```cpp
DebugEngine + ThreadedModuleSystem + IntraIO
→ Performance boost sans changer 1 ligne de game logic
→ Chaque module dans son thread dédié
→ Parallélisation automatique
```

### Phase 3 : Production (High Performance)
```cpp
HighPerfEngine + MultithreadedModuleSystem + LocalIO
→ Scale transparent, modules inchangés
→ Pool de threads optimisé
→ Communication inter-processus
```

### Phase 4 : Scale Massive (Distribution)
```cpp
DataOrientedEngine + ClusterModuleSystem + NetworkIO
→ Distribution multi-serveurs
→ SIMD optimization automatique
→ Claude Code développe toujours par sous-système (modules inchangés)
```

## Avantages Architecture

### Pour Claude Code
- **Contexte par sous-système** : un module isolé vs 50K+ lignes interconnectées
- **Focus logique** : Zéro infrastructure, pure game logic
- **Iteration speed** : 5 secondes vs 5-10 minutes
- **Parallel development** : 3+ instances simultanées
- **Hot-reload** : Feedback instantané

### Pour Performance
- **Modular scaling** : Chaque module à sa fréquence optimale
- **Resource allocation** : CPU budget précis par module
- **Evolution path** : Debug → Production sans réécriture
- **Network tolerance** : Latence adaptée par module type

### Pour Maintenance
- **Isolation complète** : Failures localisées
- **Testing granular** : Chaque module testable indépendamment
- **Code reuse** : Modules réutilisables entre projets
- **Documentation focused** : Chaque module auto-documenté

## Implementation Roadmap

### Étape 1 : Core Infrastructure
- Implémenter IEngine, IModuleSystem, IModule, IIO interfaces
- DebugEngine + SequentialModuleSystem + IntraIO
- Module loader avec hot-reload basique

### Étape 2 : Premier Module
- TankModule.cpp (200 lignes)
- Test standalone
- Intégration avec core

### Étape 3 : Modules Core
- EconomyModule, FactoryModule, LogisticModule
- Communication JSON entre modules
- State preservation

### Étape 4 : Performance
- ThreadedModuleSystem
- Optimisation hot-reload
- Métriques performance

Cette architecture révolutionnaire permet de développer des jeux AAA complexes avec Claude Code en découpant le système par sous-système (chaque module = un sous-système, aussi gros que sa responsabilité l'exige), tout en conservant la puissance architecturale nécessaire pour des systèmes distribués massifs.