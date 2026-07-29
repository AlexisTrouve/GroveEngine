# Plan W — les murs (occultation opaque)

> **Statut** : ✅ **W1 + W2 + W3 LIVRÉS le 2026-07-29.** Mesure : ombre 48 (l'ambiant seul) contre
> 120 éclairé, sondes à distance égale. Verrouillé par `SceneCollectorTest [occluder]` +
> `LightingGpu [occluder]`.
> **Socle** : [table de transmittance polaire](lighting-transmittance-core.md) — **lire d'abord**.
> Ce plan n'y ajoute qu'une chose : de quoi écrire de la matière opaque dans la carte d'occultation.
> **Prérequis livré** : [lumières 2D L1+L2](lighting-2d.md)

## 1. Ce que ça donne

Une lampe cesse de traverser les murs. Derrière un obstacle, la contribution de cette lampe tombe à
zéro — une ombre portée, nette, qui suit la lampe quand elle bouge.

C'est le premier des trois parce que c'est le seul qui se lit d'un coup d'œil : soit l'ombre est là,
soit elle n'y est pas. Les filtres et les atténuateurs demandent de comparer deux nuances.

## 2. Ce que ça ajoute au socle

**Une seule chose** : un occulteur écrit `(0,0,0)` dans la carte de transmittance. Le socle fait le
reste — le produit préfixe s'annule au premier zéro rencontré et reste nul sur toute la suite du
rayon. L'ombre n'est pas un cas particulier du code, c'est une conséquence arithmétique.

## 3. Surface d'écriture

| Topic | Charge | Notes |
|---|---|---|
| `render:occluder` | `{x, y, w, h}` | rectangle opaque, **éphémère** (republié chaque frame) |
| `render:occluder:add` / `:update` / `:remove` | `{renderId, x, y, w, h}` | mode **retenu**, pour la géométrie statique du décor |

**`x, y` = coin haut-gauche** — c'est un rectangle, pas une primitive centrée : la convention
d'ancrage veut que le nom porte l'ancre, et `x,y` est le nom d'un coin
([render-anchor-convention.md](render-anchor-convention.md)).

**Le mode retenu n'est pas une optimisation prématurée ici**, contrairement aux lumières : les murs
d'un niveau ne bougent pas, et les republier chaque frame ferait payer un coût proportionnel à la
taille du décor pour une donnée constante. C'est l'inverse exact du raisonnement qui a rendu les
lumières éphémères — et la raison est la même : suivre ce que la donnée fait réellement.

## 4. Découpage

| Tranche | Contenu | Preuve |
|---|---|---|
| **W1** | `render:occluder` éphémère → carte d'occultation | headless : le rect atterrit dans le paquet, ancré au coin ; rien de publié ⇒ tableau nul |
| **W2** | l'ombre à l'écran | `[gpu]` : un point **derrière** le mur est nettement plus sombre qu'un point à côté, à distance **égale** de la lampe |
| **W3** ✅ | mode retenu (`:add`/`:update`/`:remove`) | ✅ livré. `:update` fusionne partiellement — une porte coulissante bouge sans redire son extent, et un update qui remettrait les champs omis à zéro SUPPRIMERAIT le mur en ayant l'air de le déplacer |

### L'assertion qui fait tout le travail en W2

**À distance égale de la lampe.** C'est le point à ne pas rater : comparer un pixel derrière le mur
à un pixel « loin » prouverait seulement que l'atténuation radiale fonctionne — ce que L2 fait déjà.
Les deux sondes doivent être **sur le même cercle** autour de la lampe, l'une occultée et l'autre
non. Sans ça, le test serait vert avec une occultation entièrement absente.

C'est le même piège que la caméra non déplacée du test de L2 : un discriminant placé là où les deux
hypothèses coïncident.

## 5. Risques

1. **Occulteurs hors champ** — dépend de l'arbitrage §5 du socle. Avec une carte partagée en espace
   écran, un mur qui sort du cadre cesse d'occulter, et son ombre disparaît **alors que la lampe est
   toujours visible**. La marge autour du viewport repousse le défaut sans l'éliminer.
2. ~~**Résolution angulaire**~~ ⚠️ **Le risque était réel, mais pas là où ce plan le plaçait — et il
   est passé en production.** Il annonçait le crénelage comme une propriété de la *table polaire*
   (N angles), structure qui n'a finalement pas été livrée : C2 marche directement. Le crénelage est
   quand même arrivé, par un autre chemin — la marche prenait un nombre **fixe** de pas (16) entre la
   lampe et le fragment, donc un pas valait `distance/16`, et le bord d'ombre sortait en escalier dont
   la marche était ce pas (~19 px sous une lampe de 340). **Repéré à l'œil par Alexi sur les captures
   du blog, pas par un test.**

   ✅ **Corrigé le 2026-07-29, en DEUX temps** — et le deuxième est le vrai.

   **(a) Le pas constant en pixels.** Écart à la droite : 9,4 px → 2,9 px. Nécessaire, insuffisant :
   Alexi a regardé et dit « c'est mieux mais c'est toujours un escalier ». Il avait raison.

   **(b) Le tramage, parce qu'un occulteur opaque rend une réponse BINAIRE.** Un mur écrit 0 : dès
   qu'**un seul** échantillon tombe dedans, le produit s'annule. Tous les pixels situés à moins d'un
   pas de la frontière répondent donc **exactement pareil**, et aucun raffinement de
   l'échantillonnage ne change ça — un pas plus fin ne fait que raccourcir la marche de l'escalier, et
   la paye linéairement. Trois tentatives ont buté là-dessus :
   - pas plus fin → palier plus court, escalier toujours net ;
   - carte d'occultation filtrée (demi-résolution) → le dégradé ne survit pas au `pow(prod, stepLen)`,
     qui lit un texel à moitié couvert comme une **densité** alors que c'est une **couverture** ;
   - premier tramage `mod(x + 3y, 8)` → **c'était un dégradé, pas un tramage** : 1/8 d'écart entre
     pixels voisins, donc ils marchaient pareil et il n'y avait rien à moyenner.

   Ce qui marche : un tramage où les pixels **voisins** reçoivent des décalages **opposés**
   (`mod(5x + 3y, 8)`, qui cycle 0-5-2-7-4-1-6-3), plus une résolution 5 taps de la cible de lumière
   dans le composite. Le tramage étale la frontière binaire sur les pixels, le composite la remoyenne
   en rampe. **Écart : 3,5 px → 1,6 px, palier max 11 px → 3 px**, et à l'œil une vraie ligne
   anticrénelée.

   Verrouillé par `LightingGpu [march]` : ajustement d'une droite sur le bord mesuré, seuil **2,5 px
   choisi depuis la mesure des deux états** (1,6 avec, 3,5 sans), plus une assertion sur l'existence
   de valeurs **intermédiaires** au bord.

   **Ce que ça apprend, en trois points :**

   1. Le plan avait *raison sur le symptôme et faux sur la cause* — il décrivait la cause d'une
      structure qu'on n'a pas construite. Un risque énoncé comme propriété d'une implémentation
      précise cesse d'être surveillé dès qu'on change d'implémentation, alors que le symptôme, lui,
      était générique. **Formuler les risques par le symptôme observable.**
   2. **Une réponse binaire ne s'adoucit pas en amont.** Tant que le verdict par pixel est 0 ou 1,
      raffiner ou filtrer l'entrée ne fait que déplacer la falaise. Il faut soit supersampler, soit
      étaler la décision sur les pixels voisins et la remoyenner — ce qu'on a fait.
   3. **Ma métrique récompensait le bruit.** La longueur de palier chute quand le bord devient
      bruité, donc le tramage seul « améliorait » le chiffre en dégradant l'image. C'est l'œil qui a
      tranché entre grain et rampe, pas le nombre. Une métrique de proxy doit être validée contre ce
      qu'elle prétend mesurer avant qu'on lui fasse confiance pour arbitrer.
3. **Auto-occultation** — un sprite qui est à la fois éclairé et occulteur s'ombre lui-même sur son
   bord côté lampe. Attendu physiquement, souvent laid en 2D vu de dessus. Prévoir de pouvoir
   déclarer un occulteur **sans** qu'il s'auto-ombre, ou accepter et documenter.

## 6. Hors périmètre

- **Occulteurs non rectangulaires** (polygones, masque par sprite). Le rect couvre le décor de
  tuiles ; une forme arbitraire est une tranche à part.
- **Ombres douces** au sens pénombre physique (lampe de rayon non nul). Le flou du risque 2 est un
  anticrénelage, pas une pénombre — ne pas confondre les deux dans la doc consommateur.
- **Occultation de la VUE** (le joueur ne voit pas derrière le mur). C'est un système de visibilité,
  pas d'éclairage ; ça vit côté gameplay, pas dans le renderer. Question restée ouverte lors du
  cadrage — à trancher si le besoin existe.

## 7. Sources

- [2D Pixel Perfect Shadows — mattdesl](https://github.com/mattdesl/lwjgl-basics/wiki/2D-Pixel-Perfect-Shadows) — les trois passes, le flou angulaire contre le crénelage du bord, et la limite mesurée ~20 lampes.
- [Fast 2D shadows in Unity using 1D shadow mapping](https://www.gamedeveloper.com/programming/fast-2d-shadows-in-unity-using-1d-shadow-mapping) — la transformation polaire pas à pas.
