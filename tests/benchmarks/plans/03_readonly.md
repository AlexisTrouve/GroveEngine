# Plan: DataNode Read-Only API Benchmarks

## Objectif
Comparer `getChild()` (copie) vs `getChildReadOnly()` (zero-copy).

---

## Benchmark I: getChild() avec copie (baseline)

**Test**: Mesurer coût des copies mémoire.

**Setup**:
- DataNode tree: root → player → stats → health
- Appeler `getChild("player")` 10000 fois
- Mesurer temps total et allocations mémoire

**Mesures**:
- Temps total: X ms
- Allocations: Y allocs (via compteur custom ou valgrind)
- Mémoire allouée: Z KB

**Rôle**: Baseline pour comparaison

---

## Benchmark J: getChildReadOnly() sans copie

**Test**: Speedup avec zero-copy.

**Setup**:
- Même tree que benchmark I
- Appeler `getChildReadOnly("player")` 10000 fois
- Mesurer temps et allocations

**Mesures**:
- Temps total: X ms
- Allocations: 0 (attendu)
- Speedup: temps_I / temps_J

**Succès**:
- Speedup > 2x
- Zero allocations

---

## Benchmark K: Lectures concurrentes

**Test**: Throughput avec multiple threads.

**Setup**:
- DataNode tree partagé (read-only)
- 10 threads, chacun fait 1000 reads avec `getChildReadOnly()`
- Mesurer throughput global et contention

**Mesures**:
- Reads/sec: X reads/s
- Speedup vs single-thread: ratio
- Contention locks (si mesurable)

**Graphe**: Throughput = f(nb threads)

**Succès**: Speedup quasi-linéaire (read-only = pas de locks)

---

## Benchmark L: Navigation profonde

**Test**: Speedup sur tree profond.

**Setup**:
- Tree 10 niveaux: root → l1 → l2 → ... → l10
- Naviguer jusqu'au niveau 10 avec:
  - `getChild()` chaîné (10 copies)
  - `getChildReadOnly()` chaîné (0 copie)
- Répéter 1000 fois

**Mesures**:
| Méthode             | Temps (ms) | Allocations |
|---------------------|------------|-------------|
| getChild() x10      | ?          | ~10 per iter|
| getChildReadOnly()  | ?          | 0           |

**Speedup**: ratio (attendu >5x pour 10 niveaux)

**Succès**: Speedup croît avec profondeur

---

## Implémentation

**Fichier**: `benchmark_readonly.cpp`

**Dépendances**:
- `JsonDataNode` (src/)
- Helpers: Timer, Stats, Reporter
- `<thread>` pour benchmark K

**Structure**:
```cpp
void benchmarkI_getChild_baseline();
void benchmarkJ_getChildReadOnly();
void benchmarkK_concurrent_reads();
void benchmarkL_deep_navigation();

int main() {
    benchmarkI_getChild_baseline();
    benchmarkJ_getChildReadOnly();
    benchmarkK_concurrent_reads();
    benchmarkL_deep_navigation();
}
```

**Référence**:
- `src/JsonDataNode.cpp:30` (getChildReadOnly implementation)
- `tests/integration/test_13_cross_system.cpp` (concurrent reads)

**Note**: Pour mesurer allocations, wrapper `new`/`delete` ou utiliser custom allocator
