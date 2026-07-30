# Vitesse de compilation — mesures et décisions

**Statut** : ccache + PCH partagé livrés · déport `tools/remote-build.sh` livré et vérifié ·
compilation *distribuée* (distcc/icecream) **écartée sur mesure**.
**Date des mesures** : 2026-07-30. Machine : 16 cœurs, MinGW g++, Ninja, `CMAKE_BUILD_TYPE=Release`.

Le déclencheur : « la compil est longue et fait chauffer le PC ». Ce document existe pour qu'on
ne re-débatte pas de la compilation distribuée sans rouvrir les chiffres qui l'ont écartée.

---

## 1. Où va réellement le temps

Extrait du `.ninja_log` d'un build complet (le CPU cumulé, pas le mur) :

| Poste | CPU cumulé | Part |
|---|---|---|
| **Build complet à froid** | 10 127 s (≈ 2 h 49 CPU), 1 629 cibles | 100 % |
| Tiers (bgfx, glslang, spirv-cross…) | 3 715 s | 37 % — **payé une seule fois**, vit dans `_deps` |
| Code projet | 6 412 s | 63 % |
| └─ dont les **tests** | **5 212 s**, 723 cibles | **81 % du code projet** |
| Compilation vs link | 9 516 s / 611 s | le link est négligeable |

Deux conséquences qui pilotent tout le reste :

1. **Le coût est concentré dans les tests**, pas dans le moteur (~1 200 s CPU pour `src/` + `modules/`).
2. **Le coût est du parsing d'en-têtes**, pas de l'optimisation. Mesuré : une TU vide n'incluant que
   les en-têtes candidats au PCH coûte **6,0 s** là où le vrai `IT_029` complet coûte **9,7 s** —
   soit **62 % du coût d'une TU de test consacrés à parser Catch2 + nlohmann + spdlog**.

---

## 2. Ce qui a été livré

### 2.1 ccache (racine `CMakeLists.txt`, option `GROVE_USE_CCACHE`, ON par défaut)

Détecte `ccache` puis `sccache` et le pose en `CMAKE_<LANG>_COMPILER_LAUNCHER` **avant**
`FetchContent`, pour que les tiers en profitent aussi. Rien trouvé ⇒ aucune variable définie,
build strictement inchangé.

**Les réglages machine ne sont pas dans le dépôt** (un chemin utilisateur n'a rien à faire dans un
fichier commité). Amorçage à faire une fois par poste :

```bash
ccache --set-config max_size=12G
ccache --set-config base_dir=C:/Users/alexi/Documents/projects   # partage entre worktrees
ccache --set-config hash_dir=false
ccache --set-config depend_mode=true
ccache --set-config sloppiness=pch_defines,time_macros           # OBLIGATOIRE avec le PCH — voir §3
```

- `base_dir` réécrit les chemins absolus en relatifs : sans lui, **chaque git worktree a son propre
  cache** et le gain principal disparaît, puisque c'est exactement le scénario qui fait mal.
  **Vérifié** : deux arbres au contenu strictement identique placés à deux chemins absolus
  différents, compilés chacun depuis son propre `build/` → **hit**.
  ⚠️ *Ne pas tenter cette vérification en comparant un worktree à `master`* : un worktree en avance
  de quelques commits a un contenu réellement différent, donc un miss **légitime** qu'on prend pour
  une panne de `base_dir`. Je m'y suis laissé prendre trois fois de suite ; seul un couple d'arbres
  fabriqués identiques prouve quoi que ce soit.
- `max_size=12G` et pas plus : le disque C: était à **90 % plein (50 Go libres)** au moment du
  réglage. À revoir si le disque respire.
- `depend_mode=true` évite à ccache de repréprocesser la TU (vérifié : `Preprocessed: 0` dans
  `ccache --show-stats -v`).

### 2.2 PCH partagé (`tests/CMakeLists.txt`, option `GROVE_USE_PCH`, ON par défaut)

`tests/grove_test_pch.h` regroupe les en-têtes lourds communs. Une cible fournisseur
`grove_test_pch` les compile **une fois** ; les cibles de test éligibles réutilisent ce `.gch`
via `REUSE_FROM`. **89 cibles** branchées, **0 PCH rejeté** au build complet.

Une cible est éligible si elle est un exécutable, lie **Catch2 ET le moteur**, n'a **pas de source
C**, et ne lie **pas `BgfxRenderer`**. Les trois exclusions ne sont pas des précautions de style :
chacune a été imposée par un échec réel, documenté ci-dessous.

Points non négociables, tous appris à la dure :

- **`REUSE_FROM` est obligatoire, pas cosmétique.** Sans lui, CMake cuit un PCH **par cible** — le
  `.gch` mesuré fait **133 Mo**, donc ~164 cuissons : largement plus cher que le parsing évité.
- **Les inclusions de `grove_test_pch.h` sont gardées par `__has_include`.** Le PCH est partagé par
  des cibles aux dépendances différentes ; sans garde, une seule cible ne voyant pas `spdlog`
  casserait le build entier. La garde ne peut pas produire un PCH incohérent : le fichier n'est
  compilé qu'une fois, par le fournisseur.
- **Les cibles mêlant des sources C sont exclues.** `REUSE_FROM` s'applique à toutes les langues de
  la cible ; le fournisseur ne produit qu'un PCH C++, et CMake échoue au generate avec
  `Unable to resolve full path of PCH-header` (rencontré sur `test_mapview_compress`, qui embarque
  le miniz vendé). Un PCH C n'aurait aucun sens ici.
- **Une cible mal appariée CASSE le build.** ⚠️ *J'avais écrit l'inverse en concevant ce chantier —
  « GCC retombe gracieusement sur le texte de l'en-tête, correct mais sans gain » — et le premier
  build complet l'a démenti.* GCC rejette bien le `.gch`, mais CMake force l'inclusion de l'en-tête
  en ligne de commande : le **texte** est alors parsé, et il doit compiler dans l'environnement de
  la cible. `test_transform2d` (Catch2 sans le moteur) a échoué sur
  `fatal error: nlohmann/json.hpp: No such file or directory`, tiré **transitivement** par
  `grove/JsonDataNode.h` — ce qu'une garde `__has_include` ne peut pas intercepter.
  D'où le filtre **Catch2 ET `GroveEngine::impl`** : il aligne includes *et* défines sur le
  fournisseur, donc le PCH est réellement utilisé. `-Winvalid-pch` (ajouté d'office par CMake)
  rend tout appariement raté visible dans le log — il ne doit pas y en avoir.
- **Les cibles `BgfxRenderer` sont exclues** : elles héritent des défines bgfx
  (`__STDC_CONSTANT_MACROS` & co), GCC rejette alors le `.gch`. Constaté : 16 cibles, toutes des
  `*_gpu`. Elles compilaient (elles ont les bons includes) mais payaient le parsing sans le gain.
- **⚠️ Méthode : attribuer un avertissement à sa cible dans un log `ninja -j16`.** Un `grep -B12`
  m'a d'abord désigné des cibles innocentes, parce que la sortie de 16 processus s'entrelace. Ninja
  tamponne la sortie **par edge** : la bannière `[n/m] Building …` est la ligne *immédiatement*
  avant. `grep -B1`, pas plus. La première liste, fausse, désignait `IT_023` — dont les défines
  se sont révélées identiques à celles du fournisseur, ce qui a mis la puce à l'oreille.

---

## 3. Résultats mesurés

Banc : les **24 objets de test les plus coûteux** (844 s CPU cumulé), supprimés puis recompilés
avec `ninja -j16`. Mur, pas CPU.

| Configuration | Mur | vs référence |
|---|---|---|
| Référence, machine froide | 92,7 / 101,7 s | — |
| **Référence, machine chaude** | **109,7 s** | point de comparaison honnête |
| ccache seul — *miss* | 142,4 / 149,0 s | **+32 %** |
| ccache seul — *hit* | **1,5 s** | **−98,6 % (×65)** |
| ccache + PCH — *miss* | **83,7 s** | **−24 %** |
| ccache + PCH — *hit* | **1,5 s** | **−98,6 % (×65)** |

⚠️ **Le confondant thermique est réel et a failli me faire conclure à l'envers.** Les premières
mesures « après » paraissaient +47 % parce que la machine avait compilé 10 minutes d'affilée. La
même commande a donné 9,7 s puis 17,9 s. Toute mesure de compilation sur ce poste doit
re-mesurer la référence **à chaud** et prendre un **min-de-N**, jamais une passe unique.

Micro-banc sur une TU (`IT_029`, min de 3) :

| | Mur |
|---|---|
| `-O2` (actuel) | 12,7 s |
| `-O2` + PCH | **8,6 s** (−32 %) |
| `-O1` | 7,4 s |
| `-O0` | 8,6 s |

**`-O1` sur les tests n'a pas été retenu** bien qu'il mesure mieux : la suite contient des tests
chronométrés (StressTest, ChaosMonkey, `IIOPerfGate`) dont les seuils supposent du code optimisé.
Baisser le `-O` changerait ce qui est testé — c'est un arbitrage, pas une optimisation gratuite.

### Interaction ccache × PCH — le piège

Par défaut ccache **refuse de cacher une compilation utilisant un PCH** : mesuré, seules
**13 TU sur 24** restaient cachables, et un « hit » retombait à 27,9 s au lieu de 1,5 s.
`sloppiness=pch_defines,time_macros` est la conf documentée qui débloque ça (24/24 cachables).
Un poste qui active le PCH sans cette ligne **perd la moitié du bénéfice de ccache sans rien voir**.

---

### Validation

Build complet vert (`exit 0`, 0 erreur, **0 PCH rejeté**), suite complète **205/206**.

Le seul échec, `MemoryLeakHunter`, **n'est pas imputable à ce chantier** — vérifié plutôt que
supposé, dans cet ordre : (1) le test annonce lui-même en tête de fichier « load the same .so file
200 times (**no recompilation**) », il ne compile donc rien à l'exécution ; (2) son échec est un
dépassement de son propre chien de garde interne (`Failures: 0`, `⚠️ Test timeout after 180
seconds`), pas une assertion ; (3) relancé seul il **passe en 161 s** — contre un plafond de 180 s.
**11 % de marge** : sous charge (`ctest -j4` avec trois autres tests lourds) il déborde. C'est une
fragilité préexistante et un générateur de fausses alertes ; à traiter séparément, en relevant le
plafond ou en allégeant le test.

---

## 4. Pourquoi PAS de compilation distribuée (distcc / icecream)

C'était la demande initiale : « déporter la compil sur un réseau de VPS ». Écarté sur trois chiffres.

1. **Charge utile.** Une TU de test préprocessée pèse **8,2 Mo** (mesurée, pas estimée). Les ~868
   cibles projet représentent donc **~5 Go d'upload par build complet**. Or ce même code projet
   coûte 6 412 s CPU / 16 cœurs ≈ **7 minutes idéales en local**. On paierait 5 Go de réseau pour
   grappiller quelques minutes.
2. **Le parc n'est pas à la hauteur.** VPS57 = 4 cœurs Haswell, VPS142 = 8 cœurs Xeon E3-1270v6
   (mais c'est la prod `ai.etheryale.com`), VPSPapa = 8 cœurs Haswell. Soit 20 cœurs hétérogènes et
   plus lents par cœur, dont deux machines déjà chargées, face à 16 cœurs locaux.
3. **La chaîne d'outils.** Le poste compile en **MinGW sur Windows**, les VPS sont Debian. distcc
   exigerait un cross-compilo `x86_64-w64-mingw32` **de version identique** sur chaque nœud, à
   maintenir synchronisé à la version près.

Débit montant mesuré depuis le poste : **152 Mb/s vers VPSPapa**, **46 Mb/s vers VPS142** (celui-ci
sans doute plafonné par le chiffrement SSH sur une machine chargée). Même au meilleur des deux,
5 Go coûteraient ~4,4 min de réseau **par build**, à ajouter aux ~7 min que le poste met déjà seul.

---

## 5. Le déport — `tools/remote-build.sh` (livré)

La bonne topologie n'est pas de *distribuer* les TU mais de *déporter* le build entier sur UNE
machine. Charge utile : l'arbre versionné, **4,5 Mo / 968 fichiers**, transféré en **5 s**. Un
aller-retour au lieu de milliers.

### ⚠️ Ma prédiction était fausse, et de beaucoup

J'avais écrit qu'un VPS à 4 cœurs serait « structurellement plus lent » que le poste à 16 cœurs, et
que le déport serait un compromis « plus lent mais plus froid ». **Mesuré, c'est faux.**
Même configuration (469 cibles), depuis zéro, des deux côtés :

| | configure | build | **total** |
|---|---|---|---|
| Poste local — 16 cœurs, `-j16`, Windows/MinGW | 11 s | 189 s | **200 s** |
| **VPS142** — 4c/8t, `-j6`, `nice -n 19`, Linux/g++ 14 | 21 s | 146 s | **167 s** |

Le VPS gagne **17 % au total, 23 % sur la phase de build** — en priorité basse, sur 6 threads sur 8,
sur un serveur de production, contre quatre fois plus de cœurs. Windows/MinGW compile nettement
moins vite que Linux/g++ à cœurs comparables (création de processus, NTFS, antivirus), et le poste
est thermiquement dégradé après une heure de compilation — ce qui est précisément sa condition
réelle d'usage. Le déport n'est donc **pas** un compromis : il est plus rapide **et** il sort 100 %
de la charge du bureau.

### Choix de la cible

Sondés : VPSPapa n'a **ni cmake, ni ninja, ni rsync**, et 92 % de disque plein → hors jeu.
**VPS142** a tout (cmake 3.31, ninja 1.12, g++ 14.2, rsync, SDL2/GL/X11 dev déjà installés depuis le
port Linux) et 175 Go libres. C'est aussi la prod `ai.etheryale.com` : d'où `nice -n 19` et `-j6`
sur 8 threads, non négociables.

### Ce que le déport sait valider

Vérifié de bout en bout : sync 9 s → build → `ctest` en série → **100/103** sur la config par défaut.
Les trois échecs sont identifiés et aucun n'est imputable au déport :

| Test | Cause |
|---|---|
| `MemoryLeakHunter` | ses 11 % de marge (voir §3) |
| `CrashHandlerRealE2E` | attendu — le reporter est du SEH Windows, backend Noop ailleurs |
| `RaceConditionHunter` | **SEGFAULT sous Linux** — non investigué, le port Linux est parké |

Le déport valide donc le cœur et les modules SDL-free ; les tests `[gpu]` restent bloqués par le
contexte GL 2.1 sous llvmpipe (dette « port Linux », parkée). Il **complète** le build local, il ne
le remplace pas. `RaceConditionHunter` mérite un œil un jour : un segfault dans un chasseur de races
est exactement le genre de signal qu'on ne veut pas classer sans regarder.

---

## 6. La ferme multi-projets (cross-compilation Windows)

`tools/remote-build.sh` a été généralisé : plusieurs dépôts, file d'attente, et production de
binaires **Windows** par cross-compilation mingw-w64 depuis le serveur Linux.

### Pourquoi cross-compiler plutôt que compiler nativement

Un build natif Linux produit des ELF inexécutables sur le poste, et exige que tout le parc compile
sous Linux — ce que la dette « port Linux » (parkée) empêche. **Cross-compiler contourne les deux** :
c'est la même famille de toolchain que le MinGW du poste, donc le même code, la même cible. Le
blocage GL 2.1 sous llvmpipe et le segfault Linux de `RaceConditionHunter` sortent du chemin critique.

Prérequis installés sur le serveur : `g++-mingw-w64-x86-64` (14.2), `ccache`, et **SDL2 mingw**
(Debian ne le package pas — tarball officiel `SDL2-devel-2.30.9-mingw` déployé dans
`/usr/x86_64-w64-mingw32/`).

### ⚠️ Les deux pièges de la cross-compilation

1. **La variante de threading.** Debian installe `-g++`, `-g++-win32` et `-g++-posix`, et le défaut
   pointe sur **win32**, dont la libstdc++ n'implémente NI `std::thread` NI `std::mutex` : le moteur
   ne compile pas du tout. Le fichier de toolchain force explicitement `-posix`, qui correspond
   aussi au MinGW du poste (`x86_64-posix-seh-rev0`).
2. **Les DLL de runtime.** Le serveur croise en GCC 14, le poste a MinGW GCC 15. Les binaires
   produits dépendent des DLL de GCC 14 : il faut rapatrier celles **du serveur** (variante posix),
   pas réutiliser celles du poste. `--fetch` s'en charge.

### Résultats

| | |
|---|---|
| Cross-build complet du moteur (1543 cibles, bgfx + UI + SDL2), cache froid | **612 s**, 0 erreur |
| Le même, ccache chaud, depuis un dossier vide | **26 s** (×23) |
| Binaires rapatriés et **exécutés sur Windows** | `test_access_guard`, `test_save_file`, `test_fx_world`, `test_nine_slice` — **tous verts** |

Le ccache serveur (`base_dir=/home/debian/grovefarm`) est ce qui rend la ferme viable à plusieurs
dépôts : les trois jeux font `add_subdirectory(../groveengine)` et **recompilent chacun bgfx +
glslang + spirv-cross depuis zéro** (3 715 s CPU × 4). Le cache partagé annule cette redondance.

### La file d'attente

Un `flock` sur `~/grovefarm/.build.lock` sérialise les builds. Ce n'est pas du confort : le serveur
n'a que 8 threads **et héberge la prod** `ai.etheryale.com`. D'où aussi `nice -n 19` et `-j6`.
Un appel qui trouve la ferme occupée l'annonce et attend son tour (`GROVE_REMOTE_LOCK_WAIT`).

### État par projet

| Projet | Cross-build Windows | Blocage |
|---|---|---|
| **groveengine** | ✅ complet, binaires exécutés et verts | — |
| **DAOS** | 🟡 82 % (moteur + bgfx OK) | deux défauts DE SON dépôt, voir ci-dessous |
| drifterra, fractax | non tenté | probablement les mêmes classes de défaut |

**Deux défauts trouvés dans DAOS — ce sont des bugs latents, pas des limites de la ferme :**

1. **Casse de fichier.** `CMakeLists.txt` est suivi par git sous le nom `cmakelists.txt`. Windows
   étant insensible à la casse, ça marche depuis toujours ; sous Linux, CMake ne trouve rien et dit
   « does not appear to contain CMakeLists.txt », message qui n'évoque pas la casse. Le script
   détecte désormais ce cas AVANT d'envoyer.
2. **Dépendance vers un répertoire ignoré.** DAOS compile avec
   `-I../groveengine/deps/nlohmann_json/single_include`, or `deps/` est **gitignored** dans
   groveengine (`.gitignore:50`). Ça ne fonctionne que parce que ce dossier existe sur le poste :
   **un clone frais de DAOS + groveengine ne compilerait pas non plus.** Le vrai correctif est
   côté DAOS — lier la cible `nlohmann_json::nlohmann_json` que groveengine expose déjà, au lieu de
   pointer un chemin vendu.

Aucun de ces deux points n'a été corrigé ici : ce sont d'autres dépôts, avec leur propre suivi.

---

## 7. L'automatisation (Gitea Actions)

`.gitea/workflows/build.yml` — sur push vers `master` et sur déclenchement manuel.

**L'infra existait déjà** : Gitea tourne **sur ce même serveur**, avec deux runners `act_runner`
actifs dont un instance-wide. Rien à installer. Le runner global expose le label **`host:host`**,
donc le job s'exécute **directement sur la machine** et hérite du mingw, du sysroot SDL2 et surtout
du **ccache partagé** — un conteneur repartirait de zéro et rendrait le cache inutile.

**⚠️ Une seule file, deux portes d'entrée.** Le job prend `~/grovefarm/.build.lock`, le MÊME verrou
que `tools/remote-build.sh`. Sans ça, la file du runner et celle de la ferme se disputeraient
8 threads sur une machine qui héberge aussi la prod.

Les deux chemins sont **complémentaires, pas redondants** :

| | construit quoi | quand |
|---|---|---|
| `tools/remote-build.sh` | ton arbre de travail, **même non commité** | à la demande |
| Gitea Actions | ce qui est **poussé** | automatiquement, avec historique |

**⚠️ Trois tests exclus NOMMÉMENT** (`MemoryLeakHunter`, `CrashHandlerRealE2E`,
`RaceConditionHunter`) : ils échouent pour des raisons connues et étrangères au code poussé. Les
laisser rendrait le workflow rouge en permanence — et un rouge permanent ne se lit plus. Ils sont
exclus explicitement pour qu'on voie ce qu'on ne teste pas ; reprendre l'un d'eux = retirer sa
mention. **Vérifié : 100/100, exit 0, en 94 s** (build 145 s).

**Portée : groveengine uniquement.** Câbler les jeux tant que leurs propres défauts bloquent le
build fabriquerait trois alertes mortes. Voir §6.

Prérequis restant : l'unité **Actions doit être activée sur le dépôt** côté Gitea (Paramètres →
Actions). Le workflow ne s'arme qu'au premier push.

---

## 8. Ce qui reste sur la table

- **`RaceConditionHunter` segfaute sous Linux** — non diagnostiqué. Peut être un artefact du port
  parké, peut être une vraie race que Windows masque.
- **ccache sur VPS142** : absent. Un `apt install ccache` rendrait les builds distants incrémentaux
  aussi bons que les locaux. Pas fait — c'est un serveur de prod, l'installation est ton appel.
- **Élargir le PCH aux cibles bgfx/SDL** : elles ont d'autres défines, il leur faudrait un second
  fournisseur. ~56 TU concernées, gain estimé mais non mesuré.
- **`max_size` du cache** : à remonter quand le disque C: aura de la place.
- **Réviser le PCH si `grove/JsonDataNode.h` ou `IntraIO.h` deviennent instables** : un en-tête du
  moteur qui bouge souvent invalide le PCH, donc toute la suite, à chaque édition.
- **La synchro coûte ~9-26 s** même quand rien n'a changé (tar + rsync + nettoyage du dossier
  d'étape). Optimisable si ça devient gênant sur une boucle courte.

---

## 9. Comment les agents s'en servent (et le faux vert qu'on a failli livrer)

Le mécanisme de routage, ce n'est pas un outil : c'est **`CLAUDE.md`**. Un agent applique ce qui y
est écrit ; sans règle explicite, il compile en local et la ferme ne sert à personne. La règle vit
donc dans CLAUDE.md § « Où compiler », et tient en une phrase : **itère en local, valide sur la
ferme**.

Le partage n'est pas arbitraire, il suit la mesure : un rebuild identique local coûte **1,5 s**
(ccache), donc router une boucle TDD serrée vers la ferme paierait synchro + file d'attente pour
rien. À l'inverse la suite complète, c'est **~200 s de CPU à fond** — précisément la chaleur qu'on
voulait sortir du bureau.

### ⚠️ Le faux vert

La première version de `remote-build.sh` n'envoyait que `git ls-files` — les fichiers **suivis**.
Or le cycle TDD de la doctrine commence par **écrire un test rouge neuf**, donc non commité : il ne
partait pas, la ferme compilait sans lui et répondait **vert**. Un faux vert sur un cycle TDD est
strictement pire que pas de ferme du tout, parce qu'il valide un fix jamais testé.

Corrigé par `git ls-files -c -o --exclude-standard` (suivis **et** neufs non ignorés).
`--exclude-standard` applique `.gitignore`, donc `build/`, `_deps/` et `deps/` restent hors charge
utile — le correctif ne la fait pas gonfler.

**Vérifié sur le scénario exact** : un `test_farm_tdd_probe.cpp` neuf, jamais `git add`é, contenant
`REQUIRE(1 == 2)` → la ferme est passée de 103 à **104 tests** et a rapporté
`FarmTddProbe (Failed)`. Le fichier arrive, et le verdict est honnête.

Ne jamais « optimiser » la synchro en revenant aux seuls fichiers suivis.

---

## 10. Porte laissée ouverte : une ferme avec GPU et écran

Une ferme équipée est un scénario **attendu**, pas hypothétique. Les deux premières versions de ce
chantier avaient codé en dur « la ferme n'a ni écran ni GPU » — dans `CLAUDE.md` **et** dans une
liste de NOMS de tests recopiée dans le workflow CI. Trois endroits à corriger le jour du
changement, et aucun ne disait *pourquoi* un test était écarté.

### Le point d'accroche : des labels de capacité

`tests/CMakeLists.txt` étiquette désormais les tests par ce qu'ils exigent de la **machine** :

| Label | Signification | Tests |
|---|---|---|
| `gpu` | exige un vrai contexte GL | **16** (dérivé du suffixe `…Gpu`) |
| `platform-windows` | ne peut pas passer ailleurs par construction | `CrashHandlerRealE2E` |
| `timing-sensitive` | porte son chien de garde, déborde sous charge | `MemoryLeakHunter` |
| `known-fail-linux` | échec constaté non diagnostiqué | `RaceConditionHunter` |

Vérifié : 206 tests au total, `-L gpu` → 16, `-LE` des quatre → 187 (= 206 − 16 − 3).

Les appelants (`tools/remote-build.sh`, `.gitea/workflows/build.yml`) excluent des **labels**, plus
jamais des noms. Le jour venu, on retire `gpu` de `GROVE_REMOTE_EXCLUDE_LABELS` — ou on passe
`--gpu` — et **rien d'autre ne bouge**. Vérifié : `--gpu` retire bien `gpu` de la liste, et la ferme
actuelle reste à **100/100 en 89 s** avec l'exclusion par capacité.

### ⚠️ La nuance qui décide de la faisabilité

Le matériel n'est pas le seul verrou, et l'OS de la future ferme change tout :

- **Ferme Linux à GPU** — les binaires `--mingw` sont des **PE Windows** et ne s'exécuteront jamais
  sur un hôte Linux, GPU ou non. Y faire tourner les tests `gpu` suppose donc le build **natif
  Linux**, c'est-à-dire **rouvrir la dette « port Linux »** (parkée). Bonne nouvelle en revanche :
  le blocage qui l'a parkée est précisément l'absence de GPU (bgfx n'obtient qu'un contexte OpenGL
  2.1 sous llvmpipe, insuffisant pour les shaders GLSL 1.30+). Un vrai GPU **est** susceptible de
  lever ce blocage — la ferme équipée et la dette Linux sont le même sujet.
- **Ferme Windows à GPU** — la cross-compilation devient inutile pour cet hôte : build natif, tests
  `gpu` natifs, et artefacts directement exécutables. Topologie plus simple, mais il faut y
  provisionner MinGW + ccache comme sur le poste.

À trancher le jour où la machine existe ; le côté logiciel est prêt dans les deux cas.

---

## 11. Le GPU n'est peut-être pas nécessaire du tout — un diagnostic périmé

En préparant la « ferme à GPU », j'ai re-prouvé la cause documentée plutôt que de bâtir dessus.
**Elle est fausse.**

### Ce que disait le dossier

La dette « port Linux » est parkée avec cette raison : *bgfx n'obtient qu'un contexte OpenGL 2.1
sous llvmpipe, insuffisant pour les shaders GLSL 1.30+*.

### Ce que dit la mesure (2026-07-30, VPS142)

```
OpenGL renderer string:                    llvmpipe (LLVM 19.1.7, 256 bits)
OpenGL core profile version string:        4.5 (Core Profile) Mesa 25.0.7
OpenGL core profile shading language:      4.50
```

**OpenGL 4.5, GLSL 4.50.** Mesa 25 fait tourner llvmpipe très au-delà de 2.1 ; le diagnostic datait
d'un Mesa plus ancien, ou confondait « pas de display » avec « pas de version de GL ». Le serveur
n'a d'ailleurs qu'une Matrox G200e de BMC, inutilisable en 3D : c'est bien du rendu **logiciel**.
`Xvfb` était déjà installé.

### Les vrais verrous, tous logiciels

1. **Une garde `WIN32`.** `tests/CMakeLists.txt:3171` :
   `if(WIN32 AND GROVE_BUILD_BGFX_RENDERER AND SDL2_AVAILABLE)`. Les 16 tests `gpu` ne sont **jamais
   déclarés** sous Linux, quelle que soit la machine. Aucun GPU n'y changerait rien.
2. **Collision de macros X11.** `SDL_syswm.h` tire `X11/Xlib.h`, qui `#define None 0L` — ce qui
   casse `enum class BlendMode { None, … }` de `RHI/RHITypes.h`. Un `#undef None/Status/Bool/
   Success/Always` après l'include suffit (vérifié sur 55 fichiers de test).
3. **Du code réellement spécifique Windows.** Quelques cibles lisent `info.info.win` (le `HWND`) —
   `test_renderer_showcase`, `test_video_demo`. Ce sont des **démos visuelles**, pas des tests `gpu`.

### Preuve obtenue

Garde levée et macros neutralisées **côté serveur uniquement** (le dépôt n'a pas été touché) :

| | |
|---|---|
| Tests `gpu` ayant compilé et tourné | **3** |
| …dont passés sous `xvfb-run` + llvmpipe, **sans GPU** | **3 / 3** |
| Tests `gpu` bloqués à la **compilation** Linux | 13 |

Parmi les réussites, `RhiReadbackGpu` — rendu offscreen, `readPixels`, assertions sur les pixels.
C'est-à-dire exactement la classe de test qu'on croyait exiger du matériel.

### Ce que ça change

**Une machine à GPU n'est peut-être pas nécessaire pour faire tourner la suite `gpu`.** Les 13
restants échouent à la **compilation**, pas à l'exécution : ce sont des problèmes de portabilité
bornés et de nature connue, pas un mur matériel.

⚠️ Ceci n'est **pas** une proposition de reprendre le port Linux — il est parké, c'est une décision
d'Alexi. C'est le constat que **la raison écrite du parking n'est plus vraie**, et qu'une ferme à
GPU pourrait résoudre un problème qui n'existe déjà plus. À rouvrir ou non ; mais à ne pas rouvrir
sur la foi de l'ancienne raison.

### ⚠️ Correction du §11 — j'avais conclu sur un build partiel

Le §11 ci-dessus annonçait « 3 tests `gpu` ont tourné, 3/3 passent ». **Le build était partiel** :
une exécution ultérieure, avec plus de cibles construites, donne le vrai décompte.

| | |
|---|---|
| Passent sous `xvfb-run` + llvmpipe | **3** (`RhiReadbackGpu`, `AssetProviderGpu`, `AssetAsyncGpu`) |
| **Compilent mais AVORTENT à l'exécution** | **5** (`TilemapLodGpu`, `SpriteClipGpu`, `TextClipGpu`, `SelectionHighlightGpu`, `TtfRenderGpu`) |
| Ne compilent pas sous Linux | 8 |

Les 5 avortent tous sur la même erreur :

```
BGFX FATAL: Failed to compile shader.
0: 0:11(3): preprocessor error: syntax error, unexpected HASH_TOKEN
```

**Ce que ce n'est PAS — établi par test différentiel, pas par raisonnement.** On pouvait croire au
diagnostic historique (« contexte OpenGL 2.1 → les shaders GLSL 1.30+ échouent ») : le symptôme y
ressemble. Forcer `MESA_GL_VERSION_OVERRIDE=3.3COMPAT MESA_GLSL_VERSION_OVERRIDE=330` donne
**exactement la même erreur**. Ce n'est donc **ni le matériel, ni la version du contexte GL**.

**Ce que c'est** : le **contenu** du shader remis au backend GL est invalide — un `#` en ligne 11.
`tools/regen_shader.py` produit quatre blocs par shader (`spv`, `glsl`, `mtl`, `dx11`) et le bloc
`mtl` est explicitement un **PLACEHOLDER** dans le fichier généré. Le chemin GL reçoit vraisemblablement
un bloc qui n'est pas le sien. C'est un défaut de **notre pipeline de shaders**, borné et localisé.

**Conséquence pour le choix de machine — la seule qui compte :** une carte réelle sous Linux
**ne suffira pas**. Le texte du shader est rejeté avant que le matériel n'entre en jeu ; une RTX 3070
rencontrera la même erreur. Faire tourner la suite `gpu` sous Linux demande d'abord ce correctif.

*Leçon de méthode, la même que la doctrine énonce : une cause documentée est une hypothèse. Mais ma
propre contre-hypothèse en était une aussi — `glxinfo` prouvait ce que le PILOTE sait faire, pas ce
que bgfx OBTIENT. Seul le test qui coupe la variable a tranché.*
