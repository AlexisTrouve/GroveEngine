# Handoff — sessions des 27 & 28 juillet 2026

> ➡️ **Suite : [handoff des 28 & 29 juillet](session-handoff-2026-07-29.md)** — l'éclairage 2D
> (L1→L3 + socle + murs), `IIO::unsubscribe`, le blend additif. Les trois pistes ouvertes ci-dessous
> y sont reprises et deux sont levées.

> **État à la sortie** : `master` = **`af4a226`**, poussé sur gitea + github, arbre propre,
> suite **182/182**. Rien en attente, rien de cassé.
> Point de départ : `28b8def` (176/177, avec un échec rouge sur master depuis un moment).

Ce document dit **où on en est, ce qui est clos, et ce qui ne l'est pas** — pas l'historique complet,
que `git log` porte déjà mieux. Chaque chantier a son propre doc, référencé.

---

## 1. Ce qui est clos

### Tilemap — wishlist DAOS bouclée
Le dézoom prend les couleurs du tileset bindé (moyenne alpha-pondérée par couche), avec palette
explicite en override et tileset chargeable depuis un blob de pixels bruts. **DAOS n'attend plus rien
du moteur** côté tilemap. → `docs/design/tilemap-lod-palette.md`

### Passe UI 9-slice — P0 → P4
**11 widgets** peuvent porter un cadre, via `UIFrame` partagé. → `docs/design/ui-sprite-pass.md`

### Dette texte — les 4 items levés
Vraie police TrueType + graisse réelle (Roboto Apache 2.0 livré), ellipsis sur débordement, chrome de
fenêtre encastré dans le cadre. → `docs/design/ui-nineslice-handoff.md`

### Corruption de tas d'`IOSystemStress`
`thread_local std::string` dans `publish()` : son destructeur, à la sortie de thread, corrompait le
tas. **5/150 (3,3 %) → 0/120.** → `docs/design/iosystemstress-heap-corruption-handoff.md`

### Transforms de sprite (demande DAOS)
`flipX`/`flipY` ajoutés ; **`rotation` existait déjà** et fonctionnait.
→ `docs/design/sprite-transforms.md`

### Robustesse & correctifs
Un widget malformé ne tue plus l'écran (`IT_061`) ; `MapViewViewerE2E`, rouge sur master, réparé —
**aucun bug moteur**, le test comparait deux mécanismes de réduction différents.

---

## 2. Ce qui reste ouvert

### Nuisances connues → `docs/design/known-annoyances.md`
`TestModule.cpp` réécrit par l'`AutoCompiler` et qui s'invite dans les commits ; trois tests lourds qui
échouent sous `ctest -j` et passent en série. Ni l'un ni l'autre n'est un bug, les deux coûtent du
temps à répétition.

### Pistes non demandées, par valeur décroissante
1. **Le gate de la corruption de tas est le repro autonome, pas le ctest.** `IIOThreadPublish` passe
   *aussi* sur le moteur non corrigé — c'est écrit dans son en-tête. Rendre le défaut reproductible
   dans une cible liée à `GroveEngine::impl` fermerait proprement la boucle.
2. ~~**`IIO` n'expose aucun `unsubscribe`.**~~ ✅ **LEVÉ le 2026-07-28** (`0548ee9`) — token +
   `unsubscribe()` + `ScopedSubscription` RAII. Détail : `docs/design/iio-unsubscribe.md`.
   *Énoncé d'origine, conservé pour le contexte* : ⚠️ La plus grosse des trois, et elle vient du terrain :
   tout objet à durée de vie bornée qui `subscribe` en capturant `this` sur un IIO plus longévif que
   lui laisse un handler orphelin sur un pointeur mort à sa destruction — le dispatch suivant est un
   UAF silencieux en release. Un consommateur **ne peut pas s'en protéger**, l'API pour retirer une
   souscription n'existe pas. Diagnostiqué par Drifterra (un UAF de scène, six rounds de chasse) ;
   la machinerie existe déjà côté manager (`unregisterSubscription`, `topicTree.unregisterSubscriberAll`),
   il manque le maillon `IIO`. Forme visée : un handle RAII rendu par `subscribe`, dont le destructeur
   retire la souscription. Test rouge : « dispatch après destruction d'un abonné → doit être impossible ».
3. **`flipX` sur `render:sprite:update`** — délibérément non supporté (double-flip). À porter dans
   `SpriteInstance` si un consommateur passe son paper-doll en mode retenu.

*Levé le 2026-07-28* : l'onglet actif/inactif spritable (`UITabs::tabFrame`) — voir
`docs/design/ui-sprite-pass.md` § P5.

---

## 3. Ce que ces deux jours ont appris (et qui vaut au-delà d'eux)

### Un test vert jamais vu échouer ne vaut rien
**Cinq fois** un test a menti ou failli mentir, et à chaque fois le sabotage délibéré l'a révélé :

| Cas | Ce qui clochait |
|---|---|
| Chrome de fenêtre | rouge **accidentel** — le test s'abonnait à un topic inexistant, il ne captait rien |
| Police TTF | passait **aussi** avec le 8×8 : je mesurais l'encre du dernier glyphe, pas les avances |
| Graisse | passait aussi — l'avance du `M` de Roboto Bold égale celle du Regular, c'est l'encre qui sépare |
| Ellipsis | deux erreurs d'arithmétique **dans mes propres cas de test** |
| Ancrage des glyphes | le verrou n'existait pas ; le bug dormait, invisible en monospace |

**Réflexe qui paie** : casser volontairement le code et vérifier que le test tombe — *et qu'il tombe
pour la bonne raison*.

### Mesurer avant d'asserter
Trois discriminants « évidents » se sont révélés non discriminants (extent horizontal pour la graisse,
encre pour la proportionnalité, `cellCount < cellsFit` pour le cull). À chaque fois, mesurer d'abord et
choisir le seuil ensuite a donné un test à la fois juste et robuste.

### Rendre le bug rapide et déterministe AVANT de le chercher
La corruption de tas : 4 s et 3,3 % → 1 s et 100 %. gdb, inutilisable au premier taux, est devenu
décisif au second. Puis des **coupes différentielles** une par une, pas de la lecture de code.
Les deux meilleurs indices étaient contre-intuitifs : *supprimer* le souscripteur **aggravait**, et un
seul message suffisait.

### Consigner ses propres erreurs plutôt que les effacer
Quatre conclusions ont dû être corrigées en route : « localisé dans TEST 6 », la frontière de tas
inter-DLL, la pièce scrollbar partagée, et les métriques `stbtt_GetPackedQuad`. Toutes figurent dans
les docs **barrées avec la mesure qui les infirme** — parce qu'une hypothèse plausible et fausse, si on
l'efface, sera reprise par le prochain.

### Un bug latent peut attendre son révélateur
L'ancrage des glyphes était faux depuis toujours : `SpriteInstance.x/y` est un **centre**, `TextPass` y
écrivait un **coin**. Avec une police monospace, décalage constant sur toute la chaîne — invisible. La
police proportionnelle l'a rendu criant. Corollaire : quand un changement « révèle » un défaut, se
demander s'il l'a **créé** ou seulement **exposé**.
