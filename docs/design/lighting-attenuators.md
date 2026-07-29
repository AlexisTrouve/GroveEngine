# Plan A — les atténuateurs (absorption progressive)

> **Statut** : 📋 plan, rien d'implémenté.
> **Socle** : [table de transmittance polaire](lighting-transmittance-core.md) — **lire d'abord**.
> **Dépend de** : [plan W](lighting-walls.md) et [plan F](lighting-filters.md), dont il continue la
> matière.

## 1. Ce que ça donne

Un milieu qui **affaiblit** la lumière au lieu de la bloquer : brouillard, fumée, feuillage, eau
trouble. Plus la lumière en traverse, moins il en reste. Le faisceau d'un phare se perd dans la brume
au lieu de s'arrêter net.

## 2. Ce que ça ajoute au socle : la continuité

Murs et filtres sont des matières **discrètes** — on les traverse ou non. Un atténuateur est
**continu** : la perte dépend de la **distance parcourue dedans**, pas du fait d'y être entré.

Le socle le porte déjà, et c'est pour ça qu'il stocke une transmittance **par unité de longueur** et
non par occulteur :

```
T = Π transmittanceParUnité ^ (longueur du pas)
```

Un mur annule en un pas. Un brouillard de coefficient α donne `exp(−α·d)` — la **loi de
Beer-Lambert** ([Volumetric Rendering](https://wallisc.github.io/rendering/2020/05/02/Volumetric-Rendering-Part-1.html)),
qui tombe directement de la forme multiplicative choisie au socle.

Autrement dit : ce plan n'ajoute pas de mathématiques. Il ajoute **une façon d'écrire une densité**
là où W et F écrivaient une valeur ponctuelle.

## 3. ⚠️ Atténuer la lumière ≠ avoir l'air brumeux

**C'est le malentendu le plus probable de ce chantier, et il faut le poser avant tout le reste.**

Ce plan fait *perdre* de la lumière dans le milieu. Il ne fait **pas** le voile laiteux qu'on associe
au brouillard. Ce voile-là est de la **diffusion** — la lumière qui rebondit dans le milieu et
revient vers l'œil — et c'est un **second terme**, additif, indépendant de celui-ci.

Concrètement, avec ce plan seul :
- un phare éclaire moins loin dans la brume ✅
- une zone brumeuse n'a pas l'air blanchâtre ❌
- un faisceau n'est pas visible « de côté » ❌

Les deux derniers sont ce que les gens attendent en disant « brouillard ». Faire l'un sans l'autre
donne une scène qui s'assombrit sans raison visible. **À trancher avant A1 : est-ce l'absorption que
tu veux, ou l'aspect ?** Si c'est l'aspect, il faut un plan de diffusion en plus — pas à la place.

## 4. Surface d'écriture

| Topic | Charge | Notes |
|---|---|---|
| `render:fog` | `{x, y, w, h, density, color?}` | volume atténuant rectangulaire, éphémère |
| `render:fog:add` / `:update` / `:remove` | `{renderId, …}` | retenu — une nappe de brume ne bouge pas vite |

`density` est le **coefficient α** de Beer-Lambert, pas une opacité 0..1 : il n'a pas de borne haute,
et doubler la distance traversée double son effet dans l'exposant. Nommer le champ `density` plutôt
qu'`opacity` est délibéré — appeler « opacité » une grandeur non bornée garantirait qu'on la règle à
1 en croyant saturer.

`color` (défaut blanc) permet une absorption **sélective** : une brume qui mange le bleu plus vite
que le rouge donne des couchers de soleil. C'est le même champ que les filtres, avec une
interprétation continue.

## 5. Découpage

| Tranche | Contenu | Preuve |
|---|---|---|
| **A1** | `render:fog` → carte de densité | headless : oracle sur `exp(−α·d)` à trois distances |
| **A2** | l'atténuation à l'écran | `[gpu]` : à **densité doublée**, la lumière restante est le **carré** de la précédente |
| **A3** | mode retenu + absorption colorée | headless + `[gpu]` sur la divergence des canaux |

### Pourquoi A2 teste un carré et pas « c'est plus sombre »

« Avec du brouillard c'est plus sombre » serait vert avec n'importe quel assombrissement — un simple
facteur constant passerait.

Ce qui caractérise Beer-Lambert, c'est **l'exponentielle** : doubler α, ou doubler la distance,
**élève au carré** ce qui reste. Trois mesures suffisent à distinguer une exponentielle d'une
droite, et c'est la seule assertion qui prouve qu'on a implémenté une absorption plutôt qu'une
soustraction.

C'est la même exigence qu'en L2, où il fallait séparer une atténuation carrée d'une linéaire : la
forme de la courbe est le contrat, pas le fait que ça baisse.

## 6. Risques

1. **Le pas de marche fixe la précision.** Le socle échantillonne la table à N rayons ; une nappe
   fine entre deux pas est invisible, une nappe traversée en biais est sous-estimée. Un brouillard
   dense et mince est le pire cas.
2. **La densité et le rayon de lampe interagissent** de façon non intuitive : `exp(−α·d)` combiné à
   l'atténuation radiale `(1−d/r)²` donne une chute très rapide. Attendez-vous à devoir baisser α
   d'un ordre de grandeur par rapport à l'intuition, et documentez une valeur de départ.
3. **Rien n'atténue l'ambiant.** Ce plan agit sur les **lampes**, pas sur le terme ambiant, qui n'a
   pas de trajet — il est global par construction. Une scène très brumeuse mais fortement ambiante
   ne paraîtra pas brumeuse du tout. C'est cohérent avec le modèle, et parfaitement déroutant sans
   l'avoir écrit.

## 7. Hors périmètre

- **La diffusion** (§3) — le voile laiteux et les faisceaux visibles. C'est un terme additif à part.
- **Le brouillard texturé / animé** — une nappe non uniforme. Le rect uniforme est le socle
  d'authoring ; une texture de densité est une extension naturelle mais pas gratuite.
- **Le brouillard de guerre**, qui ressemble mais n'a rien à voir : il masque de
  l'**information**, pas de la lumière, et il existe déjà côté tilemap (`render:tilemap:fog`). Ne
  pas les confondre dans la doc — deux systèmes, deux buts.

## 8. Sources

- [Volumetric Rendering, Part 1 — Beer-Lambert](https://wallisc.github.io/rendering/2020/05/02/Volumetric-Rendering-Part-1.html) — `I = I₀·exp(−α·d)` et le rôle du coefficient d'absorption.
- [Volumetric Raymarching — GM Shaders](https://mini.gmshaders.com/p/volumetric) — l'accumulation pas à pas de densité et de transmittance, transposable en 2D.
- [Real-time cloudscapes with volumetric raymarching](https://blog.maximeheckel.com/posts/real-time-cloudscapes-with-volumetric-raymarching/) — la séparation entre absorption et diffusion, c'est-à-dire exactement la distinction du §3.
