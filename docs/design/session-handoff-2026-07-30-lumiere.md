# Handoff — l'éclairage 2D, du socle aux nébuleuses (29 & 30 juillet 2026)

> **État à la sortie** : `master` = **`d94827a`**, arbre propre.
> ⚠️ **1 commit non poussé** (`d94827a`) et **1 commit de retard** sur les remotes (`d247e92`, UI) —
> une autre session pousse en parallèle, il faut fusionner avant de pousser. Détail au §5.
> **Suite** : 205/205 au dernier run complet ; 200/200 hors les cinq tests lourds déjà répertoriés
> comme instables (le run complet dépasse la fenêtre d'outillage disponible).
>
> Sessions précédentes : [27-28 juillet](session-handoff-2026-07.md) ·
> [28-29 juillet](session-handoff-2026-07-29.md) · en parallèle, la session
> [saisie de texte & brouillard tilemap](session-handoff-2026-07-29-saisie-brouillard.md).

Ce document dit **où on en est, ce qui est clos, ce que j'ai eu faux, et ce qui reste**. Chaque
chantier a son propre doc, référencé.

---

## 1. L'éclairage 2D est complet

Les trois plans annoncés sont clos, plus deux tranches qui n'étaient pas au programme.

| Tranche | Ce qu'elle livre | Preuve mesurée |
|---|---|---|
| **F1-F3** | [filtres colorés](lighting-filters.md) — un vitrail teinte au lieu de bloquer | derrière un verre rouge `r=143 b=39`, sans verre `r=159 b=159` |
| **A1** | [milieux](lighting-attenuators.md) — absorption de Beer-Lambert | L0=140 / L1=74 / L2=33, soit `L2·L0 = L1²` |
| **A2** | **diffusion** — le milieu devient visible | sur fond NOIR : 0 / 0 / **255** |
| **A3** | milieux en mode retenu | fusion partielle qui re-dérive |
| **A4** | **nébuleuses** — densité radiale (hors périmètre à l'origine) | cœur 0,63 / rasant 0,79 / **hors disque 1,00** |
| **A5** | nébuleuses en mode retenu | ferme une asymétrie qu'A4 avait créée |

**La thèse du socle est vérifiée quatre fois sur quatre** : murs, vitraux, brouillard et nébuleuses
écrivent dans **une seule carte**, avec **un seul blend multiplicatif** — donc ils se composent dans
n'importe quel ordre, sans le moindre tri de profondeur.
→ [socle de transmittance](lighting-transmittance-core.md)

### La seule décision d'architecture de tout le chantier

```
final = scene × (ambiant + lumière) + lumière × scatter
```

Le terme diffusé est **additif**, jamais multiplicatif avec la scène. Dans le vide `scene` est noire,
donc un terme multiplicatif laisserait un faisceau traversant une nébuleuse **invisible** — l'inverse
exact de la fonctionnalité. Sabotage vérifié : repasser en multiplicatif ramène 255 à 0.

### Deux économies que les plans n'avaient pas vues

- **A2 n'a pas eu besoin de sa cible additive.** Le coefficient de diffusion tient dans le canal
  **alpha inutilisé** de la carte d'occultation, où le blend multiplicatif l'accumule en
  `Π(1−scatter)`. Ni cible ni passe de plus.
- **A3 devait apporter l'absorption colorée** : elle était déjà dans A1.

## 2. Deux correctifs de qualité d'image, tous deux repérés à l'œil

Aucun test ne les a trouvés. **Alexi les a vus sur les captures du blog.**

**La marche avançait par fractions de distance.** Un nombre fixe de pas entre la lampe et le
fragment ⇒ un pas valait `distance/16`, et le bord d'ombre sortait en escalier **qui s'aggravait avec
la taille de la lampe** — aucune constante ne pouvait être la bonne. Le pas est désormais constant
**en pixels écran**.

**Et ça ne suffisait pas** : « c'est mieux mais c'est toujours un escalier ». Un occulteur opaque écrit
0, donc le verdict de la marche est **binaire** — aucun raffinement en amont ne l'adoucit. Trois
tentatives ont buté là-dessus (pas plus fin, carte filtrée, premier tramage qui était un dégradé)
avant la bonne : étaler la décision binaire sur les pixels voisins avec des décalages **opposés**, puis
la remoyenner au composite. **Écart à la droite : 9,4 px → 1,6 px.**
→ [plan W, risque 2](lighting-walls.md)

## 3. Le budget de lampes, mesuré

`tests/visual/benchmark_lighting.cpp` remplace le chiffre « des dizaines de lampes » qui avait été
**retiré** de la doc faute de mesure.

| | par viewport couvert | budget 60 fps |
|---|---|---|
| aucune matière publiée | **19,5 µs** | ~850 viewports |
| matière publiée | **355 µs** | **~47 viewports** |

**Le coût est du fill rate et rien d'autre** — le nombre de lampes n'apparaît pas dans le modèle (coût
par viewport plat à ±1 % de 32 à 128 lampes). **Un seul occulteur multiplie tout par 18**, et le
facteur ne dépend pas de la *quantité* de matière : un mur coûte autant que cinq cents. C'est un coût
de **présence**.

Corollaire rassurant, et c'était la question qui motivait le banc : **un jeu sans mur ne paie presque
rien**. Drifterra, DAOS et Fractax sont dans ce cas.

⚠️ **Anomalie non expliquée, signalée** : à rayon 60, 1024 lampes coûtent 0,33 ms et 4096 en coûtent
**26,6** — facteur 80 pour facteur 4, là où tout le reste est linéaire. Pas du fill rate, non
diagnostiqué, sans portée pratique.

## 4. ⚠️ CE QUE J'AI EU FAUX — à lire avant de faire confiance à un diagnostic de cette session

**J'ai commité un correctif (`921aa6a`) dont la cause énoncée était fausse**, et je l'avais
documentée avec assurance, y compris sous forme de règle générale dans `CLAUDE.md`.

L'histoire : un test d'asset **sans rapport** (`AssetSpriteGpu`) mourait en corruption de tas au
teardown. Trois coupes différentielles ont « désigné » le partage de bytecode entre programmes, et
j'ai écrit `vs_nebula.sc` là-dessus, plus la règle « deux programmes ne partagent pas un étage ».

**Vérifié après coup** : la configuration incriminée passe **5/5** avec un build propre, et **aucun
bytecode n'est dupliqué** dans le dépôt (hachage des 24 blobs). La vraie cause était un **artefact de
build périmé**.

**Le piège de diagnostic est le vrai sujet**, et il est maintenant au registre
([known-annoyances §3bis](known-annoyances.md)) : le défaut est **déterministe** tant qu'on ne
reconstruit pas, donc il ressemble à un vrai bug ; et **chaque coupe différentielle reconstruit la
cible**, si bien que c'est la reconstruction qui guérit, pas la coupe. N'importe quelle modification
récente aurait été désignée.

**Règle** : avant d'attribuer une corruption de tas à un changement, vérifier que la variante *saine*
**échoue encore APRÈS reconstruction**.

`vs_nebula.sc` est conservé pour une raison honnête et beaucoup plus modeste — `u_nebula` dit ce qu'il
place — et son en-tête raconte l'erreur au lieu de la masquer.

## 5. ⚠️ L'environnement : deux sessions dans le même dépôt

Une autre session travaille en parallèle dans un **worktree séparé** (`.claude/worktrees/ui-textinput/`)
sur la saisie de texte UI et le brouillard tilemap. Elle a poussé ~20 commits pendant cette session et
continue.

**Conséquences concrètes :**

1. **`git fetch` avant tout push.** J'ai dû fusionner deux fois ; la seconde fois un commit de plus est
   arrivé pendant que les tests tournaient. **Fusionner, pas rebaser** : opération atomique, et ça ne
   réécrit aucun commit que l'autre agent pourrait référencer.
2. **Un commit de mon travail A2 est attribué à l'un de ses commits** (`e461f5b`, intitulé mapview) —
   un `git add -A` de leur côté a avalé mes modifications en cours. Le code est juste et testé, seule
   l'attribution est fausse. **Non corrigé délibérément** : réécrire l'historique sous une session
   active est le vrai risque. Arbitrage d'Alexi.
3. **Ne jamais `git add -A`** ici. Stager par chemins explicites, et vérifier que `TestModule.cpp`
   (réécrit par l'AutoCompiler) n'entre pas dans un commit.

## 6. Ce qui reste ouvert

### Le prochain pas évident
**Le bloom.** Annoncé depuis L1, et tout est en place : les cibles sont en RGBA16F précisément pour
ça, le sur-brillant est conservé, le composite est le point d'accroche.

### Dettes nommées
- **Pas de variantes Metal réelles** pour `fs_light`, `fs_nebula`, `vs_nebula` — la chaîne de build
  n'a pas de backend Metal, ces blocs sont des placeholders. Bloquant le jour d'un portage Mac.
- **L'anomalie du banc** à 4096 petites lampes (§3).
- **C3, la table polaire** — l'optimisation de la marche. Le banc dit maintenant qu'elle gagnerait sur
  les scènes AVEC matière (×18) ; c'est la mesure qui manquait pour la justifier.
- **`ModuleDependencies` a segfaulté une fois** en contexte de suite, passe 43/43 isolé. Probablement
  la même famille que §4 (artefact périmé). Pas au registre : une seule occurrence.

### Hors périmètre, documenté
Pas d'ombres douces (il faudrait une source d'aire), pas de densité texturée (une nébuleuse est un
disque de couleur unie), les occulteurs bloquent la **lumière et pas la vue**.

## 7. Ce que ces deux jours ont appris

### Une réponse binaire ne s'adoucit pas en amont
Tant que le verdict par pixel est 0 ou 1, raffiner ou filtrer l'entrée ne fait que **déplacer la
falaise**. Il faut soit supersampler, soit étaler la décision sur les voisins et la remoyenner.

### Ma métrique récompensait le bruit
La longueur de palier chute quand un bord devient granuleux — donc le tramage seul « améliorait » le
chiffre en dégradant l'image. C'est l'œil qui a tranché entre grain et rampe. **Une métrique de proxy
se valide contre ce qu'elle prétend mesurer avant qu'on lui fasse confiance pour arbitrer.**

### Mesurer avant de construire, y compris pour dire « non »
Avant d'écrire `render:nebula`, la question a été posée au moteur : peut-on approcher une nébuleuse en
empilant des rectangles ? Réponse — quatorze contours concentriques, une ziggourat. La primitive était
nécessaire, **et ce n'est pas une supposition**.

### Un correctif qui a l'air inefficace parce qu'il n'est pas dans le binaire
En déplaçant un script dans `tools/`, son dossier de sortie a changé et l'assembleur lisait encore
l'ancien : j'ai mesuré le shader d'avant et conclu que mon tramage ne servait à rien. Les deux étapes
sont maintenant **une seule commande** (`tools/regen_shader.py`).

### Un banc qui n'écrit qu'à la fin ne mesure rien
Le premier essai à forte charge est mort en cours de route et a emporté toutes les lignes déjà
mesurées. Chaque ligne est maintenant imprimée **et vidée** dès sa mesure : la dernière ligne visible
*est* le résultat, même si la suivante tue le programme.

### Et la plus chère : §4
Trois « preuves » par coupe différentielle ont confirmé une cause fausse, parce que le remède était
caché dans le protocole de mesure lui-même.
