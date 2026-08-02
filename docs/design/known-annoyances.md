# Nuisances connues (non bloquantes, mais elles coûtent du temps à qui les découvre)

Petites dettes qui ne méritent pas leur propre chantier mais qui piègent, encore et encore, quiconque
lit un run de tests ou prépare un commit. Consignées pour qu'on cesse de les redécouvrir.

## 1. `tests/modules/TestModule.cpp` s'invite dans les commits

`tests/helpers/AutoCompiler.cpp` **réécrit la source** de `TestModule.cpp` pendant les tests de
hot-reload (il change `moduleVersion = "vN"` pour forcer une recompilation). Le fichier est donc
modifié par le simple fait de lancer la suite.

**Conséquence** : un `git add -A` après un run embarque cette modification. C'est arrivé **deux fois
en une journée** (sorti des commits à chaque fois), et l'historique montre que le fichier a déjà
churné ainsi par le passé.

*Ce n'est pas un candidat au `.gitignore`* : le fichier est une source compilée, il doit rester suivi.
Pistes : que l'AutoCompiler travaille sur une **copie dans le répertoire de build**, ou qu'il
**restaure** la valeur d'origine en fin de test.

## 2. Trois tests lourds échouent sous `ctest -j`, passent en série

`StressTest`, `MemoryLeakHunter` et `ChaosMonkey` échouent par intermittence sous `ctest -j2`/`-j4` et
passent systématiquement lancés seuls (mesuré : 3/3 chacun, durées stables ~40-160 s). Ils sont affamés
par le parallélisme, pas cassés.

> ⚠️ **CORRECTION du 2026-07-31 — `ChaosMonkey` n'appartient PLUS à ce paragraphe.** Il échoue
> désormais **3 fois sur 3 lancé SEUL**, machine à 11 % de charge, sur une assertion de budget
> d'horloge. Ce n'est plus de la famine : voir **§2ter**. La phrase ci-dessus reste vraie pour
> `StressTest` et `MemoryLeakHunter`. Laissée telle quelle avec cette réserve plutôt que réécrite,
> parce que la mesure d'origine était juste **à sa date** — c'est le comportement qui a changé.

**Conséquence** : un run complet ressort régulièrement à 181/182 pour une raison qui n'en est pas une,
et il faut à chaque fois relancer pour distinguer ça d'une vraie régression. Quelqu'un finira par
prendre l'un pour l'autre — dans un sens comme dans l'autre.

Pistes : `RUN_SERIAL` sur ces trois cibles (CTest sait le faire), ou des budgets de temps plus larges.

⚠️ **`IOSystemStress` n'appartient PAS à cette liste** : il échouait pour une vraie corruption de tas,
corrigée le 2026-07-28 (`docs/design/iosystemstress-heap-corruption-handoff.md`). Ne pas le ranger avec
les autres sous prétexte qu'il est lourd — l'erreur a déjà été faite une fois.

### 2bis. `ProductionHotReload` a un budget en temps d'horloge, franchi de 1 %

Observé le 2026-07-30 sur un run complet : **échec sur `Reload time should be < 1000ms`, mesuré
1009,72 ms**. Lancé seul juste après : **passe en 31 s**, avec un rechargement très en dessous du
budget. L'état était préservé, la recompilation avait réussi — seul le chronomètre a parlé.

**Ce que ça vaut** : le test contient une assertion sur un temps d'horloge **absolu**, et ce temps
inclut une **recompilation de module** — donc il dépend de la charge de la machine et du cache disque,
pas du code testé. À 1 % de dépassement, la bonne lecture est « la machine était occupée », et le
réflexe est de **relancer seul avant de chercher une régression**.

**Piège** : ce dépassement se présente comme un échec fonctionnel dans le résumé de `ctest` (`Failed`,
pas `Timeout`), au milieu d'un run de 200 tests. Il ressemble donc davantage à un vrai bug que les
échecs de famine du §2, alors qu'il en est le cousin.

### 2ter. `ChaosMonkey` franchit son budget de 17 %, machine au repos (2026-07-31)

Troisième cas de la même famille, et **le plus net** — c'est ce qui en fait un motif et non une
anecdote.

```
❌ ASSERTION FAILED: Total duration should be < 60 seconds
   Expected: < 60      Actual: 70.128
```
`tests/integration/test_02_chaos_monkey.cpp:211`, plafond **en dur**, commenté
« *Just check it completed within reasonable bounds* ».

**Ce qui distingue ce cas des deux précédents, et pourquoi il compte davantage :**

| | §2 (famine) | §2bis (`ProductionHotReload`) | §2ter (`ChaosMonkey`) |
|---|---|---|---|
| Se reproduit seul ? | non, passe 3/3 | non, passe seul | **OUI, 3/3 échecs** |
| Charge machine | `-j2`/`-j4` | occupée | **11 %** |
| Dépassement | — | 1 % | **17 %** |

À 1 % sur une machine chargée, « la machine était occupée » est la bonne lecture. À **17 % sur une
machine au repos**, non : le budget lui-même ne convient plus à ce poste.

**Le test n'est pas en défaut sur le fond** — toutes les récupérations réussissent, aucun blocage,
la croissance mémoire passe. Seul le chronomètre parle. Le coût est dominé par ~430 ms par
récupération, sommés sur toutes les injections de panne.

⚠️ **Hypothèse concurrente NON tranchée** : les trois mesures ont été prises après un build complet
de plus de dix minutes, et ce poste **sature thermiquement sous compilation** (cf. `build-speed.md`).
Une mesure **à froid** départagerait « budget trop serré » de « poste bridé ». Élément de contexte :
lors d'un des runs, la suite complète a tourné **18 % plus vite** que la fois précédente (370 s contre
453 s) et `ChaosMonkey` a **quand même** échoué — ça affaiblit l'hypothèse thermique sans l'éliminer.

⚠️ **La CI Linux le passe** (189/189). Le rouge est propre à ce poste.

**Piège de résolution à éviter** : lui coller le label `timing-sensitive` le **retirerait de la CI**,
où il passe — on échangerait une couverture réelle contre le confort d'un vert local. Si le budget
doit bouger, la piste est de garder les assertions de **correction** comme barrière (pas de blocage,
mémoire, récupérations réussies) et de traiter la durée comme une **métrique rapportée**.

> **Ce que les trois cas disent ensemble** : un budget d'horloge absolu est une propriété du
> **matériel** déguisée en propriété du **code**. Il finira toujours par tomber, et son échec
> n'apprend rien sur la correction — tout en ressemblant, dans le résumé de `ctest`, à un vrai bug.

### 2quater. `ErrorRecovery` rejoint la famille — et **`ChaosMonkey` n'est plus seul** (2026-07-31, soir)

```
❌ ASSERTION FAILED: Recovery time should be < 500ms
   Expected: < 500     Actual: 555.653
   tests/integration/test_06_error_recovery.cpp:255
```

Même signature exactement : **toutes les phases fonctionnelles passent** (l'état est extrait, le
hot-reload aboutit, le module redevient sain, 120 frames de stabilité), et seule l'assertion
chronométrique finale tombe. Dépassement **11 %**. Il échoue **seul** comme en suite, donc ce n'est
pas de la contention.

⚠️ **Ce que ça change pour qui lit `ctest` : la phrase « 1 rouge = état sain » ne tient plus.** Ce
soir-là la suite complète est ressortie à **4 rouges sur 210** — et les quatre étaient innocents :

| Test | Seul | En suite | Famille |
|---|---|---|---|
| `ChaosMonkey` | ❌ | ❌ | budget d'horloge (§2ter) |
| `ErrorRecovery` | ❌ | ❌ | **budget d'horloge (celle-ci)** |
| `StressTest` | ✅ | ❌ | contention (§2) |
| `MultiVersionCoexistence` | ✅ | ❌ | contention (§2) |

Le **nombre** de rouges n'est donc pas un signal : il suit la vitesse du poste. Ce soir-là la suite a
mis **757 s** contre ~450 s mesurées trois jours plus tôt — 68 % plus lente, et deux budgets absolus
sont passés du bon côté au mauvais sans qu'une ligne de code ait bougé. Le signal utilisable est le
**nom** du test et la **raison** de son échec, jamais le compte.

**La méthode qui a tranché en cinq minutes**, à réutiliser telle quelle :

1. relancer les inattendus **seuls** — ça sépare contention et échec dur ;
2. lire l'assertion, pas le résumé — « Recovery time should be < 500ms » se lit d'un coup d'œil ;
3. **comparer la date du binaire à celle de l'édition** :
   ```bash
   stat -c "%y  %n" build/tests/test_06_error_recovery.exe modules/BgfxRenderer/Scene/SceneCollector.cpp
   ```
   L'exécutable datait de 09:51, l'édition de 20:32 : **le binaire qui échouait était celui d'avant le
   travail**, jamais recompilé. Ce n'est plus un argument, c'est une preuve. (C'est le piège
   « artefact périmé » du §3bis pris **par l'autre bout** : là il cachait un correctif, ici il
   démontre une innocence.)

> **Leçon de méthode** : devant N rouges après un changement, l'instinct est de chercher lequel on a
> cassé. La question moins chère est **« mon code est-il seulement DANS ce binaire ? »** — deux de ces
> quatre tests ne lient même pas `BgfxRenderer`.

## 3bis. ⚠️ Un artefact périmé se déguise en CORRUPTION DE TAS

**Symptôme** : un test `[gpu]` meurt en `0xC0000374` (corruption de tas) *après* son dernier assert,
pendant le teardown du renderer — et sur un test **sans rapport** avec ce qu'on vient de modifier
(vu sur `AssetSpriteGpu`, `RuntimeTextureGpu`, `AssetAsyncModuleGpu`, `ModuleDependencies`).

**Cause** : un artefact de build incohérent, typiquement laissé par la nuisance n°3 ci-dessus (une
commande qui compile pendant que la suite tourne, ou un build tué en cours).

**Remède** : `cmake --build build --target <la_cible_du_test>` puis relancer. Ça suffit.

### ⚠️ Le piège de diagnostic, qui coûte bien plus cher que le symptôme

Ce défaut est **déterministe** tant qu'on ne reconstruit pas — donc il ressemble à un vrai bug, et
une coupe différentielle « désigne » n'importe quelle modification récente. **Parce que chaque coupe
reconstruit la cible, et que c'est la reconstruction qui guérit.**

Coût réel payé le 2026-07-29 : un correctif entier (`vs_nebula.sc`) écrit, commité et documenté sur
une cause **inventée** — bgfx dédoublonnerait les shaders et déséquilibrerait son comptage. Vérifié
après coup : la configuration incriminée passe **5/5** avec un build propre. Le diagnostic était faux
de bout en bout, et trois « preuves » par coupe l'avaient confirmé.

**Règle** : avant d'attribuer une corruption de tas à un changement, vérifier que la variante *saine*
**échoue encore APRÈS reconstruction**. Sinon on ne mesure que l'effet de reconstruire.

### 3ter. Le `0xC0000374` qui SURVIT à la reconstruction — par salves, victime tournante (2026-08-01)

⚠️ **Tout ce qui précède décrit le cas où l'artefact périmé explique la corruption. Il existe un
second cas, et il ne s'explique pas comme ça.** Ne pas les confondre : le premier se guérit en
reconstruisant, le second non.

Mesuré le 01/08 sur un build **à jour** (`ninja: no work to do`) :

| | Résultat |
|---|---|
| `AtlasPackerGpu` seul (ctest, ×3) | ✅ vert |
| `AtlasPackerGpu` seul (binaire, ×6, **depuis `build/tests`**) | ✅ vert |
| Tête du jeu GPU, tests 193→199 | ✅ 7/7 |
| Jeu GPU complet, 5 passes **d'affilée** | ❌ **5/5 sur `AtlasPackerGpu`**, `0xC0000374` |
| Jeu GPU complet, 2 passes **30 min plus tard** | ✅ 17/17 |

**La victime tourne** : `AssetTopicsGpu` le matin même, `AtlasPackerGpu` l'après-midi, plus les
quatre du §3bis. Ce n'est donc pas « tel test est fragile », c'est **une corruption de tas au
teardown du renderer qui frappe le test GPU qui passe par là**.

**Et ça vient par SALVES** — c'est l'indice le plus utile du lot. Une fois installé, l'échec se
répète en série ; puis il disparaît sans que rien n'ait été reconstruit. Ça oriente vers un **état
externe persistant qui se dégrade puis se rétablit** (pilote GPU, mémoire allouée par le pilote),
pas vers une course interne au processus — qui, elle, donnerait des échecs dispersés.

⚠️ **`NineSliceGpu` n'y est pour rien.** Il a porté l'étiquette « intermittence résiduelle » pendant
des jours ; mesuré : **0 échec sur 12** lancé seul, et **vert dans les trois exécutions de suite
complète enregistrées**. L'étiquette ne venait que d'une **seule observation**, consignée dans un
handoff puis relue comme un fait établi.

**Deux erreurs de mesure commises en établissant ça**, gardées parce qu'elles se referont :

- **Cinq passes dans la MÊME fenêtre de quatre minutes ne sont pas cinq échantillons.** Elles
  partagent l'état transitoire qui cause la panne — j'en ai conclu « déterministe », et 30 minutes
  plus tard c'était vert. *Répéter dans une fenêtre ne teste pas ce que répéter à travers des
  fenêtres teste.*
- **Mauvais répertoire courant** : le test lit `"../../assets/..."`, donc il faut le lancer depuis
  `build/tests` (ce que fait ctest), pas depuis `build/`. Six faux échecs `rc=1` — et `rc=1` (une
  assertion) n'est PAS `0xC0000374` (une corruption) : la différence de code de sortie disait déjà
  que je ne regardais pas la même panne.

⚠️ **UN BISECT DÉSIGNE UN COUPABLE FAUX — vécu le 02/08, et c'est le piège le plus cher du lot.**

Chaîne de mesures obtenue en une heure, toutes exactes :

| Mesure | Résultat |
|---|---|
| `NineSliceGpu` à HEAD | ❌ 4/4 |
| à HEAD, changement du jour retiré | ❌ 3/3 |
| au commit précédent le travail du jour | ✅ 3/3 |
| ⟹ un seul commit de production entre les deux | *« c'est lui »* |
| le commit accusé, re-testé directement | ✅ 3/3 |
| HEAD, re-testé 20 min plus tard | ✅ **4/4** |

**La salve s'est arrêtée entre deux points de mesure. Le bisect ne mesurait pas le code, il mesurait
l'horloge.** Et il produisait une chaîne de preuve impeccable en désignant un innocent — ici un
refactor dont le binaire accusé **ne liait même pas le module modifié**.

C'est une aggravation du §3bis : là, chaque coupe reconstruisait et la reconstruction guérissait, donc
au moins la cause était dans le protocole. Ici **rien dans le protocole ne rattrape** — la seule
question qui a sauvé la mise est celle du §2quater : *mon code est-il seulement DANS ce binaire ?*

> **Règle** : devant un `0xC0000374`, **ne jamais bisecter**. Établir d'abord si le défaut est une
> salve (re-tester le MÊME binaire à 20 minutes d'intervalle). Un bisect sur un phénomène
> intermittent est une machine à fabriquer des faux coupables.

**Victimes observées le 02/08, toutes dans la même journée** : `AssetTopicsGpu` (matin, puis à
nouveau le soir), `AtlasPackerGpu` (après-midi, 5/5 dans une fenêtre), `NineSliceGpu` (4/4 seul, puis
vert 4/4 vingt minutes après), `UIDemoGpu`. Quatre noms, aucun stable — et à un moment, `NineSliceGpu`
passait pendant que deux autres échouaient.

**Geste suivant si on veut la cause** : une campagne longue et détachée — boucler le jeu GPU pendant
une heure en horodatant chaque salve, pour voir si elle corrèle avec la charge, la thermique, ou un
nombre cumulé de créations/destructions de device. Pas une session interactive : le phénomène a une
constante de temps de l'ordre de la dizaine de minutes.

## 3. ⚠️ NE JAMAIS bâtir pendant que la suite tourne

**`ProductionHotReload` et `RaceConditionHunter` sont eux-mêmes clients du système de build** :
`tests/helpers/AutoCompiler.cpp` lance `ninja -C .. TestModule` à chaque itération, pour de vrai. Un
`cmake --build` lancé en parallèle met donc **deux ninja sur le même dossier**.

Coût mesuré : **deux heures**, le 2026-07-28, sur trois symptômes successifs qui n'avaient rien à voir
avec le code :

1. `ProductionHotReload` en *Timeout* + `RaceConditionHunter` en échec, avec
   « Compile success rate too low: 20 % » — les deux ninja se marchaient dessus ;
2. `.ninja_deps` **corrompue** (« ninja: warning: premature end of file; recovering ») → ninja perd
   les dépendances d'entêtes et **rebâtit tout à chaque invocation**, y compris bgfx. Chaque tentative
   de rattrapage relançait une reconstruction complète ;
3. un build **tué en plein lien** a laissé `libUIModule.dll` **tronquée** → **20 E2E UI rouges d'un
   coup**, avec `LoadLibrary failed with error code 193` (= image invalide, pas un bug de widget).

**Comment le reconnaître** : `error 193` au chargement d'un module, ou « premature end of file » de
ninja, ou un taux de compilation anormalement bas dans un rapport de hot-reload. Aucun de ces trois
n'est un défaut du moteur.

**Comment s'en sortir** : `rm build/.ninja_deps` (c'est un cache, sa suppression coûte une
reconstruction complète et rien d'autre), puis rebâtir les cibles de modules (`UIModule`,
`BgfxRenderer`, `InputModule`) pour remplacer les DLL éventuellement tronquées, puis relancer la suite
**sans rien toucher**.

*Leçon générale* : quand une suite entière vire au rouge d'un bloc après un changement local et
circonscrit, l'hypothèse par défaut n'est pas « j'ai tout cassé » mais « l'artefact que je teste n'est
pas celui que je crois ». Chercher l'erreur réelle (ici : le code d'erreur de `LoadLibrary`) avant de
soupçonner le code — c'est la même discipline que « localiser par la mesure avant de fixer ».
