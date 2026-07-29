# Plan F — les filtres (transmission colorée)

> **Statut** : 📋 plan, rien d'implémenté.
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

## 4. Surface d'écriture

| Topic | Charge | Notes |
|---|---|---|
| `render:filter` | `{x, y, w, h, color, opacity?}` | rectangle filtrant, éphémère |
| `render:filter:add` / `:update` / `:remove` | `{renderId, …}` | retenu — un vitrail ne bouge pas |

`color` est la **teinte transmise**, pas la couleur du verre vu de face — ce sont deux choses
différentes et les confondre est l'erreur classique. Un verre qui *paraît* rouge transmet du rouge,
donc ici les deux coïncident ; un verre qui paraît noir mais transmet du rouge existe aussi.

`opacity` (défaut 1) module entre « aucun effet » et « pleine teinte », comme le modèle des moteurs
3D ([Unreal — Colored Translucent Shadows](https://dev.epicgames.com/documentation/unreal-engine/using-colored-translucent-shadows-in-unreal-engine)) :
la part de lumière transmise dépend de l'opacité du matériau.

⚠️ **Un filtre est-il aussi un occulteur ?** Non par défaut : il teinte la lumière **sans** être
opaque. Un vitrail qui doit à la fois teindre ET assombrir se déclare avec une teinte sombre — le
modèle n'a pas besoin des deux notions.

## 5. Découpage

| Tranche | Contenu | Preuve |
|---|---|---|
| **F1** | `render:filter` éphémère → carte, teinte accumulée | headless : la teinte atterrit ; oracle du produit sur deux filtres superposés |
| **F2** | la teinte à l'écran | `[gpu]` : derrière un filtre rouge, le canal **rouge** survit et le **bleu** s'effondre — à distance égale de la lampe |
| **F3** | mode retenu | headless : persistance + suppression |

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
3. **Précision de la table.** En RGBA8 la transmittance accumulée quantifie vite dans les valeurs
   basses ; le socle stocke déjà en flottant si la cible le permet, sinon un filtre sombre bandera.

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
