# Session Suivante : Fix IO Routing

## 🎯 Contexte
Implémentation du scénario 11 (IO System Stress Test). Le test est créé et compile, mais le routing des messages entre modules IntraIO ne fonctionne pas.

## 🐛 Problème Identifié
**Bug Architecture** : `IntraIO::publish()` et `IntraIO::subscribe()` ne communiquent PAS avec `IntraIOManager` singleton.

### Flux Actuel (Cassé)
```
Module A publish("test", data)
  ↓
IntraIO::publish() → messageQueue locale ❌
  
Module B subscribe("test")
  ↓  
IntraIO::subscribe() → subscriptions locales ❌

Résultat: Aucun message routé entre modules !
```

### Flux Corrigé (Implémenté)
```
Module A publish("test", data)
  ↓
IntraIO::publish()
  ↓ extract JSON from JsonDataNode
  ↓
IntraIOManager::routeMessage(instanceId, topic, json) ✅
  ↓
Pour chaque subscriber:
  - Copy JSON
  - Créer nouveau JsonDataNode
  - deliverMessage() → queue du subscriber

Module B subscribe("test")
  ↓
IntraIO::subscribe()
  ↓
IntraIOManager::registerSubscription(instanceId, pattern) ✅
```

## ✅ Modifications Effectuées

### 1. IntraIOManager.h (ligne 74)
```cpp
// AVANT
void routeMessage(const std::string& sourceid, const std::string& topic, std::unique_ptr<IDataNode> message);

// APRÈS  
void routeMessage(const std::string& sourceid, const std::string& topic, const json& messageData);
```

### 2. IntraIOManager.cpp
- Ajout include: `#include <grove/JsonDataNode.h>`
- Ligne 102-148: Nouvelle implémentation de `routeMessage()`:
  - Prend `const json&` au lieu de `unique_ptr<IDataNode>`
  - Pour chaque subscriber matching:
    - `json dataCopy = messageData;` (copie JSON)
    - `auto dataNode = std::make_unique<JsonDataNode>("message", dataCopy);`
    - `deliverMessage(topic, std::move(dataNode), isLowFreq);`
  - **Fix 1-to-many** : Continue la boucle au lieu de break (ligne 134)

### 3. IntraIO.cpp
- Ajout include: `#include <grove/IntraIOManager.h>`

**publish()** (ligne 24-40):
```cpp
void IntraIO::publish(const std::string& topic, std::unique_ptr<IDataNode> message) {
    std::lock_guard<std::mutex> lock(operationMutex);
    totalPublished++;
    
    // Extract JSON
    auto* jsonNode = dynamic_cast<JsonDataNode*>(message.get());
    if (!jsonNode) throw std::runtime_error("Requires JsonDataNode");
    
    const nlohmann::json& jsonData = jsonNode->getJsonData();
    
    // Route via Manager ← NOUVEAU !
    IntraIOManager::getInstance().routeMessage(instanceId, topic, jsonData);
}
```

**subscribe()** (ligne 38-51):
```cpp
void IntraIO::subscribe(const std::string& topicPattern, const SubscriptionConfig& config) {
    // ... existing code ...
    highFreqSubscriptions.push_back(std::move(sub));
    
    // Register with Manager ← NOUVEAU !
    IntraIOManager::getInstance().registerSubscription(instanceId, topicPattern, false);
}
```

**subscribeLowFreq()** (ligne 53-66):
```cpp
void IntraIO::subscribeLowFreq(const std::string& topicPattern, const SubscriptionConfig& config) {
    // ... existing code ...
    lowFreqSubscriptions.push_back(std::move(sub));
    
    // Register with Manager ← NOUVEAU !
    IntraIOManager::getInstance().registerSubscription(instanceId, topicPattern, true);
}
```

## 🚀 Prochaines Étapes

### 1. Build
```bash
cd /mnt/c/Users/alexi/Documents/projects/groveengine/build
cmake --build . -j4
```

### 2. Run Test
```bash
cd /mnt/c/Users/alexi/Documents/projects/groveengine/build/tests
./test_11_io_system
```

### 3. Résultats Attendus
- ✅ TEST 1: Basic Pub/Sub → 100/100 messages reçus
- ✅ TEST 2: Pattern Matching → patterns matchent correctement
- ✅ TEST 3: Multi-Module → TOUS les subscribers reçoivent (1-to-many fixé!)
- ✅ TEST 4-6: Autres tests passent

### 4. Si Erreurs de Compilation
Vérifier que tous les includes sont présents:
- `IntraIOManager.cpp`: `#include <grove/JsonDataNode.h>`
- `IntraIO.cpp`: `#include <grove/IntraIOManager.h>`

### 5. Si Tests Échouent
Activer les logs pour debug:
```cpp
IntraIOManager::getInstance().setLogLevel(spdlog::level::debug);
```

Vérifier dans les logs:
- `📨 Routing message:` apparaît quand publish()
- `📋 Registered subscription:` apparaît quand subscribe()
- `↪️ Delivered to` apparaît pour chaque delivery

## 📊 Architecture Finale

```
IDataNode (abstraction)
    ↓
JsonDataNode (implémentation avec nlohmann::json)
    ↓
IntraIO (instance par module)
    - publish() → extrait JSON → routeMessage()
    - subscribe() → registerSubscription()
    - deliverMessage() ← reçoit de Manager
    ↓
IntraIOManager (singleton central)
    - routeMessage() → copie JSON → deliverMessage() aux subscribers
    - routingTable : patterns → instances
```

**Avantages de cette architecture**:
- ✅ JSON est copiable (pas besoin de clone())
- ✅ 1-to-many fonctionne (copie JSON pour chaque subscriber)
- ✅ Compatible futur NetworkIO (JSON sérialisable)
- ✅ Abstraction IDataNode préservée

## 📝 Fichiers Modifiés
1. `/include/grove/IntraIOManager.h` (signature routeMessage)
2. `/src/IntraIOManager.cpp` (implémentation routing avec JSON)
3. `/src/IntraIO.cpp` (publish/subscribe appellent Manager)

## ✅ Todo List
- [x] Modifier signature routeMessage() pour JSON
- [x] Implémenter copie JSON et recreation DataNode
- [x] Modifier subscribe() pour enregistrer au Manager
- [x] Modifier subscribeLowFreq() pour enregistrer au Manager
- [x] Modifier publish() pour router via Manager
- [ ] **Build le projet**
- [ ] **Run test_11_io_system**
- [ ] **Vérifier que tous les tests passent**

Bonne chance ! 🚀
