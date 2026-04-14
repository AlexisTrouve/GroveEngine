# Game Systems Roadmap — GroveEngine

Systèmes à implémenter pour supporter Last Cadia (et les jeux futurs).
Chaque système est générique — la logique de jeu est dans le game layer.

Référence architecture : voir `../FractaX/docs/ENGINE_BOUNDARY.md`

---

## Statuts
- 🔴 Non commencé
- 🟡 En design
- 🟢 Design validé
- ⚙️ En implémentation
- ✅ Implémenté

---

## Systèmes à créer

| Système | Statut | Priorité | Description |
|---|---|---|---|
| `TilemapSystem` | 🟡 | P0 | Grille de cases, états, propagation générique |
| `BuildingSystem` | 🟡 | P0 | Bâtiments sur tilemap, dégradation, matériaux |
| `EconomySystem` | 🟡 | P1 | Graphe de production configurable par data |
| `LogisticsSystem` | 🟡 | P1 | Réseau de transport, convois, interruptions |
| `CombatSystem` | 🟡 | P1 | Résolution combat, moral, formations |
| `CommandSystem` | 🟡 | P1 | Hiérarchie configurable, ordres, commandants IA |
| `MLBrainSystem` | 🟡 | P2 | Neuroévolution générique, fitness hookable |
| `GemmaSystem` | 🟡 | P2 | Client API LLM générique, snapshot hookable |
| `FrontlineSystem` | 🟡 | P2 | Contrôle territorial, grille de pression |

---

## Systèmes existants (à vérifier/adapter)

| Système | Statut | Notes |
|---|---|---|
| Module system | ✅ | Hot-reload, pub/sub IIO |
| Renderer (bgfx) | ✅ | Sprites instanciés, tilemaps, UI |
| Threaded modules | ✅ | Phase 2 validée |
| Pathfinding | 🔴 | À créer — flow fields + A* + HPA* |
| Audio | 🔴 | À créer |

---

## Ordre d'implémentation recommandé

### Phase 1 — Foundation (P0)
1. `TilemapSystem` — base de tout le reste
2. `BuildingSystem` — dépend de TilemapSystem
3. Pathfinding — flow fields basiques sur tilemap

### Phase 2 — Simulation (P1)
4. `EconomySystem` — graphe de production
5. `LogisticsSystem` — dépend de TilemapSystem + EconomySystem
6. `CombatSystem` — résolution + moral
7. `CommandSystem` — dépend de CombatSystem

### Phase 3 — Intelligence (P2)
8. `MLBrainSystem` — neuroévolution
9. `GemmaSystem` — client API LLM
10. `FrontlineSystem` — dépend de TilemapSystem + CombatSystem

---

## Principe de design

Chaque système suit le même pattern :

```cpp
// Engine layer — générique
class TilemapSystem : public IModule {
    // Config chargée depuis data (JSON/TOML)
    // Aucune référence à un jeu spécifique
    // Communique uniquement via IIO Topics
};

// Game layer — Last Cadia
// config/lastcadia/tilemap.json
// {
//   "cell_size": 32,
//   "tile_types": ["grass", "forest", "road", "water", "ruin", "rubble"],
//   "propagation_rules": [
//     { "type": "fire", "source_material": "wood", "spread_chance": 0.3, "spread_radius": 1 }
//   ]
// }
```

Le jeu ne sous-classe pas les modules engine. Il les configure.
