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
