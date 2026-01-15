# Plan: Benchmark Helpers

## Objectif
Créer des utilitaires réutilisables pour tous les benchmarks.

## Fichiers à créer

### 1. BenchmarkTimer.h
**Rôle**: Mesurer précisément le temps d'exécution.

**Interface clé**:
```cpp
class BenchmarkTimer {
    void start();
    double elapsedMs();
    double elapsedUs();
};
```

**Implémentation**: `std::chrono::high_resolution_clock`

---

### 2. BenchmarkStats.h
**Rôle**: Calculer statistiques sur échantillons (p50, p95, p99, avg, min, max, stddev).

**Interface clé**:
```cpp
class BenchmarkStats {
    void addSample(double value);
    double mean();
    double median();
    double p95();
    double p99();
    double min();
    double max();
    double stddev();
};
```

**Implémentation**:
- Stocker samples dans `std::vector<double>`
- Trier pour percentiles
- Formules stats standards

---

### 3. BenchmarkReporter.h
**Rôle**: Affichage formaté des résultats.

**Interface clé**:
```cpp
class BenchmarkReporter {
    void printHeader(const std::string& name);
    void printResult(const std::string& metric, double value, const std::string& unit);
    void printComparison(const std::string& name1, double val1,
                        const std::string& name2, double val2);
    void printSummary();
};
```

**Output style**:
```
════════════════════════════════════════
BENCHMARK: TopicTree Scalability
════════════════════════════════════════
10 subscribers     :    1.23 µs  (avg)
100 subscribers    :    1.31 µs  (+6.5%)
────────────────────────────────────────
✅ RESULT: O(k) confirmed
════════════════════════════════════════
```

## Validation
- Compiler chaque helper isolément
- Tester avec un mini-benchmark exemple
- Vérifier output formaté correct
