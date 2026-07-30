# Plan B — le bloom (post-traitement, tranche 1)

> **Statut** : ✅ **LIVRÉ** le 2026-07-30 — B0 → B3, plus un défaut RHI trouvé en chemin.
> Écarts et erreurs de ce plan consignés au **§8**, à lire avant de faire confiance au reste.
> **Socle** : [éclairage 2D](lighting-2d.md) — les cibles sont en RGBA16F **précisément pour ça**
> ([l'arbitrage d'Alexi](lighting-2d.md#8-arbitrage-tranché--rgba16f-alexi-2026-07-28)).
> **Suite annoncée après** : fondus et colorimétrie, sur la même passe de présentation.

---

## 1. Ce que ça livre

Une lueur autour de ce qui est **sur-exposé** : un halo qui débourre hors de la lampe, une traînée de
moteur qui bave, un vitrail qui éblouit. C'est le rendu que la sur-brillance conservée depuis L1 rend
possible et que personne ne consomme encore.

Surface d'auteur — **un seul message, persistant** (un réglage, pas une donnée de frame) :

```
render:bloom { intensity, threshold?, radius? }
```

| Champ | Défaut | Sens |
|---|---|---|
| `intensity` | **0 = ÉTEINT** | combien de lueur est rajoutée. C'est l'interrupteur. |
| `threshold` | **1.0** | luminance au-delà de laquelle un pixel brille. 1.0 = « seulement le sur-brillant ». |
| `radius` | **16** | étendue de la lueur, en **pixels écran**. |

`radius` est en pixels écran et pas en fraction de quoi que ce soit — c'est la leçon de la marche
d'occultation, qui prenait un nombre de pas fixe et produisait un escalier qui **empirait avec la
taille de la lampe** ([plan W, risque 2](lighting-walls.md)). Une lueur définie en fraction d'écran
changerait d'épaisseur au redimensionnement de la fenêtre.

## 2. De quoi le bloom se nourrit — LE choix d'architecture

Trois sources possibles, et ce n'est pas un détail d'implémentation.

| Source | Ce qui brille | Coût | Verdict |
|---|---|---|---|
| le buffer de **lumière** seul | les lampes | 0 passe de plus | ❌ |
| **recalculer** la formule d'éclairage à l'extraction | tout | 0 passe de plus | ❌ |
| la **frame composée** | tout | 1 cible + 1 passe | ✅ |

**Le buffer de lumière seul est rejeté** : il laisserait sans lueur un **sprite additif**
(`blend:"additive"`), qui existe dans le moteur exactement pour les choses qui brillent — la forme du
panache de moteur. Ce serait le cas d'usage le plus évident en 2D, absent.

**Recalculer la formule est rejeté** pour une raison de maintenance : `final = scene × (ambiant +
lumière) + lumière × scatter` vivrait alors dans **deux** shaders. Ces deux copies dériveraient, et la
divergence se verrait comme un bug de bloom alors qu'elle serait un bug de duplication. Le composite
est propriétaire de cette formule.

**Donc la frame composée.** Le composite écrit dans une cible HDR, et une passe de présentation ajoute
la lueur au backbuffer.

### Le corollaire à documenter fort

**Le bloom exige que l'éclairage soit actif.** Sans `render:ambient`, il n'y a aucune cible hors écran
à échantillonner — le contournement à coût nul de L1 fait que la scène va directement au backbuffer,
et le backbuffer ne s'échantillonne pas. Un jeu qui ne veut **que** du bloom publie un **ambiant
blanc** : il est neutre par construction (il laisse la scène inchangée), c'est déjà documenté comme le
point d'entrée « je n'ai pas besoin d'un jeu sombre pour utiliser l'éclairage ».

## 3. Le contournement à coût nul, encore

`intensity == 0` (le défaut, donc l'absence de `render:bloom`) ⇒ **aucune cible bloom construite,
aucune passe enregistrée, le composite écrit au backbuffer comme aujourd'hui, à l'octet près**. C'est
la même garantie que L1 et elle protège les mêmes consommateurs (Drifterra, DAOS, Fractax).

Ce contournement est ce qui rend le test de plomberie possible, et ce test est le vrai discriminant de
la tranche B2 : `bloom {intensity: 1, threshold: 10}` — rien dans la scène ne dépasse une luminance de
10, donc **la sortie doit être identique au bloom éteint**, alors que le trajet HDR complet a été
emprunté. Ça sépare « la plomberie déforme-t-elle l'image » de « la lueur fonctionne-t-elle », deux
échecs qu'un seul test mélangerait.

## 4. Le pipeline

```
occultation (4) → monde (0) → lampes (2) → composite (3) → extraction (5) → flou H (6) → flou V (7) → présentation (8) → HUD (1)
                              ↓ FB lumière   ↓ FB HDR        ↓ FB bloom0     ↓ bloom1      ↓ bloom0     ↓ BACKBUFFER
```

**Le HUD passe APRÈS la présentation** : il ne brille pas et il n'est pas écrasé. C'est voulu — une
interface nette au-dessus d'un monde qui éblouit, pas un HUD flou.

Cibles ajoutées, **uniquement quand le bloom est actif** :

| Cible | Taille | Format | Rôle |
|---|---|---|---|
| `m_hdrFB` | plein écran | RGBA16F | ce que le composite écrit désormais |
| `m_bloomFB[0]`, `[1]` | **quart** de chaque dimension | RGBA16F | extraction + ping-pong du flou séparable |

Le quart de résolution n'est pas une économie honteuse, c'est **la moitié du flou** : la réduction
elle-même moyenne 4×4 pixels, donc une partie du travail du noyau est déjà faite par l'échantillonnage
bilinéaire. Une lueur est basse fréquence par nature ; la résoudre au pixel serait payer pour une
information qu'on s'apprête à étaler.

## 5. Les tranches

| | Ce qu'elle fait | Verrou |
|---|---|---|
| **B0** | l'oracle C++ : `grove::light::bloom` (luminance, seuil à genou doux, noyau gaussien) | `BloomMathUnit` |
| **B1** | `render:bloom` → `FramePacket::BloomSettings` (persistant, `intensity 0` = éteint) | `SceneCollectorTest [bloom]` |
| **B2** | la plomberie : cible HDR + passe de présentation + le détachement de vue | `RhiReadbackGpu [unbind]` + `LightingGpu [bloom]` cas « seuil inatteignable » |
| **B3** | le bloom : extraction + flou séparable | `LightingGpu [bloom]` |

### B0 — pourquoi un oracle C++ pour un effet purement visuel

Trois raisons, et la troisième est la vraie.

1. C'est la convention de tout l'éclairage ici : les courbes de lampe sont en C++ dans `grove::light`
   et les shaders les miment, ce qui les rend vérifiables sans GPU (`LightMathUnit`).
2. Le **genou doux** se juge sur sa **dérivée**, et une dérivée ne se lit pas sur une capture.
   ⚠️ **Correction de ce plan, écrite pendant B0** : j'avais d'abord justifié le genou par une
   *discontinuité de valeur* qui « scintillerait ». **C'est faux.** La version nette,
   `max(0, luma − seuil) / luma`, vaut exactement 0 au seuil et croît depuis 0 : elle est continue en
   valeur. Un test sur les sauts de valeur serait donc passé au vert **avec un seuil net** — le
   « discriminant qui ne discrimine pas », pour la troisième fois de la semaine. Ce que le genou
   change est la **pente**, qui saute de 0 à 1/seuil sans lui : la lueur s'amorce par un ourlet net
   là où la scène atteint le seuil. Le vrai discriminant est trivial : **sous le seuil mais dans le
   genou, la version nette rend exactement zéro et le genou rend quelque chose.**
3. **Les poids du noyau sont téléversés depuis le C++, pas écrits en dur dans le shader.** Un noyau
   dont les poids ne somment pas exactement à 1 change la luminosité globale de la lueur — un bug qui
   ressemble à un mauvais réglage d'`intensity` et qu'on « corrige » alors en tournant le bouton. En
   les téléversant, l'oracle est la source de vérité **unique** et le test CPU prouve ce que le GPU
   utilise. Les écrire en dur dans le `.sc` en ferait une copie qui dérive.

### Le genou doux, et pourquoi pas un seuil net

Un seuil net est exactement la discontinuité que la loi d'ingénierie maison interdit (« saturer en
douceur, pas borner dur ») : au voisinage du seuil, deux pixels quasi identiques donnent l'un une
lueur pleine, l'autre rien. En mouvement, ça **clignote**. La courbe retenue (Karis) est quadratique
sur une largeur de genou puis linéaire :

```
genou = seuil / 2                       (pas de bouton de plus : un réglage qu'on ne saurait pas régler)
doux  = clamp(luma - seuil + genou, 0, 2·genou)² / (4·genou)
part  = max(doux, luma - seuil) / max(luma, ε)
```

`part` est la **fraction** de la couleur qui brille, pas une couleur : la teinte est ainsi préservée,
là où seuiller chaque canal séparément décalerait la couleur d'un pixel dont un seul canal dépasse.

## 6. Risques

1. ⚠️ **`setViewFramebuffer(view, {})` ne détache RIEN** — trouvé en lisant le code avant d'écrire.
   `BgfxDevice` fait `if (handle.id >= m_framebuffers.size()) return;`, donc un handle invalide sort
   par la porte de derrière. Le module s'en sert **déjà** (ligne 862) pour rendre la vue 0 quand
   l'éclairage s'éteint : la vue reste attachée à un framebuffer qui est détruit à la ligne suivante.
   Personne ne l'a vu parce qu'aucun consommateur n'éteint l'éclairage après l'avoir allumé. Mon
   chemin « bloom éteint après avoir été allumé » en dépend, donc c'est corrigé ici, avec son test
   rouge au niveau RHI (attacher, rendre, détacher, rendre une autre couleur, vérifier que la cible
   n'a **pas** bougé).
2. **Le test GPU ne peut pas lire la vue du composite** quand le bloom est actif : elle va dans la
   cible HDR, que le module réattache à chaque frame. Il doit lire la **vue de présentation**. C'est
   le même piège qu'en L1 (§7 de lighting-2d.md), un cran plus loin — et un test qui lirait le
   composite mesurerait la frame **avant** la lueur et passerait au vert en ne prouvant rien.
3. **Le discriminant du glow** doit être un pixel **hors du rayon de la lampe**. La retombée y vaut
   *exactement* 0 par construction, donc toute lumière mesurée là ne peut venir que du bloom. Mesurer
   au centre de la lampe ne discriminerait rien : il est déjà saturé.
4. **`radius` grand = anneaux.** Le noyau est un 9-tap dont on écarte les taps ; au-delà d'une
   soixantaine de pixels, les taps se séparent visiblement et la lueur montre des cernes au lieu d'un
   dégradé. La vraie réponse serait une chaîne de mips ; ce n'est pas cette tranche. À documenter comme
   limite, pas à cacher.
5. **Redimensionnement** — les cibles bloom sont filles des cibles d'éclairage ; leur libération doit
   être accrochée à celle des cibles d'éclairage, sinon un redimensionnement laisse un HDR à l'ancienne
   taille échantillonné par une présentation à la nouvelle.

## 7. Ce qui a été mesuré

`LightingGpu [bloom]`, lampe r=30 i=4 sur une scène blanche, ambiant à 4 %, bloom `intensity 2`,
`radius 40` :

| Mesure | Bloom éteint | Bloom allumé | Ce que ça prouve |
|---|---|---|---|
| **plomberie** (seuil inatteignable) | 10 / 255 | **10 / 255** | le trajet HDR ne déforme rien, à l'octet près |
| **lueur**, hors du rayon de la lampe | 10 | **40** | de la lumière là où la retombée vaut *exactement* 0 |
| **localité**, dans le coin | 10 | **10** | la lueur est locale, pas un éclaircissement global |

**Sabotages** (chacun n'a fait tomber que ce qu'il devait) :

- écartement du flou forcé à 0 → la lueur ne sort plus de la lampe (10, inchangé) ;
- présentation échantillonnant la cible HDR au lieu de la lueur floutée → la plomberie **et** la
  localité tombent. ⚠️ **La mesure de LUEUR passait encore** : sans le contrôle de localité, cette
  faute traversait le test. C'était sa raison d'être, écrite au §6.3 avant le code — et c'est la
  meilleure preuve que cette section valait la peine d'être écrite.

## 8. ⚠️ Écarts et erreurs de ce plan

**La justification du genou doux était fausse** (corrigée au §5, en place). J'avais écrit que le seuil
net produisait une *discontinuité de valeur* qui scintille ; il n'en produit pas. Le test que ce plan
appelait aurait donc passé au vert avec un seuil net. C'est la troisième occurrence de la semaine du
« discriminant qui ne discrimine pas », et la seule chose qui l'a attrapée est d'avoir saboté le code
pour vérifier ce que le test attrapait vraiment.

**Ma marge de test était devinée deux fois.**
1. Sur la courbe : j'avais écrit « la fraction reste sous 0,10 à 95 % du seuil ». La vraie valeur est
   0,1066, et l'échec n'apprenait rien. La borne est maintenant **calculée** et écrite dans le test.
2. Sur le GPU : le premier point d'échantillonnage était à 15 px hors du bord de la lampe et donnait
   **+5 sur 255** — réel, mais faible, et pour une raison physique (seule la partie du disque dont la
   luminance dépasse le seuil brille, et le flou dilue cette petite tache d'un facteur ~13). En
   mesurant 5 px hors du bord, l'effet est de **+30** pour un discriminant identique. Ce n'est pas un
   réglage pour passer : le plancher de bruit est mesuré à 0 par le contrôle de localité.

**B2 et B3 sont un seul commit.** Les séparer aurait livré une passe de présentation qui ne fait
qu'une copie — un état intermédiaire sans valeur propre. Le test de plomberie, lui, a bien été écrit
séparément, ce qui donne le bénéfice qu'on cherchait à la découpe.

**Pas de branche Metal, au lieu d'une branche Metal.** Le plan disait « placeholder » ; en pratique
la branche a été **retirée**, comme pour `fs_nebula`, parce qu'une branche `Metal` pointant vers des
symboles inexistants ne compile pas — et qu'en écrire une qui retombe sur SPIR-V déguiserait le trou.
Le repli SPIR-V est explicite et commenté.

**Trois programmes réutilisent `vs_composite`** — délibérément l'inverse de la règle « deux programmes
ne partagent pas un étage » écrite pendant la session A4. Cette règle venait d'un diagnostic faux
(un artefact de build périmé, [known-annoyances §3bis](known-annoyances.md)). Dupliquer trois fois un
vertex pour obéir à une règle démentie aurait été ajouter de la dette au nom d'une erreur.

**Le risque n°1 s'est réalisé, et plus tôt que prévu** : le détachement de vue était bien cassé. Il
est corrigé côté RHI avec son test rouge (`RhiReadbackGpu [unbind]`), et le rouge a montré la cible
recevant `0xEE` après un détachement censé la protéger — le défaut est donc *constaté*, pas seulement
lu dans le code.

## 9. Hors périmètre, explicitement

- **Pas de chaîne de mips** (donc pas de très grands rayons propres) — voir risque 4.
- **Pas de tonemapping ni de courbe de couleur.** La passe de présentation est l'endroit où ils
  atterriront, et c'est une raison de son existence, mais elle ne fait ici qu'ajouter la lueur.
- **Pas de bloom sur le HUD** — c'est un choix, pas un oubli (§4).
- **Pas de lens dirt, pas de flares, pas d'anamorphose.**
