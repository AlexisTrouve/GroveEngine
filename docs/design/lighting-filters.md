# Plan F — les filtres (transmission colorée)

> **Statut** : ✅ **F1 + F2 + F3 LIVRÉS le 2026-07-29.** Mesure : derrière un vitrail rouge,
> `r=143 b=39` ; du même côté du cercle, sans verre, `r=159 b=159`. Verrouillé par
> `TransmittanceUnit [filter]` + `SceneCollectorTest [filter]` + `PipelineHeadless [filter]` +
> `LightingGpu [filter]`. Suite complète 194/194.
> **Socle** : [table de transmittance polaire](lighting-transmittance-core.md) — **lire d'abord**.
> **Dépend de** : [plan W](lighting-walls.md), dont il généralise la matière.

## 1. Ce que ça donne

Une surface qui laisse passer la lumière **en la modifiant** : un vitrail qui projette du rouge au
sol, de l'eau qui verdit ce qu'elle recouvre, une visière teintée. La lumière traverse, mais elle
n'est plus la même de l'autre côté.

## 2. Ce que ça ajoute au socle : presque rien, et c'est le sujet

Un mur écrit `(0,0,0)` dans la carte de transmittance. **Un filtre écrit une couleur.** Le socle ne
change pas d'une ligne : le même produit préfixe qui annule tout derrière un mur teinte tout
derrière un vitrail.

C'est la raison d'être du socle. Sans lui, les filtres auraient exigé de jeter la structure du plan W
— un shadow map 1D stocke une distance, et une distance ne peut pas porter une couleur.

## 3. Une propriété qui économise un chantier entier

**Le produit est commutatif.** Traverser le verre rouge puis le bleu donne exactement le même
résultat que l'inverse.

Conséquence directe : **aucun tri à faire**. Pas d'ordre de profondeur entre occulteurs, pas de
structure triée par lampe, pas de « quel filtre est devant l'autre ». C'est un cas rare où la
physique du modèle supprime du code au lieu d'en ajouter — et ça mérite d'être écrit noir sur blanc,
parce que le réflexe serait de commencer par construire ce tri.

### ⚠️ Correction — le code ne l'avait PAS, et personne ne l'aurait vu

*Constaté en implémentant F1.*

Ce paragraphe décrit le **modèle**, et il est juste. Mais la passe d'occultation livrée par le plan W
écrivait ses rectangles en mode **opaque** : deux quads superposés ne se multipliaient pas, le
dernier dessiné **écrasait** l'autre.

Sans conséquence tant qu'il n'y avait que des murs — noir sur noir donne noir quel que soit le
gagnant. Le défaut n'existait donc pas encore, il **attendait** le premier filtre. Et sous cette
forme il se serait manifesté comme « l'ordre de publication change le résultat », c'est-à-dire
exactement le problème d'ordre de profondeur dont ce paragraphe se félicite de ne pas avoir besoin.

Corrigé en passant la passe en **blend multiplicatif**, ce qui rend murs et filtres identiques d'un
point de vue mécanique. Le mur est resté vérifié au pixel après le changement (`0 × x = 0`).

**Leçon transposable** : une propriété vraie du modèle n'est pas automatiquement vraie du code, et
tant qu'un seul cas dégénéré l'exerce, rien ne le révèle. Le premier cas non dégénéré est le moment
où il faut relire l'implémentation, pas seulement l'étendre.

## 4. Surface d'écriture

| Topic | Charge | Notes |
|---|---|---|
| `render:filter` | `{x, y, w, h, color, opacity?}` | rectangle filtrant, éphémère |
| `render:filter:add` / `:update` / `:remove` | `{renderId, …}` | retenu — un vitrail ne bouge pas |

`color` est la **teinte transmise**, pas la couleur du verre vu de face — ce sont deux choses
différentes et les confondre est l'erreur classique. Un verre qui *paraît* rouge transmet du rouge,
donc ici les deux coïncident ; un verre qui paraît noir mais transmet du rouge existe aussi.

### ⚠️ La question que ce plan ne tranchait pas : teinte totale ou teinte par unité ?

*Tranché en implémentant F1 — le plan disait « la teinte transmise » sans dire sur quelle longueur.*

La carte stocke du **par unité de longueur**, et ce n'est pas négociable : elle est partagée avec le
brouillard, dont tout l'intérêt est qu'un trajet plus long absorbe davantage.

Mais un auteur qui écrit `color: rouge` sur un vitrail attend du rouge derrière. Lui servir la
sémantique par-unité l'obligerait à taper `0.9703` pour obtenir un rouge à `0.3` sur un panneau de
40 unités — et toute valeur tapée d'instinct sortirait en **mur opaque**. L'écart entre les deux
lectures n'est pas cosmétique, il rend la surface d'écriture inutilisable.

**Retenu** : `color` est la teinte après **une traversée perpendiculaire de l'axe mince** du panneau,
`min(w,h)`. Le moteur convertit (`grove::light::perUnitForTint`, oracle testé). Traverser en biais ou
en longueur teinte davantage — c'est la géométrie qui parle, et c'est physiquement juste.

Le choix de `min(w,h)` porte une hypothèse : **une vitre se traverse par son côté étroit**. Vraie
pour une fenêtre (200×10) comme pour une flaque vue de dessus (50×50). Si un cas la contredit, un
champ `thickness` explicite est l'extension naturelle — pas rajouté d'emblée, faute de besoin.

`opacity` (défaut 1) module entre « aucun effet » et « pleine teinte », comme le modèle des moteurs
3D ([Unreal — Colored Translucent Shadows](https://dev.epicgames.com/documentation/unreal-engine/using-colored-translucent-shadows-in-unreal-engine)) :
la part de lumière transmise dépend de l'opacité du matériau.

⚠️ **Un filtre est-il aussi un occulteur ?** Non par défaut : il teinte la lumière **sans** être
opaque. Un vitrail qui doit à la fois teindre ET assombrir se déclare avec une teinte sombre — le
modèle n'a pas besoin des deux notions.

## 5. Découpage

| Tranche | Contenu | Preuve |
|---|---|---|
| **F1** ✅ | `render:filter` éphémère → carte, teinte accumulée | ✅ livré. headless : la teinte atterrit ; oracle du produit sur deux filtres superposés |
| **F2** ✅ | la teinte à l'écran | ✅ livré. `[gpu]` : derrière un filtre rouge `r=143 b=39`, sans verre `r=159 b=159` — à distance égale de la lampe |
| **F3** ✅ | mode retenu | ✅ livré. headless : persistance, fusion partielle, suppression — **et la reconversion au redimensionnement** (voir ci-dessous) |

### Le piège de F3, que ce plan n'avait pas vu

Le mode retenu des murs stocke le rectangle tel quel. Pour un filtre ça ne suffit pas : le paquet
transporte la valeur **convertie**, et la conversion dépend de l'épaisseur.

Un `:update {renderId, w:16}` sur une vitre de 4 unités garderait donc une valeur par unité calculée
pour l'ancienne épaisseur — la fenêtre élargie s'assombrirait d'une **puissance quatre** (0.2 → 0.0016)
sans qu'aucun message ne dise pourquoi. Le mode retenu stocke donc la teinte de l'**auteur** et
re-dérive à chaque frame.

C'est une conséquence directe du choix de §4 (teinte totale plutôt que par unité) : déplacer la
conversion vers l'auteur déplace aussi la charge de la maintenir cohérente.

### Les 16 niveaux que perd le rouge en F2

Mesuré : `r=143` derrière le verre contre `r=159` à côté, alors que le rouge est censé traverser
**intact** (`perUnit = 1.0`). Ce n'est pas une absorption résiduelle : les deux sondes sont à 27,5 et
28,5 unités de la lampe (décalage du demi-pixel), et l'atténuation `(1−d/r)²` rend exactement 159 et
143. Vérifié au calcul avant d'accepter le vert.

Réflexe : quand une mesure s'écarte du modèle, chiffrer l'écart **avant** de le déclarer négligeable.
Ici il s'expliquait entièrement ; s'il ne s'était pas expliqué, il y avait un bug.

### L'assertion de F2 mérite qu'on s'y arrête

Comparer une luminance globale ne prouverait **rien** : un filtre rouge assombrit, et un simple mur
semi-opaque assombrirait pareil. Ce qui distingue une teinte d'une atténuation, c'est que les canaux
**divergent** — le rouge tient, le bleu tombe.

Donc l'assertion porte sur le **rapport entre canaux**, pas sur leur somme. Un test sur la luminance
serait vert avec un filtre entièrement gris, c'est-à-dire en ne prouvant pas la seule chose que ce
chantier apporte.

Et comme en W2 : les deux sondes sur le **même cercle** autour de la lampe.

## 6. Risques

1. **Le filtre teinte la lumière, pas la vue.** Regarder *à travers* un vitrail ne teintera pas ce
   qu'on voit — seule la lumière qui le traverse est affectée. C'est une limite qui surprendra, et
   elle doit être dans la doc consommateur avant le premier ticket.
2. **Les couleurs sombres mangent tout vite.** Le produit est multiplicatif : trois filtres à 0.3
   laissent 2,7 % de lumière. Attendu physiquement, souvent trop brutal en jeu. Prévoir que
   `opacity` serve de garde-fou d'auteur.
3. ~~**Précision de la table.**~~ ✅ **Mesuré, et le risque était mal posé.** La carte est en RGBA8,
   mais la valeur écrite est **constante sur tout le panneau** — il n'y a donc rien à bander *à
   l'intérieur* d'un filtre. Le vrai défaut est ailleurs : la valeur par unité se tasse vers 1 quand
   le panneau épaissit, si bien qu'au-delà d'une centaine d'unités le quantum 8 bits décale la teinte
   obtenue d'une fraction visible. Uniformément, donc invisible comme artefact et seulement lisible
   comme « la couleur n'est pas tout à fait celle demandée ». Documenté côté consommateur ; l'échappatoire
   si le besoin apparaît est une carte d'occultation en RGBA16F, pas un changement de modèle.

## 7. Hors périmètre

- **Réfraction** (la lumière qui change de direction). Ici elle ne fait que changer de couleur.
- **Filtres texturés** — un motif de vitrail au lieu d'un rect uni. C'est un masque de projection,
  et c'est la question laissée ouverte au cadrage : « filtre » a été compris comme *transmission
  colorée*, pas comme *pochoir*. Si le besoin est le pochoir, c'est un quatrième plan et pas une
  extension de celui-ci.

## 8. Sources

- [Using Colored Translucent Shadows — Unreal](https://dev.epicgames.com/documentation/unreal-engine/using-colored-translucent-shadows-in-unreal-engine) — la lumière transmise vaut la couleur du matériau pondérée par son opacité ; opacité 0 = rien de transmis, 1 = totalement opaque.
- [Colored Translucent Shadow for Stained Glass — Epic forums](https://forums.unrealengine.com/t/tutorial-colored-translucent-shadow-for-stained-glass-window/12411) — le montage à deux plans, et pourquoi la teinte se déclare séparément de l'aspect vu de face.
- ⚠️ **Aucune référence 2D trouvée.** La recherche ne rend que du 3D : la transmission colorée en 2D est un terrain peu documenté, ce qui veut dire pas de recette à copier — et pas de piège déjà répertorié non plus.
