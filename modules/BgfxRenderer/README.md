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

**Combien de lampes ?** Mesuré, pas estimé (`tests/visual/benchmark_lighting.cpp`) : le coût est du
**fill rate**, proportionnel aux *viewports couverts*, et le nombre de lampes n'entre pas dans le
modèle. **Sans matière publiée : ~850 viewports couverts par frame à 60 fps. Avec de la matière :
~47.** Un seul occulteur multiplie donc tout par **18** — c'est un coût de *présence*, pas de volume
(un mur coûte autant que cinq cents). Réduire les **rayons** est le levier ; réduire le nombre ne
sert qu'à proportion de la surface retirée.

Conception : [`docs/design/lighting-2d.md`](../../docs/design/lighting-2d.md) (point d'entrée).
Guide consommateur détaillé : [DEVELOPER_GUIDE](../../docs/DEVELOPER_GUIDE.md).

### Bloom (post-traitement)

Une lueur autour de ce qui est **sur-exposé**. Un seul réglage, persistant :

```cpp
auto b = std::make_unique<JsonDataNode>("b");
b->setDouble("intensity", 1.5);      // 0 = ÉTEINT (le défaut). C'est l'interrupteur.
b->setDouble("threshold", 1.0);      // luminance au-delà de laquelle un pixel brille
b->setDouble("radius", 24.0);        // étendue, en PIXELS ÉCRAN
io->publish("render:bloom", std::move(b));
```

**⚠️ Le bloom exige l'éclairage actif.** Il se nourrit de la frame *composée*, et sans `render:ambient`
il n'y a pas de composite du tout — la scène va au backbuffer, qui ne s'échantillonne pas. Pour du
post-traitement sans look éclairé : un **ambiant blanc**, neutre par construction.

**Pourquoi la frame composée et pas le buffer de lumière** — c'est le seul choix d'architecture, et il
a une conséquence exploitable : la source étant l'image finale, un **sprite additif**
(`blend:"additive"`) brille aussi, pas seulement les lampes. C'est la forme du panache de moteur.
L'autre option coûtait une passe de moins et le laissait éteint.

Le seuil a un **genou doux** (la moitié du seuil, pas un bouton) : sans lui la *pente* de la lueur
saute au franchissement et le halo démarre par un ourlet visible. La courbe est en C++ dans
`grove::light::brightPassFraction` — un jeu peut demander « cette couleur brillerait-elle ? » sans
relecture GPU, comme pour la retombée des lampes.

**Coût** : une cible RGBA16F plein écran de plus et quatre passes plein écran, dont trois au **quart**
de la résolution. Payé seulement tant que `intensity > 0`. **Le HUD ne brille pas** — il est soumis
après la présentation ; interface nette au-dessus d'un monde qui éblouit, c'est voulu.

**La résolution du flou suit le rayon** (`grove::light::bloomDownsample` → 1/4, 1/8 ou 1/16), et c'est ce
qui rend une lueur large possible. Les 9 taps tombent aux **mêmes positions écran** quel que soit le
facteur — le plus externe doit valoir `radius`, c'est imposé — donc ce qui change est l'**empreinte** d'un
tap, soit un texel. À 1/4 un tap couvre 4 px et laisse 12 px de trou dès que le rayon dépasse ~24 px : ces
trous sont un **feston**. Rayon utile **jusqu'à ~96 px**.

⚠️ Au-delà, ça redégrade : passer à 1/32 échangerait le feston contre des **blocs** visibles, donc on
s'arrête à 1/16. Et le plafond de 24 px a été **vu sur une capture** à 40 px — le « ~60 px » écrit avant
lui par raisonnement était faux.

Conception : [`docs/design/lighting-bloom.md`](../../docs/design/lighting-bloom.md).

### Tonemapping (post-traitement)

Comprime la plage HDR pour que **deux sur-brillances restent différentes** au lieu de devenir le même
blanc. Réglage persistant, **indépendant du bloom** :

```cpp
auto t = std::make_unique<JsonDataNode>("t");
t->setString("mode", "aces");     // "none" (défaut = ÉTEINT), "reinhard" ou "aces"
t->setDouble("exposure", 1.4);    // multiplie la scène AVANT la courbe
io->publish("render:tonemap", std::move(t));
```

**⚠️ L'activer ASSOMBRIT la scène, et ce n'est pas un bug.** `reinhard(1) = 0,5` : ce qui était blanc
plein devient un gris moyen. C'est ce que fait une courbe de compression — elle fait de la place
au-dessus. Monter `exposure` (démarrer vers 1,5–2,5) jusqu'à replacer les tons moyens.

Mesuré, sur une surface blanche sous une lampe :

| Intensité | sans tonemap | reinhard | aces |
|---|---|---|---|
| 2 | **255** | 170 | 233 |
| 8 | **255** | 226 | 255 |

Sans courbe, les deux sont le même blanc : le sur-brillant que les cibles RGBA16F existent pour
conserver était jeté à la dernière ligne du pipeline.

| Mode | Ce que ça donne |
|---|---|
| `reinhard` | doux et prévisible ; **n'atteint jamais 1**, donc sépare indéfiniment. Pour une plage dynamique extrême. |
| `aces` | filmique, contrasté. ⚠️ **Sature vers 6** et ré-écrête au-delà — régler `exposure` pour tenir sous son point blanc. |

La lueur du bloom est ajoutée **avant** la courbe (sinon elle ressortirait au-dessus de 1 et
ré-écrêterait, donnant un aplat blanc collé sur l'image), la courbe s'applique **par canal** (sur la
luminance seule, on obtiendrait des halos fluo), et **le HUD n'est pas tonemappé** — il passe après la
présentation, donc l'interface reste lisible quelle que soit l'exposition.

Conception : [`docs/design/lighting-tonemap.md`](../../docs/design/lighting-tonemap.md).

### Fondus (post-traitement)

```cpp
auto f = std::make_unique<JsonDataNode>("f");
f->setDouble("amount", 1.0);        // 0 = éteint (défaut) .. 1 = l'écran EST la couleur
// f->setInt("color", 0xFF2010FF);  // facultatif ; NOIR par défaut
io->publish("render:fade", std::move(f));
```

**✅ Contrairement au bloom et au tonemapping, un fondu n'exige RIEN** — ni `render:ambient`, ni cible
HDR. C'est un quad mélangé par-dessus le résultat, donc il fonctionne dans un jeu qui n'éclaire pas.
C'est le seul effet de cette famille utilisable tel quel par les trois consommateurs actuels.

**Il couvre le HUD**, parce qu'il est dessiné sur sa propre vue soumise **en dernier** — après
l'interface. C'est voulu : une transition de scène doit emporter l'UI, sinon les menus flottent sur un
écran noir. (Le bloom et le tonemapping font l'inverse et épargnent le HUD.)

**C'est au jeu de ramper `amount`** : pas de durée, pas d'easing. Le moteur ne possède pas cette
horloge — une durée intégrée devrait décider si une pause gèle le fondu, et retirerait toute courbe
non linéaire à l'auteur.

`amount` est borné à [0,1] : au-delà, un mix **extrapole** et donne des artefacts au lieu d'un écran
plein. L'octet alpha de la couleur est ignoré — c'est `amount` qui fait office d'alpha.

Conception : [`docs/design/lighting-fade.md`](../../docs/design/lighting-fade.md).

### Colorimétrie (post-traitement)

Retouche l'image **finie** : le même décor devient un matin froid, un souvenir délavé ou une alerte
rouge sans qu'un asset change.

```cpp
auto g = std::make_unique<JsonDataNode>("g");
g->setDouble("saturation", 0.3);   // 0 = noir et blanc, 1 = neutre (défaut), >1 = criard
g->setDouble("contrast", 1.2);     // <1 rapproche du gris moyen, >1 en écarte
g->setInt("tint", 0x8090FFFF);     // blanc = neutre (défaut) — une COULEUR, pas trois flottants
io->publish("render:grade", std::move(g));
```

**Il n'y a pas de bouton « luminosité », volontairement.** Il existe déjà : c'est `exposure` du
tonemapping, et il est du **bon côté de la courbe**. Un gain après la compression ne ferait que saturer
plus tôt, en annulant ce que le tonemapping venait de sauver. Même raison : **la teinte ne peut
qu'assombrir** un canal (un octet plafonne à 1,0) — pour éclaircir, on monte `exposure`.

**Elle épargne le HUD**, contrairement au fondu : un monde désaturé sous une interface qui garde ses
couleurs est le comportement voulu, le HUD étant un objet de lecture et non un élément de la fiction.
Désaturer un texte d'alerte rouge l'effacerait au moment précis où il compte.

**L'ordre est fixe** : teinte → contraste → saturation, celui d'un étalonnage réel, et il n'est pas
commutatif — teinter après avoir désaturé donnerait un virage sépia.

Deux détails qui viennent de la math et évitent une heure de perplexité :

- **le contraste pivote sur 0,5** et pas 0,18, parce qu'on opère *après* le tonemapping, dans un espace
  d'affichage où le gris moyen est à 0,5. Un pivot linéaire assombrirait toute image contrastée ;
- **la désaturation suit la luminance perceptuelle** (celle du seuil du bloom) : un rouge pur donne
  54/255 et un bleu pur 18/255, parce que l'œil les voit à des clartés très différentes. Une moyenne
  `(r+g+b)/3` les enverrait tous deux à 85 et aplatirait la palette.

Conception : [`docs/design/lighting-grade.md`](../../docs/design/lighting-grade.md).

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

- [x] ~~**Mesurer le budget de lampes.**~~ Fait (`tests/visual/benchmark_lighting.cpp`) : 19,5 µs par
      viewport couvert sans matière, 355 µs avec. Voir § Éclairage 2D.
- [x] ~~**Post-traitement (bloom)**~~ Fait — `render:bloom`, voir § Bloom. Restent le **tonemapping**,
      les **fondus** et la **colorimétrie**, qui iront sur la passe de présentation qu'il a introduite.
- [ ] **Chaîne de mips pour le bloom** — sans elle, un rayon au-delà de ~60 px montre des cernes
- [ ] **Variantes Metal des shaders** : `fs_light`/`fs_nebula`/`vs_nebula` et les trois shaders de
      post-traitement n'ont pas de bloc Metal réel (placeholder), faute de backend Metal sur la chaîne
- [ ] `flipX`/`flipY` sur `render:sprite:update` (délibérément non supporté : double-flip)

## Dépendances

- **bgfx** : Téléchargé automatiquement via CMake FetchContent
- **GroveEngine::impl** : Core engine (IModule, IIO, IDataNode)
- **spdlog** : Logging
