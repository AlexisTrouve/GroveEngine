# Handoff — sessions des 28 & 29 juillet 2026

> ➡️ **Suite : [l'éclairage 2D, du socle aux nébuleuses (29-30 juillet)](session-handoff-2026-07-30-lumiere.md)**
> — les plans F et A livrés, le crénelage des ombres corrigé, le budget de lampes enfin mesuré.
> Les trois chantiers cadrés ci-dessous y sont tous clos.

> **État à la sortie** : `master` = **`15a30ef`**, arbre propre, suite **194/194**.
> ⚠️ **6 commits NON POUSSÉS** (`914f9ef` → `15a30ef`). Le reste est sur gitea + github.
> Point de départ : `af4a226`. 28 commits.
>
> Session précédente : [handoff 27-28 juillet](session-handoff-2026-07.md).

Ce document dit **où on en est, ce qui est clos, ce qui ne l'est pas, et ce que la session a appris**.
Chaque chantier a son propre doc, référencé.

---

## 1. Le gros morceau : l'éclairage 2D, de rien à des murs qui projettent des ombres

Sept tranches, chacune prouvée au pixel et vérifiée en cassant le code.

| Tranche | Ce qu'elle livre | Doc |
|---|---|---|
| **L1** | cibles hors-écran + composite, **ambiant seul** | [lighting-2d.md](lighting-2d.md) |
| **L2** | lampes radiales, atténuation `(1−d/r)²` | idem |
| **L3** | lampes **coniques** (`dirDeg`/`spreadDeg`) | idem |
| **C1** | math polaire pure + accumulation de transmittance | [socle](lighting-transmittance-core.md) |
| **C2** | **marche d'occultation** dans le shader de lampe | idem |
| **W1-W3** | **murs** : `render:occluder` éphémère + retenu | [plan W](lighting-walls.md) |

**Mesures finales** : ambiant 1.0 → 255 / 0.5 → 128 · lampe sous caméra déplacée → centre 255 /
coins 48 · cône → devant 137 / derrière 48 · mur → ombre 48 / éclairé 120.

**Le contournement à coût nul tient** : sans `render:ambient`, aucune cible n'est créée, aucune vue
redirigée, et la sortie est identique à l'octet près. Drifterra, DAOS et Fractax ne paient rien.

### La décision de conception qui a tout structuré

Les trois demandes suivantes — murs, filtres, atténuateurs — se ramènent à **une seule question** :
*combien de lumière, et de quelle couleur, survit du point A au point B ?* Ce n'est pas trois
techniques, c'est une transmittance multiplicative accumulée le long du rayon, avec trois façons de
la renseigner. **Le mur est le cas dégénéré.**

Conséquence directe : le socle est un document **central**, et les trois plans s'y accrochent sans le
dupliquer. Livrer les murs sur un shadow map 1D (qui ne stocke qu'une *distance*, donc binaire par
construction) aurait obligé à tout jeter aux filtres.

## 2. Le reste

- **`IIO::unsubscribe` + `ScopedSubscription`** — [doc](iio-unsubscribe.md). `subscribe` était une
  porte à sens unique : un objet plus court que son IIO laissait un handler orphelin sur de la
  mémoire libérée, **et ne pouvait rien y faire**. Diagnostiqué depuis le terrain (Drifterra, six
  rounds de chasse sur un bug qui n'était pas le nôtre).
- **`DumpDetail::Full`** — le tas dans le minidump, opt-in, défaut inchangé. 58 Ko → 35 Mo.
- **`UITabs::tabFrame`** — la passe UI 9-slice est complète, P0 → P5.
- **Blend additif sur `render:sprite`** — un quad étiré qui *glow*, impossible auparavant.
  Preuve mesurée : intersection 255 contre 54 et 28.
- **`blog/` + `IMAGES.md`** — huit captures réelles, manifest à jour.

---

## 3. Ce qui reste ouvert

### Chantiers cadrés, plans écrits, rien d'implémenté
1. **[Plan F — filtres colorés](lighting-filters.md)** : un occulteur qui teinte au lieu de bloquer.
   Le socle le porte déjà — un filtre écrit une **couleur** au lieu de noir.
2. **[Plan A — les milieux](lighting-attenuators.md)** : ⚠️ **réécrit** le 29/07. La diffusion est
   devenue obligatoire (nébuleuses : un milieu doit se **voir**, pas seulement assombrir). Le seul
   changement d'architecture : le terme diffusé est **additif au final**, jamais multiplicatif avec
   la scène — sinon aucun faisceau n'est visible dans le vide.
3. **C3 — la table polaire** : optimisation de la marche, à écrire **quand une mesure** montrera que
   la marche coûte trop cher.

### Arbitrages tranchés, révocables
- **Carte d'occultation partagée en espace écran** (tranché seul, faute de réponse). Un mur qui sort
  du cadre cesse d'occulter. Visible à l'écran ⇒ renversable.
- **RGBA16F** pour les cibles (tranché par Alexi) — le bloom se nourrit de ce qui dépasse 1.0.

### Dettes
- **Le budget « des dizaines de lampes » est périmé.** La référence du shadow map 1D mesure le
  décrochage vers **20**, et c'était sans transmittance. À remesurer, et à corriger dans la doc
  consommateur.
- **Particules-lumières** ([§6bis](lighting-2d.md)) : ma règle « pas de chemin bulk » a une
  précondition écrite pour des **lampes**. Une lumière par particule en sort.
- **`GROVE_MEM_TRACKING=ON`** fait tomber 7 tests, non attribués.
- **SDL2_mixer absent** ⇒ le module vidéo n'entre pas dans le build.
- **Hook 2 (défilement d'UV)** : ✅ **probablement inutile** — vérifié que `wrap=Repeat` est le
  défaut et que ni le VS ni le FS ne bornent les UV. Un jeu peut faire défiler dès aujourd'hui en
  animant `u0/u1`. Seul manque réel : les textures **atlasées** ne peuvent pas défiler.

---

## 4. Ce que ces deux jours ont appris

### « Sortie inchangée » est aussi ce que donne du code mort
La preuve de C2 était que la marche d'occultation **ne change rien**. C'est exactement ce qu'aurait
donné une marche qui ne s'exécute pas. Il a fallu passer la carte en **noir** pour prouver qu'elle
tournait — le centre est tombé de 255 à 48.

**Réflexe** : quand la preuve d'une tranche est une *neutralité*, il faut un second sabotage qui
prouve l'*activité*. Les deux se ressemblent et une seule ne suffit pas.

### Un discriminant placé là où les deux hypothèses coïncident ne discrimine rien
Trois fois le même piège, évité trois fois **parce qu'il était écrit dans le plan avant le test** :

| Test | Le piège | Le remède |
|---|---|---|
| L2 | caméra par défaut ⇒ transformation quasi-identité | caméra **déplacée et zoomée** |
| L3 | comparer près/loin ⇒ ne teste que l'atténuation | sondes à **distance égale** |
| W2 | idem | sondes à **distance égale**, de part et d'autre |

Et le plan A porte déjà le suivant : A2 exigera un **fond noir**, sinon `scene × lumière` est déjà
non nul et le terme diffusé n'expliquerait rien.

### Un test vert en isolation et rouge en suite n'est pas du bruit de suite
W2 passait seul, les quatre échouaient ensemble. Cause : **une vue sans draw est sautée par bgfx, et
une vue sautée n'exécute jamais son clear**. Il fallait une frame *sans* occulteur après une frame
qui en avait — c'est-à-dire **exactement** ce que seule la suite produisait.

Corrigé à la racine (sans occulteur, la carte n'est pas consultée du tout) plutôt qu'en aménageant la
dépendance à la sémantique clear-on-touch.

### Corriger son propre plan coûte moins cher que le défendre
Deux fois :
- **le socle** posait la table polaire comme structure de base ; en l'attaquant, j'ai vu qu'elle
  évaluait le coût de *construire la table* sans le comparer à l'alternative qui n'en construit
  aucune. La table est devenue une **optimisation** (§4bis) ;
- **le plan A** ne couvrait que l'absorption ; « nébuleuses » l'a rendu faux, réécrit entièrement.

### Ne pas chiffrer un chantier avant d'avoir lu le code
J'ai devisé le hook 2 (défilement d'UV) comme une tranche entière — instance à agrandir, shader neuf,
quatre backends. Puis j'ai lu le shader : `wrap=Repeat` par défaut, aucun clamp nulle part. **La
capacité existait déjà.** Le devis était juste, la question ne l'était pas.

### Deux nuisances d'outillage qui ont coûté cher
- **Ne jamais bâtir pendant que la suite tourne** — `ProductionHotReload` lance son propre `ninja`.
  Deux heures perdues sur trois symptômes (timeout, `.ninja_deps` corrompue, `libUIModule.dll`
  tronquée → `LoadLibrary error 193`). Consigné dans [known-annoyances.md](known-annoyances.md).
- **`sed -i` sur un glob large réécrit TOUS les fichiers**, même sans correspondance — 70 fichiers
  touchés au lieu de 15, et le diff pollué par des fins de ligne. Trier par contenu réel
  (`git diff --ignore-cr-at-eol`) avant de committer.

### Un piège de shader qui aurait survécu à toute la campagne de tests
`smoothstep(e, e, x)` avec des bornes **égales** est indéfini en GLSL. Vu **à la relecture**, pas au
test : « indéfini » ici voudrait dire correct sur ce pilote et faux sur le suivant. Même famille que
le flip vertical conditionnel, qui ne se voit que sur une famille de backends.
