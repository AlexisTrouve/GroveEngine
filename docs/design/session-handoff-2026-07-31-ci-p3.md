# Handoff — session du 31 juillet 2026 : CI, ferme bloquée, dette P3

> **État à la sortie** : `master` = **`91e409e`**, arbre propre, poussé sur gitea **et** github
> (mêmes SHAs). Aucun force push. **CI verte** (189/189 + cross-compilation Windows). Un seul
> worktree, un seul checkout.

Ce document garde ce qui n'est dans aucun doc de chantier : ce que la journée a **appris**, et les
erreurs commises en la faisant. Le détail technique est dans
[build-speed.md §7](build-speed.md) · [audit-perf-mess-2026-07-29.md §P3](audit-perf-mess-2026-07-29.md)
· [known-annoyances.md §2ter](known-annoyances.md).

---

## 1. Ce qui a été livré

| Chantier | Commits | Résultat |
|---|---|---|
| CI — modules, branches, verrou | `eabfb58` → `edb9cb0` | **103 → 189 tests validés** |
| Nettoyage dépôt | `6467f4d` | `RUST_MIGRATION.md` retiré, worktree unitaire |
| Dette P3 | `4bbe07d`, `bd67f3b`, `91e409e` | **1108 → 409 lignes** sur deux fonctions |

Plus : **ProjectMind remis d'aplomb** (son plan actif datait de mai et décrivait un moteur deux
phases en retard) et **la ferme débloquée** après 14 h d'arrêt.

---

## 2. Les deux découvertes qui recadrent un sujet

### 2.1 Le trou de la CI n'était pas celui qu'on croyait

La tâche disait « la CI ne valide pas le rendu, à cause du label `gpu` ». **Faux sur les deux points.**

Le job configurait `cmake -B build` **sans aucun `-DGROVE_BUILD_*`**, et les défauts sont OFF pour
cinq modules. Ces tests n'étaient donc pas *exclus*, ils étaient **ABSENTS de l'arbre** — et **aucun
filtre par label ne pouvait le montrer**. La CI validait 103 tests sur 189, c'est-à-dire la moitié du
moteur, en paraissant verte.

Et le label `gpu` **n'écarte rien** : les 16 tests GPU sont gardés par `WIN32`, donc sur Linux ils ne
se *construisent* pas. Retirer le label ne les ferait pas apparaître.

> **La leçon** : un raisonnement sur ce qui est *filtré* ne dit rien sur ce qui est *construit*.
> Chercher l'exclusion quand le problème est l'absence, c'est fouiller la mauvaise couche — et le
> symptôme (« il manque des tests ») est identique dans les deux cas.

### 2.2 Une partie de « l'intermittence » n'en est pas

`ChaosMonkey` figurait dans la liste des échecs tournants. Mesuré : **3 échecs sur 3 lancé seul,
machine à 11 %**. Pas une course, pas un crash — **une assertion de budget d'horloge**, 70 s contre un
plafond en dur de 60. Toutes les récupérations réussissent.

C'est le **troisième** cas de cette famille dans le dépôt (avec `ProductionHotReload` et les tests
affamés sous `-j`), et le plus net : les deux autres passaient seuls.

> **Un budget d'horloge absolu est une propriété du MATÉRIEL déguisée en propriété du CODE.** Il
> finira toujours par tomber, son échec n'apprend rien sur la correction, et il ressemble à un vrai
> bug dans le résumé de `ctest`.

⚠️ Non tranché : les mesures ont suivi un build de 10 min sur un poste qui sature thermiquement. Une
mesure **à froid** départagerait « budget trop serré » de « poste bridé ». Élément contraire retenu :
une fois, la suite a tourné **18 % plus vite** et `ChaosMonkey` a **quand même** échoué.

---

## 3. La leçon centrale — et elle a récidivé quatre fois

> **Le réglage qui rend un résultat FACILE, PRÉVISIBLE ou RAPIDE à obtenir est très souvent celui qui
> le rend INCAPABLE DE DISCRIMINER.**

En cherchant à isoler une variable, on choisit spontanément la valeur la plus « propre » — neutre,
ronde, large. Or **neutre veut souvent dire sans effet**, et **large veut dire qui capte aussi autre
chose**. Le confort du réglage et son pouvoir discriminant tirent en sens opposés.

Les quatre récidives de la journée, toutes rattrapées mais jamais du premier coup :

1. **Faux détenteur de verrou tenu 30 s**, alors que la synchro en dure plus. Le script est passé au
   vert **sans jamais emprunter le chemin testé**.
2. **Fenêtre `-newermt "-40 min"`** pour trouver le log de la CI : elle captait aussi le run
   **précédent**. « Verdict après 30 s : succeeded » venait de l'ancien job.
3. **`find -printf "%TH:%TM" | sort`** : tri sur l'heure sans la date, donc des logs de la veille en
   tête de liste.
4. *(la veille, même famille)* ambiant **blanc** choisi pour la prévisibilité — neutre par
   construction, donc test vert contre l'implémentation fausse.

**La parade, avant de croire un vert** : *quel résultat aurait donné la version FAUSSE ?* Si la
réponse est « le même », la mesure ne vaut rien.

C'est directement pour ça que les deux refactors ont été validés par **comparaison texte des corps
contre l'original** (`git show HEAD`) et pas seulement par la compilation : **le compilateur attrape
une capture manquante, pas un corps qu'un script aurait silencieusement tronqué.**

---

## 4. Mes erreurs, gardées parce qu'elles se reproduiront

- **Deux affirmations rétractées en cours de session** : « le vert de la CI ne dit rien du rendu »
  (faux — le rendu *headless* est couvert) et « je ne peux pas montrer le log de la CI » (faux — il
  est dans `/var/lib/gitea/data/actions_log/`, seul le workspace du runner est effacé). Les deux
  figuraient dans un sitrep avant d'être corrigées.
- **Syntaxe here-string PowerShell dans l'outil Bash** → un `@` dans le sujet du commit. Amendé avant
  tout push. Les deux outils ont deux syntaxes, ce n'est pas interchangeable.
- **Première mesure sur la ferme perdue** : pilotée à travers ma session SSH, tuée par la limite de
  tâche de fond alors qu'elle attendait encore dans la file. Le travail distant doit être **détaché
  côté serveur**.

---

## 5. Ce qui reste

**`ChaosMonkey`** — rouge dur, caractérisé, non résolu. Décision en attente : que fait-on d'un budget
d'horloge absolu ? ⚠️ **Piège** : lui coller `timing-sensitive` le retirerait de la CI **où il passe**,
échangeant une couverture réelle contre un vert local.

**P3, deux fonctions** — `SceneCollector::finalize` (430) et `UIModule::updateUI` (452).
⚠️ **La seconde n'est PAS justiciable du même geste** : aucune répétition, de l'état qui circule, y
découper des phases est un pari. Elle ressemble aux autres **par sa taille seulement**.

**Les 16 tests `[gpu]`** — seul trou de couverture CI restant, et il est étroit. N'arrivera que sur
une ferme **Windows** à GPU.

**L'intermittence résiduelle** — `NineSliceGpu` a bien le profil (rouge en suite, vert 3/3 seul).
Toujours non diagnostiquée.

---

## 6. Recettes

**Travail long sur la ferme — détacher côté serveur**, sinon il meurt avec la session SSH :
```bash
ssh HOST 'cat > ~/travail.sh' < script.sh
ssh HOST 'chmod +x ~/travail.sh && setsid nohup ~/travail.sh > ~/travail.log 2>&1 </dev/null & echo lance'
```
La redirection vers un **fichier** est essentielle : écrire sur le tube d'un SSH mort donne un
SIGPIPE en pleine tâche (vécu — un build orphelin est mort à 92 %).

**Verdict d'un run de CI** : `/var/lib/gitea/data/actions_log/<org>/<repo>/<n>/<id>.log` (`sudo`).
Le workspace du runner est effacé en fin de job ; l'API `/actions/runs` répond **403** avec le jeton
du vault.

**Guetteur de CI** : borner sur un horodatage **postérieur au dernier log existant**, jamais sur une
fenêtre glissante. Et couvrir `Job succeeded|Job failed` — un moniteur qui ne guette que la bonne
nouvelle reste muet sur un plantage, et ce silence est indiscernable de « ça tourne encore ».

**Base de référence avant tout refactor** : la suite AVANT de toucher au code. Sans elle, un rouge
préexistant est attribué au chantier en cours — et ce jour-là il y en avait deux.
