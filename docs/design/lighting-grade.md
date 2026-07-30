# Plan G — la colorimétrie (post-traitement, tranche 4 — la dernière)

> **Statut** : ✅ **LIVRÉ** le 2026-07-30 (Ga → Gc). ⚠️ Cette tranche a trouvé un **bug d'ordre de
> soumission** introduit deux tranches plus tôt — §9.
> **Famille** : [bloom](lighting-bloom.md) → [tonemapping](lighting-tonemap.md) →
> [fondus](lighting-fade.md) → **colorimétrie**. Elle ferme la queue annoncée en plan B.
> **Place** : la **passe de présentation**, après la courbe — contrairement au fondu, et pour une
> raison précise (§3).

---

## 1. Ce que c'est

Une transformation de couleur appliquée à l'**image finie**, pixel par pixel. Pas un éclairage, pas un
effet sur les objets : la scène est déjà rendue, composée et tonemappée — on retouche le résultat.

C'est ce qui permet qu'un même décor soit *un matin froid*, *un souvenir délavé* ou *une alerte rouge*
sans qu'un seul asset change.

```
render:grade { saturation?, contrast?, tint? }
```

| Champ | Défaut | Math | Ce que ça achète |
|---|---|---|---|
| `saturation` | **1 = neutre** | `mix(luma, couleur, s)` | 0 = noir et blanc, >1 = criard. Flashback, vision de mort, monde qui se vide |
| `contrast` | **1 = neutre** | `(c − pivot)·k + pivot` | écarte ou rapproche du gris moyen. Brume laiteuse (bas), scène dure (haut) |
| `tint` | **blanc = neutre** | multiplication RGB | la balance des blancs. Nuit bleutée, couchant ambré |

Les trois neutres ensemble ⇒ **identité exacte**, et c'est le contournement à coût nul (§4).

### La luminance est celle du bloom, pas une nouvelle

`saturation` interpole vers la **luminance Rec. 709**, celle que `grove::light::luminance` calcule déjà
pour le seuil du bloom. Deux conséquences, et la seconde est la vraie raison :

1. une désaturation respecte la perception (le vert pèse plus que le bleu) au lieu de faire une moyenne
   plate qui rendrait un ciel bleu et une prairie verte au même gris ;
2. **il n'existe qu'UNE définition de « la luminance » dans ce moteur.** En écrire une seconde ici,
   même identique, garantirait qu'elles divergent un jour — et la divergence se verrait comme un bug
   de désaturation alors qu'elle serait un bug de duplication.

## 2. ⚠️ Ce que ce plan NE livre PAS : la « luminosité »

Le trio classique d'un étalonneur est *luminosité / contraste / saturation*. **Il n'y aura pas de
bouton de luminosité**, et c'est un refus argumenté et non un oubli.

Il existe déjà : c'est **`exposure`**, du [tonemapping](lighting-tonemap.md) — et il est du **bon côté
de la courbe**. Un gain appliqué *après* la compression ne ferait que saturer plus tôt, en réécrêtant
exactement ce que le tonemapping venait de sauver. On aurait donc deux boutons pour une seule idée,
dont le plus accessible serait le pire — et la garantie de tourner le mauvais un jour sur deux.

**Si on veut une image plus claire, on monte `exposure`.** C'est écrit ici pour que personne n'ajoute
`brightness` « pour faire complet ».

## 3. La place : la présentation, et l'asymétrie avec le fondu

Sur la **passe de présentation**, juste après la courbe de tonemapping. Conséquences, et elles sont
opposées à celles du fondu :

| | colorimétrie | fondu |
|---|---|---|
| touche le HUD | ❌ **non** | ✅ oui |
| exige l'éclairage actif | ✅ oui | ❌ non |

**La colorimétrie doit épargner le HUD.** Un monde désaturé sous une interface qui garde ses couleurs
est le comportement voulu : le HUD est un objet de lecture, pas un élément de la fiction. Le désaturer
avec le monde rendrait un texte d'alerte rouge illisible au moment précis où il compte.

**Le fondu, lui, la recouvre** puisqu'il passe encore après — donc une transition au noir emporte le
monde étalonné *et* l'interface. Les deux comportements sont opposés et chacun est juste pour son
effet ; c'est exactement pourquoi le fondu a eu sa propre passe.

### L'ordre des trois opérations, qui n'est pas commutatif

```
c = tonemap(...)            // déjà fait
c = c × tint                // 1. balance des blancs
c = (c − 0.5) × contrast + 0.5   // 2. contraste autour du gris moyen
c = mix(luma(c), c, saturation)   // 3. saturation
```

**Cet ordre est celui d'un étalonnage réel**, et il n'est pas indifférent : teinter *après* avoir
désaturé donnerait une image monochrome colorée (un virage sépia) au lieu d'une image équilibrée puis
désaturée. Les deux sont des effets légitimes, mais un seul est ce qu'on attend de trois boutons
nommés ainsi.

**Le pivot du contraste est 0,5** et pas 0,18. On opère **après** le tonemapping, donc dans un espace
d'affichage borné à [0,1] où le gris moyen est à 0,5 — pas dans le linéaire de scène où la référence
serait 18 %. Utiliser 0,18 ici assombrirait toute image dont le contraste est relevé, ce qui se lirait
comme « le contraste assombrit », un défaut incompréhensible sans cette ligne.

## 4. Le contournement à coût nul, une cinquième fois

Les trois valeurs neutres ⇒ la colorimétrie est **inerte** : le shader ne l'applique pas, et surtout
elle **n'active pas** la passe de présentation. Un jeu qui ne publie rien, ou qui publie
`render:grade {saturation: 1}`, paie exactement ce qu'il payait avant.

⚠️ La comparaison aux neutres est **exacte** (pas d'epsilon). Deux raisons : les valeurs viennent de
défauts JSON, donc elles valent exactement `1.0f` quand elles sont absentes ; et un jeu qui publie
délibérément un neutre demande explicitement « pas d'étalonnage », ce qui doit rester gratuit.

## 5. Pourquoi un oracle ici, alors que le fondu n'en a pas eu

[Le fondu](lighting-fade.md) §4 a explicitement renoncé à sa pièce pure : un `lerp` n'a rien qu'on
puisse se tromper à écrire. **Ici, si.**

- la **luminance** à réutiliser plutôt qu'à réinventer (§1) ;
- le **pivot** du contraste, un choix qui a une bonne et une mauvaise réponse (§3) ;
- l'**ordre** des trois opérations, non commutatif (§3) ;
- et la propriété qui se vérifie exactement : `saturation 0` doit rendre un **gris dont la luminance
  est celle de l'original** — pas n'importe quel gris.

Le motif n'est donc pas suivi par habitude ni abandonné par flemme : il s'applique quand il y a une
décision à verrouiller.

## 6. Les tranches

| | Ce qu'elle fait | Verrou |
|---|---|---|
| **Ga** | l'oracle : `grove::light::gradeColor` (teinte → contraste → saturation) | `GradeMathUnit` |
| **Gb** | `render:grade` → `FramePacket::GradeSettings` (persistant, neutres = éteint) | `SceneCollectorTest [grade]` |
| **Gc** | la présentation applique l'étalonnage ; il active la passe comme le tonemapping | `LightingGpu [grade]` |

## 7. Le discriminant, choisi avant le code

**Une scène de DEUX couleurs franches**, un carré rouge et un carré bleu, plus un carré HUD vert.

1. **`saturation 0`** → les deux carrés deviennent gris, et **de gris DIFFÉRENTS** : le rouge Rec. 709
   pèse 0,2126 et le bleu 0,0722, donc le gris du rouge doit être ~3× plus clair que celui du bleu.
   ⚠️ **C'est ce qui discrimine une vraie désaturation d'une moyenne `(r+g+b)/3`** — laquelle rendrait
   les deux au même gris. Une simple assertion « c'est devenu gris » passerait avec la mauvaise formule.
2. **`tint` bleu** → le canal rouge de la scène chute, le bleu non.
3. **`contrast 2`** → un pixel sous 0,5 s'assombrit ET un pixel au-dessus s'éclaircit. Une assertion sur
   un seul des deux passerait avec un simple gain, qui n'est pas un contraste.
4. **⚠️ Le HUD ne bouge PAS** sous `saturation 0`. C'est l'assertion qui verrouille la place choisie :
   une colorimétrie posée sur la passe du fondu désaturerait aussi l'interface, en passant les trois
   premières mesures.
5. **Neutres ⇒ image identique** à l'octet près, alors qu'aucune passe n'aurait dû être activée.

## 8. Risques

1. **L'activation** : la colorimétrie doit allumer la passe de présentation comme le tonemapping le
   fait, sinon publier `render:grade` sans bloom ni tonemap ne ferait rien — le « chaînon jamais
   câblé », déjà rencontré au plan T et attrapé par sabotage.
2. **Le contraste peut sortir de [0,1]** : `(0 − 0.5)·2 + 0.5 = −0.5`. Il faut borner, sinon un canal
   négatif donne un comportement dépendant du backend. Borner APRÈS les trois opérations et pas entre
   chacune, sinon on écrête un intermédiaire que la suite aurait ramené dans la plage.
3. **`saturation` non bornée en haut** est volontaire (>1 = couleurs criardes, un effet légitime), mais
   elle peut alors dépasser 1 par canal — même remarque que ci-dessus, la borne finale s'en occupe.

## 9. ⚠️ Le bug que cette tranche a trouvé (et qui datait des fondus)

Le test de colorimétrie a échoué sur son assertion n°4 — **le HUD était étalonné alors qu'il devait être
épargné** — en lisant `48` sur le canal vert, soit la couleur d'effacement. Le HUD n'était donc pas là
du tout.

**La cause** : `bgfx::setViewOrder(0, count, order)` remplit une table *position → vue*. Passer 7
entrées ne remappe que les positions 0 à 6 ; les positions 7, 8 et 9 gardent leurs **valeurs par
défaut** — 7, 8, 9. Les vues 8 (présentation) et 9 (fondu) étaient donc listées **deux fois**, et leur
seconde soumission tombait **après** le HUD : la présentation, opaque et plein écran, l'écrasait.

**Le bug datait du plan F2.** Il y avait alors trois listes d'ordre selon la configuration, et deux
étaient trop courtes.

⚠️ **Et le test des fondus ne l'avait pas vu**, pour deux raisons qui se complétaient exactement :

- sa mesure à mi-course (`amount 0.5`, la seule sensible à un double dessin) était sur le chemin **non
  éclairé**, où aucun ordre n'est imposé ;
- sa mesure éclairée était à `amount 1`, où dessiner deux fois est **idempotent**.

Deux angles morts qui, ensemble, laissaient passer un doublement du fondu à mi-course sur un jeu
éclairé.

**Le remède** est plus simple que ce qu'il remplace : **une seule liste, permutation complète de 0 à 9**.
Il ne reste alors aucune position par défaut, donc aucun doublon possible. Les vues sans draw étant
sautées par bgfx, cette liste unique vaut pour les trois configurations — le bloom éteint ne coûte rien
à ses trois vues. Trois branches deviennent une, et la classe de bug disparaît au lieu d'être corrigée
trois fois.

## 10. Hors périmètre

- **Pas de LUT 3D.** C'est la façon professionnelle de faire un étalonnage complet (une table cubique
  échantillonnée), et c'est un chantier à part : format de fichier, chargement, texture 3D, filtrage.
  Trois boutons couvrent l'usage courant d'un jeu 2D ; une LUT couvre l'usage d'un film.
- **Pas de courbes par canal** ni de roues de couleur (lift/gamma/gain séparés pour les ombres, tons
  moyens et hautes lumières). Même raison : c'est une interface d'étalonneur, pas un réglage de jeu.
- **Pas de vignettage ni d'aberration chromatique** — ce sont des effets d'objectif, pas de la
  colorimétrie, et ils dépendent de la position à l'écran.
- **Pas de luminosité** (§2), et ce n'est pas un manque.
