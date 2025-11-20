# Plan: IntraIO Batching Benchmarks

## Objectif
Mesurer les gains de performance du batching et son overhead.

---

## Benchmark E: Baseline sans batching

**Test**: Mesurer performance sans batching (high-freq subscriber).

**Setup**:
- 1 subscriber high-freq sur pattern `"test:*"`
- Publier 10000 messages rapidement
- Mesurer temps total, latence moyenne, throughput

**Mesures**:
- Temps total: X ms
- Messages/sec: Y msg/s
- Latence moyenne: Z µs
- Allocations mémoire

**Rôle**: Baseline pour comparer avec batching

---

## Benchmark F: Avec batching

**Test**: Réduction du nombre de messages grâce au batching.

**Setup**:
- 1 subscriber low-freq (`batchInterval=100ms`) sur `"test:*"`
- Publier 10000 messages sur 5 secondes (2000 msg/s)
- Mesurer nombre de batches reçus

**Mesures**:
- Nombre de batches: ~50 (attendu pour 5s @ 100ms interval)
- Réduction: 10000 messages → 50 batches (200x)
- Overhead batching: (temps F - temps E) / temps E
- Latence additionnelle: avg delay avant flush

**Succès**: Réduction > 100x, overhead < 5%

---

## Benchmark G: Overhead du thread de flush

**Test**: CPU usage du `batchFlushLoop`.

**Setup**:
- Créer 0, 10, 100 buffers low-freq actifs
- Mesurer CPU usage du thread (via `/proc/stat` ou `getrusage`)
- Interval: 100ms, durée: 10s

**Mesures**:
| Buffers actifs | CPU usage (%) |
|----------------|---------------|
| 0              | ?             |
| 10             | ?             |
| 100            | ?             |

**Succès**: CPU usage < 5% même avec 100 buffers

---

## Benchmark H: Scalabilité subscribers low-freq

**Test**: Temps de flush global croît linéairement avec nb subs.

**Setup**:
- Créer N subscribers low-freq (100ms interval)
- Tous sur patterns différents
- Publier 1000 messages matchant tous
- Mesurer temps du flush périodique

**Mesures**:
| Subscribers | Temps flush (ms) | Croissance |
|-------------|------------------|------------|
| 1           | ?                | baseline   |
| 10          | ?                | ~10x       |
| 100         | ?                | ~100x      |

**Graphe**: Temps flush = f(N subs) → linéaire

**Succès**: Croissance linéaire (pas quadratique)

---

## Implémentation

**Fichier**: `benchmark_batching.cpp`

**Dépendances**:
- `IntraIOManager` (src/)
- Helpers: Timer, Stats, Reporter

**Structure**:
```cpp
void benchmarkE_baseline();
void benchmarkF_batching();
void benchmarkG_thread_overhead();
void benchmarkH_scalability();

int main() {
    benchmarkE_baseline();
    benchmarkF_batching();
    benchmarkG_thread_overhead();
    benchmarkH_scalability();
}
```

**Référence**: `tests/integration/test_11_io_system.cpp` (scenario 6: batching)

**Note**: Utiliser `std::this_thread::sleep_for()` pour contrôler timing des messages
