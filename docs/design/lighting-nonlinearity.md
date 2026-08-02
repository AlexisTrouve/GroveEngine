# La non-linéarité d'éclairage — plan de diagnostic

> **Statut** : 📋 PLAN (2026-08-02). C'est un plan de **diagnostic**, pas d'optimisation — et cette
> distinction est le cœur du document.
>
> **Contrainte de cadrage** : aucun consommateur n'éclaire quoi que ce soit (mesuré le 02/08 —
> Drifterra, DAOS et Fractax publient 11 à 15 topics `render:*` chacun, aucun `render:ambient`). Le
> seul livrable justifié aujourd'hui est donc **un diagnostic correct**. Une optimisation que
> personne n'encaisse ne se construit pas ; une attribution fausse laissée dans `CLAUDE.md`, elle,
> coûte à quiconque la relira.

## 1. Le fait

`tests/visual/benchmark_lighting.cpp`, RTX 4060 Laptop @1280×720, lampes de rayon 60 :

| Lampes | GPU |
|---|---|
| 1024 | 0,33 ms |
| 4096 | **26,6 ms** |

**×80 pour ×4.** Toutes les autres lignes du banc sont linéaires en *viewports couverts* — c'est le
modèle de coût que le banc a établi et qui tient partout ailleurs.

`CLAUDE.md` consigne : « *pas du fill rate, semble lié aux draw-calls ou aux changements d'état, non
diagnostiqué, sans portée pratique* ».

⚠️ **Cette phrase est une HYPOTHÈSE, pas une mesure.** Elle a été écrite en constatant que le modèle
de fill rate ne s'appliquait pas — c'est-à-dire par élimination, pas par observation. Elle a depuis
été relue plusieurs fois comme un fait établi.

## 2. Trois couches candidates, chacune avec sa preuve de code

Le point commun des trois : **elles coûtent toutes par lampe**. Ce n'est donc pas la forme de la
courbe qui les distinguera, mais où le temps est passé.

### (a) Le banc publie un message IIO + un nœud JSON par lampe

```cpp
for (int i = 0; i < n; ++i) {
    auto l = std::make_unique<JsonDataNode>("l");   // une allocation
    ...
    m_gIO->publish("render:light", std::move(l));   // un message
}
```

À 4096 lampes : 4096 nœuds et 4096 messages **par frame**. Or le moteur a mesuré ailleurs que ce
chemin plafonne dans les **bas milliers par frame à 60 fps** — c'est le mur qui a justifié
`submitSpriteBatch` puis le blob packé de `render:sprite:batch`.

**La rupture est entre 1024 et 4096. C'est exactement où ce plafond tombe.**

⚠️ Si c'est ça, **l'instrument mesure sa propre lenteur** et le renderer n'y est pour rien.

### (b) La passe émet un appel de dessin par lampe

`modules/BgfxRenderer/Passes/LightPass.cpp` :

```cpp
for (size_t i = 0; i < frame.lightCount; ++i) {
    cmd.setUniform(m_lightUniform, light, 1);
    cmd.setUniform(m_lightColorUniform, lightColor, 1);
    cmd.setUniform(m_lightConeUniform, cone, 1);
    cmd.submit(CompositePass::kLightView, m_shader, 0);
}
```

4096 lampes = **4096 `submit` + 12288 `setUniform`**. C'est l'hypothèse de `CLAUDE.md`, et elle a une
base réelle. Le coût d'un `submit` bgfx est majoritairement de l'**encodage CPU**, pas du GPU — ce
qui compte pour l'étape 1 ci-dessous.

### (c) Un vrai coût GPU

Saturation d'un buffer transitoire, changement de contexte, occupancy. Le moins probable au vu de (a)
et (b), mais à ne pas écarter avant d'avoir séparé le reste.

## 3. Le protocole, du moins cher au plus cher

### Étape 1 — RELIRE la mesure qui existe déjà (~5 min)

Le banc rapporte **`wall ms`, `gpu ms` ET `cpu ms`** en colonnes séparées. La réponse est peut-être
déjà là, jamais lue.

- ×80 dans `cpuMs` ⇒ (a) ou (b) — le GPU est hors de cause.
- ×80 dans `gpuMs` ⇒ (c), et c'est le seul cas où il y a un vrai sujet de rendu.

⚠️ **Ne rien coder avant cette lecture.** Elle est gratuite et elle élimine potentiellement les deux
tiers du champ.

### Étape 2 — séparer (a) de (b), si l'étape 1 dit « CPU »

Les deux sont du CPU par lampe, donc `cpuMs` ne les sépare pas. Deux voies, par coût croissant :

1. **Chronométrer la boucle de publication dans le banc** — quelques lignes, mesure directe du coût
   de (a). Si elle explique le ×80 à elle seule, (b) est innocentée sans y toucher.
2. Sinon, **`GROVE_PROFILE_ZONE`** existe déjà (`include/grove/profile/`, zones `engine:step` /
   `engine:iopump` déjà instrumentées). Poser une zone autour de la boucle de `LightPass` donne (b)
   directement.

### Étape 3 — le verdict décide de la suite, et il peut décider « rien »

| Verdict | Ce qu'on fait |
|---|---|
| **(a) le banc** | Corriger `CLAUDE.md` : l'attribution est fausse d'une couche. Noter que le banc mesure son propre plafond de publication au-delà de ~1k lampes, donc que ses lignes 4096 ne disent rien du rendu. **Aucun code moteur ne bouge.** |
| **(b) les draw-calls** | Diagnostic juste, sujet réel. La direction naturelle est l'**instanciation** : un seul `submit` pour N lampes, les paramètres passant par un buffer d'instances plutôt que par des uniformes. ⚠️ **À chiffrer, pas à lancer** — cf. §4. |
| **(c) le GPU** | Chasse ouverte : capacité de buffer transitoire, occupancy. Le banc sert alors d'outil de bissection (faire varier N par petits pas autour de la rupture pour trouver le seuil exact — un seuil NET trahit une capacité, une pente trahit une saturation). |

## 4. Ce que ce plan refuse de faire

**Optimiser avant de savoir.** Les trois couches ont la même forme de courbe ; choisir la plus
plausible est exactement ce qui a produit la ligne fausse de `CLAUDE.md`.

**Instancier les lampes « parce que c'est mieux ».** Même si (b) est le coupable : 4096 lampes ne
correspondent à aucun usage. Le budget mesuré dit qu'à 720p avec des murs, **~45 lampes** consomment
déjà la frame entière. Un jeu qui en pose 4096 a un problème avant d'arriver au renderer.
→ Si (b) est confirmé, le livrable est **le diagnostic écrit + un chiffrage**, et la décision de
construire revient à Alexi.

**Toucher au budget documenté.** Les chiffres de `CLAUDE.md` (19,5 µs/viewport sans matière,
355 µs avec) sont mesurés et tiennent ; ce plan ne concerne que la ligne 4096.

## 5. Pièges connus, à relire avant de mesurer

- ⚠️ **Le banc n'est pas un ctest** : c'est un test visuel qui ouvre une fenêtre. Il se lance
  **depuis la racine du projet** (les captures GPU ont trois conventions de répertoire courant dans
  ce dépôt — cf. `known-annoyances`). Trois erreurs de cwd ont été commises le 02/08, dont une a fait
  croire à un rendu mort.
- ⚠️ **Les salves de `0xC0000374`** (§3ter de `known-annoyances`) peuvent frapper n'importe quel
  binaire GPU pendant la campagne. Ne **jamais** bisecter sur un plantage de ce type sans avoir
  d'abord établi que ce n'est pas une salve — un bisect y fabrique un faux coupable avec une chaîne
  de preuve impeccable.
- ⚠️ **Mesurer plusieurs fois dans la MÊME fenêtre de quelques minutes n'est pas mesurer plusieurs
  fois** : les répétitions partagent l'état transitoire de la machine. Espacer, ou l'écrire.
