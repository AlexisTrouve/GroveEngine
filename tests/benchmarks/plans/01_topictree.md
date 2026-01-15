# Plan: TopicTree Routing Benchmarks

## Objectif
Prouver que le routing est **O(k)** et mesurer le speedup vs approche naïve.

---

## Benchmark A: Scalabilité avec nombre de subscribers

**Test**: Temps de routing constant malgré augmentation du nombre de subs.

**Setup**:
- Topic fixe: `"player:123:damage"` (k=3)
- Créer N subscribers avec patterns variés
- Mesurer `findSubscribers()` pour 10k routes

**Mesures**:
| Subscribers | Temps moyen (µs) | Variation |
|-------------|------------------|-----------|
| 10          | ?                | baseline  |
| 100         | ?                | < 10%     |
| 1000        | ?                | < 10%     |
| 10000       | ?                | < 10%     |

**Succès**: Variation < 10% → O(k) confirmé

---

## Benchmark B: Comparaison TopicTree vs Naïve

**Test**: Speedup par rapport à linear search.

**Setup**:
- Implémenter version naïve: loop sur tous subs, match chacun
- 1000 subscribers
- 10000 routes

**Mesures**:
- TopicTree: temps total
- Naïve: temps total
- Speedup: ratio (attendu >10x)

**Succès**: Speedup > 10x

---

## Benchmark C: Impact de la profondeur (k)

**Test**: Temps croît linéairement avec profondeur du topic.

**Setup**:
- Topics de profondeur variable
- 100 subscribers
- 10000 routes par profondeur

**Mesures**:
| Profondeur k | Topic exemple       | Temps (µs) |
|--------------|---------------------|------------|
| 2            | `a:b`               | ?          |
| 5            | `a:b:c:d:e`         | ?          |
| 10           | `a:b:c:...:j`       | ?          |

**Graphe**: Temps = f(k) → droite linéaire

**Succès**: Croissance linéaire avec k

---

## Benchmark D: Wildcards complexes

**Test**: Performance selon type de wildcard.

**Setup**:
- 100 subscribers
- Patterns variés
- 10000 routes

**Mesures**:
| Pattern         | Exemple   | Temps (µs) |
|-----------------|-----------|------------|
| Exact           | `a:b:c`   | ?          |
| Single wildcard | `a:*:c`   | ?          |
| Multi wildcard  | `a:.*`    | ?          |
| Multiple        | `*:*:*`   | ?          |

**Succès**: Wildcards < 2x overhead vs exact match

---

## Implémentation

**Fichier**: `benchmark_topictree.cpp`

**Dépendances**:
- `topictree::topictree` (external)
- Helpers: Timer, Stats, Reporter

**Structure**:
```cpp
void benchmarkA_scalability();
void benchmarkB_naive_comparison();
void benchmarkC_depth_impact();
void benchmarkD_wildcards();

int main() {
    benchmarkA_scalability();
    benchmarkB_naive_comparison();
    benchmarkC_depth_impact();
    benchmarkD_wildcards();
}
```

**Output attendu**: 4 sections avec headers, tableaux de résultats, verdicts ✅/❌
