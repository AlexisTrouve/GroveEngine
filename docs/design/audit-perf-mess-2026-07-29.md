# Audit qualité — performance & dette structurelle (29 juillet 2026)

> Portée : moteur complet (`src/`, `modules/`), lu à `9ba4404`. Chaque constat de perf est **mesuré**
> ou dit explicitement qu'il ne l'est pas. Aucun correctif appliqué — c'est un état des lieux.

> ## Bilan (2026-07-30) — audit entièrement traité
>
> | | Verdict | Ce que ça a donné |
> |---|---|---|
> | **P1** | ✅ corrigé | poussée `ui:data` identique : 15,8 ms → **0,09 ms** |
> | **P2** | ✅ mesuré → corrigé, puis **clos** | la cause n'était pas la redondance mais une **exception** par binding textuel : 2031 → **630 ns** |
> | **P3** | 🟢 **3 fonctions sur 5** | ~2280 lignes → ~735 ; restent `SceneCollector::finalize` et `UIModule::updateUI` (cette dernière ⚠️ d'une autre nature) |
> | **P4** | ✅ mesuré → **écarté** | 0,5 % d'une frame au pire ; restructurer ne se justifie pas |
> | **P5** | dette assumée | inchangée, rouverte seulement si un 3e cas apparaît |
>
> **Ce que l'exercice a appris, et qui vaut plus que les correctifs** : deux des cinq lignes
> désignaient la bonne cible pour la mauvaise raison. P2 blâmait une redondance visible à l'œil nu
> alors que le coût réel était une exception invisible — un gros refactor pour 0,4 % au lieu de
> trois lignes pour 3,2×. P4 semblait « gratuit à corriger » et s'est révélé sans objet. **Un
> raisonnement de FORME identifie où regarder ; il ne dit pas quoi faire.** Mesurer avant de
> restructurer, y compris quand le défaut saute aux yeux.

**Méthode.** Les affirmations non mesurées ne comptent pas. Le constat P1 a été prouvé par une sonde
jetable (compilée, exécutée, puis supprimée) ; les tailles sont comptées par un script à suivi de
profondeur d'accolades. **Ma première passe de mesure était fausse** — une heuristique appariait mal
le début et la fin des fonctions et m'a donné un classement bidon (`BgfxDevice` « 740 lignes » était
un corps de CLASSE, `setConfiguration` « 536 » sortait de nulle part). Les chiffres ci-dessous sont
ceux de la seconde passe, vérifiés un par un.

---

## P1 — ✅ CORRIGÉ (2026-07-30) — une poussée `ui:data` reconstruisait tous les répéteurs

> **Après correctif, mesuré sur la même sonde, 30 lignes** : une poussée identique passe de
> **180 `:remove` + 180 `:add` et 15,8 ms** à **0 / 0 et 0,09 ms**. Soit ~173× sur le temps, et le
> trafic retenu tombe à rien.
>
> Deux étages. **(A) à la source** : le handler `ui:data` compare le modèle entrant à l'actuel et
> sort s'il est identique — ce qui épargne aussi la ré-résolution de tous les bindings (P2
> ci-dessous), pas seulement le répéteur. **(B) par hôte** : chaque répéteur retient l'empreinte
> exacte de son tableau et ne se reconstruit que s'il a bougé — indispensable parce qu'un HUD qui
> pousse `{hp, fleet}` change `hp` chaque frame et jamais `fleet`, cas où (A) ne protège plus rien.
> La garde ne saute QUE la destruction/reconstruction : `resolveAllBindings` continue de tourner,
> donc les valeurs affichées restent fraîches.
>
> Au passage, le gabarit est parsé **une fois par hôte** au lieu d'une fois par élément — il ne
> change jamais à l'exécution, les N−1 autres parses étaient du travail pur perdu.
>
> **Choix assumé** : pas de comparaison sur `ui:data:set` / `ui:data:merge`. Détecter un patch sans
> effet demanderait de copier tout le modèle à chaque appel pour le comparer ensuite — un coût
> nouveau pour attraper un cas qui n'existe pas : un `set` est par construction l'intention de
> changer quelque chose.
>
> **Verrouillé par `IT_068`** (`UIRepeaterIdleE2E`), trois cas dont le troisième porte tout le poids :
> une poussée MODIFIÉE doit encore reconstruire. Sans lui, une garde dégénérée en « ne jamais
> reconstruire » passerait au vert en cassant le répéteur — vérifié en la dégénérant exprès sous sa
> forme subtile (construire une fois, jamais plus) : cas 1 et 2 verts, cas 3 rouge.

### L'état des lieux d'origine

## P1 (constat initial) — 🔴 Une poussée `ui:data` reconstruit INTÉGRALEMENT tous les répéteurs

**Le constat le plus grave, et il est mesuré.** Sur `test_e2e_repeater.json`, une poussée `ui:data`
portant des **données strictement identiques** à la précédente :

| Éléments | `:remove` | `:add` | `:update` | Durée |
|---|---|---|---|---|
| 5 | 30 | 30 | 0 | **2,5 ms** |
| 30 | 180 | 180 | 0 | **15,8 ms** |

Linéaire en N — donc c'est bien la reconstruction par élément, pas un coût fixe. **≈ 0,5 ms par
ligne de répéteur, par poussée.**

À 30 lignes, une seule poussée consomme **15,8 ms sur un budget de 16,6 ms** à 60 fps. Un HUD qui
rafraîchit ses données chaque frame — le cas d'usage normal d'un HUD — ne tient pas la frame à lui
tout seul.

**Cause** (`UIModule::expandRepeaters`, `UIModule.cpp:620`) : aucune garde. À chaque appel, pour
chaque hôte répéteur :

```cpp
for (auto& c : host->children) c->releaseRenderEntries(*m_renderer);   // purge TOUT
host->children.clear();                                                 // detruit TOUT
for (size_t i = 0; i < arr->size(); ++i) {
    uibind::json tj = uibind::json::parse(host->repeatTemplateJson);     // re-PARSE par element
    auto inst = m_tree->parseWidget(tnode);                              // re-CONSTRUIT par element
```

Le `json::parse` du gabarit est refait **par élément** alors que le gabarit ne change jamais, et les
360 messages retained retombent sur le chemin IIO+JSON déjà documenté comme le mur de débit du
moteur.

**Ce qui rend le constat solide plutôt que théorique** : le mécanisme *virtualisé* voisin
(`updateTemplateLists`, même fichier) porte DÉJÀ la garde qui manque ici —
`if (!list->windowDirty(m_dataVersion)) continue;`. Deux mécanismes de répéteur coexistent donc, un
gardé et un non. Le correctif n'est pas à inventer, il est à côté.

**Pistes, par coût croissant** — (a) garde par version de données : ne rien faire si `m_dataVersion`
n'a pas bougé depuis la dernière expansion de CET hôte ; (b) parser le gabarit **une fois** par hôte
et le réutiliser ; (c) diffusion par diff (réutiliser les instances existantes, n'ajuster que le
delta de taille) plutôt que détruire/reconstruire ; (d) faire converger les deux mécanismes.
La (a) seule supprime le cas pathologique — pousser des données inchangées.

---

## P2 — ✅ MESURÉ puis CLOS (2026-07-30) — le constat était mal cadré

> **La redondance n'était pas le problème. L'exception l'était.**
>
> `resolveNumber` convertissait le texte avec `std::stod`, qui **lève une exception** sur toute
> chaîne non numérique — attrapée et jetée. Or la résolution numérique d'un binding TEXTUEL
> (`text: "{{name}}"`, le binding le plus courant qui soit) tombait dans ce chemin **à chaque
> poussée de données**. Une exception y était le cas NORMAL, pas l'exception.
>
> Mesuré : `resolveNumber` coûtait **104 ns sur une valeur numérique contre 3508 ns sur une
> chaîne — facteur 34**, et l'écart est exactement le prix de lever puis attraper.
>
> Remplacé par `strtod` (pas d'exception), à sémantique **strictement identique** à `stod` :
> conversion partielle acceptée (`"12abc"` → 12), rien de convertible → défaut, dépassement →
> défaut. Verrouillé par `test_ui_binding` sur ces trois cas limites — ce sont précisément ceux où
> un parseur naïf divergerait en silence.
>
> | | Avant | Après |
> |---|---|---|
> | Les trois résolutions, par binding | 2031 ns | **630 ns** |
> | `resolveNumber` sur une chaîne | 3508 ns | **172 ns** |
> | Écran de 200 bindings | 0,406 ms | **0,126 ms** |
>
> **Et la conclusion sur la redondance elle-même : ne pas la corriger.** Une fois l'exception
> partie, les deux résolutions jetées coûtent **0,067 ms sur 200 bindings** — 0,4 % d'une frame.
> Restructurer `applyBoundProp` (évaluation paresseuse ou type variant) sur les dix-sept widgets
> pour ça serait un mauvais échange : beaucoup de surface touchée, une lisibilité dégradée, un gain
> sous le bruit. La ligne est close, pas reportée.
>
> **Ce que ça apprend** : le premier audit avait vu la redondance et pas ce qu'elle coûtait
> vraiment. « Trois appels au lieu d'un » est un raisonnement de forme ; il désignait la bonne
> ligne pour la mauvaise raison, et aurait mené à un gros refactor pour 0,4 % au lieu d'un
> correctif de trois lignes pour 3,2×. **Mesurer avant de restructurer, même quand la redondance
> est visible à l'œil nu.**

### Le constat d'origine

## P2 (constat initial) — 🟠 Chaque binding est résolu TROIS fois, deux résultats jetés

`resolveWidgetBindings` (`UIModule.cpp:584`) :

```cpp
w->applyBoundProp(b.first,
                  uibind::interpolate(*scope, b.second),    // 1 resolution + 1 allocation string
                  uibind::resolveNumber(*scope, b.second),  // 2e resolution du MEME chemin
                  uibind::resolveBool(*scope, b.second));   // 3e resolution du MEME chemin
```

Le widget en choisit **une** dans `applyBoundProp` et jette les deux autres. Chaque binding paie donc
trois parcours de chemin JSON et une allocation de chaîne inconditionnelle.

Non mesuré isolément — mais il s'exécute sur exactement le même chemin que P1, dont il multiplie le
coût. À traiter *après* P1 : si P1 est gardé, P2 devient marginal. C'est l'ordre qui compte, pas la
somme des deux.

*Note d'honnêteté* : la signature à trois formes est ce qui rend `applyBoundProp` simple côté widget.
Corriger P2 veut dire soit une évaluation paresseuse, soit un type de valeur variant — ce n'est pas
gratuit en lisibilité. À ne faire que si la mesure le réclame après P1.

---

## P3 — 🟢 Cinq fonctions au-delà de 350 lignes — TROIS traitées (2026-07-30 puis 07-31)

> **État au 2026-07-31 : 3 sur 5 résorbées, ~2280 lignes ramenées à ~735.**

| Fonction | Audit (07-29) | Re-mesuré (07-31) | Après | Commit |
|---|---|---|---|---|
| `UITree::registerDefaultWidgets` | 621 | — | **17** (table) | 07-30 |
| `BgfxRendererModule::setConfiguration` | 620 | **672** | **309** | `4bbe07d` + `bd67f3b` |
| `UIModule::setConfiguration` | 419 | **436** | **100** | `91e409e` |
| `UIModule::updateUI` | 517 | **452** | — | ⚠️ voir ci-dessous |
| `SceneCollector::finalize` | 368 | **430** | — | à faire |

⚠️ **Les chiffres de l'audit étaient faux dans les deux sens** (620→672, 419→436, 368→430, mais
517→452). Une ligne de dette est une hypothèse **datée** : re-mesurer avant de planifier dessus a
coûté trente secondes et a changé l'ordre des priorités.

### Le geste, et la règle qui l'a borné

Les deux `setConfiguration` étaient des **tables déguisées en code** : 14 et 25 abonnements IIO dont
les handlers, écrits en lambdas inline, noyaient la séquence d'initialisation. Le corps de chaque
handler part en méthode privée nommée, son commentaire d'entête descend avec lui, et il reste une
table lisible d'un coup d'œil.

> **La règle qui a borné les deux coupes : n'extraire que ce dont on peut prouver l'innocuité.**
> Les abonnements sont **entrelacés** avec la construction du graphe de rendu et rien ne prouve cet
> ordre indifférent — donc on extrait le **corps**, jamais l'appel. Pour la même raison, `ClearPass`
> et `TilemapPass` restent dans `setConfiguration` : les abonnements tilemap les suivent
> immédiatement.

Ce qui reste **délibérément en ligne** : les handlers de 2 à 4 lignes (`asset:register`,
`asset:preload`, `asset:setPriority`, `asset:unload`, `input:mouse:move`, `input:mouse:wheel`). Les
extraire allongerait sans clarifier — *Simplicity First* reste subordonnée à la modularité, pas
l'inverse.

### La vérification qui compte

La transformation a été **scriptée**, donc non crue sur parole. Quatre angles : volume (rien perdu) ·
câblage (chaque abonnement sur la bonne méthode) · **corps comparés texte contre texte à l'original
(`git show HEAD`) — 10/10 puis 23/23** · compilation + `[gpu]` 17/17 + suite complète sans régression.

⚠️ Le troisième angle est le seul non redondant : **le compilateur attrape une capture manquante, pas
un corps qu'un script aurait silencieusement tronqué.** Les 50 lambdas concernées ne capturaient que
`this` — vérifié *avant* de couper, c'est ce qui rendait l'extraction mécaniquement sûre.

### Les deux restantes

**`SceneCollector::finalize` (430)** — motif répété par type de primitive (« fusionner retenu +
éphémère dans l'allocateur, poser `packet.X`/`packet.XCount` »). ⚠️ Mais **deux déclinaisons ne sont
pas uniformes** : les tilemaps portent des drapeaux `dirty` et des couches, les textes traînent des
chaînes copiées à part. Un gabarit qui les avalerait casserait quelque chose — gabarit pour les
primitives uniformes, tilemaps et textes laissés explicites.

**`UIModule::updateUI` (452)** — ⚠️ **d'une AUTRE NATURE, ne pas traiter par analogie.** 54 branches,
une seule boucle, **aucune répétition**, et de l'état qui circule entre les étapes. Y découper des
« phases » est un pari, pas une extraction mécanique. Elle ressemble aux autres par sa taille
seulement — et c'est exactement le raisonnement de forme contre lequel ce document met en garde.

La plus coûteuse en pratique est la première : `registerDefaultWidgets` contient **seize fabriques de
widgets en lambdas inline**. Conséquence concrète — ajouter un widget oblige à éditer une fonction de
621 lignes dans un fichier central, alors que tout le reste du widget vit dans son propre fichier.
C'est un point de contention garanti dès que deux sessions touchent l'UI en parallèle, et c'est
exactement ce qui s'est produit aujourd'hui sur `BgfxRendererModule.cpp` et `tests/CMakeLists.txt`.

Piste peu risquée et incrémentale : donner à chaque widget une fabrique statique dans SON fichier
(`UIButton::fromNode`), `registerDefaultWidgets` n'étant plus qu'une table de seize lignes. Se fait
un widget à la fois, sans big-bang.

---

## P4 — ✅ MESURÉ puis ÉCARTÉ (2026-07-30) — le coût ne justifie pas le travail

> **Mesuré, pas estimé** (cascade seule, reproduite à l'identique) :
>
> | Position | Coût par message |
> |---|---|
> | 1re branche | 6,8 ns |
> | 17e (`render:text`) | 13 ns |
> | **42e (`render:rect`)** | **42,5 ns** |
>
> À **2000 rects par frame** — un écran d'UI très dense qui change entièrement — la cascade coûte
> **0,085 ms, soit 0,5 % d'une frame**. Rapportée au coût d'un message sur le chemin IIO+JSON
> (~3 µs), elle en represente **1,4 %**.
>
> **Décision : ne rien restructurer.** Un dispatch par table de hachage ajouterait de la machinerie
> pour un gain sous le bruit ; et réordonner la cascade par fréquence d'usage installerait une règle
> d'ordre que personne ne tiendra — la preuve est déjà là : la chaîne est passée de **31 à 42
> branches en une semaine** (le travail lumière a ajouté filtres, brouillards et nébuleuses), sans
> que l'ordre soit jamais considéré. Une convention qui se dégrade toute seule n'est pas une
> convention.
>
> Le constat d'origine disait déjà « le coût réel est faible et je ne le vends pas plus cher ». La
> mesure le confirme et le chiffre : la ligne est **close**, pas reportée. Si un jour la cascade
> devient un vrai coût, ce sera visible dans un profil — pas dans une intuition de forme.

### Le constat d'origine

## P4 (constat initial) — 🟡 Dispatch par cascade de comparaisons, l'usage le plus fréquent en dernier

`SceneCollector::processMessage` compare `msg.topic` à 31 littéraux dans un `else if` linéaire
(`SceneCollector.cpp:91` à 193). **`render:rect` est la 31ᵉ et dernière branche** — or c'est ce que
publie l'UI pour chaque fond de panneau, de bouton, de bordure, de case à cocher, de piste de slider.

**Le coût réel est faible et je ne le vends pas plus cher** : comparer deux `std::string` de longueurs
différentes s'arrête sur la longueur, donc l'essentiel des 31 comparaisons est quasi gratuit. C'est
d'abord un problème de **lisibilité et d'ordre** : la structure suggère une priorité que le profil
d'usage contredit. Un `switch` sur un hash de topic, ou simplement remonter les branches chaudes,
règle les deux.

---

## P5 — 🟡 Quatorze surcharges `releaseRenderEntries` recopiées

Le patron « libérer mes ids extras, remettre mes drapeaux paresseux, déléguer à la base » est
maintenant écrit à la main dans quatorze widgets. `IT_067` empêche l'oubli d'être silencieux, ce qui
était l'urgence — mais la répétition demeure.

**Dette assumée, pas oubliée** : l'alternative (la base tient la liste des ids, plus aucune surcharge)
a été examinée le 29 juillet et écartée — elle touche quatorze surcharges qui fonctionnent, pour un
refactor que rien ne réclame. À rouvrir seulement si un troisième cas de la même famille apparaît.

---

## Ce que l'audit N'A PAS trouvé (et c'est une information)

- **`UIRenderer::updateRect` / `updateText`** : la détection de changement précède toute allocation,
  et rien n'est publié si rien ne bouge. Le cœur du rendu retained est propre.
- **Les entrées retained** sont dans une `unordered_map` — pas de coût de recherche caché.
- **Les listes virtualisées** ont déjà leur garde d'inactivité (`windowDirty`) et un commentaire
  `PERF idle-gate` qui l'explique. Quelqu'un y a pensé ; c'est P1 qui est resté à l'écart.
- **Le chemin IIO** : le zéro-copie (payload partagé immuable) est en place, comme documenté.

Autrement dit, le moteur n'a pas un problème de perf diffus. Il a **un point chaud précis** (P1) sur
un chemin que le reste du code avait déjà appris à protéger ailleurs.

---

## Ordre recommandé

1. ~~**P1**~~ — ✅ fait le 2026-07-30 (voir l'encadré en tête de P1).
2. ~~**P3** sur `registerDefaultWidgets`~~ — ✅ fait le 2026-07-30.
   ~~**P3** sur les deux `setConfiguration`~~ — ✅ fait le 2026-07-31 (1108 → 409 lignes).
   Restent `SceneCollector::finalize` (gabarit possible, mais tilemaps et textes ne sont pas
   uniformes) et `UIModule::updateUI` (⚠️ **pas** justiciable du même geste — voir §P3).
3. ~~**P4**~~ — ✅ mesuré puis écarté le 2026-07-30 : 0,5 % d'une frame au pire, la restructuration ne se justifie pas.
4. ~~**P2**~~ — ✅ mesuré puis clos le 2026-07-30 : la vraie cause était une exception, pas la redondance.
5. **P5** — ne rien faire pour l'instant.
