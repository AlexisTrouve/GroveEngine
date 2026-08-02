# Handoff — 2 août 2026 : mode retenu, `updateUI`, et deux mesures qui ne mesuraient rien

> Ce document garde ce qui n'est dans aucun doc de chantier : ce que la journée a **appris**, et les
> erreurs commises en la faisant. Le détail technique est dans les docs liés en fin de section.

---

## 0. REPRENDRE À FROID — à lire en premier

### L'état

| | |
|---|---|
| `master` | **9 commits non poussés** au moment d'écrire — vérifier `git log origin/master..HEAD` |
| Suite | **211/212**, mesurée en fin de session : un seul rouge, `ChaosMonkey` (581 s) |
| Dette P3 | ✅ **CLOSE** — cinq fonctions sur cinq |

Le run du matin donnait **212/212** — `ChaosMonkey` passait. Celui du soir non. **Aucune ligne de
code n'a bougé de son côté entre les deux** : c'est le budget d'horloge absolu qui bascule avec la
vitesse du poste, exactement ce que décrit le §2quater. C'est aussi la meilleure illustration du
point ci-dessous.

⚠️ **Un run à 1, 2 ou 4 rouges n'est PAS forcément une régression.** Le compte suit la vitesse du
poste, pas le code. Lire le **nom** et la **raison**, jamais le compte —
[known-annoyances §2quater](known-annoyances.md).

### Ce qui est ouvert, avec le geste suivant

**1. Salves de `0xC0000374` sur les tests GPU.** Victime tournante — quatre noms différents vus dans
la seule journée du 02/08. **Survit à la reconstruction.**
→ *Geste* : campagne **longue et détachée** (une heure de boucle horodatée), pas une session
interactive. ⚠️ **Ne JAMAIS bisecter dessus** avant d'avoir établi que ce n'est pas une salve : ça
fabrique un faux coupable avec une chaîne de preuve impeccable (vécu le 02/08, §3ter).

**2. Budgets d'horloge absolus** — `ChaosMonkey` et `ErrorRecovery`. Ta décision, pas du travail.
→ *Geste* : une mesure à froid la prépare. ⚠️ Le label `timing-sensitive` les sortirait de la CI **où
ils passent**.

**3. `capture_ship_inspector` rend un panneau de ressources VIDE** alors que son code y pousse 50
lignes. Prouvé **antérieur** au travail du jour (rendu différentiel), jamais expliqué.
→ *Geste* : soit le capture ment, soit les listes groupées ont un défaut. Une demi-heure.

**4. `grove::anim::blendAngle` à sortir de `detail`** — deux minutes, besoin **mesuré** (DAOS en a
besoin pour son fondu de pose, cf. `DAOS/docs/groveengine_retour.md`).

### Les pièges qui coûtent le plus

- **TROIS conventions de répertoire courant** cohabitent : racine du projet
  (`capture_ship_inspector`, `benchmark_lighting`), `build/` (showcases), `build/tests` (tests GPU,
  E2E). **Je suis tombé dans les trois le 02/08**, dont une fois vingt minutes après avoir documenté
  le piège — et une fois en croyant que le texturing était mort. Un binaire lancé au mauvais endroit
  ne trouve pas ses assets et rend **une image vide ou zéro widget**, sans erreur.
- **Un `0xC0000374` a DEUX causes distinctes** : l'artefact périmé (§3bis, guérit par
  reconstruction) et la salve (§3ter, y survit). Les confondre coûte une session.
- **Le compte de rouges n'est pas un signal.** Cf. §0 ci-dessus.

---

## 1. Ce qui a été livré

| Chantier | Résultat |
|---|---|
| Audit des `:update` retenus | **3 défauts** de plus (texte ×2, tilemap) + la règle qui les explique |
| `updateUI` | **452 → 259 lignes**, en 3 étapes dont 2 de filets — dette P3 **close** |
| `render:sprite:batch` | sait enfin dessiner un **atlas** (stride 12) |
| Non-linéarité d'éclairage | **n'existe pas** — instrument cassé, ligne de `CLAUDE.md` corrigée |
| Audit DAOS | la logique de pose est écrite **11 fois**, les copies ont divergé |

---

## 2. La leçon de la journée : *vérifiable* n'est pas *vérifié*

Quatre affirmations exactes, aucune étayée jusqu'au bout. Chaque fois, la vérification manquante
tenait en une commande.

| J'affirmais | Ce que la mesure a dit |
|---|---|
| « `updateUI` n'a aucune répétition à factoriser » | faux — 200 lignes de dispatch, et le fichier contenait **déjà trois précédents** de son traitement |
| « l'audio positionnel est le meilleur rapport valeur/effort » | **aucun consommateur** ne publie un seul topic `sound:*` |
| « les deux publishers de `:update` sont sains » | il y en avait **trois** |
| « le filtre `UI\|Input` couvre les tests UI » | vrai — mais **jamais vérifié** avant qu'Alexi le demande |

> **Ce que ça change en pratique** : avant d'écrire « j'ai vérifié », se demander *par quelle
> commande*. Si la réponse est « j'ai lu deux fichiers », ce n'est pas une vérification, c'est un
> échantillon.

---

## 3. Deux mesures qui ne mesuraient rien — et elles ne se ressemblent pas

### 3.1 La non-linéarité d'éclairage : un instrument cassé

`CLAUDE.md` portait depuis des semaines « ×80 pour ×4 lampes … semble lié aux draw-calls, non
diagnostiqué ». C'était une hypothèse obtenue **par élimination** (« ce n'est pas du fill rate »),
relue comme un fait, et assez crédible pour devenir un candidat de chantier.

Le ×80 n'existait que dans la colonne `gpu ms`, qui annonçait **31,80 ms de GPU dans une frame de
14,18 ms**. L'horloge murale, elle, est linéaire.

**Deux instruments qui se contredisent ne se départagent pas en choisissant le plus précis, mais en
en construisant un troisième qui les couvre** — ici, forcer la synchronisation (`--vsync`) pour que
le mur devienne dépendant du GPU. Cinq lignes.

⚠️ **Et le contrôle est ce qui donne sa valeur au résultat** : la même méthode, sur des lignes dont
le coût GPU est réel, montre le mur s'étirer et coller au chronomètre. Sans ce contrôle, « le mur ne
bouge pas » aurait aussi bien pu vouloir dire « le mur ne mesure rien ».

### 3.2 Le bisect sur une salve : un protocole cassé

Chaîne obtenue en une heure, **toutes les mesures exactes** : rouge à HEAD, rouge sans le changement
du jour, **vert au commit d'avant**, un seul commit de production entre les deux. Bisect de manuel.

**Faux de bout en bout.** La salve s'était arrêtée entre deux points de mesure — le bisect mesurait
l'horloge, pas le code. Le binaire accusé ne liait même pas le module modifié.

> C'est pire que l'artefact périmé du §3bis : là, la reconstruction guérissait, donc la cause était
> dans le protocole. **Ici rien dans le protocole ne rattrape.** Seule la question du §2quater a
> sauvé la mise : *mon code est-il seulement DANS ce binaire ?*

---

## 4. Le motif qui revient : une liste RECOPIÉE dérive, une fonction PARTAGÉE non

Trouvé côté moteur, puis retrouvé **à l'identique** côté DAOS le même jour.

**Moteur** — 3 des 4 primitives retenues qui recopiaient leur liste de champs entre `:add` et
`:update` avaient dérivé ; **0 des 4** qui partageaient un lecteur. Le mode de panne est SILENCIEUX :
la donnée est reçue, désérialisée et stockée, simplement pas là où le rendu la lit.

**DAOS** — la pose des colons est écrite **onze fois** dans six fonctions de scène, avec des jeux de
branches différents. Un colon qui travaille balance sa pioche dans une scène et reste immobile dans
l'autre.

→ [retained-update-contract.md](retained-update-contract.md) · `DAOS/docs/groveengine_retour.md`

---

## 5. Ce que l'audit des consommateurs a vraiment appris

Mesuré : les trois jeux (Drifterra, DAOS, Fractax) utilisent **`render:*` et `InputModule`, rien
d'autre**. UIModule, SoundManager, FxModule, DialogueModule, VideoModule : **zéro utilisateur**, ni
par topic, ni par lien statique, ni par en-tête.

⚠️ **Et une raison est apparue en documentant** : le chemin bulk IIO était à la fois **incapable de
dessiner un atlas** et **introuvable** — `CLAUDE.md` ne le mentionnait pas, et le DEVELOPER_GUIDE
documentait à sa place le **repli** en l'annotant « optimized ». Deux raisons indépendantes de ne pas
l'utiliser. Avant de conclure qu'un sous-système est mal conçu, vérifier qu'il est **trouvable**.

> **Conséquence de cadrage, à ne pas perdre** : le moteur est en **avance sur sa demande**. Ajouter
> des fonctionnalités n'a pas de levier ; corriger et rendre trouvable ce qui existe, si.

---

## 6. Mes erreurs, gardées parce qu'elles se reproduiront

- **Trois erreurs de répertoire courant**, dont une vingt minutes après avoir documenté le piège.
  Écrire la note ne suffit pas — il faudrait un garde-fou qui échoue bruyamment.
- **Un « audit » qui n'auditait rien** : sur demande d'un audit de DAOS, j'ai d'abord produit un
  catalogue des modules du moteur qu'ils n'appellent pas, écrit depuis un grep de noms de topics.
  Recadré, refait en lisant leur code.
- **« 5 échecs sur 5 donc déterministe »**, alors que les cinq passes tenaient dans la même fenêtre
  de quatre minutes. Répéter DANS une fenêtre ne teste pas ce que répéter À TRAVERS des fenêtres
  teste.
- **Un CoT qui proposait le mauvais geste** : « pousser la dispatch dans les widgets » s'appuyait sur
  trois précédents réels, mais ceux-ci s'appliquaient à des corps **jumeaux** ; les sept branches ne
  partagent aucun corps. Invalidé par une mesure (aucun widget ne nomme un topic) au moment de
  l'implémenter, pas au moment de le proposer.
- **Deux échappements Python** cassés en éditant du C++ (`\n` littéral, sortie unicode sur cp1252).
  Éditer du code avec un script est rapide et rate des choses qu'un `Edit` direct ne rate pas.

---

## 7. Où sont les documents

`retained-update-contract.md` (la règle du lecteur partagé) · `anim-state-machine.md` (l'`Animator`
et ses **écarts au plan**) · `anim-consumers.md` (pourquoi il ne sert PAS à DAOS) ·
`lighting-nonlinearity.md` (le diagnostic complet) · `known-annoyances.md` §2quater/§3ter (les deux
familles de faux rouges) · `DAOS/docs/groveengine_retour.md` (retour + audit, **non poussé chez
eux**).
