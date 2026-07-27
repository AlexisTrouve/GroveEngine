# Tilemap — couleur LOD dérivée du tileset (+ override palette)

> **Statut** : plan validé sur l'archi, **pas encore implémenté**. Écrit le 2026-07-27.
> **Origine** : demande du projet consommateur DAOS (`DAOS/docs/groveengine_wishlist.md`, 2026-07-27).
> **Contrainte transverse** : tout additif — le défaut historique (palette 8 couleurs) doit rester
> byte-identique pour qui ne fait rien. Le moteur est partagé (Drifterra, mapview, DAOS).

## 1. Le problème

La bande LOD (zoom out) tire sa couleur de `lod::paletteColor(id)`
(`modules/BgfxRenderer/Passes/LodColor.h:19-33`) — **8 couleurs codées en dur**, `kColors[(id-1) % 8]`.

Ce chemin est pris **même quand un vrai tileset multi-couches est bindé** sur la bande détail :
`buildLodMipChain` (`LodColor.h:56`) est le seul producteur de la couleur LOD et il lit les *tile ids*,
jamais l'atlas. Conséquence pour tout jeu qui bind un tileset : **zoomé = son art, dézoomé = une palette
sans rapport qui wrap modulo 8**. DAOS (25 matériaux) le voit crûment ; Drifterra et mapview vivent
avec ce défaut latent depuis toujours.

### Pourquoi la bande LOD parle de couleurs et pas de textures

Contrainte dure, pas un choix de confort : au zoom out un pixel écran couvre des dizaines de tuiles.
Échantillonner l'atlas demanderait l'id de tuile par pixel — or la texture d'index est **R16UI
POINT/CLAMP**, jamais filtrable ni mippable (moyenner des *ids* n'a aucun sens : la moyenne de « roche »
et « eau » n'est pas « sable »). D'où la texture LOD : une RGBA8 mippée, 1 texel = 1 tuile, trilinéaire.
**C'est déjà une texture** — la seule question ouverte est ce qui remplit son mip 0.

## 2. La décision

Deux voies étaient sur la table (elles sont les options (a) et (b) du wishlist DAOS) :

| | (a) palette fournie par le jeu | **(b) dérivée du tileset** |
|---|---|---|
| Source de vérité | **deux** (l'art + la palette, à garder synchro) | **une** (l'art) |
| Nouveau topic | oui | aucun — le jeu publie déjà son tileset |
| Tuiles texturées (herbe, roche) | le jeu doit deviner la moyenne à l'œil | exacte par construction |
| Qui en profite | qui publie le topic | tout jeu qui bind un tileset, rétroactivement |

**Retenu : (b) en défaut, (a) en override.** (b) est la bonne physique et coûte ~8 lignes de calcul —
`loadArrayFromMemory` (`TextureLoader.cpp:149-160`) décode déjà le PNG en pixels CPU **et** les découpe
en couches contiguës juste avant de les jeter (`stbi_image_free`, l.160) : la moyenne par couche se
calcule là, sans readback GPU ni re-décodage. (a) reste utile au cas « jeu sans art du tout », dont les
couleurs sont pure data.

> Note d'historique : le refus initial de (b) reposait sur « les pixels CPU sont jetés au chargement,
> il faudrait un readback ». C'est **faux** — vérifié à `TextureLoader.cpp:158-160`.

## 3. La règle de résolution (le cœur — une seule règle)

Couleur LOD de la tuile `t` sur une couche dont le tileset est `T` :

| Ordre | Source | Voie |
|---|---|---|
| 1 | palette explicite enregistrée pour `T` | **(a)** override |
| 2 | table dérivée du tileset `T` (moyenne par couche, calculée au chargement) | **(b)** défaut |
| 3 | `lod::paletteColor(t)` — les 8 couleurs actuelles | historique |

- `t == 0` → transparent (inchangé).
- `t` **hors bornes** de la table retenue → **transparent + log one-shot**. Pas de wrap silencieux :
  au détail cette tuile n'existe pas non plus (l'atlas array n'a pas la couche), un trou visible est
  le signalement honnête.

`T = 0` (l'atlas procédural) n'a **jamais** de table dérivée → il tombe toujours en règle 3. Le
cantonnement « ne rien changer au chemin procédural » n'est donc pas un cas spécial à coder : il sort
gratuitement de la règle.

## 4. Découpage — TDD, test rouge d'abord à chaque slice

### S0 — noyau pur : la moyenne (~45 min)

**Nouveau** `modules/BgfxRenderer/Resources/AtlasAverage.h`, à côté de `AtlasSlice.h` :

```cpp
averageLayers(const uint32_t* arr, int tileW, int tileH, int layers) -> std::vector<uint32_t>
```

Pur, GPU-free, header-only (même posture que `AtlasSlice.h` / `LodColor.h`).

Moyenne **pondérée par l'alpha** : `RGB = Σ(rgb·a)/Σa`, `A = moyenne(a)`, `Σa == 0` → transparent.
*Pourquoi* : une tuile à 80 % transparente doit sortir « rouge pâle » (RGB rouge, A bas), pas « rouge
sombre » — une moyenne en droit assombrirait la couleur puis la composition la ré-assombrirait
(double-assombrissement). *Divergence assumée avec `avg4` (`LodColor.h:36`)* qui, lui, moyenne en droit :
il opère sur des couleurs **déjà par-tuile** où l'effet est du second ordre, et le changer serait une
régression sur le chemin existant.

**Test rouge** : nouveau `tests/unit/test_atlas_average.cpp` → `AtlasAverageUnit`
(enregistrer dans `tests/CMakeLists.txt`, près de `AtlasSliceUnit`) :
couche unie → la couleur exacte · damier → la moyenne exacte · couche semi-transparente → pondération
alpha · 0 couche.

### S1 — la table dérivée remonte jusqu'à la passe (~45 min)

- `TextureLoader::LoadResult` (`TextureLoader.h:19-26`) += `std::vector<uint32_t> layerColors`,
  rempli dans `loadArrayFromMemory` **là où `arr` est encore vivant** (`TextureLoader.cpp:158-160`).
- `TilemapPass::setTilesetLodColors(uint16_t textureId, std::vector<uint32_t>)` + stockage
  `m_lodDerived` — miroir de `m_tilesets` (`TilemapPass.h:121`) mais **owning** : c'est de la data CPU,
  pas un handle non-owning.
- Câblage dans le handler `render:tilemap:tileset` (`BgfxRendererModule.cpp:182-184`), juste après
  `setTileset`.

### S2 — le bake consomme la table + **invalidation** (~1 h 30 — le gros morceau)

- `lod::buildLodMipChain` gagne deux params optionnels `const uint32_t* palette, int paletteSize`
  (défaut `nullptr` → `paletteColor`). **Non-régression par construction**, et la fonction reste pure
  et testable headless.
- `bakeLodColorFor` (`TilemapPass.cpp:28`) prend la table résolue ; les sites d'appel la choisissent
  par `chunk.textureId` (couche 0 — `TilemapPass.cpp:292` retained, `:343` éphémère) et par
  `chunk.layers[li+1].textureId` (overlays — `TilemapPass.cpp:307` ; `TilemapLayer` porte bien son
  `textureId`, `Frame/FramePacket.h:53`).

**⚠️ Invalidation — le vrai piège.** Le LOD est baké à l'ajout du chunk puis **caché** par chunk id
(`m_retainedIndex[id].lod`, re-bake seulement si `needsCreate || chunk.dirty`, `TilemapPass.cpp:290-292`).
Un tileset enregistré **après** les chunks ne repeindrait donc rien — on imposerait un ordre de
publication implicite et non documenté, c'est-à-dire un échec silencieux.

Mécanisme retenu : compteur `m_lodEpoch` bumpé à **tout** changement de table (dérivée ou explicite)
+ `IndexTexture::lodEpoch` ; à l'exécution, `idx.lodEpoch != m_lodEpoch` → re-bake. O(1) à invalider,
aucun suivi chunk→tileset à maintenir.

**Le chemin éphémère** (`TilemapPass.cpp:338-347`) ne bake qu'à la (re)création : la condition d'epoch
doit y être ajoutée **explicitement**, pas fondue dans `needsCreate`.

**Test rouge** : `TilemapLodGpu` (`tests/unit/test_tilemap_lod_gpu.cpp`) — nouveau cas
**« tileset enregistré APRÈS le chunk »**, readback pixel du LOD == moyenne de la couche. Sans ce cas,
la feature marche à l'ordre de publication près et casse en silence.

### S3 — l'override (a) (~1 h)

- Topic `render:tilemap:palette {textureId?, colors: <blob RGBA8>}` → `setLodPalette(texId, colors)`
  (bump d'epoch). Blob plutôt que tableau d'ints : DAOS l'utilise déjà pour `render:texture:upload`.
- `textureId` défaut **0** : couvre au passage le cas « jeu sans art du tout » (peindre le chemin
  procédural sans publier le moindre PNG). Opt-in strict — rien publié = rien changé.
- Handler à côté de `render:tilemap:anim` / `:tileset` dans `BgfxRendererModule.cpp` (`SceneCollector`
  ignore ces topics, cf. le commentaire l.156-159).

**Test rouge** : `LodColorUnit` (`tests/unit/test_lod_color.cpp`) — les 3 oracles du wishlist DAOS :
(i) mip0 == `palette[id]` texel par texel · (ii) mip 1×1 == moyenne des couleurs présentes (le
box-filter reste exact) · (iii) **sans** palette == inchangé.

### S4 — doc + preuve de non-régression (~30 min)

- `docs/design/tilemap-renderer.md` (le doc qui annonçait déjà cette couture : « Game-provided palette
  tileIndex → color, art-directable »), table des topics de `modules/BgfxRenderer/README.md`,
  `docs/DEVELOPER_GUIDE.md`, ligne BgfxRenderer de `CLAUDE.md`.
- `LodColorUnit` + `TilemapLodGpu` **existants** doivent passer **sans modification** — c'est la preuve
  que le défaut historique est intact.

**Total estimé : ~4-5 h.**

## 5. Le risque qui ne peut pas être éliminé

**(b) change l'apparence dézoom de tout jeu qui bind déjà un tileset** — donc mapview
(`tests/visual/TerrainTileset.h`) et Drifterra. C'est le but (les deux bandes deviennent cohérentes),
mais c'est un changement **visible**, et aucun cantonnement ne l'évite : `T = 0` protège le chemin
procédural et les tests, pas eux.

Vérification déterministe disponible, sans œil : `test_mapview_viewer --shot` avant/après → diff
d'image sur la bande dézoom. À faire en S4.

Un opt-in explicite (`derivedLod: true` en config renderer) coûterait ~15 min en S1 — **écarté** :
il pérenniserait l'incohérence par défaut et personne ne l'activerait.

## 6. Hors périmètre

- **Item #2 du wishlist DAOS** (variante « pixels » de `render:tilemap:tileset`, pour éviter d'écrire
  un PNG temporaire au démarrage) — **non traité ici**. Avec (b), DAOS écrit toujours son PNG, donc
  leur friction reste entière. Coût si on l'enchaîne : ~1 h en variante **blob PNG encodé** (une branche
  vers `loadArrayFromMemory`, qui décode déjà depuis la mémoire) contre 2-3 h en **raw RGBA** tel que
  le wishlist le formule (nouveau chemin de découpe sans décodage). ⚠️ Le wishlist suppose que
  `loadArrayFromMemory` accepte des pixels bruts : **c'est faux**, elle prend des octets d'image
  encodée (`TextureLoader.h:39` le dit explicitement).
- Rien côté DAOS (leur repo, leur agent).

## 7. Points laissés à l'arbitrage

1. **Opt-in ou pas** pour le changement visuel de mapview/Drifterra ? *(reco : pas d'opt-in)*
2. **Item #2** : enchaîné dans la foulée en variante blob PNG encodé (~1 h), ou abandonné ?
