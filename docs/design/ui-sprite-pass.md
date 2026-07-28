# UI — passe « spriter le chrome » (9-slice généralisé)

> **Statut** : plan, **pas implémenté**. Écrit le 2026-07-27.
> **Objet** : étendre au reste de l'UI le traitement art déjà appliqué aux **boutons** et aux
> **fenêtres** (bloc `frame:{asset,srcW,srcH,inset|left/right/top/bottom}` → `render:nineslice`).
> **Contrainte transverse** : additif. Aucun `frame:` publié ⇒ look plat actuel, inchangé.
> Contexte de la première livraison : `docs/design/ui-nineslice.md` (shippé comme dette assumée).

## 1. Le constat — le blocage n'est pas « quel widget », c'est la duplication

Le support 9-slice est aujourd'hui copié-collé **en trois endroits par widget** :

| Où | Quoi |
|---|---|
| `Core/UITree.cpp` | le bloc de parse `frame:` — **dupliqué mot pour mot** (l.80 fenêtre, l.307 bouton) |
| `Widgets/X.h` | les champs `frameAsset/frameSrcW/frameSrcH/frameL/R/T/B` + le case `setProp("frameAsset")` |
| `Widgets/X.cpp` | `registerEntry()` + l'appel `updateNineSlice(...)` + la branche « sinon, look plat » + le repli des entrées plates |

Étendre tel quel à 8 widgets = **24 sites de copier-coller** pour un seul concept. C'est
frontalement contre « Modularité & Maintenabilité — prime absolu ». **La première tâche n'est donc pas
de spriter un widget, c'est d'extraire la pièce partagée.** Ensuite, ajouter `frame:` à un widget
coûte ~3 lignes — et la largeur devient quasi gratuite, seul l'**art** coûte encore.

### Deux pièces à extraire, pas une

1. **`UIFrame`** — le chrome 9-slice : les 7 champs, le parse, l'émission, le repli.
2. **La scrollbar (rail + pouce)** — ⚠️ **ce point du plan était surestimé, corrigé à l'implémentation
   (P0b)**. La duplication *mot pour mot* est **à l'intérieur de `UIList`** (ses deux chemins de rendu,
   l.432/434 et l.530/532) — réelle et extraite. **Entre les deux widgets, en revanche, la ressemblance
   est superficielle** : `UIScrollPanel` porte 4 champs de texture + `scrollbarHoverColor` +
   `scrollbarBgColor`, `UIList` n'a que `scrollbarTrackColor`. Une struct commune serait soit un plus
   petit dénominateur (on perdrait les textures du panel), soit un adaptateur par widget plus coûteux
   que les ~6 lignes gagnées. **Pièce inter-widgets reportée à P2**, où donner à `UIList` un rail/pouce
   spritable lui donnera une forme dictée par un besoin réel au lieu d'une généralisation spéculative.

## 2. Priorité — corrigée par l'usage, pas par le nombre de rects

Premier classement fait au nombre de primitives plates : **mauvaise métrique**. L'impact réel est
*rects × fréquence d'emploi*. Emploi mesuré sur les layouts du repo (`assets/ui/*.json`) :

| Widget | rects plats | usages | commentaire |
|---|---|---|---|
| **panel** | 1 | **123** | ⭐ portée maximale pour le coût minimal : un seul fond à sprites |
| button | 5 | 94 | ✅ déjà fait |
| window | 7 | 16 | ✅ déjà fait (dette : chrome par-dessus le frame) |
| **checkbox** | 3 | 16 | case + coche |
| **progressbar** | 3 | 12 | rail + remplissage |
| **scrollpanel** | 13 | 9 | ⭐ **4 rects de bordure** (haut/bas/gauche/droite) = cas d'école du 9-slice |
| **list** | 16 | 8 | fond, fond de rangée (survol/sélection), en-tête de groupe, scrollbar ×2 |
| textinput | 5 | 3 | champ + caret |
| drawer / tabs / modal | 1 / 3 / 2 | 3 / 2 / 2 | rares |
| radial | 0 | 2 | rien à spriter (déjà en `render:sector`) |

`label` (170) n'a aucun chrome — hors sujet. `vertical`/`horizontal`/`absolute`/`stack`/`grid` sont
des conteneurs de layout, pas du chrome.

## 3. Phases — TDD, test rouge d'abord, arrêtables une par une

### P0 — extraire `UIFrame` + la scrollbar (aucun changement visuel)

- `Widgets/UIFrame.h` : struct (asset, srcW/H, l/r/t/b) + `active()` + `parse(const IDataNode&)` +
  `emit(renderer, id, x, y, w, h, color, layer)` + `collapse(renderer, id)`.
- Rebrancher **button** et **window** dessus ; supprimer les deux blocs de parse jumeaux de `UITree.cpp`.
- Dédupliquer la scrollbar **de `UIList` avec elle-même** (`UIList::renderScrollbar`) — cf. la
  correction au §1 : la mise en commun *inter-widgets* n'est pas justifiée aujourd'hui.

**État : ✅ P0a (UIFrame) et P0b (dédup UIList) livrées.** `UINineSliceE2E` + `NineSliceGpu` passent
sans modification, 54/54 tests UI, 177/177 sur la suite.

**Preuve que c'est invisible** : `UINineSliceE2E` (IT_060) + `NineSliceGpu` (lecture de pixels)
doivent passer **sans modification**. Plus un `UIFrameUnit` sur le parse (pur, headless).

> ⚠️ **Le piège de P0.** L'émission alloue les couches en séquence (`renderer.nextLayer()` appelé
> plusieurs fois par widget, y compris pour *replier* les entrées inutilisées). **Le nombre et
> l'ordre de ces appels sont porteurs de sens** : les factoriser en changeant la séquence décale
> silencieusement le z-order. La factorisation doit préserver la suite d'appels à l'identique — et
> c'est exactement ce que le test GPU en lecture de pixels attrape, pas un test de compilation.

### P1 — `panel` (la portée)

123 usages, un seul rect de fond. `frame:` sur panel ⇒ tout conteneur peut porter un cadre.

### P2 — `scrollpanel` + `list`

- scrollpanel : les **4 rects de bordure → un `render:nineslice`** ; rail/pouce sprités via la pièce P0.
- list : fond, **fond de rangée** (états normal/survol/sélection — c'est ce qu'on voit le plus dans
  un inventaire ou une liste de flotte), en-tête de groupe.
- Au passage : les deux chemins de scrollbar de `UIList` se rejoignent sur la pièce partagée.

### P3 — `checkbox` + `progressbar`

Utilisés (16 et 12) et entièrement plats. Case+coche, rail+remplissage.

### P4 — la traîne : `textinput`, `tabs`, `modal`, `drawer` ✅ FAIT

La promesse de P0 s'est vérifiée : les quatre ont été câblés d'un bloc, chacun en un membre + un
parse + un emit. **Le choix de la surface a demandé plus de réflexion que le code** :
- `textinput` → le **champ**, et le cadre remplace AUSSI la bande de bordure (c'est ce qu'un
  nine-patch exprime nativement) ;
- `tabs` → le **fond de contenu** seulement. L'onglet actif/inactif est la même forme que
  `UIList::rowFrame` et mérite sa propre tranche plutôt que d'être glissé ici — **livré depuis, voir
  P5** ;
- `modal` → le **dialogue**, jamais le voile : étirer de l'art sur un fond assombri plein écran
  n'a aucun sens ;
- `drawer` → le panneau coulissant.

### P5 — l'onglet lui-même : `UITabs::tabFrame` ✅ FAIT

La tranche que P4 avait explicitement renvoyée. Un seul `tabFrame` habille **chaque onglet** de la
bande, et c'est la **teinte** qui porte l'état : `activeTabColor` / `inactiveTabColor` deviennent le
tint du nine-patch, exactement comme `UIList::rowFrame` chevauche la couleur sélection/survol/zébrure
et comme le cadre d'un bouton chevauche sa couleur d'état.

**Un seul asset pour les deux états** — pas de second fichier d'art à créer puis à garder synchrone,
et un changement d'onglet se contente de re-teinter. C'est le même arbitrage que partout ailleurs
dans cette passe : l'art porte la forme, la couleur porte l'état.

Le piège du câblage n'était pas le cadre mais le **compte** : le nombre d'onglets vient des **pages
enfants**, pas du tableau `tabs` de libellés — un onglet sans page derrière n'est pas dessiné. Le
premier layout de test, qui n'avait que des libellés, produisait zéro onglet.

Vérifié en cassant : la teinte forcée à `inactiveTabColor` fait tomber `IT_060` sur exactement
l'assertion de couleur (`0x2c3540ff` au lieu de `0x3a6ea5ff`). Une géométrie seule aurait été verte
tout en perdant la seule chose qu'une barre d'onglets doit montrer — **lequel est actif**.

**État de la passe : P0 → P5 livrées.** 190/190.

**Chaque phase livre un E2E qui clique réellement** (précédent : `UINineSliceE2E`/IT_060) + une
lecture de pixels GPU là où la continuité de bordure est l'enjeu. Sans ça le verdict reste
« non vérifié » — cf. la règle UI de `CLAUDE.md`.

## 4. Décisions prises (arbitrages non renvoyés)

1. **P0 livrée et vérifiée seule**, avant tout changement visuel. Un refacto invisible dont la preuve
   est « les tests existants passent inchangés » se juge mieux isolé. Tu peux arrêter après n'importe
   quelle phase.
2. **Périmètre guidé par la preuve d'usage**, pas l'exhaustivité : P1-P3 couvrent ce que les layouts
   emploient réellement ; P4 reçoit la capacité sans art. **`radial` est exclu** (aucun rect, il passe
   déjà par `render:sector`) — le spriter serait du travail mort.
3. **Art = cadres générés SVG→PNG**, comme la démo 9-slice existante. Le vrai art est du **contenu**,
   pas du moteur (même posture que les stems audio : la logique est livrée, les assets viennent après).

## 5. Dette adjacente rencontrée

- **`UIButton::render()` loggue à chaque frame** (`spdlog::info` × 2, borné aux 10 premiers boutons)
  — du debug oublié dans le chemin de rendu, à l'intérieur même du bloc que P0 réécrit. Proposition :
  suppression en P0 (on y est, et ça ne survit pas à une revue).
- La dette déjà notée sur le 9-slice (**chrome de fenêtre par-dessus le frame**, pas de débordement
  de texte, police bitmap 8×8) tombe naturellement dans P0-P2.

## 6. Reste ouvert

- **Consommateurs** : Drifterra et DAOS ont leurs propres layouts. P1 (panel) change l'apparence de
  tout panel **qui déclare un `frame:`** — donc opt-in strict, aucun risque de régression silencieuse
  chez eux. Rien à relayer avant P1.
- Chiffrage volontairement absent : il dépend de ce que P0 révèle sur la séquence de couches. À
  poser à la fin de P0, sur du réel.
