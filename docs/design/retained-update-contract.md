# Le contrat des `:update` retenus — une règle, tirée de deux dérives

> **La règle, en une phrase** : les champs d'une primitive retenue se lisent par une **fonction
> partagée** entre `:add` et `:update`. Recopier la liste des champs dans les deux chemins est la
> cause mécanique des quatre défauts trouvés les 31/07 et 01/08 — pas une négligence ponctuelle.

## 1. Les faits

Deux audits, à un jour d'écart, sur des primitives sans rapport entre elles.

| Primitive | Comment `:update` lit ses champs | A dérivé ? |
|---|---|---|
| `render:sprite` | liste **recopiée** | ✅ oui — `u0..v1`, `flipX/flipY`, `blend` jetés |
| `render:text` | liste **recopiée** | ✅ oui — `fontId` jeté, chaîne vide ignorée |
| `render:tilemap` | liste **recopiée** | ✅ oui — les couches jamais relues |
| `render:occluder` | liste **recopiée** (4 champs) | ❌ pas encore |
| `render:fog` | **`readFogFields` partagé** | ❌ impossible |
| `render:filter` | **`readFilterFields` partagé** | ❌ impossible |
| `render:nebula` | **`readNebulaFields` partagé** | ❌ impossible |
| `render:nineslice` | `:update` **appelle** `:add` (`expandNineSlice`) | ❌ impossible |

Trois recopies sur quatre ont dérivé. Zéro partage sur quatre a dérivé. L'occluder n'a pas dérivé
**parce qu'il n'a que quatre champs** — c'est le même risque structurel, à une échelle où l'œil
suffit encore. Ce n'est pas une garantie, c'est un sursis.

⚠️ **Le mode de panne est SILENCIEUX dans les trois cas** : la donnée est reçue, désérialisée et
stockée. Elle n'est simplement pas écrite là où le rendu la lit. Aucune erreur, aucun log, aucun
assert — juste une image qui ne change pas. C'est ce qui explique que trois défauts aient survécu
à des mois d'usage.

## 2. Les trois sémantiques, et comment choisir

Un champ omis par un `:update` peut vouloir dire trois choses. Les trois sont légitimes ; les
mélanger sans le dire ne l'est pas.

| Sémantique | Un champ omis… | Pour quoi |
|---|---|---|
| **Conserver** | garde sa valeur | ce qui décrit une **position** : `cx/cy`, `layer`, `color` — on ajuste |
| **Instantané complet** | revient au **défaut** | ce qui décrit une **apparence** : UV, flip, blend, clip — on republie en entier |
| **Fusion partielle assumée** | garde sa valeur, **et c'est argumenté** | l'occluder : une porte coulissante doit bouger sans redire son étendue |

**La frontière position/apparence n'est pas cosmétique.** Un flip EST un échange d'UV : le
« conserver » obligerait à mémoriser les UV de base à côté de l'instance, et un flip réappliqué sur
des UV déjà miroitées **basculerait** d'une frame à l'autre. En repartant du défaut, l'état publié
est l'état obtenu — idempotent par construction. C'est ce qui permet à un jeu de publier son ÉTAT
plutôt que ses TRANSITIONS.

> Une divergence **pensée** n'est pas une dérive. L'occluder diverge du sprite et il a raison ; ce
> qui compte est que la raison soit **écrite à côté du code**, pas déduite après coup.

## 3. Le piège « absent » vs « vide »

```cpp
// FAUX — un libellé devient impossible à EFFACER
std::string s = data.getString("text", "");
if (!s.empty()) targetStr[id] = s;

// JUSTE — c'est la PRÉSENCE de la clé qui porte l'intention
if (data.hasProperty("text")) targetStr[id] = data.getString("text", "");
```

Même famille que « omis » vs « remis à zéro », un cran plus bas. Un test sur la **valeur** ne peut
pas distinguer « le champ n'a pas été envoyé » de « le champ a été envoyé vide », et les deux ont
des sens opposés.

⚠️ Corollaire vérifié plutôt que supposé : une chaîne vide sort du paquet à **`nullptr`**, pas à
chaîne vide (`finalize` n'alloue rien, `TextPass` saute une commande à `text` nul). C'est correct —
mais un correctif qui rendrait un `nullptr` là où le consommateur n'en attend pas transformerait un
bug cosmétique en déréférencement. **Vérifier le consommateur avant de changer une représentation.**

## 4. Checklist pour une nouvelle primitive retenue

1. **Un seul lecteur de champs**, `readXFields(const IDataNode&, X& target)`, appelé par `:add`
   (sur une valeur neuve) **et** par `:update` (sur la valeur stockée). La sémantique « conserver »
   tombe alors toute seule, et l'ajout d'un champ ne peut pas oublier un chemin.
2. **Écrire la sémantique choisie** au-dessus du lecteur, avec sa raison.
3. **`renderId == 0` refusé** — sinon toute primitive non identifiée partage un slot et chaque
   `add` écrase le précédent en silence.
4. **`:update` sur un id inconnu** : soit un `add` (sprite, tilemap), soit un no-op (matière). Les
   deux se défendent ; choisir et l'écrire.
5. **`:remove` purge TOUS les buckets** (monde ET HUD) : le message ne porte pas de `space`, donc on
   ne peut pas savoir lequel viser.

## 5. Comment le tester — le point qui a laissé passer les quatre

**Asserter le `FramePacket`, jamais le message publié.**

`IT_054` (l'E2E du flipbook) s'abonne à `render:sprite:update` et lit `m.data->getDouble("u0")`. Il
prouve que l'UI **émet** les bonnes UV — et il est resté vert pendant tout le temps où le collector
les jetait. **Un E2E qui observe le bus vérifie l'émetteur, pas la chaîne.**

Deuxième piège, plus vicieux parce qu'il ressemble à de la couverture : l'ancien test `[flip]`
simulait une sous-rect d'atlas **à la main** avec un `textureId` numérique — la seule combinaison où
le flip survivait — et son commentaire *argumentait* couvrir le cas atlas. **Aucun test ne câblait un
`AssetManager` sur le `SceneCollector`** ; c'est le trou de couverture en une ligne, et il explique
les deux.

> Avant de croire un test de chemin retenu : *quel chemin emprunte le VRAI consommateur ?* Si le test
> emprunte l'autre, il ne garde rien.

## 6. Où c'est

- `modules/BgfxRenderer/Scene/SceneCollector.cpp` — les `parse*Add`/`parse*Update` et les lecteurs
  partagés (`readFogFields`, `readFilterFields`, `readNebulaFields`, `readTilemapLayers`).
- `tests/integration/test_scene_collector.cpp` — sections « Mode RETENU » et « AUDIT ».
- `docs/design/sprite-transforms.md` — le cas sprite en détail.
