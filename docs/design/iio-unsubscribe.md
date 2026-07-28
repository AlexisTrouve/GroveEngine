# `IIO::unsubscribe` — fermer la porte à sens unique

> **Statut** : ✅ livré le 2026-07-28.
> **Origine** : diagnostiqué depuis le terrain par Drifterra, au terme d'une chasse de six rounds sur
> un segfault intermittent (~30 % des runs) qu'on a d'abord cru moteur. Il ne l'était pas — mais
> l'absence d'`unsubscribe` **empêchait le consommateur de se protéger**.

## 1. Le défaut

`subscribe()` était une **porte à sens unique** : on entrait, on ne sortait jamais.

Conséquence pour tout objet dont la durée de vie est plus courte que l'instance `IIO` à laquelle il
s'abonne — une scène, un écran, un widget :

```cpp
Scene::onEnter() {
    io.subscribe("game:state", [this](const Message& m) { onState(m); });  // capture `this`
}
// ~Scene()  → l'objet meurt. Le handler, lui, RESTE posé sur l'IIO.
```

Au dispatch suivant sur ce topic, le transport appelle un handler qui déréférence un `this` mort.
**Use-after-free silencieux en release.** Et le consommateur ne pouvait rien y faire : aucune API ne
permettait de retirer la souscription.

Ce n'est pas un bug d'un projet isolé, c'est un **piège structurel pour tout design scene-driven** sur
un IIO partagé. Le cas réel : la scène était détruite, le host faisait un pompage de plus, un module
republiait sur le topic, et le handler orphelin recevait le message.

## 2. Pourquoi le handle RAII n'est PAS ce que `subscribe` retourne

C'était la forme proposée, et elle est **irréalisable telle quelle**.

Tous les appelants existants ignorent la valeur de retour de `subscribe()`. Si celle-ci était un
handle qui se désabonne à la destruction, le temporaire mourrait **à la fin de l'instruction** et
retirerait la souscription immédiatement. Chaque `subscribe()` du moteur serait devenu un no-op — une
casse totale, et silencieuse.

D'où la séparation :

| Couche | Rôle |
|---|---|
| `SubscriptionId subscribe(...)` | rend un **token ignorable**. Les appelants existants ne changent pas d'une ligne. |
| `bool unsubscribe(SubscriptionId)` | la primitive. |
| `ScopedSubscription` | handle RAII **opt-in**, construit par-dessus, pour qui veut lier la souscription à une durée de vie. |

Le token est un **compteur monotone jamais réutilisé**, pas un index dans le vecteur : un token périmé
doit être inerte, pas un moyen de retirer la souscription qui occupe désormais la même case.

## 3. Le piège de la route partagée

La route côté `IntraIOManager` est par **(instance, motif)** — elle enregistre « cette instance écoute
P », pas « combien de handlers l'écoutent ».

Donc `unsubscribe` ne détruit la route que si **plus aucune** souscription locale n'utilise ce motif.
Sinon, retirer un handler parmi deux sur le même motif aurait **rendu muet le survivant** : un bug pire
que la fuite qu'on corrige, et silencieux lui aussi.

C'est verrouillé explicitement (`IIOUnsubscribe`, cas 2) : deux abonnés sur un motif, on en retire un,
**l'autre doit continuer à recevoir**.

## 4. Se désabonner depuis un handler

Autorisé, et c'est le cas courant (écouteur one-shot, objet qui se démonte depuis son propre callback).

Ça marche parce que `pullAndDispatch` était **déjà** en deux phases : la phase 1 copie sous le verrou
les `(handler, message)` à exécuter, la phase 2 les invoque hors du verrou. Muter la liste des
souscriptions pendant le dispatch ne peut donc pas invalider l'appel en vol. La propriété existait
pour une autre raison (éviter l'ABBA avec `managerMutex`) ; elle se trouve être exactement ce qu'il
fallait ici.

## 5. Discipline de verrou

Identique à `subscribe()` : muter l'état local sous `operationMutex`, **relâcher**, puis appeler
`IntraIOManager` (qui prend `managerMutex`). L'ordre inverse est l'ABBA documenté en tête de
`publish()`.

## 6. Ce que ça verrouille — `tests/regression/test_iio_unsubscribe.cpp`

Le handler qui pend ne s'assert pas directement : lire de la mémoire libérée est un comportement
indéfini, pas une valeur testable. Ce qui est testable, et ce qui referme le trou, c'est que le
handler **cesse d'être appelé**. Chaque cas prouve donc qu'il tire d'abord, puis qu'il ne tire plus —
l'avant/après *est* l'assertion, si bien qu'un `unsubscribe` no-op ne peut pas passer.

1. retrait → le handler ne tire plus ; retirer deux fois, ou un token inconnu, rend `false` ;
2. deux abonnés sur un motif, un retiré → **le survivant reçoit toujours** ;
3. un handler se retire **lui-même** en cours de dispatch ;
4. `ScopedSubscription` membre d'un objet → la destruction de l'objet retire la souscription (le cas
   qui a motivé l'API) ;
5. déplacement du handle → libération **exactement une fois** (un handle déplacé-depuis qui se
   désabonnerait encore tuerait la souscription du nouveau propriétaire).

**Vérifié en cassant** : `unsubscribe` réduit à `return true;` fait tomber 4 cas sur 5, tous sur
`2 == 1` — la signature exacte du « le handler a refiré après le retrait ».

## 7. Limite assumée

`ScopedSubscription` ne doit pas **survivre** à l'`IIO` qu'il vise (il se désabonnerait à travers un
pointeur mort). C'est le cas normal — une instance `IIO` appartient à l'`IntraIOManager` et survit aux
objets qui s'y abonnent — mais un handle détenu par quelque chose de plus longévif que le transport
doit être `reset()` avant que le transport parte.

On n'a délibérément **pas** mis de `weak_ptr` ni de compteur de références pour couvrir ce cas : ça
alourdirait chaque souscription pour un renversement de durée de vie qui, s'il se produit, signale un
problème d'architecture chez l'appelant plutôt qu'un manque du transport.

## 8. Où c'est

- `include/grove/IIO.h` — `SubscriptionId`, les signatures, `ScopedSubscription`.
- `include/grove/IntraIO.h` — `Subscription::id`, `subscriptionSeq_`.
- `src/IntraIO.cpp` — `unsubscribe()` + les deux `subscribe*` qui rendent le token.
- `tests/regression/test_iio_unsubscribe.cpp` — `IIOUnsubscribe`.
