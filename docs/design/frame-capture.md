# Plan — capture de frame headless, comme capacité de première classe

> **État** : plan, rien d'implémenté. Écrit le 2026-07-30 sur `a1ecefa`.
> **Origine** : la dette « le rendu HUD/map n'est pas vérifié » de
> `drifterra/docs/grove_integration.md` §368.

---

## 1. Le cadrage, corrigé par la mesure

**La dette telle qu'elle est écrite est périmée.** Elle date de juin et affirme que le moteur ne
couvre `render:debug:rect` / `render:text` que par des showcases visuels. Le même document se
contredit d'ailleurs vingt lignes plus haut (« partiellement levée côté moteur »).

État réel au 2026-07-30, vérifié :

| Ce que la dette dit non couvert | Réalité |
|---|---|
| `render:text` | `test_scene_collector` asserte add/update/remove + l'ordre par `layer` |
| `render:debug:rect` | asserté par `test_scene_collector` |
| `render:rect` | asserté, **plus** le bucketing HUD via `space:"screen"` |
| la vue HUD elle-même | `HudViewUnit` (`test_hud_view.cpp`) |
| les pixels | `TextClipGpu`, `TtfRenderGpu`, `NineSliceGpu`, `SelectionHighlightGpu` |

**Donc : rien à faire sur le périmètre littéral.** Ce qui reste est ce que le document dit
lui-même — « la dette CÔTÉ JEU subsiste » : la composition du HUD de Drifterra ne se vérifie que
chez eux.

**La question utile est donc déplacée** : le moteur leur donne-t-il de quoi l'écrire ?

---

## 2. Ce qui manque vraiment, mesuré

`IRHIDevice::readFramebuffer` existe et est documenté comme « ce qui alimente les tests `[gpu]` ».
`BgfxRendererModule::getDevice()` est **déjà public**. L'accès n'est donc pas le problème :
**l'orchestration** l'est — créer une cible, y rediriger la bonne vue, rendre, relire, indexer les
pixels.

Personne ne l'a emballée, et le résultat se compte :

| Motif ré-implémenté à la main dans `tests/` | Occurrences |
|---|---|
| Fenêtre SDL cachée + extraction du handle natif (avec ses `#ifdef`) | **31 fichiers** |
| Capture **niveau module** (piloter `BgfxRendererModule` puis relire) | **17** |
| Capture **niveau passe** (device direct, sans module) | **6** |

> **C'est ça, l'argument.** Si nos propres tests trouvent le geste assez pénible pour le dupliquer
> dix-sept fois, un consommateur ne l'écrira jamais. Une capacité qui existe mais que personne
> n'utilise n'est pas une capacité.

---

## 3. La décision d'architecture

**Le helper reçoit le handle natif de l'appelant ; il ne crée pas de fenêtre.**

bgfx exige un handle natif même pour du hors-écran. SDL vit dans `InputModule`, et le cœur du
moteur en est délibérément exempt. Un helper qui créerait lui-même la fenêtre tirerait SDL dans la
surface publique du renderer — prix refusé. Un jeu a déjà une fenêtre ; il passe la sienne.

La création de fenêtre cachée (six lignes et ses `#ifdef` de plateforme) part dans une commodité
**de test uniquement**, qui dédoublonne à elle seule les 31 fichiers.

**Deux besoins distincts, deux objets.** Les 17 sites « module » et les 6 sites « passe » ne veulent
pas la même chose ; les confondre produirait un helper qui ne va bien à aucun des deux. On traite le
niveau **module** d'abord : c'est celui dont un consommateur a besoin, et il couvre 17 sites contre 6.

### Forme visée

```cpp
// include/grove/render/FrameCapture.h  (à confirmer en C1)
namespace grove::render {

struct Pixel { uint8_t r, g, b, a; };

class FrameCapture {
public:
    // `nwh`/`ndt` : handle natif FOURNI par l'appelant. Le moteur ne crée pas de fenêtre.
    static std::optional<FrameCapture> create(BgfxRendererModule& renderer, uint16_t w, uint16_t h);

    // Redirige, fait avancer le renderer d'une frame, relit. false si la relecture échoue.
    bool grab(const IDataNode& input);

    Pixel at(uint16_t x, uint16_t y) const;      // RGBA8, origine haut-gauche
    const std::vector<uint8_t>& pixels() const;
};

}  // namespace grove::render
```

Cible ergonomique côté consommateur :

```cpp
publishHudTopics();
auto shot = grove::render::FrameCapture::create(renderer, 256, 256);
REQUIRE(shot && shot->grab(input));
CHECK(shot->at(10, 10).r > 200);   // le panneau de menace est bien là
```

---

## 4. ⚠️ La question ouverte qui décide de la forme

**Que capture-t-on exactement : une vue, ou l'image finale ?**

Ce n'est pas un détail d'implémentation, et je ne sais pas encore trancher :

- La vue monde (0) et la vue HUD (1) sont deux vues distinctes.
- Quand l'éclairage est actif, un **passe de composite puis de présentation** écrit l'image finale.
- **Le HUD est soumis APRÈS la passe de présentation** (c'est ce qui fait qu'il ne reçoit pas le
  bloom — décision explicite du chantier lumière).

Donc rediriger la vue 0 ne capture **ni** le composite **ni** le HUD. Or un test de HUD veut
précisément l'image finale, HUD compris. Capturer la vraie sortie finale suppose sans doute de
rediriger la passe de présentation vers une cible hors-écran — plus invasif que rediriger une vue.

**Ça se tranche par la mesure en C1**, pas par le raisonnement : on essaie, on relit les pixels, on
regarde ce qu'on obtient. Repli acceptable si la sortie finale résiste : capturer la vue HUD seule,
ce qui couvre déjà l'assertion « mon panneau est au bon endroit de la bonne couleur » — et le dire
franchement dans l'API plutôt que de laisser croire qu'on capture l'écran.

---

## 5. Découpage

### C1 — le helper, et la question de la vue tranchée par la mesure

Écrire `FrameCapture` au niveau module + la commodité de fenêtre cachée côté test. Le livrable de
C1 n'est pas seulement le code : c'est **la réponse mesurée** à §4, écrite ici.

**Gate** : un test neuf qui publie un `render:rect` de couleur connue et asserte le pixel. Puis
**preuve d'inertie** : changer la couleur attendue doit le faire tomber — sinon le test ne lit rien.

### C2 — trois sites témoins, et la preuve d'équivalence

Réécrire **trois** des 17 sites sur le helper (un test GPU assertif, un outil `capture_*`, un cas
avec éclairage actif). Ils doivent passer **à l'identique**, puis tomber quand on casse le helper.

> Pourquoi trois et pas dix-sept : trois suffisent à prouver la forme, et les tests GPU sont
> précisément ceux qui exhibent la corruption de tas par artefact périmé — un gros diff mécanique
> dedans brouillerait le signal si ça retombe.

### C3 — le volume (optionnel, à faire quand la zone est calme)

Les 14 sites module restants, puis les 6 sites passe avec un second helper si la forme de C1 ne leur
va pas. **Ne se décide qu'après C2.**

### Hors périmètre, assumé

- Le screenshot-**diff** (comparer à une image de référence) : c'est une couche au-dessus, et elle
  amène la gestion des images de référence, leur versionnement et leur tolérance. Pas dans ce plan.
- La dette côté Drifterra elle-même : c'est chez eux, avec leur HUD.

---

## 6. Risques

- **La question §4 peut invalider la forme.** Si capturer l'image finale demande de toucher la passe
  de présentation, C1 devient plus invasif que prévu — c'est pour ça que C1 mesure avant de figer
  l'API.
- **Engagement d'API publique.** Une fois dans `include/`, la forme se corrige mal. Les 17 sites sont
  un bon banc d'essai, mais aucun n'est le consommateur visé — la forme peut être juste pour nos
  tests et pénible pour un jeu.
- **Zone sensible.** Les tests GPU exhibent une corruption de tas par artefact périmé
  (`0xC0000374`, cf. CLAUDE.md). Avant de conclure à une régression du helper : **reconstruire la
  cible**.
- **Le chantier peut se conclure par "non".** Si C1 montre que la capture de l'image finale exige de
  déformer le pipeline, la bonne réponse est peut-être de s'arrêter au helper de test (option A du
  CoT) et de le dire. Un plan qui ne peut pas se conclure par un abandon n'est pas un plan.

---

## 7. Ce que ce plan ne prétend pas

Il ne ferme pas la dette de Drifterra — il leur donne l'outil qui leur manque pour la fermer.
La distinction est faite exprès : **on ne peut pas tester la composition de HUD d'un autre projet.**
