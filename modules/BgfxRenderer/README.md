# BgfxRenderer Module

⚠️ **Development Stage**: Experimental, non-deterministic. See [main README](../../README.md#current-status) for limitations.

Module de rendu 2D pour GroveEngine, basé sur [bgfx](https://github.com/bkaradzic/bgfx).

## Features

- **Abstraction RHI** : Aucune dépendance bgfx exposée hors de `BgfxDevice.cpp`
- **Multi-backend** : DirectX 11/12, OpenGL, Vulkan, Metal (auto-détecté)
- **MT-ready** : Architecture task-based, lock-free frame allocator
- **Hot-reload** : Support complet du hot-reload GroveEngine
- **Batching** : Sprites groupés par texture pour performance

## Architecture

```
BgfxRenderer/
├── BgfxRendererModule.h/.cpp   # Point d'entrée IModule
├── RHI/                        # Render Hardware Interface
│   ├── RHITypes.h              # Handles typés, enums
│   ├── RHIDevice.h             # Interface abstraite
│   ├── RHICommandBuffer.h/.cpp # Command recording
│   └── BgfxDevice.cpp          # Implémentation bgfx
├── Frame/
│   ├── FrameAllocator.h/.cpp   # Allocateur lock-free
│   └── FramePacket.h           # Données immuables par frame
├── RenderGraph/
│   ├── RenderPass.h            # Interface pass
│   └── RenderGraph.h/.cpp      # Gestion des passes
├── Passes/
│   ├── ClearPass.h/.cpp        # Clear framebuffer
│   ├── TilemapPass.h/.cpp      # Chunks de tuiles + LOD
│   ├── SpritePass.h/.cpp       # Sprites + batching
│   ├── TextPass.h/.cpp         # Glyphes (bitmap 8x8 ou TTF)
│   ├── ParticlePass.h/.cpp     # Particules additives
│   ├── SectorPass.h/.cpp       # Secteurs angulaires (jauges, menus radiaux)
│   ├── DebugPass.h/.cpp        # Debug lines/shapes
│   ├── OcclusionPass.h/.cpp    # Matiere rectangulaire -> carte de transmittance
│   ├── NebulaPass.h/.cpp       # Milieux radiaux -> la meme carte
│   ├── LightPass.h/.cpp        # Lampes, accumulees additivement
│   └── CompositePass.h/.cpp    # scene x (ambiant + lumiere) + diffuse
├── Scene/
│   └── SceneCollector.h/.cpp   # Collecte depuis IIO
└── Resources/
    └── ResourceCache.h/.cpp    # Cache textures/shaders
```

## Build

### Windows (recommandé pour le rendu)

```powershell
cd "E:\Users\Alexis Trouvé\Documents\Projets\GroveEngine"

# Build rapide
.\build_renderer.bat

# Ou avec options
.\build_renderer.bat debug     # Build Debug
.\build_renderer.bat clean     # Clean + rebuild
.\build_renderer.bat vs        # Ouvrir Visual Studio
```

### Linux/WSL

```bash
cmake -B build -DGROVE_BUILD_BGFX_RENDERER=ON
cmake --build build -j4
```

Dépendances Linux :
```bash
sudo apt-get install libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
```

## Configuration

Le module est configuré via `IDataNode` dans `setConfiguration()` :

```json
{
    "windowWidth": 1280,
    "windowHeight": 720,
    "backend": "auto",
    "shaderPath": "./shaders",
    "vsync": true,
    "maxSpritesPerBatch": 10000,
    "frameAllocatorSizeMB": 16,
    "nativeWindowHandle": 0
}
```

| Paramètre | Type | Défaut | Description |
|-----------|------|--------|-------------|
| `windowWidth` | int | 1280 | Largeur fenêtre |
| `windowHeight` | int | 720 | Hauteur fenêtre |
| `backend` | string | "auto" | Backend graphique (auto, opengl, vulkan, dx11, dx12, metal) |
| `shaderPath` | string | "./shaders" | Chemin des shaders compilés |
| `vsync` | bool | true | Synchronisation verticale |
| `maxSpritesPerBatch` | int | 10000 | Sprites max par batch |
| `frameAllocatorSizeMB` | int | 16 | Taille allocateur frame (MB) |
| `nativeWindowHandle` | int | 0 | Handle fenêtre native (HWND, Window, etc.) |

## Communication IIO

Le renderer subscribe à `render:*` et traite les messages suivants :

### Sprites

```cpp
// Topic: render:sprite
auto sprite = std::make_unique<JsonDataNode>("sprite");
sprite->setDouble("x", 100.0);
sprite->setDouble("y", 200.0);
sprite->setDouble("scaleX", 1.0);
sprite->setDouble("scaleY", 1.0);
sprite->setDouble("rotation", 0.0);        // Radians
sprite->setDouble("u0", 0.0);              // UV min
sprite->setDouble("v0", 0.0);
sprite->setDouble("u1", 1.0);              // UV max
sprite->setDouble("v1", 1.0);
sprite->setInt("color", 0xFFFFFFFF);       // RGBA
sprite->setInt("textureId", 0);
sprite->setInt("layer", 0);                // Z-order
io->publish("render:sprite", std::move(sprite));
```

### Batch de sprites

```cpp
// Topic: render:sprite:batch
auto batch = std::make_unique<JsonDataNode>("batch");
auto sprites = std::make_unique<JsonDataNode>("sprites");
// Ajouter plusieurs sprites comme enfants...
batch->setChild("sprites", std::move(sprites));
io->publish("render:sprite:batch", std::move(batch));
```

### Caméra

```cpp
// Topic: render:camera
auto cam = std::make_unique<JsonDataNode>("camera");
cam->setDouble("x", 0.0);
cam->setDouble("y", 0.0);
cam->setDouble("zoom", 1.0);
cam->setInt("viewportX", 0);
cam->setInt("viewportY", 0);
cam->setInt("viewportW", 1280);
cam->setInt("viewportH", 720);
io->publish("render:camera", std::move(cam));
```

### Clear color

```cpp
// Topic: render:clear
auto clear = std::make_unique<JsonDataNode>("clear");
clear->setInt("color", 0x303030FF);        // RGBA
io->publish("render:clear", std::move(clear));
```

### Debug (lignes et rectangles)

```cpp
// Topic: render:debug:line
auto line = std::make_unique<JsonDataNode>("line");
line->setDouble("x1", 0.0);
line->setDouble("y1", 0.0);
line->setDouble("x2", 100.0);
line->setDouble("y2", 100.0);
line->setInt("color", 0xFF0000FF);         // Rouge
io->publish("render:debug:line", std::move(line));

// Topic: render:debug:rect
auto rect = std::make_unique<JsonDataNode>("rect");
rect->setDouble("x", 50.0);
rect->setDouble("y", 50.0);
rect->setDouble("w", 100.0);
rect->setDouble("h", 100.0);
rect->setInt("color", 0x00FF00FF);         // Vert
rect->setBool("filled", false);
io->publish("render:debug:rect", std::move(rect));
```

### Éclairage 2D

Le monde part dans une cible hors-écran, les lampes s'accumulent dans une seconde, et un quad plein
écran compose. **Rien n'est éclairé tant que `render:ambient` n'a pas été publié** : sans lui, aucune
cible n'est créée et la sortie est identique à l'octet près à un build sans éclairage.

```cpp
// 1. L'ambiant ALLUME le système. Un ambiant blanc ne change rien à la scène
//    et laisse les lampes seulement l'éclaircir — pas besoin d'un jeu sombre.
auto amb = std::make_unique<JsonDataNode>("a");
amb->setInt("color", 0x202838FF);            // nuit bleue
io->publish("render:ambient", std::move(amb));

// 2. Une lampe. ÉPHÉMÈRE : à republier chaque frame. cx,cy = CENTRE.
auto l = std::make_unique<JsonDataNode>("l");
l->setDouble("cx", 320.0); l->setDouble("cy", 180.0);
l->setDouble("radius", 140.0);               // unités MONDE
l->setInt("color", 0xFFC070FF);
l->setDouble("intensity", 1.6);              // peut dépasser 1 (cible RGBA16F)
// l->setDouble("dirDeg", 30.0); l->setDouble("spreadDeg", 55.0);  // -> cône
io->publish("render:light", std::move(l));

// 3. Un mur. RETENU, parce qu'un mur ne bouge pas — l'inverse du conseil sur les lampes.
auto o = std::make_unique<JsonDataNode>("o");
o->setInt("renderId", 1);
o->setDouble("x", 400.0); o->setDouble("y", 0.0);   // x,y = COIN haut-gauche
o->setDouble("w", 24.0);  o->setDouble("h", 360.0);
io->publish("render:occluder:add", std::move(o));
```

**Une seule carte, un seul blend.** Murs, vitraux et milieux écrivent tous une *transmittance par
unité de longueur* dans la même carte d'occultation, que le shader de lampe parcourt. Le blend étant
multiplicatif, des matières superposées se composent **dans n'importe quel ordre** — aucun tri de
profondeur nulle part.

| Matière | Ce qu'elle écrit | Effet |
|---|---|---|
| **Mur** (`render:occluder`) | `0` | ombre portée : un zéro annule le produit, tout ce qui suit s'éteint |
| **Vitrail** (`render:filter`) | une **couleur** | la lumière passe, teintée |
| **Brouillard** (`render:fog`) | `exp(−α)` | absorbe d'autant plus qu'on traverse loin |
| **Nébuleuse** (`render:nebula`) | idem, mais **densité radiale** | un nuage, sans bord géométrique |

Les quatre existent en **éphémère** et en **retenu** (`:add`/`:update`/`:remove`). Le retenu est la
bonne forme dès que la matière ne bouge pas — un mur, un vitrail, une nébuleuse ; l'éphémère est le
bon choix pour les lampes, qui suivent en général quelque chose de mobile.

⚠️ **Pièges qui coûtent du temps**

- **`density` n'est pas une opacité** : c'est le coefficient α de Beer-Lambert, sans borne haute et
  *par unité de longueur*. Partir de **`0.02`**, pas de `0.9` — sur un rayon de 150, `0.9` donne
  `exp(−135)`, un disque noir.
- **Une matière plus fine que ~3 pixels ÉCRAN peut être enjambée** par la marche : un mur très mince
  lâche quand on **dézoome**, pas quand la lampe grossit. Rien n'oblige le rect occulteur à coïncider
  avec le sprite qui le dessine.
- **Rien n'atténue l'ambiant** (il n'a pas de trajet) : une scène très brumeuse mais fortement
  ambiante ne paraîtra pas brumeuse.
- **Les occulteurs bloquent la LUMIÈRE, pas la VUE.** La visibilité est un système de gameplay.
- **Empiler des `render:fog` ne fait pas un nuage** — ça donne des contours rectangulaires
  concentriques. Empiler des `render:nebula`, si.

Conception : [`docs/design/lighting-2d.md`](../../docs/design/lighting-2d.md) (point d'entrée).
Guide consommateur détaillé : [DEVELOPER_GUIDE](../../docs/DEVELOPER_GUIDE.md).

### Topics complets

| Topic | Description |
|-------|-------------|
| `render:sprite` | Un sprite (`x,y` = centre) |
| `render:rect` | Rectangle plein coloré `{x,y,w,h,color,layer}` (coin haut-gauche) — quad **layeré** (sprite-pass, avant le texte), pour fonds de HUD. ≠ `render:debug:rect` (toujours au-dessus, sans layer) |
| `render:sprite:batch` | Batch de sprites |
| `render:tilemap` | Chunk **éphémère** (ré-uploadé chaque frame) — `{x,y,width,height,tileW,tileH,textureId,tileData}` |
| `render:tilemap:tileset` | **Bind un tileset** à `textureId` : `{textureId,tileW,tileH,path}` **ou** `{textureId,tileW,tileH,imgW,imgH,+blob "pixels"}` — découpe une grille `tileW×tileH` en texture2DArray (tile id `T` → layer `T-1` ; id `0` = transparent). Source = un **PNG sur disque** ou des **pixels RGBA8 déjà décodés** (`imgW*imgH*4` octets exactement) pour un jeu qui *génère* son tileset au démarrage. Les deux fournis → **`pixels` gagne** + warning ; taille incohérente → **échec**, pas d'atlas partiel. ⚠️ `imgW`/`imgH` et non `w`/`h` (`tileW`/`tileH` sont déjà sur ce topic). Fixe aussi **les couleurs du dézoom** gratuitement : la moyenne de chaque couche devient la table LOD de ce tileset (les deux bandes s'accordent). Ordre libre — le binder après les chunks les re-bake |
| `render:tilemap:palette` | **Override la table couleur du dézoom** : `{textureId?, colors:<blob RGBA8>}` — 4 octets par entrée (R,G,B,A), entrée `i` = tile id `i+1`. Pour un jeu dont les couleurs sont pure data, sans art à moyenner. `textureId` défaut `0` (atlas procédural). Un id hors table est **transparent** (jamais wrappé). Rien publié = palette historique inchangée |
| `render:tilemap:add` | Chunk **retained** par `id` (upload-once), `+fogData?` (fog-of-war), LOD/zoom seamless. ⚠️ `x,y`=monde, **pas** de `chunkX`/`tileSize`/`layer` |
| `render:tilemap:update` | MAJ chunk retained : full `{id,tileData}` ou patch partiel `{id,x,y,w,h,tileData}` (coords-tuiles) |
| `render:tilemap:remove` | Retire un chunk retained `{id}` |
| `render:text` | Texte à afficher |
| `render:particle` | Particule |
| `render:camera` | Configuration caméra |
| `render:clear` | Clear color |
| `render:debug:line` | Ligne de debug |
| `render:debug:rect` | Rectangle de debug |
| `render:ambient` | `{color}` — **allume l'éclairage**. Absent ou 0 = système entièrement contourné, sortie identique à un build sans éclairage |
| `render:light` | `{cx,cy,radius,color,intensity?,dirDeg?,spreadDeg?}` — lampe **éphémère**, `cx,cy` = CENTRE, `radius` en unités monde. `dirDeg`/`spreadDeg` (défaut 360 = omni) la transforment en cône |
| `render:occluder` | `{x,y,w,h}` — mur opaque **éphémère**, `x,y` = COIN. `+ :add`/`:update`/`:remove` **retenus** (la bonne forme pour du décor) |
| `render:filter` | `{x,y,w,h,color,opacity?}` — vitrail **éphémère**, `x,y` = COIN. `color` = la teinte après UNE traversée perpendiculaire de l'axe mince. `+ :add`/`:update`/`:remove` |
| `render:fog` | `{x,y,w,h,density,color?,scatter?}` — milieu rectangulaire **éphémère**, `x,y` = COIN. `density` = α de Beer-Lambert (**pas** une opacité). `scatter` rend le milieu **visible**. `+ :add`/`:update`/`:remove` |
| `render:nebula` | `{cx,cy,radius,density,color?,scatter?}` — milieu **radial** éphémère, `cx,cy` = CENTRE. Densité maximale au cœur, nulle au bord : le quad de découpe est invisible. `+ :add`/`:update`/`:remove` **retenus** — la bonne forme pour un nuage, qui fait quatre à six volumes superposés |

## Intégration

### Exemple minimal

```cpp
#include <grove/ModuleLoader.h>
#include <grove/JsonDataNode.h>
#include <grove/IntraIOManager.h>

int main() {
    // Créer le gestionnaire IO
    auto ioManager = std::make_unique<IntraIOManager>();
    auto io = ioManager->createIO("renderer");

    // Charger le module
    ModuleLoader loader;
    loader.load("./modules/libBgfxRenderer.dll", "renderer");

    // Configurer
    JsonDataNode config("config");
    config.setInt("windowWidth", 1920);
    config.setInt("windowHeight", 1080);
    config.setInt("nativeWindowHandle", (int)(intptr_t)hwnd);  // Ton HWND

    auto* module = loader.getModule();
    module->setConfiguration(config, io.get(), nullptr);

    // Main loop
    JsonDataNode input("input");
    while (running) {
        input.setDouble("deltaTime", deltaTime);

        // Envoyer des sprites via IIO
        auto sprite = std::make_unique<JsonDataNode>("sprite");
        sprite->setDouble("x", playerX);
        sprite->setDouble("y", playerY);
        sprite->setInt("textureId", 0);
        io->publish("render:sprite", std::move(sprite));

        // Process (collecte IIO + rendu)
        module->process(input);
    }

    module->shutdown();
    return 0;
}
```

## Règles d'architecture

| Règle | Raison |
|-------|--------|
| **Zéro `bgfx::` hors de `BgfxDevice.cpp`** | Abstraction propre, changement backend possible |
| **FramePacket const dans les passes** | Thread-safety, pas de mutation pendant render |
| **CommandBuffer par thread** | Pas de lock pendant l'encoding |
| **Handles, jamais de pointeurs raw** | Indirection = safe pour relocation |
| **Allocation via FrameAllocator** | Lock-free, reset gratuit chaque frame |

## TODO

*(Tout ce que cette liste réclamait autrefois — textures, shaders, tilemap, texte, particules,
cibles de rendu, multi-vues — est livré. Une TODO qui ment coûte plus cher qu'une TODO absente.)*

- [ ] **Mesurer le budget de lampes.** Chaque lampe est un quad coûtant jusqu'à 64 accès texture par
      pixel couvert ; le chiffre « des dizaines » qui circulait est antérieur à la marche
      d'occultation et n'a pas été revérifié depuis.
- [ ] **Post-traitement** (bloom en premier) — la cible RGBA16F conserve déjà le sur-brillant pour ça
- [ ] **Variantes Metal des shaders** : `fs_light`/`fs_nebula` n'ont pas de bloc Metal réel
      (placeholder), faute de backend Metal sur la chaîne de build
- [ ] `flipX`/`flipY` sur `render:sprite:update` (délibérément non supporté : double-flip)

## Dépendances

- **bgfx** : Téléchargé automatiquement via CMake FetchContent
- **GroveEngine::impl** : Core engine (IModule, IIO, IDataNode)
- **spdlog** : Logging
