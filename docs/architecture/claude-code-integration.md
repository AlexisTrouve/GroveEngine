# Intégration Claude Code : Guide Technique

## 🎯 Objectif : Développement IA-First

Cette architecture est spécifiquement conçue pour **maximiser l'efficacité de Claude Code** dans le développement de jeux complexes.

## 🧠 Contraintes Cognitives de l'IA

### Problème Fondamental : Context Window
- **Claude Code limite** : ~200K tokens de contexte
- **Jeu AAA typique** : 500K+ lignes de code interconnectées
- **Résultat** : IA ne peut pas appréhender le système complet

### Solution : Modules Autonomes par Sous-Système
```cpp
// Au lieu de ça (impossible pour l'IA) :
TankSystem.cpp (5000 lignes) +
PhysicsEngine.cpp (8000 lignes) +
NetworkLayer.cpp (3000 lignes) +
GraphicsRenderer.cpp (12000 lignes)
= 28000 lignes interconnectées

// On fait ça (parfait pour l'IA) :
TankModule.cpp (200 lignes)
= Logique pure, zéro dépendance
```

## 🏗️ Architecture Claude Code Friendly

### Structure Cognitive Optimale

```
warfactory/
├── modules/tank/              # 🎯 Claude travaille ICI
│   ├── CLAUDE.md              # Instructions spécialisées
│   ├── CMakeLists.txt         # Build autonome (cmake .)
│   ├── shared/                # Headers locaux
│   ├── src/TankModule.cpp     # 200 lignes PURE logic
│   └── build/                 # → tank.so
└── [reste du projet invisible pour Claude]
```

### Principe : Information Hiding Cognitif
- **Claude voit SEULEMENT** : TankModule.cpp + CLAUDE.md + interfaces
- **Claude ne voit JAMAIS** : Engine architecture, networking, threading
- **Résultat** : Focus 100% sur logique métier

## 📋 Workflow Claude Code Optimisé

### 1. Session Initialization
```bash
# Claude démarre TOUJOURS dans un module spécifique
cd modules/tank/

# Context loading minimal
files_to_read = [
    "CLAUDE.md",           # 50 lignes d'instructions
    "src/TankModule.cpp",  # 200 lignes de logic
    "shared/IModule.h"     # 30 lignes d'interface
]
# Total : 280 lignes vs 50K+ dans architecture classique
```

### 2. Development Loop
```bash
# 1. Claude lit le contexte micro
read("src/TankModule.cpp")

# 2. Claude modifie la logique pure
edit("src/TankModule.cpp")

# 3. Test instantané
cmake . && make tank-module
./build/tank-module

# 4. Hot-reload dans le jeu
# Aucune recompilation complète !
```

### 3. Parallel Development
**Point 8 - Développement Parallèle :** Multiple instances Claude Code simultanées sans conflits

```bash
# Instance Claude A
cd modules/tank/ && work_on("tank logic")

# Instance Claude B
cd modules/economy/ && work_on("market simulation")

# Instance Claude C
cd modules/ai/ && work_on("behavior trees")

# Zero conflicts, parallel development
```

**Architecture de non-conflit :**
- **Isolation complète** : Chaque module = contexte indépendant
- **Builds autonomes** : `cmake .` par module, zéro dépendance parent
- **État séparé** : Aucun fichier partagé entre modules
- **Git-friendly** : Commits isolés par module

## 🎯 Instructions CLAUDE.md Spécialisées
**Point 9 - CLAUDE.md Spécialisés :** Instructions contextuelles limitées par module

**Principe révolutionnaire :**
- **CLAUDE.md global** : Trop générique, 150+ lignes, inefficace
- **CLAUDE.md par module** : Ultra-spécialisé, 50 lignes max, efficacité maximale
- **Instructions contextuelles** : Tank ≠ Economy ≠ Factory ≠ War

### Template Type par Module

#### modules/tank/CLAUDE.md
```markdown
# Tank Module - Pure Combat Logic

## Context
You work EXCLUSIVELY on tank behavior. No networking, no threading.

## Responsibilities
- Movement: acceleration, turning, terrain interaction
- Combat: targeting, firing, armor calculations
- States: idle, moving, attacking, destroyed

## Interface Contract
Input JSON: {"type": "move", "direction": "north", "speed": 0.8}
Output JSON: {"position": [x, y], "facing": angle, "status": "moving"}

## Scope
- TankModule: one subsystem (tank behavior); size by responsibility, not line count
- Pure logic only: No sockets, threads, engine dependencies
- JSON in/out: All communication via JSON messages

## Build Commands
cmake . && make tank-module    # Builds tank.so
./build/tank-module           # Test standalone

NEVER leave this directory or reference parent paths!
```

#### modules/economy/CLAUDE.md
```markdown
# Economy Module - Pure Market Logic

## Context
You work EXCLUSIVELY on economic simulation. No infrastructure.

## Responsibilities
- Market dynamics: supply/demand, pricing
- Trading: buy/sell orders, market makers
- Economics: inflation, market cycles

## Interface Contract
Input: {"type": "trade", "item": "steel", "quantity": 100, "action": "buy"}
Output: {"status": "executed", "price": 5.2, "total": 520.0}

## Focus Areas
1. Market algorithms (supply/demand curves)
2. Price discovery mechanisms
3. Economic modeling

NEVER reference networking, threading, or parent directories!
```

### Contraintes Strictes pour Claude
1. **NEVER `cd ..`** ou référence parent
2. **ALWAYS `cmake .`** (pas cmake ..)
3. **ONLY JSON communication** avec autres modules
4. **UN module = UN sous-système** (granularité par responsabilité, pas par nombre de lignes ; découpe seulement quand deux sous-systèmes distincts se mêlent)
5. **ZERO infrastructure code** dans le contexte

## 🔧 Build System Cognitif

### Autonomous Build : Zero Mental Overhead

```cmake
# modules/tank/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(TankModule)  # Self-contained

# Everything local
include_directories(shared)
add_library(tank-module SHARED src/TankModule.cpp)

# Local build directory
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
```

**Avantage Claude** : Aucun concept de "projet parent" à comprendre !

### Hot-Reload pour Rapid Iteration
```cpp
// Engine hot-reload automatique
class ModuleLoader {
    void reloadIfChanged(const std::string& modulePath) {
        if(fileChanged(modulePath)) {
            unloadModule(modulePath);
            loadModule(modulePath);  // Reload .so
            // Game continue sans interruption !
        }
    }
};
```

**Workflow Claude** : Edit → Save → See changes instantly in game

## 🧪 Testing Strategy : AI-Optimized

### Unit Tests Intégrés
```cpp
// Dans TankModule.cpp
#ifdef TESTING
void runTests() {
    // Test 1: Movement
    auto input = json{{"type", "move"}, {"direction", "north"}};
    auto result = process(input);
    assert(result["status"] == "moving");

    // Test 2: Combat
    input = json{{"type", "attack"}, {"target", "enemy_1"}};
    result = process(input);
    assert(result["action"] == "fire");

    std::cout << "✅ All tank tests passed!" << std::endl;
}
#endif
```

### Standalone Testing
```bash
# Claude peut tester sans le engine complet
cd modules/tank/
make tank-module
./build/tank-module  # Run standalone avec tests intégrés
```

**Avantage IA** : Testing sans infrastructure complexe !

## 🔄 Hot-Reload Architecture

### Module State Preservation
```cpp
class TankModule {
private:
    json persistentState;  // Sauvegardé lors hot-reload

public:
    json getState() override {
        return persistentState;  // Engine sauvegarde l'état
    }

    void setState(const json& state) override {
        persistentState = state;  // Restore après reload
    }
};
```

### Seamless Development
1. **Claude modifie** TankModule.cpp
2. **Engine détecte** file change
3. **Automatic save** module state
4. **Reload .so** avec nouveau code
5. **Restore state** → Game continue
6. **Test immediately** nouvelles modifications

## 🎮 Debug Mode : IA Paradise

### Debug Engine Features
```cpp
class DebugEngine : public IEngine {
    // Execution step-by-step pour analyse
    void stepMode() { processOneModule(); }

    // Logging détaillé pour Claude
    void verboseLogging() {
        log("Module tank input: " + input.dump());
        log("Module tank output: " + output.dump());
    }

    // Module isolation pour debugging
    void isolateModule(const std::string& name) {
        // Run ONLY this module, others = mock
    }
};
```

### Claude Debug Workflow
```bash
# 1. Set debug mode
echo '{"debug_mode": true, "isolated_module": "tank"}' > config/debug.json

# 2. Run with step mode
./warfactory-engine --step-mode

# 3. Claude voit EXACT input/output de son module
# Perfect pour comprendre les interactions !
```

## 📊 Métriques Claude Code

### Avant : Architecture Monolithique
- **Context size** : 50K+ lignes (impossible)
- **Build time** : 2-5 minutes
- **Iteration cycle** : Edit → Compile → Restart → Test (10+ min)
- **Bug localization** : Needle in haystack
- **Parallel work** : Impossible (conflicts)

### Après : Architecture Modulaire
- **Context size** : un seul sous-système isolé (parfait)
- **Build time** : 5-10 secondes
- **Iteration cycle** : Edit → Hot-reload → Test (30 sec)
- **Bug localization** : Surgical precision
- **Parallel work** : 3+ Claude instances

### ROI Development
- **Claude efficiency** : 10x improvement
- **Development speed** : 5x faster iteration
- **Code quality** : Higher (focused contexts)
- **Bug density** : Lower (isolated modules)

## 🚀 Advanced Claude Patterns

### Pattern 1: Progressive Complexity
```cpp
// Iteration 1: Basic tank (Claude commence simple)
class TankModule {
    json process(const json& input) {
        if(input["type"] == "move") return basicMove();
        return {{"status", "idle"}};
    }
};

// Iteration 2: Add combat (Claude étend)
json process(const json& input) {
    if(input["type"] == "move") return advancedMove();
    if(input["type"] == "attack") return combat();
    return {{"status", "idle"}};
}

// Iteration 3: Add AI (Claude sophistique)
// Etc... Progression naturelle
```

### Pattern 2: Behavior Composition
```cpp
// Claude peut composer behaviors facilement
class TankModule {
    MovementBehavior movement;
    CombatBehavior combat;
    AiBehavior ai;

    json process(const json& input) {
        auto context = getCurrentContext();

        if(ai.shouldMove(context)) return movement.process(input);
        if(ai.shouldAttack(context)) return combat.process(input);
        return ai.idle(context);
    }
};
```

### Pattern 3: Data-Driven Logic
```cpp
// Claude travaille avec config, pas hard-coding
class TankModule {
    json tankConfig;  // Loaded from config/tanks.json

    json process(const json& input) {
        auto stats = tankConfig["tank_mk1"];
        auto speed = stats["max_speed"].get<double>();
        auto armor = stats["armor_thickness"].get<int>();

        // Logic basée sur data, pas constantes
        return processWithStats(input, speed, armor);
    }
};
```

## 🔮 Future Claude Integration

### Point 48 : AI-Driven Development
**Claude Code génère modules complets via prompts naturels**

```bash
# Future workflow: Natural language → Working module
User: "Create a tank module with advanced combat AI"

Claude:
1. Generates TankModule.cpp (250 lines)
2. Includes behavior trees, targeting systems
3. Auto-generates unit tests
4. Configures build system
5. Hot-reloads into running game
6. Result: Fully functional tank AI in minutes
```

**Technical Implementation:**
```cpp
class AIModuleGenerator {
    json generateModule(const std::string& prompt) {
        auto spec = parseNaturalLanguage(prompt);
        auto code = generateSourceCode(spec);
        auto tests = generateUnitTests(spec);
        auto config = generateConfig(spec);

        return {
            {"source", code},
            {"tests", tests},
            {"config", config},
            {"build_commands", generateBuild(spec)}
        };
    }
};
```

**Benefits:**
- **Rapid prototyping** : Idée → Module fonctionnel en minutes
- **AI pair programming** : Claude comprend context et génère code adapté
- **Automatic optimization** : Claude optimise performance selon targets
- **Self-validating code** : Tests générés automatiquement

### Point 49 : Natural Language Debugging
**Debug conversation Claude vs tools complexes**

```bash
# Traditional debugging (avoid)
gdb ./warfactory
(gdb) break TankModule::process
(gdb) run
(gdb) print variables...
# 20+ commands, complex analysis

# Natural Language Debugging (future)
User: "Tank moves too slowly in mud terrain"

Claude Debug Session:
🔍 Analyzing TankModule...
📊 Current speed: 28 (expected: 35)
🎯 Issue found: terrain modifier not applied correctly
📝 Location: TankModule.cpp line 142
⚡ Suggested fix: Update terrain calculation
✅ Fix applied: Tank speed now correct

# Single conversation → Problem solved
```

**Technical Architecture:**
```cpp
class NaturalLanguageDebugger {
    void analyzeIssue(const std::string& description) {
        // 1. Parse natural language problem description
        auto issue = parseIssueDescription(description);

        // 2. Analyze relevant module state
        auto moduleState = getModuleState(issue.moduleName);

        // 3. Compare expected vs actual behavior
        auto analysis = performAnalysis(issue, moduleState);

        // 4. Generate human-readable explanation
        auto explanation = generateExplanation(analysis);

        // 5. Suggest specific fixes
        auto suggestions = generateFixSuggestions(analysis);

        // 6. Apply fixes if approved
        if(user.approves(suggestions)) {
            applyFixes(suggestions);
        }
    }
};
```

**Debug Conversation Examples:**
```
User: "Economy module prices seem unstable"
Claude: Detected oscillation in price calculation. Market clearing frequency too high.
        Suggested fix: Reduce clearing cycle from 1h to 4h.

User: "Tank targeting is weird"
Claude: Found issue: Target selection prioritizes distance over threat.
        Current: target = findClosest(enemies)
        Better: target = findBestThreat(enemies, threatMatrix)

User: "Factory belt isn't working"
Claude: Belt module shows input blockage.
        Problem: Inserter rate 30/min > Belt capacity 20/min
        Fix: Upgrade belt or reduce inserter speed
```

**Benefits:**
- **Intuitive debugging** : Description naturelle → Solution précise
- **Context-aware analysis** : Claude comprend module interactions
- **Proactive suggestions** : Fixes suggérés avant implementation
- **Learning system** : Claude améliore analysis avec experience

---

## Conclusion

Cette architecture transforme Claude Code d'un **assistant de développement** en **développeur principal** capable de créer des systèmes de jeu complexes de manière autonome.

**Clé du succès** : Réduire la complexité cognitive à un niveau où l'IA peut exceller, tout en maintenant la puissance architecturale nécessaire pour un jeu AAA.