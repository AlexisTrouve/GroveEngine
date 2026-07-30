# Plan F2 — les fondus (post-traitement, tranche 3)

> **Statut** : ✅ **LIVRÉ** le 2026-07-30. ⚠️ **Le risque n°1 de ce plan était FAUX** — corrigé au §7
> après sabotage, à lire avant de se fier au reste.
> **Famille** : [bloom](lighting-bloom.md) → [tonemapping](lighting-tonemap.md) → **fondus** →
> [colorimétrie](lighting-grade.md).
> ⚠️ **Cette tranche a introduit un bug d'ordre de soumission** que la colorimétrie a trouvé, et que le
> test de fondu ne pouvait pas voir : [plan G](lighting-grade.md) §9.
> ⚠️ Mais **pas au même endroit**, et c'est la décision de cette tranche — voir §2.

---

## 1. Ce que ça livre

Un **fondu plein écran** : au noir pour une transition de scène, au blanc pour un flash, au rouge pour
un dégât. Un réglage persistant :

```
render:fade { color, amount }
```

| Champ | Défaut | Sens |
|---|---|---|
| `amount` | **0 = ÉTEINT** | de 0 (rien) à 1 (l'écran EST la couleur) |
| `color` | noir | la couleur vers laquelle on fond ; l'octet alpha est ignoré (`amount` est l'alpha) |

**Pas de durée, pas de courbe temporelle.** Le jeu rampe `amount` lui-même, frame par frame. Le moteur
ne possède pas le temps de la transition : une durée dépendrait de l'état du jeu (une pause doit-elle
geler le fondu ?), et un jeu qui veut une accélération non linéaire n'aurait plus la main. C'est le
même partage que partout ailleurs ici — le moteur fournit la primitive, la mise en scène reste au jeu.

## 2. ⚠️ CORRECTION D'UNE AFFIRMATION PRÉCÉDENTE : ce n'est PAS la passe de présentation

Le plan du bloom annonçait que « les fondus atterriront sur la passe de présentation ». **C'est faux**,
et la raison décide de toute la tranche.

**La présentation est soumise AVANT le HUD** (l'ordre est monde → … → présentation → HUD, précisément
pour que l'interface ne brille pas et ne soit pas écrasée). Un fondu au noir posé là laisserait donc le
HUD flotter, parfaitement net, sur un écran noir. Pour une transition de scène, c'est l'inverse de ce
qu'on veut.

Le fondu est donc **sa propre passe, soumise EN DERNIER**. Et ce changement de place apporte une
propriété que la présentation ne pouvait pas offrir :

| | présentation | passe dédiée, en dernier |
|---|---|---|
| couvre le HUD | ❌ | ✅ |
| exige `render:ambient` (l'éclairage) | ✅ oui | ❌ **non** |
| exige une cible HDR | ✅ oui | ❌ **non** |

**Le fondu ne demande rien.** C'est un quad plein écran mélangé par-dessus le backbuffer, quel que soit
ce qui a été dessiné avant. Il fonctionne donc **avec ou sans éclairage** — donc il est utilisable
immédiatement par Drifterra, DAOS et Fractax, qui n'éclairent rien. C'est le premier effet de
post-traitement de cette famille dont c'est le cas, et ça n'aurait pas été vrai en le rangeant avec les
deux autres.

### L'ordre après la courbe, et pourquoi ça compte

Même en oubliant le HUD, le fondu doit venir **après** le tonemapping. Un fondu au **blanc** appliqué
avant la courbe ne pourrait jamais atteindre le blanc : Reinhard tend vers 1 sans l'atteindre, donc
`mix(scène, blanc, 1)` puis compression donnerait un gris clair. Une transition qui ne finit pas sa
course est un bug visible. Après la courbe, `amount == 1` donne la couleur **exactement**.

## 3. Le contournement à coût nul, une quatrième fois

`amount == 0` (le défaut) ⇒ la passe sort immédiatement, aucun draw, aucun changement d'état. Et comme
elle n'a besoin d'aucune cible, il n'y a même rien à construire ou à libérer : c'est le plus simple des
quatre contournements de cette famille.

## 4. Pas d'oracle, et c'est délibéré

Les trois tranches précédentes ont chacune leur pièce pure dans `grove::light` — la retombée, le seuil
à genou, les courbes de tonemapping. **Celle-ci n'en aura pas.**

Un fondu est `mix(image, couleur, amount)`, une interpolation linéaire. Il n'y a pas de courbe qu'on
puisse se tromper en écrivant, pas de constante ajustée, pas de propriété subtile à vérifier. Ajouter
`grove::light::fade(a, b, t)` serait du cérémonial : un fichier, un test et un commentaire pour
envelopper un `lerp`, et une indirection de plus à suivre en lisant le shader.

**Le suivi du motif s'arrête là où il cesse d'apporter quelque chose.** C'est écrit ici pour que
l'absence se lise comme un choix et pas comme un oubli.

## 5. Les tranches

| | Ce qu'elle fait | Verrou |
|---|---|---|
| **Fa** | `render:fade` → `FramePacket::FadeSettings` (persistant, `amount 0` = éteint) | `SceneCollectorTest [fade]` |
| **Fb** | `FadePass` sur sa propre vue, soumise en dernier | `LightingGpu [fade]` |

## 6. Le discriminant, choisi avant le code

Trois mesures, et la deuxième est celle qui distingue cette conception d'une autre.

1. **Le fondu agit SANS éclairage.** Scène blanche, aucun `render:ambient`. `amount 0` → 255,
   `amount 1` vers le noir → 0, `amount 0.5` → ~128. Une passe rangée avec le bloom échouerait dès la
   première mesure, puisqu'elle n'existerait pas.
2. **⚠️ Le fondu COUVRE LE HUD.** On dessine un sprite en `space:"screen"` (donc sur la vue 1, celle du
   HUD) et on vérifie qu'il disparaît lui aussi à `amount 1`. **C'est le test qui sépare cette
   conception de celle que j'avais annoncée** : un fondu sur la passe de présentation laisserait ce
   pixel intact, et passerait pourtant la mesure 1 sans broncher.
3. **Le fondu agit AUSSI avec l'éclairage actif**, où le pipeline change entièrement de forme (la vue
   0 part dans une cible, un composite plein écran écrit le résultat). Ce cas attrape un fondu que le
   composite écraserait, ou dont la vue aurait hérité d'une cible.
   ⚠️ **Il n'attrape PAS ce que j'avais annoncé** — voir §7.1.

## 7. Risques

1. ~~**L'ordre de soumission imposé doit inclure la nouvelle vue.**~~ ⚠️ **RISQUE FAUX, corrigé après
   sabotage.** J'avais écrit qu'un oubli dans l'une des trois branches ferait disparaître le fondu.
   Retirer la vue de la liste ne change **rien** : `bgfx::setViewOrder(0, count, order)` ne remappe que
   les `count` premières places, et les vues non listées sont soumises ensuite, **par id croissant**.
   Le fondu est donc dernier parce que **son id est le plus haut (9)**, pas parce qu'il est listé.

   La vraie fragilité est ailleurs, et il faut la nommer correctement : **ajouter un jour une vue d'id
   supérieur à 9 la ferait passer après le fondu.** On liste quand même la vue dans les trois branches,
   pour que l'intention se lise à l'endroit où l'ordre est décidé — mais c'est de la documentation, pas
   le mécanisme.
2. **Le mélange doit être un vrai `mix`**, pas un additif. Un fondu au noir en additif ne ferait
   **rien du tout** (ajouter zéro), ce qui est le mode d'échec silencieux le plus probable de cette
   tranche — et il ne se verrait que sur un fondu au noir, le cas le plus courant.
3. **`amount` doit être borné**. Au-delà de 1, un `mix` extrapole : la couleur dépasserait et donnerait
   des artefacts au lieu d'un écran plein.

## 8. Hors périmètre

- **Pas de durée ni d'easing** (§1) — le jeu rampe `amount`.
- **Pas de fondu par région** ni de volet/iris : ce serait une forme, donc une primitive de dessin,
  pas un réglage global.
- **Pas de fondu sonore associé.** `sound:*` a ses propres `fadeMs` ; les coupler ici lierait deux
  modules qui n'ont pas de raison de se connaître.
