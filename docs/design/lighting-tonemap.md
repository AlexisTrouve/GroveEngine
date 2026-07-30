# Plan T — le tonemapping (post-traitement, tranche 2)

> **Statut** : plan écrit le 2026-07-30, avant la première ligne de code.
> **Socle** : la **passe de présentation** introduite par [le bloom](lighting-bloom.md) — elle existe
> explicitement pour cette queue de post-traitement, ce n'est pas un détournement.
> **Suite après** : fondus, colorimétrie. Même passe.

---

## 1. Le problème, et c'est une dette assumée depuis L1

Les cibles sont en **RGBA16F** par un arbitrage explicite d'Alexi
([lighting-2d.md §8](lighting-2d.md)), pour une raison écrite noir sur blanc à l'époque :

> « Avec un buffer écrêté à 1.0, trois lampes superposées et une lampe seule donnent le même blanc —
> il n'y a plus rien à extraire. »

Le bloom consomme bien ce sur-brillant. Mais **l'image finale l'écrête quand même** : la présentation
écrit dans un backbuffer 8 bits, donc tout ce qui dépasse 1 devient le même blanc. On a payé deux fois
la bande passante pour conserver une information qu'on jette à la dernière ligne.

Concrètement, mesurable : une lampe d'intensité 2 et une lampe d'intensité 8 rendent **exactement la
même valeur, 255**. C'est la situation que l'arbitrage RGBA16F voulait éviter, déplacée d'un cran.

Symptôme visible : un cœur de lampe devient une **tache blanche** au lieu de rester *lumineux et
coloré* — la plainte exacte rencontrée en réglant la planche 19 du blog, où une intensité 5 saturait un
tiers de l'image.

## 2. Ce que ça livre

```
render:tonemap { mode, exposure? }
```

| Champ | Défaut | Sens |
|---|---|---|
| `mode` | **`"none"` = ÉTEINT** | `"reinhard"` ou `"aces"` |
| `exposure` | 1.0 | multiplie la scène **avant** la courbe |

**⚠️ Un réglage SÉPARÉ du bloom, et c'est délibéré.** Le tonemapping *change l'image* : l'agrafer au
bloom ferait qu'activer une lueur modifierait au passage l'exposition de tout le rendu — une surprise
que personne n'a demandée. Les deux réglages partagent la plomberie, pas l'interrupteur.

### Pourquoi DEUX courbes et pas une

Un tonemap est un **look**, pas une correction. En figer un seul dans le moteur, c'est prendre à la
place du jeu une décision artistique qui se rediscutera — exactement le cas où la doctrine maison
préfère un réglage *data-driven* à un choix gravé.

| Mode | Formule | Ce que ça donne |
|---|---|---|
| `reinhard` | `x / (1 + x)` | doux, prévisible, ne franchit jamais 1. Comprime les hautes lumières sans toucher au contraste des tons moyens. |
| `aces` | approximation polynomiale de Narkowicz | **filmique** : contraste relevé, épaule qui roule vers le blanc. Le look « moderne » par défaut. |

Le coût de la seconde est une branche dans le shader et une fonction dans l'oracle. Ce n'est pas de la
flexibilité gratuite, ce sont deux rendus nommés.

### `exposure` n'est pas un luxe

Sans elle, la courbe est subie : une scène globalement sombre est encore assombrie par la compression,
une scène claire est délavée. `exposure` est le bouton qui place la scène **sur** la courbe. Un
tonemap sans exposition est inutilisable — c'est pour ça qu'elle arrive dans la même tranche et pas
« plus tard ».

## 3. Le contournement à coût nul, une troisième fois

`mode == "none"` (le défaut) **et** `bloom.intensity == 0` ⇒ aucune cible HDR, aucune passe de
présentation, le composite écrit au backbuffer à l'octet près. Rien ne change pour Drifterra, DAOS et
Fractax.

Nouveau cas à traiter, qui n'existait pas avec le bloom seul : **tonemap actif SANS bloom**. La passe
de présentation doit alors exister, mais les cibles de flou non — ce serait deux cibles plein écran
pour une lueur d'intensité nulle. La présentation reçoit donc le **placeholder 1×1 NOIR**, exactement
celui que le correctif de la lampe fantôme vient d'introduire pour le buffer de lumière. Ajouter zéro
est un no-op, et on ne consulte pas une cible que personne n'a écrite.

## 4. Où ça s'applique — et l'ordre compte

```
final = tonemap( (composite + lueur × intensité) × exposure )
```

**Le tonemap vient APRÈS l'ajout de la lueur**, pas avant. Sinon la lueur — qui est justement faite du
sur-brillant — serait ajoutée à une image déjà comprimée dans [0,1] et ressortirait par-dessus 1, donc
réécrêtée. Tonemapper le tout est ce qui fait *participer le halo à l'exposition*, et c'est la
différence entre « un halo blanc collé sur l'image » et « une source lumineuse ».

**Par canal, pas sur la luminance.** Tonemapper la luminance puis rééchelonner le RGB préserve la
saturation des hautes lumières ; par canal, une couleur très saturée roule vers le blanc en saturant.
Le second est ce que fait un film, et c'est ce qu'on veut : un cœur de lampe *doit* blanchir en son
centre. Le premier donnerait des halos fluo. Choix assumé, documenté ici parce qu'il se rediscutera.

## 5. Les tranches

| | Ce qu'elle fait | Verrou |
|---|---|---|
| **T0** | l'oracle : `grove::light::tonemapReinhard` / `tonemapACES` | `TonemapMathUnit` |
| **T1** | `render:tonemap` → `FramePacket::TonemapSettings` (persistant, `none` = éteint) | `SceneCollectorTest [tonemap]` |
| **T2** | la présentation applique la courbe ; activation indépendante du bloom | `LightingGpu [tonemap]` |

## 6. Le discriminant, choisi avant le code

**Deux lampes d'intensités différentes, toutes deux au-dessus du seuil d'écrêtage.**

- Sans tonemap : les deux rendent **255**. Indistinguables — c'est le défaut lui-même.
- Avec tonemap : la plus intense est **mesurablement plus claire**, et **les deux sont sous 255**.

C'est le même argument que celui qui a fait choisir RGBA16F, retourné en test. Et il ne peut pas être
satisfait par accident : une courbe qui se contenterait d'assombrir passerait « les deux sous 255 »
mais échouerait sur « la plus intense reste plus claire » si elle écrasait tout au même niveau.

⚠️ **Piège à éviter dans ce test** : mesurer une lampe d'intensité 0,5 et une d'intensité 0,8 ne
discriminerait RIEN — elles ne sont pas écrêtées, donc elles diffèrent déjà sans tonemap. Le test doit
se placer **au-dessus de 1**, là où l'information est perdue aujourd'hui.

## 7. Risques

1. **Le tonemap assombrit l'image**, par construction : `reinhard(1) = 0,5`. Un jeu qui l'active verra
   sa scène s'assombrir de moitié s'il ne touche pas à `exposure`. Ce n'est pas un bug, c'est ce que
   fait une courbe de compression — mais c'est la première chose qui sera signalée comme telle. À
   documenter en tête du guide consommateur, avec la valeur d'exposition de départ suggérée.
2. **La passe de présentation devient conditionnée par DEUX réglages.** Le chemin « allumé puis
   éteint » doit détacher la vue du composite pour les deux, pas seulement pour le bloom, sinon
   éteindre le tonemap laisse la frame dans une cible que plus personne ne présente — un écran noir.
   C'est le même piège que celui qui a exposé le défaut de `setViewFramebuffer`, et il est maintenant
   connu.
3. **Les constantes d'ACES sont un ajustement empirique** (Narkowicz), pas une dérivation. Elles vont
   dans l'oracle avec leur provenance, et le test vérifie les propriétés (monotone, 0→0, borné), pas
   les chiffres — un test qui re-écrirait les constantes ne prouverait rien d'autre que le copier-coller.
4. **Pas de variante Metal**, comme les autres shaders de post-traitement. Dette déjà nommée.

## 8. Hors périmètre, explicitement

- **Pas de point blanc** (Reinhard étendu). `exposure` + la courbe simple suffisent à cette tranche.
- **Pas de LUT ni de colorimétrie** — tranche suivante, même passe.
- **Pas de correction gamma.** Le pipeline travaille en valeurs linéaires écrites dans un backbuffer
  sRGB géré par bgfx ; y toucher ici changerait toutes les images existantes.
- **Pas d'auto-exposition** (adaptation temporelle). Elle demanderait une réduction de la luminance
  moyenne et un état inter-frame — un chantier à part, et un effet qu'un jeu 2D veut rarement.
