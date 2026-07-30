# Plan — rendre les widgets UI autonomes derrière leur interface

> **État** : plan, rien d'implémenté. Écrit le 2026-07-29 à `3015f8e`.
> **Origine** : question d'Alexi — « les widgets devraient pas être interfacés et rendus plus simples
> dans le module ? ». Réponse courte : oui, et la mesure dit précisément où.

---

## 1. Ce qui est vrai aujourd'hui

`UIWidget` n'expose que **8 virtuels** : `update`, `render`, `getType`, `clipsHitTest`,
`hitClipRect`, `releaseRenderEntries`, `applyBoundProp`, le destructeur. Tout est interfacé **pour
l'affichage**, rien pour l'interaction.

Conséquence, comptée :

| Où | Comparaisons `getType() == "…"` | `static_cast<UI…>` |
|---|---|---|
| `UIModule.cpp` | 30 | 30 |
| `UIContext.cpp` (`dispatchMouseButton`) | 9 | 6 |

Plus `UITree::registerDefaultWidgets` : **621 lignes**, seize fabriques JSON en lambdas inline.

**Le fait qui rend le chantier bon marché** : les méthodes d'interaction existent déjà sur les
widgets, avec des signatures **identiques**, mais ne sont pas virtuelles.

```
bool onMouseButton(int, bool, float, float)  → Button, Checkbox, Radial, Slider, TextInput, TextArea
bool onKeyInput(int, uint32_t, bool, bool)   → TextInput, TextArea
void handleMouseWheel(float)                 → List, ScrollPanel
void loseFocus() / gainFocus()               → TextInput, TextArea
```

L'interface a été écrite six fois sans jamais être déclarée. Le travail est une **remontée**, pas une
conception.

### ⚠️ Correction d'une affirmation que j'ai faite trop vite

En répondant à Alexi j'ai dit que « les 30 `if type == … static_cast` s'effondrent en un appel
virtuel ». **C'est faux.** Après dépouillement, les 39 sites se répartissent ainsi :

| Catégorie | Sites | Un virtuel suffit ? |
|---|---|---|
| Routage souris (`dispatchMouseButton`) | 6 | **Oui** |
| Routage clavier | 4 | **Oui** |
| Gestion du focus | 5 | **Oui** |
| Publication d'événements après clic (`btn->onClick`, `checkbox->checked`, `slider->value`…) | 7 | Non — voir §3 |
| Handlers de topics (`ui:list:*`, `ui:drawer:*`, `ui:modal:*`, `ui:radial:*`) | 10 | Non, et ce n'est pas un défaut |
| Fenêtres / divers | 7 | Partiellement |

Donc S1 retire **~15 couplages sur 39** et fait fondre deux `if/else` en cascade. C'est déjà le
meilleur ratio effort/gain du lot, mais ce n'est pas « les 30 ».

---

## 2. La contrainte d'architecture à ne pas violer

**Les widgets n'ont pas accès à l'IIO** — ils ne connaissent que `UIRenderer`. C'est un choix
délibéré (cf. `docs/UI_ARCHITECTURE.md`), pas un oubli : c'est ce qui les rend testables sans bus.

Donc on remonte le **routage**, jamais la **publication**. Un widget ne publiera pas `ui:action` ;
il dira *ce qui s'est passé* et le module publiera. Toute proposition qui donne un `IIO*` à un widget
est hors plan.

---

## 3. Le point de conception réel : dire ce qui s'est passé

Un `bool consumed` ne suffit pas. Aujourd'hui le module relit l'état du widget concret après coup —
`btn->onClick`, `checkbox->checked`, `slider->value` — d'où les casts de la catégorie « publication ».

Deux formulations possibles :

**(a) Le module continue de relire l'état, mais via des accesseurs de base.** Minimal, mais on
finit par pousser dans `UIWidget` des champs qui n'ont de sens que pour un widget — la classe de base
devient un fourre-tout. À éviter.

**(b) Le widget retourne un descripteur d'interaction.** Un petit agrégat neutre (`changed`,
`action`, `boolValue`, `numberValue`, `index`) que le module traduit en topics. Le widget ne connaît
toujours pas l'IIO ; il décrit, l'autre publie.

**Retenu : (b)**, mais **en S3 seulement**. S1 et S2 ne le demandent pas, et le concevoir avant
d'avoir migré le routage, c'est concevoir à l'aveugle.

---

## 4. Découpage

### S1a — routage souris — ✅ FAIT (2026-07-29)

**Livré**, avec une **extension assumée** : le plan ne visait que `dispatchMouseButton`, mais
`hitTest` portait le même commutateur à **onze** branches appelant onze prédicats qui existaient déjà
sous **trois noms pour une seule question** (`containsPoint`, `pointInWindow`, `pointInBounds`).
S'arrêter avant aurait laissé `UIContext.cpp` dépendre des onze en-têtes de widgets — le bénéfice
annoncé (« ajouter un widget ne touche plus ce fichier ») n'aurait pas été atteint. D'où un troisième
virtuel, `absorbsPoint`.

Résultat : `UIContext.cpp` passe de **20 comparaisons de type et 12 casts à 1 de chaque** (le
survivant est `updateHoverState`, qui appelle `onMouseEnter`/`onMouseLeave` — propres au bouton, hors
périmètre). Dix `#include` devenus orphelins retirés. −174 / +141 lignes.

**Nouveau test : `UIWidgetContractUnit`** (`tests/unit/test_ui_widget_contract.cpp`, 33 assertions).
Il interroge les deux prédicats directement — sans fenêtre, sans IIO, sans renderer. Il existe parce
que **la suite complète est passée 204/204 avec la régression décrite ci-dessous** : le module ne
réagit au bouton et à la roue qu'au relâchement, donc l'écart n'avait aucun effet observable par
l'IIO. Un test de bout en bout ne pouvait pas boucher ce trou ; seul le contrat le peut. Vérifié en
réintroduisant la régression : 4 assertions rouges.

---

### S1a — le détail de ce qui a été fait

`dispatchMouseButton` (`UIContext.cpp:181`) fait 9 branches / ~66 lignes dont **six sont
littéralement identiques** :

```cpp
handled = X->onMouseButton(button, pressed, ctx.mouseX, ctx.mouseY);
if (handled) return target;
```

Les trois autres (`tabs`, `modal`, `list`) ne routent rien : elles décident seulement **quand
remonter la cible** au module.

Deux virtuels sur `UIWidget` :

```cpp
// Défaut : le widget ne consomme rien.
virtual bool onMouseButton(int button, bool pressed, float x, float y) { return false; }

// Défaut : on ne remonte au module que si le widget a consommé. tabs/modal remontent au PRESS,
// list remonte toujours, button ne remonte que s'il a de quoi émettre.
virtual bool surfacesClick(bool pressed, bool handled) const { return handled; }
```

La fonction tombe à :

```cpp
const bool handled = target->onMouseButton(button, pressed, ctx.mouseX, ctx.mouseY);
return target->surfacesClick(pressed, handled) ? target : nullptr;
```

Chaque cas particulier devient 1 à 3 lignes **dans le fichier du widget concerné** :
`UITabs`/`UIModal` → `return pressed;` · `UIList` → `return true;` · `UIRadial` →
`return handled && !pressed;` · `UIButton` → `handled && !pressed && (a de quoi émettre)`.

**Gate** : la soixantaine de tests E2E UI cliquent réellement sur ces widgets (IT_020, IT_033,
IT_044→IT_051, IT_062→IT_067…). Le filet est déjà tendu, aucun test à écrire pour S1a. **Preuve
d'inertie exigée** : suite complète verte avant/après, et au moins un test cassé volontairement (un
`surfacesClick` rendu faux) pour vérifier que le filet mord vraiment sur ce chemin.

#### ⚠️ Le piège rencontré en faisant S1a : les six `return target` étaient MORTS

La fonction d'origine se terminait par `return handled ? target : nullptr;`. **Cette ligne rend
inatteignables les six `return target` anticipés** : dès que `handled` est vrai, la sortie de secours
renvoie déjà `target`. Les conditions qui les gardaient — `handled && !pressed && (!onClick.empty()
|| eventBindings.count("click") …)` pour le bouton, `handled && !pressed` pour la roue — ne
changeaient donc **jamais** le résultat.

Traduire ces conditions fidèlement dans `surfacesClick`, ce que j'ai fait d'abord, **introduit une
régression** : le bouton et la roue cessaient d'être remontés au press, alors qu'ils l'étaient.

La sémantique réelle de la fonction, une fois le code mort retiré :

```
remonté = handled  ||  (tabs|modal ET press)  ||  (list)
```

Donc `UIButton` et `UIRadial` n'ont **aucune** surcharge à écrire — le défaut `return handled` EST
leur comportement historique. Seuls `UITabs`/`UIModal` (`handled || pressed`) et `UIList` (`true`)
en ont une.

> **La leçon, réutilisable** : sur ce genre de refonte, la référence est le **flot de contrôle
> complet**, jamais le commentaire d'une branche ni son intention affichée. Un commentaire décrit ce
> que l'auteur voulait ; une sortie de secours en fin de fonction décrit ce qui se passe. Ici les
> deux divergeaient depuis longtemps sans que rien ne le signale.

### S1b — routage clavier + focus — ✅ FAIT (2026-07-29)

Trois virtuels de plus sur `UIWidget` (`handleMouseWheel`, `acceptsFocus`, `gainFocus`/`loseFocus`)
et **une classe intermédiaire**, `UITextEditWidget`.

**Pourquoi une classe intermédiaire plutôt que des virtuels sur la base** : le routage clavier
s'appuie sur `selectedText`, `deleteSelection`, `insertFilteredText`, `onKeyInput`, `text()`,
`onSubmit`. Poser ça sur `UIWidget` violerait la règle que ce plan s'est donnée (§5) — `selectedText()`
ne veut rien dire pour un libellé. La classe intermédiaire la respecte : le module caste **une** fois
vers un contrat au lieu de deux fois vers des types concrets, et les deux blocs clavier d'environ 90
lignes n'en font plus qu'un. Le collage (`input:clipboard:text`) tombe sur le même contrat.

**Deux pièges rencontrés, tous deux du même genre que celui de S1a :**

1. **Les deux flux clavier divergent réellement**, ce que les commentaires ne disaient pas. Le champ
   monoligne soumet **après** la frappe et seulement si le widget l'a traitée (il publie donc
   `ui:text_changed` *puis* `ui:text_submit`). La zone multiligne soumet **avant** et **avale** la
   touche — sinon Ctrl+Entrée insérerait un saut de ligne en plus de soumettre. Ma première fusion
   n'était fidèle à aucun des deux. D'où un second prédicat, `swallowsSubmitKey()`, qui rend la
   divergence explicite au lieu de la laisser dépendre de l'ordre des lignes dans le module.

2. **Une asymétrie de l'ancien code, corrigée au passage.** La branche `textinput` ne retirait le
   focus au précédent QUE s'il était lui-même un `textinput` — elle ignorait le cas `textarea`, que
   la branche jumelle traitait pourtant. Cliquer d'une zone multiligne vers un champ laissait donc la
   zone focalisée en interne : deux curseurs clignotants à l'écran, alors que les touches ne partaient
   qu'à un seul widget. Unifier le corps supprime l'asymétrie — c'était un défaut **de la duplication
   elle-même**.

   ⚠️ **Non verrouillé par un test, et je le dis plutôt que de le maquiller** : l'écart n'est pas
   observable depuis l'IIO (`ui:focus_lost` était publié dans les deux cas ; seul l'état interne du
   widget différait, visible uniquement au rendu du curseur). C'est précisément pourquoi il a survécu
   si longtemps.

**Bilan chiffré** (`UIModule.cpp`) : comparaisons de type **30 → 21**, casts vers un type concret
**30 → 21**. Le reste est le hors-périmètre assumé ci-dessous (handlers de topics) plus les fenêtres.

---

### S1b — le détail de ce qui était prévu

`onKeyInput` et `handleMouseWheel` deviennent virtuels avec défaut neutre ; `gainFocus`/`loseFocus`
aussi, plus un `virtual bool acceptsFocus() const { return false; }` qui remplace le
`type == "textinput" || type == "textarea"` disséminé.

Bénéfice concret : la logique « le focus passe de l'un à l'autre » cesse d'énumérer les deux types de
saisie — un futur widget focusable (liste éditable, console) marchera sans toucher au module.

**Gate** : IT_062→IT_066 (saisie, sélection, presse-papiers, multiligne) couvrent déjà ces chemins.

### S2 — sortir les fabriques — ✅ FAIT (2026-07-30)

Les **dix-sept** fabriques (pas seize — le plan comptait mal) sont devenues des `UIX::fromNode`
statiques dans le fichier de leur widget. `UITree::registerDefaultWidgets` passe de **621 lignes à une
table de 17 lignes**, et `UITree.cpp` de **873 à 325 lignes**.

Les commentaires explicatifs qui décrivaient une fabrique (flipbook, radial, tabs, modal, list,
window, textarea) sont partis **avec le code qu'ils décrivent** — les laisser dans la table aurait
raté le but du chantier. Au passage, un commentaire mal placé de longue date a été corrigé :
`// Register textinput factory` coiffait le bloc *textarea*.

Déplacement pur, aucun changement de comportement — et c'est vérifié plutôt que supposé : les corps
sont ceux d'origine, et une fabrique sabotée (le champ `action` du radial vidé) fait bien tomber
`IT_020`. Sans cette vérification, « 206/206 » ne prouverait pas que le filet couvre le code déplacé.

Reste hors périmètre, inchangé : `parseCommonProperties` et `parseWidgetBindings` restent chez
`UITree` — ils sont communs à tous les widgets, c'est leur place.

---

### S2 — ce qui était prévu

Chaque widget expose `static std::unique_ptr<UIWidget> fromNode(const IDataNode&)` **dans son propre
fichier** ; `registerDefaultWidgets` devient une table de seize lignes. Purement mécanique, se fait
un widget à la fois, chaque étape compilant et testant.

Après S1+S2 : **ajouter un widget = écrire un fichier**, contre trois endroits centraux aujourd'hui.
C'est ce qui rend un widget maison viable côté Drifterra / DAOS.

### S3 — descripteur d'interaction (optionnel)

Voir §3(b). À décider seulement une fois S1 fait.

### Hors périmètre, assumé

Les **handlers de topics** (`ui:list:set_items`, `ui:drawer:*`, `ui:modal:*`, `ui:radial:set_items`)
restent des casts vers le type concret. Ce sont des API spécifiques appelées depuis l'extérieur ; les
cacher derrière un virtuel générique déplacerait le couplage sans le réduire. Dette **assumée**, pas
oubliée.

---

## 5. Risques

- **Cœur du routage d'entrée.** Une régression y est visible partout. Mitigation : les tranches sont
  petites, la suite complète tourne entre chacune, et le filet E2E existe déjà.
- **Un défaut neutre masque un oubli.** `onMouseButton` par défaut à `false` fait qu'un widget dont
  on oublie le `override` cesse silencieusement de réagir. Mitigation : les E2E cliquent sur chaque
  type — un oubli tombe rouge. À surveiller quand même sur les widgets peu testés.
- **`master` bouge** (session lumière en parallèle). Travailler sur ce worktree, rebaser, **re-lancer
  la suite complète après chaque rebase** — un vert sur l'ancienne base ne prouve rien.
- **Tentation du fourre-tout.** Chaque virtuel ajouté à `UIWidget` doit avoir un sens pour *tout*
  widget. Si la réponse est « ça ne veut rien dire pour un label », c'est que ça n'a rien à faire sur
  la base.

---

## 6. Ordre recommandé

1. **S1a** — six branches identiques effacées, filet déjà en place. Le meilleur ratio du lot.
2. **S1b** — enchaîne naturellement, même mécanique.
3. **S2** — mécanique, incrémental, réduit les collisions de rebase entre sessions.
4. **S3** — seulement si S1 le rend évident.

Ne pas commencer par S2 : sortir seize fabriques d'un fichier qu'on va de toute façon rouvrir en S1
ferait le travail de fusion deux fois.
