# Pattern JSON Data-Driven — GroveEngine

Pattern établi dans **WarFactory** (`archives/warfactoryracine/gameData/`).
Standard à respecter pour tous les jeux sur GroveEngine, dont Last Cadia.

---

## Principe

Le moteur enregistre des **handlers** nommés (`process_type`).
Les fichiers JSON décrivent **ce qui doit se passer** — pas comment.
Le moteur lit, résout les handlers, exécute avec les paramètres fournis.

**Le jeu ne code pas de logique dans les données. Il configure des comportements.**

---

## Pattern 1 — Phases / Steps / Process

Utilisé dans `Regular_world.json` pour la génération de monde.
Applicable à : world gen, économie, vagues ennemies, événements de map.

```json
{
  "nom_du_systeme": {
    "description": "Ce que ce fichier configure",
    "phases": [
      {
        "name": "nom_phase",
        "description": "Ce que fait cette phase",
        "duration_cycles": 3,
        "steps": [
          {
            "name": "nom_step",
            "process_type": "handler_enregistre_dans_le_moteur",
            "parameters": {
              "param1": 42,
              "param2": { "min": 10, "max": 100 },
              "param3": "autre_fichier.json"
            }
          }
        ]
      }
    ]
  }
}
```

**Règles :**
- `process_type` = string qui mappe à un handler C++ enregistré dans le moteur
- `parameters` = dictionnaire arbitraire — le handler sait quoi lire
- Les références à d'autres fichiers JSON sont des chemins relatifs au dossier `gameData/`
- Les commentaires `// ===` sont tolérés (parser permissif)

---

## Pattern 2 — Définitions de ressources

Utilisé dans `rawChemicals.json`.
Applicable à : ressources, unités, bâtiments, factions.

```json
{
  "resource_id": {
    "name": "Nom affiché",
    "category": "categorie_logique",
    "logistic_category": "resource | product | waste",
    "density": 2.1,
    "stack_size": 100,
    "container_type": "type_de_conteneur",
    "ui": {
      "icon": "fichier.png",
      "color": "#RRGGBB",
      "map_color": "#RRGGBB",
      "particle_effect": "nom_effet"
    }
  }
}
```

**Règles :**
- La clé de premier niveau est l'**ID programmatique** (snake_case, jamais traduit)
- `name` est la string affichée au joueur (localisable)
- `ui` est toujours présent — même pour les ressources invisibles (icon = "hidden.png")
- `category` est libre mais doit être documentée dans un index

---

## Pattern 3 — Index / Scénarios

Utilisé dans `World_index.json`.
Applicable à : index de scénarios, index de configs, registres.

```json
{
  "system_scenarios": {
    "description": "Scénarios disponibles",
    "default_scenario": "standard",
    "scenarios": {
      "standard": {
        "name": "Nom affiché",
        "description": "Description courte",
        "config_file": "fichier_config.json",
        "features": ["feature_a", "feature_b"],
        "recommended_for": ["cas_usage_1", "cas_usage_2"]
      }
    }
  },
  "shared_resources": {
    "definitions": "definitions.json"
  },
  "settings": {
    "cle": "valeur"
  }
}
```

---

## Application Last Cadia

### Tilemap config
```json
{
  "tilemap": {
    "cell_size": 32,
    "tile_types": {
      "grass":   { "movement_cost": 1.0, "flammable": false, "cover": 0.0 },
      "forest":  { "movement_cost": 1.5, "flammable": true,  "cover": 0.4 },
      "road":    { "movement_cost": 0.7, "flammable": false, "cover": 0.0 },
      "water":   { "movement_cost": 99,  "flammable": false, "cover": 0.0 },
      "ruin":    { "movement_cost": 1.8, "flammable": false, "cover": 0.6 },
      "rubble":  { "movement_cost": 2.0, "flammable": false, "cover": 0.2 }
    },
    "propagation_rules": [
      {
        "name": "fire_spread",
        "process_type": "propagate_state",
        "parameters": {
          "source_state": "on_fire",
          "target_material": "wood",
          "spread_chance": 0.3,
          "spread_radius": 1,
          "ticks_to_spread": 3,
          "extinguish_condition": "adjacent_water_source"
        }
      }
    ]
  }
}
```

### Ressources Raw tier
```json
{
  "stone": {
    "name": "Pierre",
    "category": "raw",
    "tier": 0,
    "logistic_category": "resource",
    "stack_size": 500,
    "container_type": "bulk_solid",
    "ui": {
      "icon": "stone.png",
      "color": "#808080",
      "map_color": "#696969",
      "particle_effect": "dust"
    }
  },
  "ore": {
    "name": "Minerai",
    "category": "raw",
    "tier": 0,
    "logistic_category": "resource",
    "stack_size": 200,
    "container_type": "bulk_solid",
    "ui": {
      "icon": "ore.png",
      "color": "#8B4513",
      "map_color": "#A0522D",
      "particle_effect": "metal_dust"
    }
  }
}
```

### Building materials
```json
{
  "wood": {
    "name": "Bois",
    "defense_multiplier": 0.3,
    "hp_multiplier": 0.4,
    "flammable": true,
    "fire_resistance": 0.0,
    "build_speed_multiplier": 2.0,
    "cost": { "wood": 10 },
    "ui": { "color": "#8B4513" }
  },
  "stone": {
    "name": "Pierre",
    "defense_multiplier": 0.6,
    "hp_multiplier": 0.7,
    "flammable": false,
    "fire_resistance": 0.8,
    "build_speed_multiplier": 1.0,
    "cost": { "stone": 15 },
    "ui": { "color": "#808080" }
  },
  "concrete": {
    "name": "Béton",
    "defense_multiplier": 0.85,
    "hp_multiplier": 0.9,
    "flammable": false,
    "fire_resistance": 1.0,
    "build_speed_multiplier": 0.4,
    "cost": { "concrete": 20 },
    "ui": { "color": "#C0C0C0" }
  },
  "armor": {
    "name": "Blindage",
    "defense_multiplier": 1.0,
    "hp_multiplier": 1.0,
    "flammable": false,
    "fire_resistance": 1.0,
    "build_speed_multiplier": 1.8,
    "cost": { "steel": 30, "armor_plate": 10 },
    "ui": { "color": "#2F4F4F" }
  }
}
```

---

## Référence WarFactory

Fichiers sources dans `archives/warfactoryracine/gameData/` :

| Fichier | Pattern utilisé | Ce qu'il illustre |
|---|---|---|
| `WorldGeneration/Regular_world.json` | Phases/Steps/Process | Simulation géologique en phases |
| `WorldGeneration/World_index.json` | Index/Scénarios | Registre de scénarios avec metadata |
| `WorldGeneration/meteorites.json` | Définitions | Définitions de types de météorites |
| `Ressources/rawChemicals.json` | Définitions ressources | Ressources avec UI, densité, logistique |

Ces fichiers font foi en cas de doute sur le format à adopter.
