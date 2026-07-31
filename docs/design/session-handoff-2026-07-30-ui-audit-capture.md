# Handoff — session du 30 juillet 2026 : UI, audit perf, capture headless

> **État à la sortie** : `master` = **`e4b17b7`**, arbre propre, **tout poussé** (gitea + github,
> mêmes SHAs). Aucun force push. Worktree `frame-capture` monté, build complet.
>
> ⚠️ Session **parallèle** : le travail lumière/post-traitement ET le chantier ferme-de-build
> avançaient en même temps. J'ai rebasé **trois fois**. Voir
> [session-handoff-2026-07-30-lumiere.md](session-handoff-2026-07-30-lumiere.md).

Ce document garde ce qui n'est dans aucun doc de chantier : ce que la journée a **appris**, et les
erreurs que j'ai commises en la faisant.

---

## 1. Ce qui a été livré

| Chantier | Commits | Résultat |
|---|---|---|
| Entrées fantômes des widgets | `9ba4404` | 5 widgets corrigés, `IT_067` verrouille **la classe de bug** |
| Audit perf & dette | `3015f8e` → `bc1c55f` | P1 corrigé, P2 mesuré→corrigé→clos, P3 traité, P4 mesuré→**écarté** |
| Interface des widgets | `a8c4188`, `d247e92`, `9e79270` | S1a/S1b/S2 : le module cesse de connaître chaque type |
| Garde-fou de concurrence | `86d5756` | faux positif sur ré-entrance supprimé |
| Capture headless | `feb619c` → `e4b17b7` | C1/C2/C3 + doc : `setCaptureTarget` |

Chiffres qui valent la peine d'être retenus : la poussée `ui:data` identique passe de **15,8 ms à
0,09 ms** ; `registerDefaultWidgets` de **621 lignes à une table de 17** ; `UIContext.cpp` de **20
comparaisons de type à 1**.

---

## 2. La leçon centrale de la journée

> **Un raisonnement de FORME dit où regarder. Il ne dit pas quoi faire.**

Elle s'est vérifiée **quatre fois**, à chaque fois en me coûtant ou en manquant de me coûter cher :

1. **Le backlog mentait.** « Seul `UIRadial` surcharge `releaseRenderEntries` » — ils étaient
   **neuf**. La ligne datait de six semaines et a orienté un choix de chantier.
2. **P2 blâmait la mauvaise chose.** « Trois résolutions au lieu d'une » était visible à l'œil nu ;
   le vrai coût était une **exception** levée par binding textuel. Le refactor que la forme
   suggérait aurait rapporté 0,4 % ; le correctif réel de trois lignes a rapporté 3,2×.
3. **P4 semblait gratuit à corriger** et s'est révélé **sans objet** (0,5 % d'une frame au pire).
4. **La dette HUD de Drifterra était périmée** de six semaines, et son propre document se
   contredisait vingt lignes plus haut.

**Ce qu'on en fait** : re-mesurer une ligne de dette avant de planifier dessus. Un `grep` de dix
secondes. Trois des quatre cas se seraient effondrés à ce prix-là.

---

## 3. Mes erreurs, gardées parce qu'elles se reproduiront

### 3.1 Transcrire fidèlement du code MORT introduit une régression

`dispatchMouseButton` finissait par `return handled ? target : nullptr;`, ce qui rendait
**inatteignables** les six `return target` placés au-dessus. Leurs gardes étaient donc du code mort.
Les transcrire « fidèlement » dans le nouveau prédicat a **réduit** le comportement.

> La référence d'un refactor est le **flot de contrôle complet**, jamais le commentaire d'une branche.

### 3.2 Un test peut passer 204/204 sur une vraie régression

La suite complète est passée **avec** cette régression : le module ne réagit à ces widgets qu'au
relâchement, donc l'écart n'avait aucun effet observable par l'IIO. Un E2E ne pouvait pas boucher ce
trou — il a fallu interroger le **contrat** (`UIWidgetContractUnit`).

### 3.3 Le réglage qui rend un résultat PRÉVISIBLE est souvent celui qui le rend INDISCRIMINANT

Premier test de capture écrit avec un ambiant **blanc**, choisi pour que la couleur reste prévisible.
Or blanc est **neutre par construction** : éclairé et non éclairé rendent le même vert. Le test
passait contre l'implémentation fausse. Il a fallu un ambiant **à moitié**.

### 3.4 Un sabotage qui échoue silencieusement se lit comme une preuve

Mon script de sabotage de S2 a planté sans que je le voie ; le test est passé au vert et j'ai failli
prendre ce vert pour la preuve que le filet mordait. **Vérifier que le sabotage a bien été appliqué**
fait partie de la vérification adverse.

### 3.5 Mesurer avec le mauvais contrat, c'est conclure à l'envers

`render:ambient` prend **un entier `color`**, pas des doubles `{r,g,b}` — et 0 signifie *éteint*. Mon
ambiant « blanc » éteignait l'éclairage. Les trois lignes de ma mesure se ressemblaient, et conclure
là-dessus m'aurait fait bâtir la capture sur les vues 0+1, c'est-à-dire exactement le bug.

---

## 4. Ce que la journée a appris sur les TESTS

- **Réécrire de vrais sites teste l'API mieux qu'un test dédié.** Le défaut de C1 (liaisons jamais
  relâchées → corruption de tas au démontage) n'a été trouvé que par C2. `FrameCaptureGpu` seul
  passait, parce qu'il se démonte immédiatement.
- **Une signature de crash connue n'est pas un diagnostic.** `0xC0000374` est documenté ici comme
  « artefact périmé » ; deux fois c'était vrai, une fois c'était ma régression. Seul un build
  **réellement** stabilisé (`ninja: no work to do` vérifié, pas supposé) permet de trancher.
- **Le sabotage doit couvrir chaque mode d'échec**, pas un seul. La capture a trois façons d'être
  fausse (bonne vue / vue non finale / vue jamais écrite) et deux d'entre elles laissent **le HUD
  correct**.

---

## 5. Ce qui reste ouvert

**L'intermittence de la suite.** Un échec unique **se déplace** d'un run à l'autre — `ChaosMonkey`,
`ErrorRecovery`, `AssetTopicsGpu`, `AssetSpriteGpu`, `MemoryLeakHunter`, `FrameCaptureGpu` selon les
tours. Chacun repasse seul 2 à 3 fois ; plusieurs n'ont **aucune** référence au rendu. Ce n'est donc
pas attribuable à un chantier. **Ça mérite une session dédiée** — instrumenter avec
`tests/helpers/CrashBacktrace.h` et boucler la suite jusqu'à capture — pas une attribution de
complaisance à chaque fois qu'elle tombe.

**Les quatre fonctions longues restantes** de l'audit P3 (`setConfiguration` ×2, `updateUI`,
`SceneCollector::finalize`).

**L'incohérence LFS** : `.gitattributes` déclare `*.png filter=lfs`, mais **10 fixtures de test**
sont stockées en blobs bruts. Tout checkout neuf les affiche « modifiés » en permanence — les
fichiers sur disque sont pourtant intacts (vérifié octet par octet). Un `git status` menteur en
permanence pousse au `git add -A` distrait. Réparable par un `git add --renormalize`.

**Les 6 sites de capture niveau PASSE** (device direct, sans module) : `setCaptureTarget` ne les
couvre pas. À décider s'ils méritent leur propre helper — le plan dit d'attendre un besoin réel.

---

## 6. Coordination — trois worktrees, deux sessions

Le dépôt principal **et** `ui-repeater-p1` ont été repris par la session ferme-de-build, avec du
travail non poussé dans chacun. J'ai travaillé dans `frame-capture`, créé propre depuis
`origin/master`.

> **Règle qui s'en dégage** : avant de coder, vérifier que la copie de travail n'appartient pas à
> quelqu'un d'autre (`git status` + `git log origin/master..HEAD`). Deux fois aujourd'hui la réponse
> était oui.

Et son corollaire, déjà écrit hier mais re-vérifié trois fois : **re-lancer la suite complète après
chaque rebase**. Un vert sur l'ancienne base ne prouve rien.

---

## 7. Recettes

**La suite frôle les 600 s**, ce qui est la limite d'un lancement en tâche de fond — deux runs ont
été tués pour ça. La couper en deux :

```bash
ctest -E "StressTest|MemoryLeakHunter"     # ~400 s
ctest -R "StressTest|MemoryLeakHunter"     # ~200 s
```

**Une ferme de build existe désormais** (`tools/remote-build.sh`, chantier parallèle) : la suite
complète peut partir sur VPS142 au lieu de coûter dix minutes locales. C'est exactement le goulot
subi toute cette session.

**Avant de croire un `0xC0000374`** : reconstruire la cible, et vérifier que `cmake --build` répond
`ninja: no work to do` **deux fois de suite**.
