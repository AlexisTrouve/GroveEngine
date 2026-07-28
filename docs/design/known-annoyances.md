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

**Conséquence** : un run complet ressort régulièrement à 181/182 pour une raison qui n'en est pas une,
et il faut à chaque fois relancer pour distinguer ça d'une vraie régression. Quelqu'un finira par
prendre l'un pour l'autre — dans un sens comme dans l'autre.

Pistes : `RUN_SERIAL` sur ces trois cibles (CTest sait le faire), ou des budgets de temps plus larges.

⚠️ **`IOSystemStress` n'appartient PAS à cette liste** : il échouait pour une vraie corruption de tas,
corrigée le 2026-07-28 (`docs/design/iosystemstress-heap-corruption-handoff.md`). Ne pas le ranger avec
les autres sous prétexte qu'il est lourd — l'erreur a déjà été faite une fois.

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
