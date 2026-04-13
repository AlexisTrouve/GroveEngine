# GDD — Last Cadia
### Game Design Document v0.1 — GroveEngine

> *"Ils ne passeront pas. Pas ici. Pas aujourd'hui. Pas tant qu'un seul de nous tient encore debout."*

---

## TABLE DES MATIÈRES

1. [Vision & Core Fantasy](#1-vision--core-fantasy)
2. [Piliers de Design](#2-piliers-de-design)
3. [Système de Commandement](#3-système-de-commandement)
4. [Système de Combat](#4-système-de-combat)
5. [Système Économique](#5-système-économique)
6. [L'Ennemi Tactique](#6-lennemi-tactique)
7. [Carte & Territoire](#7-carte--territoire)
8. [Boucle de Jeu](#8-boucle-de-jeu)
9. [Architecture Technique GroveEngine](#9-architecture-technique-groveengine)
10. [Risques de Design](#10-risques-de-design)

---

## 1. VISION & CORE FANTASY

### 1.1 Phrase de vision

**Last Cadia** est un jeu de siège stratégique où tu commandes la défense d'une cité-forteresse contre un ennemi qui pense, qui apprend et qui construit — jusqu'au dernier habitant.

### 1.2 Core Fantasy

Tu n'es pas un dieu planant sur un plateau de jeu. Tu es le **Commandant de la Forteresse** — un officier dans une salle de commandement obscure, entouré de cartes tactiles et de radios qui grésillent. La cité est vivante autour de toi : des usines crachent leur fumée noire, des colonnes de ravitaillement se frayent un chemin sous les obus, des soldats tiennent des positions que tu leur as assignées et meurent si tu les laisses sans munitions.

L'ennemi, lui, n'est pas un script. Il observe. Il cartographie tes patterns défensifs. Il creuse des tranchées la nuit. Il flanque là où tu t'y attends le moins. Son cerveau stratégique (Gemma 4) tourne en fond, élabore des plans, les transmet à des unités qui les exécutent avec une précision tactique adaptative (ML). C'est un adversaire **qui joue contre toi**.

**La victoire n'est pas de survivre. C'est de tenir assez longtemps pour que les renforts arrivent — ou de les faire venir.**

### 1.3 Ce que le jeu N'EST PAS

| Ce que ce n'est pas | Ce que c'est |
|---|---|
| Tower defense (brèche = game over) | Siège : la brèche est un problème, pas la fin |
| Rush de horde stupide | Ennemi adaptatif avec doctrine militaire réelle |
| Économie placeholder (ressources abstraites) | Économie de guerre à 3 couches avec logistique réelle |
| Micro-management frénétique | Ordres opérationnels délégués à des commandants IA |
| Fantasy générique | Gothic-industrial grimdark, univers original |

### 1.4 Inspirations

| Source | Ce qu'on prend |
|---|---|
| **Warhammer 40K** (setting) | Énergie grimdark, dernier rempart, sacrifice |
| **Captain of Industry** | Économie profonde, chaînes de production, automation |
| **Banished** | Population avec besoins réels, gestion des ressources humaines |
| **Factorio** | Logistique, goulots d'étranglement, optimisation systémique |
| **Steel Division 2** | Cohérence de front, ordres de brigades, profondeur tactique |
| **Stronghold** | Siège de ville, économie liée à la défense |
| **MeguraColonyML** | Behavior adaptation ennemie par ML |

### 1.5 Setting — L'Univers Caldien

**Le Monde-Forteresse de Caldus Prime** n'a jamais connu la paix. Depuis des générations, la cité-forteresse de **Cadia Ultima** est le dernier verrou entre les plaines de cendres et le cœur industriel de la planète. Les murs ont été reconstruits dix fois. Les noms des défenseurs sont gravés dans l'obsidienne noire des pylônes-mémoriaux — il n'y a plus de place sur les pylônes.

L'ennemi — les **Fractalis** — n'est pas une horde sauvage. C'est une force militaire structurée, technologiquement comparable, avec ses propres doctrines de siège, ses ingénieurs de campagne, ses chaînes logistiques. Ils n'attaquent pas par rage. Ils attaquent parce que Cadia Ultima contrôle le seul nœud de téléportation de masse de la planète. Qui tient Cadia tient Caldus.

**Esthétique visuelle :** Gothic-industrial sombre. Fumée noire perpétuelle. Acier rouillé et béton fissuré. Flambeaux au lieu d'éclairage électrique. Cathédrales militaires. Engrenages et prières gravées dans le métal. Palette : gris ardoise, ocre brûlé, rouge sang, orange industriel.

---

## 2. PILIERS DE DESIGN

*Les 7 piliers définissent les décisions de design irréductibles. En cas de conflit entre deux features, la feature qui sert plus de piliers gagne.*

---

### Pilier 1 — L'Ennemi Pense

L'adversaire n'est pas un script déterministe. Il dispose d'une architecture IA à deux niveaux (ML tactique + LLM stratégique) qui lui permettent d'adapter sa doctrine en temps réel. **Tout mécanisme de jeu doit être conçu en anticipant qu'un adversaire intelligent pourrait l'exploiter.**

- Conséquence design : chaque fortification a une contre-mesure ennemie possible
- Conséquence design : la répétition de la même stratégie défensive est progressivement punie
- Conséquence design : l'IA ennemie doit être lisible (le joueur doit pouvoir identifier son adaptation)

---

### Pilier 2 — Le Territoire Se Dispute

La cité est un espace tridimensionnel de valeur variable. Perdre un quartier n'est pas perdre la partie — c'est perdre une ressource, une position, un avantage logistique. **Aucune zone ne doit être neutre ou sans conséquence.**

- Conséquence design : chaque district a un rôle économique ET tactique défini
- Conséquence design : les ruines sont des actifs militaires, pas des décombres inertes
- Conséquence design : la ligne de front est une entité dynamique, pas un mur statique

---

### Pilier 3 — L'Économie Est La Guerre

Les munitions qui manquent tuent des soldats. Les routes coupées isolent les fronts. Les usines bombardées stoppent la production. **L'économie n'est pas un mini-jeu parallèle : c'est le moteur de la survie militaire.**

- Conséquence design : chaque décision militaire a un coût logistique traçable
- Conséquence design : l'ennemi cible l'infrastructure, pas seulement les troupes
- Conséquence design : la logistique est un gameplay à part entière, pas une abstraction

---

### Pilier 4 — Commande, Ne Micro-gère Pas

Le joueur est un commandant, pas un sergent. Il donne des ordres à des brigades ; ses commandants IA délèguent vers le bas. Il peut descendre à n'importe quel niveau hiérarchique, mais c'est un choix coûteux en attention, pas une obligation. **L'IA de commandement sous les ordres du joueur doit être compétente par défaut.**

- Conséquence design : la délégation doit produire des résultats acceptables sans intervention
- Conséquence design : l'UI doit permettre de passer d'opérationnel à tactique sans friction
- Conséquence design : les succès autonomes des squads doivent être visibles et satisfaisants

---

### Pilier 5 — Le Sacrifice A Du Sens

Dans un grimdark, la mort est permanente et pesante. Chaque soldat perdu est une ressource consommée et un moral impacté. Chaque district sacrifié est une décision stratégique, pas un accident. **Le jeu ne doit jamais rendre les pertes indolores ou abstraites.**

- Conséquence design : les unités ont des identités (noms, historiques, spécialisations)
- Conséquence design : le moral est un système fonctionnel, pas un chiffre décoratif
- Conséquence design : les ordres suicidaires ont des conséquences sur la loyauté des commandants

---

### Pilier 6 — La Forteresse Vit

Cadia Ultima n'est pas un décor statique. Elle se dégrade, se reconstruit, évolue. Les bâtiments ont des états. La population civile contribue à l'économie et souffre du siège. Les infrastructures se détériorent sous les bombardements. **La cité est un personnage du jeu.**

- Conséquence design : la dégradation visuelle est un feedback gameplay, pas cosmétique
- Conséquence design : la population civile est un système avec des besoins et des conséquences
- Conséquence design : reconstruire sous le feu est un gameplay à part entière

---

### Pilier 7 — La Lisibilité À Toute Échelle

La vue isométrique 2.5D doit permettre de lire d'un coup d'œil : la ligne de front, les flux logistiques, l'état des unités, les zones en danger. **À tout moment, le joueur doit savoir ce qui se passe sans chercher dans des menus.**

- Conséquence design : les icônes de statut sont toujours visibles sur les unités
- Conséquence design : la ligne de front est un overlay permanent, pas un mode à activer
- Conséquence design : les flux supply sont visualisés (routes lumineuses, alertes de coupure)

---

## 3. SYSTÈME DE COMMANDEMENT

### 3.1 Hiérarchie Militaire

```
JOUEUR
  └── Brigade (2-4 régiments)            <- Contrôle direct joueur
        └── Régiment (3-5 compagnies)    <- Commandant IA (overridable)
              └── Compagnie (3-5 squads) <- Commandant IA (overridable)
                    └── Squad (4-12 unités) <- IA autonome locale
```

**Principe clé :** Le joueur donne des ordres aux Brigades. Les commandants IA en dessous décomposent ces ordres en objectifs tactiques. Le joueur peut à tout moment « descendre » dans la hiérarchie et prendre le contrôle manuel d'un niveau — mais c'est un choix qui coûte de l'attention.

**Désactivation du commandant IA :** Un bouton par niveau. Si désactivé, le commandant ne délègue plus automatiquement — le joueur devient responsable. Réactivation possible à tout moment (le commandant reprend avec l'état actuel).

---

### 3.2 Catalogue des Ordres

| Ordre | Niveau minimum | Description | Effets automatiques |
|---|---|---|---|
| **Tenir** | Compagnie | Maintenir la position coûte que coûte | Fortification des positions, gestion des pertes par roulement |
| **Attaquer** | Brigade | Offensive sur un objectif désigné | Déploiement des squads d'assaut en avant, distance en soutien |
| **Flanquer** | Régiment | Contournement d'un point ennemi | Itinéraire alternatif calculé, feinte frontale optionnelle |
| **Replier** | Compagnie | Repli ordonné vers une ligne défensive | Couverture par les éléments d'arrière-garde, destruction des positions |
| **Consolider** | Compagnie | Fortifier une position nouvellement prise | Ingénieurs prioritaires, demande de supply |
| **Harceler** | Régiment | Opérations de harcèlement sans engagement total | Raids, embuscades, sabotage logistique ennemi |
| **Encercler** | Brigade | Enveloppement d'une force ennemie | Coordination multi-régiment, coupure des lignes supply ennemies |

**Paramètres d'ordre :** Chaque ordre accepte des paramètres optionnels — priorité (normale/urgente), règles d'engagement (restrictives/standard/agressives), seuil d'abandon (% pertes avant repli automatique).

---

### 3.3 Comportements Autonomes des Squads

Les squads ont une IA locale qui gère les situations sans attendre d'ordres. Ces comportements sont **non-overridables** par défaut (survie immédiate) mais peuvent être contraints par des règles d'engagement strictes.

#### 3.3.1 Repli Tactique Automatique
- **Déclencheur :** Pertes > seuil configuré (défaut : 40% des effectifs)
- **Action :** Repli vers la position défensive la plus proche en couverture, signal envoyé au commandant de compagnie
- **Override possible :** Règles d'engagement "Tenir à tout prix" (nécessite ordre explicite + validation joueur)

#### 3.3.2 Demande de Renfort
- **Déclencheur :** Squad fixée (ennemi supérieur, pas de progression depuis N secondes)
- **Action :** Signal prioritaire au commandant de régiment → demande d'unités de dégage
- **Réponse automatique si commandant actif :** Détachement d'une squad de réserve si disponible

#### 3.3.3 Exploitation d'Opportunité
- **Déclencheur :** Flanc ennemi exposé détecté, ennemi en repli désordonné
- **Action :** La squad pousse l'avantage sans attendre d'ordre (configurable : "agressive" / "standard" / "prudente")
- **Limite :** Ne dépasse jamais l'objectif de la compagnie parent sans validation commandant

#### 3.3.4 Gestion Supply Autonome
- **Déclencheur :** Munitions < 20% de capacité
- **Action :** Repli vers le supply depot le plus proche, signal de consommation élevée au logisticien
- **Si supply depot hors portée :** Passage en économie de munitions (cadence de tir réduite, engagement seulement si contact direct)

#### 3.3.5 Fortification Réflexe
- **Déclencheur :** Ordre "Tenir" reçu OU présence dans une zone pendant > 2 minutes sans mouvement
- **Action :** Creusage de position (si terrain le permet), placement d'obstacles, optimisation des lignes de tir

---

### 3.4 Interface de Commandement

#### 3.4.1 Vue Opérationnelle (Vue principale joueur)
- Carte isométrique complète avec overlay de commandement
- **Panneau de Brigade :** Liste des brigades avec statut (% effectifs, moral, supply)
- **Ligne de front :** Affichage permanent en overlay coloré (vert = tenu, orange = disputé, rouge = ennemi)
- **Flux supply :** Routes lumineuses animées indiquant le flux logistique actif

#### 3.4.2 Panneau d'Ordre Rapide
Accessible via clic droit sur unité ou zone :

```
[ TENIR ]  [ ATTAQUER >> ]  [ FLANQUER ]  [ REPLIER << ]
[ CONSOLIDER ]  [ HARCELER ]  [ ENCERCLER ]
[ Parametres avances... ]
```

#### 3.4.3 Feed de Commandement
Journal en temps réel (colonne latérale droite) :
- `ALPHA-7 : Squad en repli — pertes 40%`
- `COMPAGNIE FERROX : Renfort demande — secteur 12-B`
- `LOGISTIQUE : Route supply coupee — Depot Nord-Est isole`
- `INTEL : Mouvement ennemi detecte — flanc Ouest`

#### 3.4.4 Zoom Tactique
Double-clic sur une unité → zoom sur la squad avec vue locale :
- Effectifs individuels (icônes colorées : vert/orange/rouge/mort)
- Munitions, moral, fatigue en barres
- Ordre actuel et durée
- Bouton "Prendre le contrôle" → désactive le commandant et passe en contrôle direct

---

### 3.5 Modèle de Commandant IA

Chaque commandant (régiment / compagnie) a des attributs qui affectent sa prise de décision :

| Attribut | Effet |
|---|---|
| **Agressivité** (1-5) | Seuil d'exploitation d'opportunité, volonté d'attaquer |
| **Prudence** (1-5) | Seuil de repli, gestion des réserves |
| **Logistique** (1-5) | Efficacité de gestion du supply, priorisation des dépôts |
| **Initiative** (1-5) | Vitesse de réaction aux changements tactiques sans ordre joueur |

**Commandants nommés :** Chaque brigade a un commandant nommé avec une fiche. Ils peuvent être tués (permadeath). La mort d'un commandant senior crée un délai de commandement et une baisse de moral temporaire.

**Loyauté :** Si un joueur donne trop d'ordres suicidaires, le moral des commandants baisse → délais d'exécution, refus d'ordres extrêmes sans justification. Système discret, jamais punitif arbitrairement.

---

## 4. SYSTÈME DE COMBAT

### 4.1 Catalogue des Unités

#### 4.1.1 Mêlée

| Type | Rôle | Force | Faiblesse |
|---|---|---|---|
| **Troupes de Ligne** | Tiennent la position, absorbent les assauts | Haute endurance, bonne défense | Lentes, vulnérables si flanquées |
| **Assaut** | Percée, nettoyage de bâtiment | Haute mobilité, dommages proches | Faibles à distance, pas de couverture naturelle |
| **Élite** | Suppression, missions spéciales | Polyvalence, moral d'acier | Rares, coûteuses en supply et en recrutement |

#### 4.1.2 Distance

| Type | Rôle | Force | Faiblesse |
|---|---|---|---|
| **Fantassins** | Soutien de ligne général | Polyvalents, abondants | Pas de spécialisation |
| **Tireurs d'Élite** | Élimination de cibles prioritaires (commandants, servants d'armes lourdes) | Longue portée, précision | Fragiles, lents à déployer |
| **Armes Lourdes** (mitrailleuses, mortiers) | Suppression de zone, anti-assaut | Énorme cadence de tir ou puissance explosive | Immobiles pendant l'utilisation, cibles prioritaires ennemies |

#### 4.1.3 Artillerie

| Type | Rôle | Portée | Notes |
|---|---|---|---|
| **Mortiers de campagne** | Soutien rapproché, tirs indirects | Courte | Déplaçables, réactifs |
| **Artillerie lourde** | Destruction de fortifications, contre-batterie | Longue | Lente à repositionner, très visible |
| **Artillerie AA** | Défense anti-aérienne | Variable | Passive/active selon doctrine |

---

### 4.2 Cohérence de Front

**Principe fondamental :** Le front est une ligne vivante. Sa cohérence détermine l'efficacité de chaque unité qui y opère.

#### 4.2.1 Mécanique de Front
- La mêlée forme la **ligne de contact** — tient l'ennemi en place
- La distance tire **derrière** la mêlée — maximise les dégâts sans risque de contact
- Les armes lourdes se positionnent aux **points d'appui** (bâtiments, élévations)
- L'artillerie reste en **arrière-plan** protégé

**Flancs exposés :**
- Si un flanc de la ligne est ouvert → les unités sur ce flanc subissent un malus de moral et d'efficacité (elles regardent dans deux directions, tirent moins précisément)
- Si l'ennemi pénètre derrière la ligne → les unités de distance sont vulnérables (pas de protection contre la mêlée, panique possible)
- **Règle d'or :** Fermer les flancs avant d'attaquer. Toujours.

#### 4.2.2 Formation de Front — États

| État | Description | Malus/Bonus |
|---|---|---|
| **Ligne cohérente** | Front continu, flancs protégés | Aucun malus, bonus moral +10% |
| **Front étiré** | Unités trop espacées, couverture partielle | Malus défense -15%, flancs exposés |
| **Front rompu** | Percée ennemie dans la ligne | Panique potentielle, moral -30%, unités isolées |
| **Encerclement partiel** | Ennemi sur 2+ côtés | Malus critique, repli d'urgence déclenché si non ordonné |

---

### 4.3 Système de Moral

Le moral est la ressource invisible la plus importante. Une unité à 0 de moral se débande, quelle que soit sa puissance de feu restante.

#### 4.3.1 Facteurs de Moral

**Positifs :**
- Présence d'un commandant à proximité (`+5%/min`)
- Victoires récentes, positions tenues sous pression (`+2%/min`)
- Supply abondant (munitions pleines, ravitaillement régulier) (`+3%/min`)
- Renforts reçus, artillerie amie active (`+4%`)

**Négatifs :**
- Pertes dans la squad (`-5% par mort dans les 60s`)
- Bombardement ennemi actif (`-8%/min`)
- Encerclement ou flanc rompu (`-15%/min`)
- Commandant tué (`-20% immédiat`)
- Supply coupé > 5 minutes (`-3%/min`)
- Ordres contradictoires répétés (`-2%/min`)

#### 4.3.2 États de Moral

| Niveau | Seuil | Comportement |
|---|---|---|
| **Héroïque** | 90-100% | Bonus offensif, exploitation spontanée |
| **Combattif** | 70-90% | Comportement normal |
| **Tendu** | 50-70% | Hésitation légère, repli à pertes moindres |
| **Ébranlé** | 30-50% | Ralentissement, demande de renfort automatique |
| **En déroute** | 10-30% | Repli non ordonné, abandon de positions |
| **Débandade** | 0-10% | Fuite, unité temporairement hors contrôle |

**Récupération de moral :** Retrait de la ligne + ravitaillement + temps. Pas de récupération en contact avec l'ennemi.

---

### 4.4 Dégradation des Bâtiments

#### 4.4.1 Machine d'États

```
INTACT --> ENDOMMAGE --> RUINE --> GRAVATS
  |            |           |
  v            v           v
(bombardement / feu nourri / sapeurs / temps sous siège)
```

#### 4.4.2 Implications Tactiques par État

| État | Protection | Ligne de tir | Valeur pour l'ennemi | Notes |
|---|---|---|---|---|
| **Intact** | Élevée (couverture lourde) | Bloquée (obstacle solide) | Obstacle / objectif | Peut abriter une garnison |
| **Endommagé** | Modérée (murs partiels) | Nouvelles ouvertures, brèches | Peut infiltrer par les brèches | Ingénieurs ennemis exploitent les brèches |
| **Ruine** | Faible mais réelle (décombres) | Multidirectionnelle, imprévisible | Position défensive ennemie possible | LES DEUX CAMPS peuvent s'y installer |
| **Gravats** | Nulle | Dégagée | Terrain difficile | Ralentit le mouvement, couverture zéro |

**Règle critique :** Détruire un bâtiment pour dégager une ligne de tir crée une position défensive pour l'ennemi. Le joueur doit évaluer : est-ce que gagner cette ligne de tir vaut de donner à l'ennemi un bunker de ruines ?

#### 4.4.3 Reconstruction

- **Ingénieurs civils et militaires** peuvent reconstruire des bâtiments Ruine → Endommagé → Intact
- Coût : matériaux (ferraille, béton), temps, sécurité (zone doit être hors contact direct)
- Priorité joueur : le joueur peut désigner des bâtiments prioritaires pour la reconstruction
- L'ennemi **cible les chantiers de reconstruction** — reconstruire sous le feu est dangereux

---

### 4.5 Siège et Préparation d'Assaut

#### 4.5.1 Cycle de Siège Défensif

Le siège ennemi suit un cycle prévisible (mais adaptable) :

1. **Bombardement de neutralisation** — artillerie sur les positions de défense désignées
2. **Raids de sapeurs** — équipes d'ingénieurs qui sapent les murs ou posent des explosifs
3. **Assaut principal** — infanterie mêlée et distance en vague coordonnée
4. **Exploitation** — si percée, les réserves poussent

Le joueur doit gérer les **priorités de contre-batterie** : neutraliser l'artillerie ennemie coûte des ressources mais réduit les dégâts du bombardement.

#### 4.5.2 Contre-mesures Défensives

| Menace | Contre-mesure | Coût |
|---|---|---|
| Bombardement artillerie | Contre-batterie, dispersion des défenses | Munitions lourdes, exposer les pièces |
| Sapeurs | Gardes rapprochés, champs de mines | Unités mobilisées, matériaux |
| Assaut de brèche | Réserves positionnées, barricades secondaires | Unités en réserve, matériaux |
| Tunnel | Détection sismique, contre-mining | Investissement infrastructure |

---

## 5. SYSTÈME ÉCONOMIQUE

*L'économie de Last Cadia est une machine de guerre. Chaque composant produit quelque chose qui nourrit ou équipe quelqu'un. La pénurie est une arme que l'ennemi utilise activement.*

### 5.1 Vue d'ensemble — Les 3 Couches

```
COUCHE 1 — Production de Base
  Mines, fermes, carrières, raffineries
  Produit : matières premières + nourriture

        |
        v

COUCHE 2 — Économie de Guerre
  Usines d'armement, ateliers, hôpitaux
  Produit : munitions, équipements, soins

        |
        v

COUCHE 3 — Logistique
  Routes, rails, dépôts, convois
  Distribue tout vers le front
```

Chaque couche peut être gérée en **mode automatique** (IA gère les priorités), **mode manuel** (joueur définit les allocations), ou **mode mixte** (automatique avec override sur certains systèmes).

---

### 5.2 Couche 1 — Production de Base

#### 5.2.1 Ressources Primaires

| Ressource | Source | Usage |
|---|---|---|
| **Ferraille** | Mines de fer, récupération de ruines | Armement, construction, réparations |
| **Carburant** | Raffineries, puits de pétrole | Véhicules, générateurs, chauffage |
| **Nourriture** | Fermes hydroponiques, entrepôts | Population civile, rations militaires |
| **Béton/Pierre** | Carrières | Construction, fortifications |
| **Composants électroniques** | Ateliers spécialisés | Équipements avancés, communication |

#### 5.2.2 Besoins de la Population Civile

La population n'est pas un décor. Elle a des **besoins réels** qui, s'ils ne sont pas satisfaits, génèrent des conséquences gameplay :

| Besoin | Si satisfait | Si non satisfait |
|---|---|---|
| **Nourriture** | Moral civil élevé, productivité +10% | Grèves, moral militaire -5%, désertions |
| **Eau** | Normal | Épidémies (-population, -productivité) |
| **Chauffage** | Normal | Maladie, moral -15% en saison froide |
| **Sécurité** | Travailleurs aux postes | Exode vers l'intérieur, -main d'oeuvre |
| **Médecine** | Récupération des blessés | Mortalité accrue, -effectifs long terme |

#### 5.2.3 Chaîne de Production Exemple — Munitions d'infanterie

```
Mine de fer --> Fonderie --> Ferraille transformée
                                    |
Raffinerie --> Poudre              |
                |                  v
                +------> Atelier d'armement --> Munitions d'infanterie
                                                        |
                                               Dépôt de munitions
                                                        |
                                            Convoi vers le front
                                                        |
                                                 Squad ravitaillée
```

**Durée de cycle :** ~15 minutes de jeu de la mine à la squad (sans goulot d'étranglement).
**Vulnérabilités :** Chaque nœud peut être bombardé, sabote, ou manquer de personnel.

---

### 5.3 Couche 2 — Économie de Guerre

#### 5.3.1 Productions Militaires Critiques

| Produit | Composants | Urgence |
|---|---|---|
| **Munitions légères** (infanterie) | Ferraille + poudre | Critique — consommation permanente |
| **Munitions lourdes** (artillerie) | Ferraille x3 + composants + poudre x2 | Élevée — gros volumes |
| **Pièces détachées** | Ferraille + composants | Moyenne — pour maintien des véhicules |
| **Équipements de combat** (armures, armes) | Ferraille + composants | Faible — remplacement lent |
| **Médicaments** | Composants + nourriture | Critique — impact direct sur récupération blessés |
| **Matériaux de construction** | Béton + ferraille | Variable — pic après bombardements |

#### 5.3.2 Hôpitaux et Récupération

Les soldats blessés ne meurent pas instantanément — ils sont **évacués vers les hôpitaux** (si la route est ouverte). Le taux de récupération dépend de :
- La capacité hospitalière (lits disponibles)
- Le stock de médicaments
- Le délai d'évacuation (plus long = moins de récupération)

**Décision de design :** Un joueur qui laisse ses routes d'évacuation coupées perd ses blessés. L'ennemi le sait et cible les routes de l'arrière.

---

### 5.4 Couche 3 — Logistique

La logistique est le système le plus stratégique du jeu. Une armée bien équipée mais mal ravitaillée est une armée morte.

#### 5.4.1 Infrastructure de Transport

| Type | Capacité | Vitesse | Vulnérabilité |
|---|---|---|---|
| **Route pavée** | Moyenne | Normale | Bombardements, sabotage |
| **Rail** | Élevée | Rapide | Infrastructures fixes, facile à détruire |
| **Route de terre** | Faible | Lente | Météo, usure, boue |
| **Portage manuel** | Très faible | Très lente | Unités mobilisées pour la logistique |

#### 5.4.2 Supply Depots

Les **dépôts de ravitaillement** sont les nœuds critiques de la logistique :
- Capacité de stockage limitée (gestion d'inventaire)
- Rayons d'approvisionnement : les unités dans le rayon peuvent se ravitailler
- Si un dépôt est pris ou détruit → toutes les unités dans son rayon passent en pénurie

**Règle de placement :** Le joueur doit réfléchir à la profondeur stratégique. Un dépôt trop proche du front est vulnérable ; trop loin, il ralentit le ravitaillement.

#### 5.4.3 Goulots d'Étranglement

Le système génère naturellement des goulots d'étranglement — points uniques de passage pour un flux logistique. Exemples :
- **Le Pont de Velkar** : seule route directe vers le front nord-ouest. Si bombardé → le front nord-ouest s'affame en 10 minutes de jeu
- **Le Nœud Ferroviaire Central** : dessert 70% des dépôts. Si pris → crise logistique massive

**L'IA ennemie (Gemma 4) identifie ces goulots dans son snapshot et les cible en priorité.**

#### 5.4.4 Convois et Flux

- Les convois (camions, chariots) se déplacent automatiquement sur les routes désignées
- Le joueur peut **prioriser des routes** (flèche de priorité sur une route → convois préférent ce chemin)
- **Escorte possible :** Assigner des unités militaires à un convoi pour le protéger
- **Pertes de convoi :** Un convoi attaqué perd sa cargaison → double pénalité (ressources + temps)

---

### 5.5 Modes de Gestion Économique

| Mode | Description | Pour qui |
|---|---|---|
| **Auto complet** | L'IA éco gère toutes les priorités | Joueurs focalisés sur le tactique |
| **Auto avec alertes** | Auto, mais signale les crises pour décision joueur | Mode recommandé par défaut |
| **Semi-manuel** | Joueur fixe les ratios de production, auto gère le reste | Joueurs intermédiaires |
| **Manuel complet** | Joueur alloue chaque ressource et chaque convoi | Hardcore / min-maxing |

**Note de design :** L'économie doit être suffisamment profonde pour que le mode manuel soit récompensant, mais suffisamment gérée en auto pour que le joueur focalisé tactique ne se noie pas.

---

## 6. L'ENNEMI TACTIQUE

*Les Fractalis ne sont pas une horde. Ils ont des doctrines, des ingénieurs, des chaînes logistiques, et deux cerveaux artificiels qui planifient leur prochaine offensive pendant que tu dors.*

### 6.1 Structure de Force Ennemie

| Rôle | Unités | Mission |
|---|---|---|
| **Avant-garde** | Éclaireurs légers, drones de reco | Cartographier les défenses, identifier les goulots logistiques |
| **Corps principal** | Infanterie mêlée et distance | Assaut frontal et semi-encerclement |
| **Ingénieurs** | Sapeurs, équipes de construction | Creuser tranchées, poser supply hubs, forts avancés, tunnels |
| **Artillerie** | Mortiers, pièces lourdes, contre-batterie | Neutralisation des défenses avant assaut |
| **Réserves** | Unités fraîches, élite | Exploiter les brèches, consolider les gains |

**Persistance des unités :** Les unités ennemies restent sur la carte avec leurs propres besoins logistiques. Elles ont faim. Elles manquent de munitions. Couper leur supply est une stratégie viable.

---

### 6.2 Ce que l'Ennemi Construit

L'ennemi n'est pas statique. Ses ingénieurs construisent des positions en temps réel :

| Construction | Durée | Effet |
|---|---|---|
| **Tranchée** | Court | Couverture pour l'infanterie, ligne de progression vers le front |
| **Supply Hub** | Moyen | Ravitaillement des unités dans la zone, capacité de soutien prolongé |
| **Position d'artillerie** | Moyen | Plateforme pour pièces lourdes, protection partielle |
| **Fort avancé** | Long | Position défensive renforcée, commandement local |
| **Tunnel sapeur** | Très long | Passage sous les murs, surprise totale si non détecté |
| **Réseau de tranchées** | Long | Connexion de positions, mouvement protégé sous le feu |

**Implication :** Le joueur doit surveiller la construction ennemie et décider quand interrompre. Un supply hub non détruit nourrit le siège pendant des heures.

---

### 6.3 Cycle d'Attaque Fractalis

Le cycle est reproductible mais **les paramètres changent à chaque itération** selon ce que l'IA a appris.

```
Phase 1 — RECONNAISSANCE
  Éclaireurs cartographient positions, supply depots, points faibles
  Durée : 2-5 min de jeu
  Signal joueur : mouvements furtifs en périphérie

Phase 2 — HARCELEMENT
  Frappes légères pour tester les réactions défensives
  Durée : 3-8 min
  Objectif IA : identifier les patterns de réponse du joueur (data pour ML)

Phase 3 — PREPARATION ARTILLERIE
  Bombardement ciblé des défenses clés, logistique, commandement
  Durée : 2-5 min
  Signal joueur : barrage incoming, alertes sur l'économie

Phase 4 — ASSAUT PRINCIPAL
  Vague coordonnée : front + flancs + bombardement simultané
  Durée : variable (jusqu'à rupture ou repli)
  Multi-wave : vague A fixe la ligne, vague B flanque, artillerie continue

Phase 5 — EXPLOITATION
  Si brèche : réserves poussent immédiatement, ne pas laisser le temps de réorganiser
  Si repli : harcèlement du repli pour maximiser les pertes

Phase 6 — CONSOLIDATION
  L'ennemi fortifie les gains, construit supply hubs, pose sa ligne de front
  Le joueur doit contre-attaquer VITE ou la position devient défendable par l'ennemi
```

---

### 6.4 Doctrine Adaptative

**C'est le coeur du différenciateur de Last Cadia.** L'IA ennemie change de doctrine en réponse aux stratégies défensives du joueur.

| Si le joueur fait... | L'ennemi adapte... |
|---|---|
| Artillerie lourde en position fixe | Dispersion, mouvement zigzag, envoi de sapeurs pour neutraliser les pièces |
| Mêlée dense en ligne de front | Tirs de suppression intensifs, débordement par les flancs, mortiers sur la ligne |
| Fortifications lourdes (murs, bunkers) | Tunnels sapeurs, artillerie anti-fortification, attaque des ravitaillements |
| Sniper dense | Smoke + avance rapide, unités d'assaut prioritaires sur les positions de sniper |
| Réserves toujours au même endroit | Frappe sur la position de réserve lors de l'assaut suivant |
| Ligne de supply directe et unique | Attaque du nœud logistique en priorité |

**Principe ML :** Le système de machine learning observe les patterns sur plusieurs assauts et ajuste les probabilités comportementales des unités. Ce n'est pas de la triche — c'est de l'apprentissage. Le joueur qui varie ses stratégies garde l'avantage.

---

### 6.5 Architecture IA Ennemie

Deux couches distinctes, deux temporalités différentes.

#### 6.5.1 Gemma 4 — Cerveau Stratégique (Couche Opérationnelle)

**Rôle :** Planification stratégique. Décide *quoi* faire, pas *comment* le faire.

**Fréquence :** Toutes les ~30 secondes de jeu (configurable). Pas chaque frame — c'est intentionnel. Un cerveau stratégique réfléchit lentement.

**Input — Snapshot JSON de la carte :**
```json
{
  "timestamp": 1842,
  "map_state": {
    "districts": [
      {
        "id": "nord_ouest",
        "controller": "defender",
        "buildings": [{"type": "usine", "state": "endommage"}],
        "defender_units": [{"type": "infanterie", "count": 24, "supply": 0.6}],
        "attacker_units": []
      }
    ],
    "front_line": [...],
    "supply_routes": [
      {"id": "pont_velkar", "active": true, "flow": 0.8}
    ]
  },
  "attacker_forces": {
    "available": {"infantry": 120, "artillery": 8, "engineers": 15},
    "deployed": [...],
    "supply_status": 0.75
  },
  "history": {
    "last_assault_result": "repousse",
    "defender_pattern": "ligne_dense_avec_artillerie_fixe"
  }
}
```

**Output — Plan JSON :**
```json
{
  "plan_id": "assault_42",
  "objective": "secteur_nord_ouest",
  "phases": [
    {
      "phase": "preparation",
      "action": "artillery_strike",
      "targets": ["supply_depot_A", "artillery_position_3"],
      "duration_estimate": 180
    },
    {
      "phase": "assault",
      "main_attack": {"direction": "nord", "force": 60},
      "flank_attack": {"direction": "ouest", "force": 30},
      "reserve": {"force": 30, "trigger": "breach_detected"}
    }
  ],
  "adaptation_notes": "Defender uses fixed artillery — send sappers first"
}
```

#### 6.5.2 ML — Réflexes Tactiques (Couche Tactique)

**Rôle :** Adaptation comportementale en temps réel des unités individuelles et des squads.

**Inspiration :** MeguraColonyML — behavior adaptation par réseau de neurones léger.

**Ce que le ML observe :**
- Positions des défenseurs lors des N derniers assauts
- Patterns de tir (zones de concentration, fréquence, réactivité)
- Positions des réserves (où le joueur envoie ses renforts habituellement)
- Temps de réaction du joueur aux différentes menaces

**Ce que le ML modifie :**
- Probabilités de chemin des unités (éviter les zones à fort taux de mortalité historique)
- Aggressivité de formation (dense vs dispersé selon les historiques de pertes)
- Ciblage prioritaire (unités qui ont causé le plus de dégâts historiquement)
- Timing d'exploitation (si le joueur met toujours X secondes à réagir, exploiter cette fenêtre)

**Séparation Gemma / ML :**
- Gemma dit : "Attaque par le nord-ouest, priorité sur le supply depot"
- ML dit : "En approchant, évite la rue principale (taux de mortalité 85% là-bas), passe par les ruines à l'est"

#### 6.5.3 IA Économique Ennemie

Séparée de Gemma et du ML — gère les ressources et la construction ennemies de façon autonome :
- Priorise construction selon les phases du cycle (sapeurs en phase préparation, forts en phase consolidation)
- Alerte Gemma si supply ennemi critique (Gemma adapte le plan)
- Gère les convois d'approvisionnement ennemis (vulnérables aux raids du joueur)

---

## 7. CARTE & TERRITOIRE

### 7.1 Zones Géographiques

La carte de Cadia Ultima est divisée en zones distinctes avec des profils tactiques et économiques différents. Chaque district est un enjeu.

| Zone | Type | Valeur Économique | Valeur Tactique |
|---|---|---|---|
| **Urbaine** | Rues denses, immeubles | Main d'oeuvre, logement | Combat de rue, nombreuses positions de couverture |
| **Industrielle** | Usines, entrepôts, rails | Production critique | Grands espaces ouverts + structures lourdes |
| **Rurale** | Fermes périphériques | Nourriture, espace de manoeuvre | Terrain ouvert, combat de mouvement |
| **Fortifiée** | Murs, tours, bastions | Peu d'économie | Défense maximale, choke points |
| **Portuaire/Logistique** | Rails, dépôts, pont | Distribution totale | Goulot d'étranglement, cible prioritaire ennemie |

---

### 7.2 Ligne de Front Dynamique

La ligne de front n'est pas une ligne fixe — c'est une zone de contact évolutive calculée en temps réel.

#### 7.2.1 Calcul de la Ligne

La ligne de front est calculée par **district** (pas par unité) :
- Chaque district a un **indice de contrôle** : somme pondérée des forces présentes, des fortifications, et du supply actif
- Si l'indice défenseur > ennemi → district "Tenu"
- Si les deux indices sont proches → district "Disputé" (orange)
- Si l'indice ennemi > défenseur → district "Ennemi" (rouge)

**Affichage :** Overlay coloré permanent sur la carte, mis à jour toutes les 5 secondes de jeu.

#### 7.2.2 États d'un District

| État | Couleur | Signification | Actions disponibles |
|---|---|---|---|
| **Tenu** | Vert | Contrôle défenseur stable | Construire, produire, recruter |
| **Disputé** | Orange | Contact actif, possession incertaine | Combat, renfort urgent |
| **Menacé** | Jaune | Ennemi approche, pas encore en contact | Renforcer, préparer défenses |
| **Ennemi** | Rouge | Contrôle Fractalis | Raid/contre-attaque possible |
| **Ruiné** | Gris | District détruit, inutilisable | Terrain difficile seulement |

---

### 7.3 Points Stratégiques

Certains points valent plus que les autres. Leur prise ou leur perte a des conséquences immédiates et systémiques.

#### 7.3.1 Usines de Production

- **Perte :** Arrêt de la production associée (munitions, ferraille, etc.)
- **Récupération :** Contre-attaque + reconstruction partielle (2-10 min selon dégâts)
- **Ciblage ennemi :** Gemma les identifie dans le snapshot et les cible en priorité si supply ennemi OK

#### 7.3.2 Supply Depots

- **Perte :** Toutes les unités dans le rayon passent en pénurie immédiate
- **Vulnérabilité :** Position fixe, grand bâtiment visible, cible de choix pour l'artillerie
- **Contre-mesure :** Multiplication des dépôts (résilience par redondance), couverture AA

#### 7.3.3 Points Hauts (Tours, Bâtiments élevés, Collines)

- **Avantage :** Bonus de portée et de précision pour les unités qui y sont (line of sight étendue)
- **Valeur IA :** L'ennemi veut ces positions pour son artillerie et ses tireurs d'élite
- **Combat pour les points hauts :** Souvent les combats les plus intenses de la carte

#### 7.3.4 Nœuds de Transport (Ponts, Carrefours, Gares)

- **Perte :** Coupure logistique majeure, isolation de secteurs entiers
- **Défense :** Fortification prioritaire, mines, garnisons permanentes
- **Destruction volontaire :** Option tactique — couper une route pour ralentir l'ennemi mais aussi couper sa propre logistique

#### 7.3.5 Centre de Commandement

- **Fonction :** Boost de moral pour toutes les unités dans la région, centre des communications
- **Perte :** Chaos de commandement — délai sur tous les ordres, moral -20%
- **Déplacement possible :** En dernier recours, le QG peut être déplacé (lent, vulnérable pendant le déménagement)

---

### 7.4 Carte de Référence — Cadia Ultima (Design Initial)

```
[NORD — Approche Ennemie Principale]

   [Rurale Nord]   [Fortif. Nord]   [Rurale Nord-Est]
         |               |                |
   [Industrielle]  [CENTRE VILLE]   [Portuaire/Rails]
         |               |                |
   [Fortif. Ouest] [District Minier] [Industrielle Est]
         |               |                |
      [MURAILLE SUD — Ligne de Repli Finale]

Légende :
 * = Point stratégique (supply depot / usine clé / point haut)
 # = Nœud de transport (pont / gare / carrefour)
```

**Districts prioritaires défenseur (Early Game) :**
1. Fortifications Nord — première ligne
2. Nœuds ferroviaires — supply artères
3. Centre industriel — production de munitions

**Districts prioritaires défenseur (Mid Game) :**
1. Toujours les fortifications, mais avec réserves en profondeur
2. Points hauts du centre-ville pour l'artillerie
3. Supply depots multipliés et dispersés

---

## 8. BOUCLE DE JEU

### 8.1 La Boucle à Trois Échelles

Le jeu fonctionne sur trois temporalités imbriquées. La tension vient de la gestion simultanée de ces trois niveaux.

---

### 8.2 Boucle Minute — L'Urgence Tactique

*Ce qui se passe en 1-5 minutes de jeu*

```
SIGNAL D'ALERTE (attaque détectée / squad en repli / supply coupé)
      |
      v
EVALUATION RAPIDE (où ? avec quoi ? combien ?)
      |
      v
ORDRE IMMÉDIAT (renfort / replier / contre-attaquer)
      |
      v
FEEDBACK (la squad tient / recule / est détruite)
      |
      v
AJUSTEMENT (nouvelle position / demande logistique / ordre commandant)
```

**Tension typique de la boucle minute :**
- Une squad envoie une demande de renfort (secteur 12-B)
- Le joueur voit que la compagnie FERROX a une squad en réserve
- Il ordonne le déplacement — mais la route passe par une zone bombardée
- Décision : route directe (risque) ou détour (délai) ?

---

### 8.3 Boucle Heure — La Gestion Opérationnelle

*Ce qui se passe en 20-60 minutes de jeu*

```
PHASE CALME (entre les assauts)
  - Production économique
  - Reconstruction des positions
  - Réorganisation des brigades
  - Renseignement sur les mouvements ennemis

PHASE DE TENSION (cycle ennemi Phase 1-3)
  - Reco ennemie détectée
  - Reinforcement des positions clés
  - Préparation de la contre-batterie
  - Positionnement des réserves

PHASE D'ASSAUT (cycle ennemi Phase 4-5)
  - Tous les systèmes en mode crise
  - Décisions rapides en cascade
  - Gestion du moral et des pertes
  - Décision de tenir ou replier

PHASE DE CONSOLIDATION (cycle ennemi Phase 6)
  - Évaluer les pertes (les deux camps)
  - Reprendre les positions si possible
  - Sécuriser les gains ou limiter les dégâts
  - Réévaluer la ligne de front
```

---

### 8.4 Boucle Session — La Progression Stratégique

*Ce qui se passe sur une session complète (2-4h)*

#### Phase Early — "Construire le Rempart"
- Établir les chaînes de production
- Fortifier la première ligne
- Apprendre les patterns ennemis du premier assaut
- Premiers sacrifices — décider quels districts défendre

#### Phase Mid — "La Guerre d'Usure"
- Les deux camps ont leurs positions — le front se stabilise
- Guerre économique : l'ennemi cible les usines, le joueur cible les supply hubs ennemis
- L'IA ennemie a accumulé des données ML — les assauts deviennent plus précis
- Le joueur doit innover (changer de doctrine) pour contrer l'adaptation

#### Phase Late — "Le Dernier Rempart"
- La cité est partiellement détruite — les ruines font partie du terrain
- Les ressources se raréfient (mines épuisées, usines détruites)
- L'ennemi prépare l'assaut final — le joueur reçoit des renforts (si il a tenu assez longtemps)
- Décision finale : tenir le Centre de Commandement jusqu'à l'évacuation ou la victoire

---

### 8.5 Conditions de Victoire et de Défaite

#### Victoire
- **Tenir jusqu'aux Renforts :** Maintenir le Centre de Commandement opérationnel jusqu'à l'arrivée des renforts alliés (timer variable selon difficulté)
- **Briser le Siège :** Détruire suffisamment d'infrastructure ennemie pour forcer le repli stratégique Fractalis

#### Défaite
- **Chute du Centre de Commandement :** Le QG ennemi prend le contrôle du CC → game over (pas de respawn)
- **Effondrement économique :** Toutes les chaînes de production détruites + aucun stock → les troupes se débandent dans les 15 minutes de jeu

#### Défaite Partielle (Conditions de Survie)
- Certains districts perdus mais la cité tient → mission continue avec pénalités
- Permet des remontées dramatiques — caractéristique du grimdark (tenir sur les ruines)

---

## 9. ARCHITECTURE TECHNIQUE GROVEENGINE

*Mapping des systèmes de jeu vers les modules GroveEngine C++17. Référence pour l'implémentation.*

### 9.1 Contraintes Techniques Existantes

| Aspect | État actuel | Impact sur Last Cadia |
|---|---|---|
| **Langage** | C++17, modulaire | OK — performances suffisantes |
| **Pub/sub** | Topic tree | Tous les systèmes communiquent via topics |
| **Renderer** | bgfx, sprites instanciés, tilemaps, particules | Tilemap pour la carte, sprites pour unités |
| **Vue** | Isométrique 2.5D | Adapté au gameplay, lisibilité à vérifier |
| **ECS** | Pas de natif | Gérer les entités avec structures custom |
| **Pathfinding** | Pas de masse — à prévoir | Priorité haute pour le MVP |

---

### 9.2 Modules à Créer

#### 9.2.1 CommandModule
**Rôle :** Gestion de la hiérarchie militaire et des ordres

```cpp
// CommandModule — gère l'arbre de commandement
// Chaque noeud peut être en mode IA ou controle joueur
class CommandNode {
    CommandLevel level;           // BRIGADE / REGIMENT / COMPAGNIE / SQUAD
    CommandNodeID parent;
    vector<CommandNodeID> children;
    bool ai_controlled;           // false = joueur a pris le controle
    CommanderStats commander;     // attributs du commandant
    Order current_order;
    float loyalty;                // impact des ordres suicidaires
};
```

**Topics publiés :**
- `command/order/issued` — ordre émis par le joueur ou un commandant IA
- `command/order/completed` — ordre terminé (succès ou échec)
- `command/commander/killed` — commandant tué (permadeath)
- `command/squad/request_reinforcement` — demande de renfort montante

**Topics souscrits :**
- `combat/unit/casualties` — pour déclencher les comportements autonomes
- `logistics/supply/shortage` — pour déclencher le repli supply des squads

---

#### 9.2.2 CombatModule
**Rôle :** Résolution des combats, cohérence de front, moral

```cpp
// CombatModule — resout les engagements entre unites
// Pas d'ECS natif : chaque unite est un objet dans un pool
class CombatUnit {
    UnitType type;                // MELEE / RANGED / ARTILLERY
    float morale;                 // 0.0 - 1.0
    float ammo;                   // 0.0 - 1.0
    float health;                 // 0.0 - 1.0
    FrontlineStatus frontStatus;  // COHESIVE / STRETCHED / BROKEN
    SquadID squad;
};
```

**Topics publiés :**
- `combat/unit/morale_changed` — changement de moral (déclenche animations UI)
- `combat/unit/casualties` — pertes (connecté à CommandModule et LogisticsModule)
- `combat/frontline/status_changed` — changement d'état de la ligne de front
- `combat/building/state_changed` — transition d'état d'un bâtiment

**Topics souscrits :**
- `command/order/issued` — pour appliquer les ordres aux unités
- `logistics/supply/ammo_delivered` — réapprovisionnement munitions

---

#### 9.2.3 EconomyModule
**Rôle :** Gestion des 3 couches économiques

```cpp
// EconomyModule — machine de production et logistique
// Graphe de production : chaque noeud est un batiment producteur
class ProductionNode {
    BuildingID building;
    vector<ResourceCost> inputs;   // ressources consommees
    vector<ResourceOutput> outputs; // ressources produites
    float efficiency;               // 0.0 - 1.0 (degrade si endommage)
    bool active;                    // false si destroy ou sans worker
};
```

**Topics publiés :**
- `economy/resource/produced` — production d'une ressource
- `economy/supply/depot_critical` — dépôt en dessous du seuil critique
- `economy/logistics/route_cut` — route logistique coupée
- `economy/population/needs_unmet` — besoin civil non satisfait

**Topics souscrits :**
- `combat/building/state_changed` — dégradation d'un bâtiment de production
- `enemy/logistics/sabotage` — sabotage ennemi d'une infrastructure

---

#### 9.2.4 EnemyAIModule
**Rôle :** Orchestration de Gemma 4 + ML + IA économique ennemie

```cpp
// EnemyAIModule — deux couches IA separees
// Gemma tourne toutes les N secondes via API asynchrone
// ML tourne en temps reel sur les decisions d'unite

class GemmaInterface {
    // Serialise le snapshot carte en JSON
    json BuildMapSnapshot() const;
    // Parse le plan d'attaque retourne par Gemma
    AttackPlan ParseGemmaPlan(const json& response);
    // Appel asynchrone — ne bloque pas le game loop
    void RequestPlanAsync();
};

class MLBehaviorAdaptor {
    // Observe les patterns defensifs du joueur
    void RecordDefenderPattern(const CombatEvent& event);
    // Ajuste les probabilites comportementales des unites ennemies
    BehaviorWeights GetAdaptedWeights(UnitType type, ZoneID zone);
};
```

**Topics publiés :**
- `enemy/ai/plan_updated` — nouveau plan Gemma reçu
- `enemy/ai/behavior_adapted` — ML a mis à jour les comportements
- `enemy/unit/order_issued` — ordre donné à une unité ennemie

**Topics souscrits :**
- `combat/unit/casualties` (côté ennemi) — données pour le ML
- `economy/resource/produced` (côté ennemi) — IA éco ennemie

---

#### 9.2.5 TerritoryModule
**Rôle :** Gestion des districts, ligne de front, points stratégiques

```cpp
// TerritoryModule — calcule l'indice de controle de chaque district
// Mis a jour toutes les N secondes (pas chaque frame — calcul couteux)
class District {
    DistrictID id;
    DistrictType type;            // URBAN / INDUSTRIAL / RURAL / FORTIFIED
    float control_index;          // positif = defenseur, negatif = ennemi
    vector<StrategicPoint> points;
    BuildingState avg_building_state;
};
```

**Topics publiés :**
- `territory/district/control_changed` — changement de contrôle d'un district
- `territory/strategic_point/captured` — point stratégique pris

---

### 9.3 Arbre de Topics (Vue Synthétique)

```
/lastcadia/
  /command/
    /order/issued
    /order/completed
    /commander/killed
    /squad/request_reinforcement
  /combat/
    /unit/morale_changed
    /unit/casualties
    /frontline/status_changed
    /building/state_changed
  /economy/
    /resource/produced
    /supply/depot_critical
    /logistics/route_cut
    /population/needs_unmet
  /enemy/
    /ai/plan_updated
    /ai/behavior_adapted
    /unit/order_issued
    /logistics/sabotage
  /territory/
    /district/control_changed
    /strategic_point/captured
  /ui/
    /alert/critical
    /overlay/frontline_update
    /feed/command_log
```

---

### 9.4 Pathfinding — Priorité Haute

**Problème :** GroveEngine n'a pas de pathfinding de masse natif. Last Cadia requiert :
- Pathfinding de squads (groupes de 4-12 unités)
- Pathfinding de convois logistiques
- Pathfinding adaptatif ennemi (ML influence les chemins)

**Solution recommandée :**
- **Flow fields** pour les grands mouvements de masse (moins coûteux que A* par unité)
- **A* local** pour les unités individuelles dans les combats de rue
- **Hiérarchical pathfinding (HPA*)** pour les décisions à longue portée (Gemma → ML → unité)

**Module à créer :** `PathfindingModule` avec interface pub/sub :
- Input : `pathfinding/request` (unité + destination + priorité)
- Output : `pathfinding/path_ready` (unité + chemin calculé)

---

## 10. RISQUES DE DESIGN

*Identifier les risques en amont permet de les mitiger par le design. Chaque risque est accompagné d'un niveau de criticité et d'une mitigation recommandée.*

---

### Risque 1 — Complexité Écrasante (CRITIQUE)

**Description :** Le jeu combine économie profonde + commandement hiérarchique + IA adaptative + moral + logistique. Un joueur non initié pourrait se sentir submergé dès les premières minutes.

**Probabilité :** Élevée (complexité objective très haute)

**Mitigation :**
- Tutoriel en couches progressives : commencer avec un seul régiment et une économie minimale
- Mode "Auto complet" pour désactiver les couches que le joueur ne veut pas gérer
- Alertes contextuelles non intrusives (pas de popup bloquant)
- Premier niveau : siège défensif simple, pas d'économie de production (ressources données)
- **Règle de design :** Si un nouveau joueur ne comprend pas ce qu'il doit faire en 3 minutes, c'est un bug de design

---

### Risque 2 — L'IA Trop Forte / Frustrante (ÉLEVÉ)

**Description :** Une IA qui apprend trop vite et punit trop sévèrement les habitudes défensives peut rendre le jeu frustrant plutôt qu'engageant.

**Probabilité :** Élevée (ML sans calibration = risque réel)

**Mitigation :**
- **Transparence IA :** Le joueur doit voir que l'IA s'adapte (message d'intel : "Les Fractalis modifient leur approche")
- **Délai d'adaptation :** Le ML ne doit pas adapter après 1 observation, mais après N répétitions (configurable)
- **Fenêtre d'exploitation :** Chaque adaptation laisse une nouvelle vulnérabilité (équilibrage obligatoire)
- **Difficulté ML :** 3 niveaux — ML désactivé / apprentissage lent / apprentissage normal
- Tester intensivement avec des playtesteurs : si plus de 60% trouvent l'IA injuste → recalibrer

---

### Risque 3 — Latence Gemma 4 Visible (ÉLEVÉ)

**Description :** L'API Gemma 4 peut avoir une latence de 1-5 secondes. Si le jeu attend ce retour pour agir, le gameplay est cassé.

**Probabilité :** Certaine (API externe = latence imprévisible)

**Mitigation :**
- **Asynchrone obligatoire :** Gemma tourne en thread séparé, le jeu ne bloque jamais
- **Plan de fallback :** Si Gemma ne répond pas dans X ms → l'IA utilise le dernier plan valide
- **Délai intentionnel intégré :** Présenter Gemma comme un "cerveau lent" — l'ennemi "réfléchit" entre les assauts. La latence réelle devient narrative
- **Mode offline :** Comportement scripté de qualité correcte si l'API est inaccessible

---

### Risque 4 — Pathfinding de Masse Non Résolu (ÉLEVÉ)

**Description :** GroveEngine n'a pas de pathfinding de masse. Des centaines d'unités sans pathfinding efficace = ralentissements ou comportements absurdes.

**Probabilité :** Certaine (à implémenter from scratch)

**Mitigation :**
- Limiter le nombre d'unités **actives simultanément** (les réserves ne calculent pas de chemin)
- Flow fields pour les grandes formations (coût O(N) vs O(N*M) pour A* individuel)
- Pathfinding par squad (une seule entité logique) pas par unité individuelle
- Prototyper le pathfinding en priorité 1 avant tout autre système — c'est le fondement

---

### Risque 5 — Économie Sans Tension (MOYEN)

**Description :** Si l'économie tourne bien automatiquement et que l'ennemi ne la cible pas efficacement, elle devient invisible et sans intérêt.

**Probabilité :** Moyenne (dépend du calibrage de l'IA ennemie)

**Mitigation :**
- S'assurer que Gemma identifie et cible systématiquement les nœuds logistiques critiques
- Placer des goulots d'étranglement dès la conception de la carte (le Pont de Velkar, etc.)
- Le mode Auto doit générer des alertes de crise régulières pour maintenir l'attention
- Test de stress : jouer une session complète en mode Auto et mesurer combien de fois l'économie crée de la tension

---

### Risque 6 — Lisibilité Isométrique (MOYEN)

**Description :** La vue isométrique 2.5D avec des bâtiments endommagés, des unités de deux camps, des overlays et des effets de particules peut devenir illisible.

**Probabilité :** Moyenne

**Mitigation :**
- Palette couleur stricte : défenseur = bleu/gris, ennemi = rouge/noir, neutre = ocre
- Icônes d'état toujours au-dessus des unités (jamais cachées par les bâtiments)
- Test de lisibilité à toutes les phases de dégradation de bâtiments
- Option daltonien + options de taille d'UI
- Overlay de territoire toujours actif par défaut (pas en mode caché)

---

### Risque 7 — Scope Trop Large pour un Prototype (MOYEN)

**Description :** La totalité du GDD représente 12-18 mois de développement minimum. Le risque est de ne jamais sortir de prototype.

**Probabilité :** Élevée si pas de MVP strict

**Mitigation — MVP vertical slice (3-4 mois) :**
- 1 carte fixe, 3 districts
- 1 brigade joueur (2 régiments, 4 compagnies, 12 squads)
- Économie simplifiée : 2 ressources (ferraille + munitions), 1 route logistique
- IA ennemie : Gemma + ML de base, 1 cycle d'assaut
- Pas de permadeath commandants, pas de moral en V1
- **Objectif MVP :** Valider que le loop "commande → combat → économie → IA adapte" est fun en 30 minutes de jeu

---

### 10.1 Synthèse des Risques

| Risque | Criticité | Action immédiate |
|---|---|---|
| Complexité écrasante | CRITIQUE | Designer le tutoriel avant les features |
| IA trop forte | ÉLEVÉ | Calibrer le ML avec une courbe d'apprentissage lente |
| Latence Gemma | ÉLEVÉ | Architecture async + fallback en priorité 1 |
| Pathfinding masse | ÉLEVÉ | Flow fields avant tout autre système |
| Économie sans tension | MOYEN | Gemma doit cibler la logistique systématiquement |
| Lisibilité isométrique | MOYEN | Tests visuels dès les premiers sprites |
| Scope trop large | MOYEN | MVP strict 3-4 mois défini ci-dessus |

---

## ANNEXES

### A. Glossaire

| Terme | Définition |
|---|---|
| **Squad** | Unité de base (4-12 soldats), IA autonome locale |
| **Compagnie** | 3-5 squads, commandant IA overridable |
| **Régiment** | 3-5 compagnies, commandant IA overridable |
| **Brigade** | 2-4 régiments, contrôle direct joueur |
| **Supply depot** | Dépôt de ravitaillement, nœud logistique |
| **Flow field** | Technique de pathfinding de masse par champ vectoriel |
| **Gemma 4** | LLM (Large Language Model) utilisé pour la planification stratégique ennemie |
| **ML** | Machine Learning — couche tactique adaptative de l'IA ennemie |
| **Fractalis** | Nom de l'ennemi dans l'univers de Last Cadia |
| **District** | Unité territoriale de la carte, avec indice de contrôle |
| **Front rompu** | État critique où l'ennemi a percé la ligne de contact |

### B. Décisions de Design Ouvertes

Ces points nécessitent une décision avant l'implémentation :

1. **Fréquence Gemma :** 30s de jeu = combien de secondes réelles ? (dépend de la vitesse du jeu)
2. **Nombre d'unités max simultanées :** À déterminer par les tests de performance pathfinding
3. **Vitesse du jeu :** Temps réel strict / pause active / vitesse variable ? (impact sur tout le design)
4. **Persistance entre missions :** Chaque mission est-elle indépendante ou y a-t-il une campagne avec progression ?
5. **Multijoueur :** Hors scope V1, mais l'architecture pub/sub devrait le permettre à terme

### C. Références Techniques

- GroveEngine DEVELOPER_GUIDE.md — architecture des modules
- GroveEngine UI_ARCHITECTURE.md — système d'interface
- GroveEngine UI_TOPICS.md — arbre de topics existant
- MeguraColonyML — référence pour l'architecture ML comportementale

---

*GDD Last Cadia v0.1 — Rédigé pour GroveEngine — Document vivant*
*Dernière mise à jour : 2025*

