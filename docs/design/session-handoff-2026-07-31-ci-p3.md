# Handoff — session du 31 juillet 2026 : CI, ferme bloquée, dette P3

> **État à la sortie** : `master` = **le commit qui porte ce document** (`da677e2` au moment de
> l'écrire), arbre propre, poussé sur gitea **et** github (mêmes SHAs). Aucun force push.
> **CI verte** (189/189 + cross-compilation Windows). Un seul worktree, un seul checkout.
>
> *Formulé ainsi parce qu'un handoff qui grave un SHA se périme au commit suivant — le sien. Celui
> d'hier annonçait un état antérieur d'un commit à sa propre existence.*

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
| Dette P3 | `4bbe07d` → `b731aa1` | **1538 → 690 lignes** sur trois fonctions |

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

## 3. La leçon centrale — cinq récidives, dont une à l'envers

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

### 3bis. Le même défaut À L'ENVERS — et il est plus vicieux

Cinquième occurrence, en fin de session, et la seule de son sens : l'outil qui devait **prouver**
que quinze blocs de `finalize` étaient factorisables a rapporté **« 15 blocs uniques, rien de
factorisable »**. C'était lui qui **sous-normalisait** — le compteur au singulier
(`packet.lightCount` pour le champ `lights`), puis la variable locale parfois homonyme du champ.
Trois passes ont été nécessaires pour qu'il converge.

> Une mesure trop LARGE conclut à tort qu'il y a un motif. Une mesure trop ÉTROITE conclut à tort
> qu'il n'y en a pas. **Le second cas ne déclenche aucune alarme** : « rien à factoriser » ressemble
> à une réponse légitime, on referme le sujet et on passe à autre chose. Le premier, au moins, finit
> par produire un test rouge.

Ce qui a sauvé la mise : diffuser deux blocs *normalisés* côte à côte au lieu de croire le verdict
agrégé. Le diff a montré immédiatement que la différence n'était pas dans le code mais dans mon
normaliseur.

### 3ter. Deux gestes, deux preuves — elles ne sont pas interchangeables

Les deux `setConfiguration` **déplaçaient** du code : la preuve était la **comparaison texte des
corps contre l'original** (`git show HEAD`), 10/10 puis 23/23. Le compilateur attrape une capture
manquante ; il n'attrape **pas** un corps qu'un script aurait silencieusement tronqué.

`SceneCollector::finalize` **réécrivait** : cette preuve ne s'appliquait plus. Elle a été remplacée
par deux autres — l'uniformité *prouvée* par normalisation (§3bis), et un **test de caractérisation
écrit avant de toucher au code**, sur un chemin que la mesure de couverture disait tenu par **zéro
test**.

> **La leçon transverse** : la nature du geste dicte la nature de la preuve. Appliquer la preuve du
> déplacement à une réécriture, c'est se rassurer avec un contrôle qui ne contrôle plus rien.

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
- **Une accolade orpheline laissée en remplaçant un bloc**, qui formait un bloc vide avec celle qui
  restait. **Ça compilait sans broncher** — un bloc vide est syntaxiquement valide. Repérée en
  relisant la zone, pas par le compilateur. ⚠️ La compilation ne valide pas ce qu'on croit : elle
  valide la grammaire, pas l'intention.
- **Un `mv` de restauration qui a rendu le fichier avec sa date d'ORIGINE**, antérieure à l'objet
  compilé : ninja n'a rien reconstruit et le binaire *saboté* tournait encore. Symptôme : un test
  rouge sur un source **identique à `HEAD`**. J'ai failli conclure « test instable » ou « j'ai cassé
  quelque chose » — les deux faux. Diagnostic par comparaison des dates, puis `touch`. C'est le piège
  « artefact périmé » déjà documenté dans `known-annoyances.md §3bis`, rencontré sous une forme
  nouvelle : ce n'est pas l'édition qui l'a déclenché, c'est la **restauration**.
- **Un `find -printf` trié sans la date** qui remontait des logs de la veille en tête — corrigé en
  triant sur l'horodatage epoch. Trivial, mais c'est la troisième forme que prend le même défaut de
  mesure dans la même journée.

---

## 5. Ce qui reste

**`ChaosMonkey`** — rouge dur, caractérisé, non résolu. Décision en attente : que fait-on d'un budget
d'horloge absolu ? ⚠️ **Piège** : lui coller `timing-sensitive` le retirerait de la CI **où il passe**,
échangeant une couverture réelle contre un vert local.

**P3, une seule fonction** — `UIModule::updateUI` (452). ⚠️ **Elle n'est PAS justiciable des gestes
de ce chantier** : ni table déguisée à révéler, ni blocs jumeaux à factoriser — aucune répétition, et
de l'état qui circule entre les étapes. Elle ressemble aux quatre autres **par sa taille seulement**.

> `SceneCollector::finalize` (430 → 281) a été traitée en fin de session, avec un geste DIFFÉRENT des
> deux `setConfiguration` : on y RÉÉCRIT au lieu de DÉPLACER, donc la comparaison textuelle des corps
> ne s'appliquait plus. Elle a été remplacée par deux garde-fous — l'uniformité **prouvée** par
> normalisation (l'outil a d'abord menti dans l'autre sens : « rien de factorisable ») et un test de
> caractérisation écrit AVANT, sur un chemin que la mesure disait couvert par **zéro test**. Détail :
> [audit-perf-mess-2026-07-29.md §P3](audit-perf-mess-2026-07-29.md).

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

**Mesurer la COUVERTURE avant de refactoriser un chemin**, pas seulement après :
```bash
for p in xCount yCount ...; do printf "%-16s %s\n" "$p" "$(grep -rho "$p" tests/ | wc -l)"; done
```
Un zéro dans cette colonne veut dire « réécrire à l'aveugle ». C'est ce qui a imposé le test de
caractérisation sur `hudSectors` — et sans lui, la factorisation de `finalize` n'aurait eu aucun
filet sur le seul chemin que rien n'observait.

**Après avoir restauré un fichier, vérifier qu'il a bien été RECONSTRUIT** :
```bash
stat -c "%y %n" src.cpp build/.../src.cpp.obj    # l'objet est-il plus recent que la source ?
```
`cp`/`mv` peuvent rendre une source **plus vieille** que son objet ; ninja ne reconstruit alors rien
et l'ancien binaire tourne. Symptôme trompeur : un test rouge sur un source identique à `HEAD`. En
cas de doute, `touch` la source — c'est plus rapide que le diagnostic.

**Sabotage adverse** : vérifier qu'il a été APPLIQUÉ (assertion dans le script + `grep` de contrôle)
avant de croire le rouge qu'il produit — et vérifier que le rouge disparaît bien après restauration
**et reconstruction**. Les deux moitiés comptent : c'est la seconde qui a piégé ici.
