# Vitesse de compilation — mesures et décisions

**Statut** : ccache + PCH partagé livrés. Compilation distribuée (distcc/icecream) **écartée sur mesure**.
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

**La bonne topologie, si on y revient**, n'est pas de *distribuer* les TU (beaucoup d'allers-retours
lourds) mais de *déporter* le build entier sur UNE machine : un `rsync` des sources (quelques Mo),
build + `ctest` distants, rapatriement du log. Un aller-retour au lieu de milliers, et le CPU local
reste à zéro — ce qui traite le problème thermique, que la distribution ne traite qu'à moitié
(le poste préprocesse et linke quand même). C'est déjà le motif employé pour TSan/ASan sur VPS142.

Le chiffre manquant pour trancher ce déport : le **débit montant réel** de la connexion.

---

## 5. Ce qui reste sur la table

- **Déport total vers UNE machine** (§4) — traite la chaleur à 100 %, pas encore fait.
- **Élargir le PCH aux cibles bgfx/SDL** : elles ont d'autres défines, il leur faudrait un second
  fournisseur. ~56 TU concernées, gain estimé mais non mesuré.
- **`max_size` du cache** : à remonter quand le disque C: aura de la place.
- **Réviser le PCH si `grove/JsonDataNode.h` ou `IntraIO.h` deviennent instables** : un en-tête du
  moteur qui bouge souvent invalide le PCH, donc toute la suite, à chaque édition.
