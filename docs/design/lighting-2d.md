# Lumières 2D — plan d'implémentation

> **Statut** : 📋 plan, rien d'implémenté.
> **Suite annoncée** : le post-traitement (bloom, fondus, colorimétrie) — il consommera la même
> plomberie de cibles de rendu. Les choix ci-dessous en tiennent compte.

## 1. L'approche retenue : buffer d'accumulation + composite

Trois candidats, un seul tient debout dans ce moteur.

| Approche | Verdict |
|---|---|
| **Éclairage par sprite** (N lumières en uniformes, calcul dans le fragment shader de chaque passe) | ❌ il faudrait modifier **tous** les shaders (sprite, tilemap, particule, secteur), le nombre de lumières serait borné par la taille du tableau d'uniformes, et le batching « un draw = une texture » n'y survivrait pas |
| **Buffer d'accumulation + composite** | ✅ **retenu** |
| Shadow volumes / raymarching | ❌ pas maintenant — c'est une tranche à part (§6) |

Le principe :

1. les passes monde dessinent dans une **cible hors-écran** (`scene`) au lieu du backbuffer ;
2. chaque lumière est un **quad additif** avec une atténuation radiale, dessiné dans une seconde
   cible (`light`) ;
3. un **quad plein écran** compose : `final = scene.rgb × (ambient + light.rgb)` → backbuffer ;
4. le HUD dessine **après**, sur le backbuffer, **jamais éclairé**.

Ce qui rend cette approche juste ici : elle est **totalement découplée des passes existantes**. Aucun
shader de sprite, tilemap ou particule ne change. Le nombre de lumières n'est borné que par le taux
de remplissage. Et l'étape (3) est exactement le point d'accroche dont le post-traitement aura besoin.

## 2. Le vrai obstacle : l'ordre des vues

**C'est le point qui décide de l'architecture, et il n'était pas évident.**

Le RHI n'expose **aucun `setViewOrder`** : l'ordre de soumission est donc celui des identifiants de
vue (comportement bgfx par défaut). Or le monde occupe la vue 0 et le HUD la vue 1 — **collées**. Il
n'y a aucune place pour insérer un composite entre les deux.

Deux issues, et la mesure tranche :

- **Renuméroter** (monde 0, lumières 1, composite 2, HUD 3) → la vue 1 est **codée en dur** dans
  `SpritePass`, `TextPass`, `SectorPass` et `BgfxRendererModule`, et verrouillée par `HudViewUnit`.
  On toucherait toutes les passes pour un problème d'ordonnancement.
- **Ajouter `setViewOrder` au RHI** → un appel `bgfx::setViewOrder` d'une ligne, plus le no-op côté
  `MockRHIDevice`. Les identifiants existants ne bougent pas. ✅ **retenu**.

L'ordre de soumission devient : monde → lumières → composite → HUD, quels que soient les ids.

## 3. Coût nul quand personne n'éclaire — non négociable

**Aucune lumière publiée et aucun ambiant réglé ⇒ on ne crée aucune cible, le monde dessine
directement dans le backbuffer, sortie octet pour octet identique à aujourd'hui.**

C'est la règle qui a protégé chaque ajout de ce moteur (cf. `flipX`, `maxWidth`, `frame`), et elle
compte double ici : Drifterra, DAOS et Fractax partagent le moteur et n'ont rien demandé. Une passe
d'éclairage qui coûterait deux cibles plein écran à un jeu non éclairé serait une régression pure.

Corollaire à tester explicitement : la non-régression est un cas de test, pas une intention.

## 4. Surface IIO

| Topic | Charge | Notes |
|---|---|---|
| `render:light` | `{cx, cy, radius, color, intensity}` | éphémère, republié chaque frame — comme `render:sprite`/`render:particle` |
| `render:ambient` | `{color}` | le terme global ; **absent ⇒ blanc (1,1,1)**, donc non éclairé = inchangé |

**`cx, cy` = CENTRE**, sans discussion : c'est une primitive positionnée, la convention d'ancrage du
moteur veut que le nom porte l'ancre (`docs/design/render-anchor-convention.md`). Une lumière avec un
`x,y` coin serait une incohérence de plus à expliquer pendant des années.

**Pas de chemin bulk, délibérément.** Le mur IIO+JSON est à ~5 k primitives/frame ; une scène
éclairée en compte des dizaines. Ajouter un `submitLightBatch` optimiserait un problème qui n'existe
pas. C'est écrit ici pour que personne ne le « corrige » plus tard.

**Éphémère seulement** pour la première tranche. Une lumière de jeu suit presque toujours une entité
qui bouge — la republier chaque frame est le cas normal. Un mode retenu (`render:light:add/update/
remove`) pour les torches statiques est une optimisation à mesurer avant d'écrire, pas à supposer.

## 5. Découpage

| Tranche | Contenu | Preuve |
|---|---|---|
| **L1** | `setViewOrder` au RHI + les cibles + le composite, **ambiant seul** (pas encore de lumières) | rouge : `render:ambient {0.5}` doit assombrir la scène de moitié ; et **aucun ambiant ⇒ sortie inchangée** |
| **L2** | lumières radiales — le cœur | pur : l'atténuation et les bornes du quad, oracle headless. GPU : un pixel dans le rayon s'éclaircit, un pixel hors du rayon est **strictement inchangé** |
| **L3** | lumières coniques | la math d'arc existe déjà dans `SectorPass`/`RadialMath.h` — à réutiliser, pas à réécrire |

L1 avant L2 est délibéré : elle livre et prouve **la plomberie** (cibles, ordre des vues, contournement
à coût nul) sans aucune lumière, donc un échec y est un échec d'infrastructure, pas de math. Le même
raisonnement que pour la P0 de la passe UI.

### La pièce pure

`grove::light` (header-only, `include/grove/light/`) — atténuation + bornes écran d'une lumière.
Aucun couplage au renderer ni à l'IIO, comme `grove::ui::computeNineSlice` ou `grove::text::fitLine`.
Testable headless avec un oracle exact ; le GPU ne prouve alors que le câblage.

## 6. Hors périmètre, et dit clairement

- **Les ombres** (occulteurs, shadow volumes 2D). C'est un chantier entier — géométrie d'occultation,
  structure d'accélération, cas limites. Le mélanger à L1-L3 garantirait de ne livrer ni l'un ni
  l'autre.
- **Les normal maps** par sprite. L'éclairage 2D plat est l'idiome ; l'ajouter doublerait le coût
  mémoire de chaque atlas pour un gain que personne n'a demandé.

## 6bis. À faire plus tard — particules-lumières et le chemin bulk

> **Statut** : dette assumée, pas planifiée. Notée le 2026-07-29 depuis une question terrain
> (Drifterra, flammes de propulseur).

**Le §4 dit « pas de chemin bulk, délibérément » — cette règle a une PRÉCONDITION, et elle est
écrite pour des LAMPES.** « Une scène éclairée en compte des dizaines » est vrai d'un éclairage de
décor ; c'est faux dès qu'on veut que **chaque particule émette sa propre lumière** (braises d'un
propulseur, étincelles). Hors de son domaine, la règle ne devient pas inoffensive — elle devient un
**biais** qui enverrait un consommateur publier des centaines de messages/frame sur un chemin
dimensionné pour des dizaines.

**Le coût réel est contre-intuitif et il faut le dire dans cet ordre :**

- une lampe coûte surtout du **fill rate**, pas un draw. Rayon 8 px ⇒ 256 pixels ; 600
  particules-lumières ⇒ ~150 k pixels, soit **moins d'une passe plein écran** en 480×270. Le GPU
  n'est pas le problème ;
- le mur est **le message IIO par particule et par frame** — le même plafond ~5 k primitives/frame
  que sprites, particules et texte ont déjà rencontré, et pour lequel les chemins bulk existent.

**Donc** : si les particules-lumières deviennent un besoin réel, `submitLightBatch` redevient
légitime et **cette section du plan devra être corrigée, pas défendue**.

**Avant d'écrire quoi que ce soit, mesurer** le nombre de particules réellement vivantes en charge
chez le consommateur. En dessous de ~200, le chemin actuel passe sans toucher au moteur.

**Et essayer d'abord l'alternative à une lampe** : particules en mélange **additif** (elles
*brillent* sans éclairer le monde) + **une seule** lumière qui suit l'émetteur. Ça donne le halo et
la lueur portée pour une lampe au lieu de six cents. Le gain réel des particules-lumières est ailleurs :
une braise qui **se détache** et va éclairer une surface loin de sa source.

## 7. Risques et inconnues

1. **Redimensionnement de fenêtre** — les cibles doivent être recréées. `createFramebuffer` ne sert
   aujourd'hui qu'à la relecture de pixels en test, à taille fixe. Le chemin resize est **neuf** et
   c'est le premier endroit où ça cassera.
2. ~~**Interaction avec les tests `[gpu]` existants**~~ — ✅ **vérifié avant d'écrire L1.**
   Les tests GPU (`NineSliceGpu`, `RuntimeTextureGpu`, `RhiReadback`) pilotent le RHI **directement**
   et font `setViewFramebuffer(0, fb)` **et** `setViewFramebuffer(1, fb)` vers leur propre cible de
   relecture. **Aucun conflit aujourd'hui** : ils ne publient ni `render:ambient` ni `render:light`,
   donc le contournement à coût nul (§3) fait que le module ne touche jamais `setViewFramebuffer`.
   **Mais ça contraint le futur test GPU des lumières** : il ne pourra PAS utiliser l'astuce
   « je redirige la vue 0 vers mon framebuffer », puisque la vue 0 sera la cible scène. Il devra lire
   la cible du **composite**. À concevoir comme tel, sinon il mesurera la scène non éclairée et
   passera au vert en ne prouvant rien.
3. **Format du buffer de lumière** — voir §8, c'est le seul arbitrage que je ne tranche pas seul.
4. ⚠️ **La transformation de la vue lumière n'est pas prouvée** (posée en L2, avant `LightPass`).
   Elle doit porter **la même caméra que la vue 0**, parce que les lumières sont publiées en
   coordonnées MONDE et que le composite échantillonne les deux cibles pixel pour pixel. Une matrice
   absente décalerait chaque lampe, et **l'erreur croîtrait avec le zoom et le pan** — donc ça
   ressemblerait à un bug de caméra ou de gameplay, pas à une ligne manquante.

   **Contrainte de conception pour le test GPU de L2, à ne pas rater** : il doit utiliser une caméra
   **déplacée et zoomée**. Avec la caméra par défaut, la transformation monde est proche de
   l'identité — une matrice manquante passerait alors inaperçue et le test serait vert en ne prouvant
   rien. C'est exactement le piège du « discriminant qui ne discrimine pas » : ici, ne pas régler la
   caméra reviendrait à mesurer le seul cas où le défaut est invisible.

## 8. ✅ Arbitrage tranché : **RGBA16F** (Alexi, 2026-07-28)

| | RGBA8 | RGBA16F |
|---|---|---|
| Coût mémoire/bande passante | ×1 | ×2 |
| Sur-brillance (valeurs > 1.0) | ❌ écrêtée | ✅ conservée |
| Bloom (le chantier suivant) | ne peut pas isoler les zones sur-exposées | c'est **exactement** ce dont il se nourrit |

Le bloom fonctionne en extrayant ce qui dépasse un seuil de luminance. Avec un buffer écrêté à 1.0,
trois lampes superposées et une lampe seule donnent le même blanc — il n'y a plus rien à extraire.

**Décision : RGBA16F.** Le post-traitement est le chantier annoncé juste après ; choisir RGBA8
maintenant reviendrait à jeter, dès L1, l'information dont le bloom se nourrit — et à reprendre la
plomberie dans deux semaines. Le doublement de bande passante sur deux cibles plein écran est le prix
assumé.

**Conséquence sur le RHI, à ne pas découvrir en route** : `createFramebuffer(width, height)` fabrique
aujourd'hui du RGBA8 en dur (il ne servait qu'à la relecture de pixels en test). Il lui faut un
paramètre de format — donc une petite extension d'interface, à faire dans L1 et à couvrir côté
`MockRHIDevice`.
