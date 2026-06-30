#pragma once

/**
 * grove::mapview::Filter — a minimal composable predicate over a field value (S1c, the "filtre" brick of §5).
 *
 * WHAT  : A small predicate tree: leaf comparisons (>, >=, <, <=, ==, !=) against a threshold, composed with
 *         AND / OR / NOT, plus Always (no-op). eval(value) -> bool decides whether a cell is drawn by a layer.
 *
 * WHY   : The modular "show only where…" the user asked for (e.g. land = elevation > seaLevel). Deliberately
 *         a MINIMAL predicate set, not a Turing-complete DSL (mapview.md §5) — premature generality avoided.
 *         A tree of plain data is also trivially serializable into the manifest later.
 *
 * HOW   : Header-only, std-only. eval is over a SINGLE value (the layer's bound field); cross-field predicates
 *         ("elevation > 0 AND biome == x") are a documented future extension (eval would take a field lookup).
 *         Float compares use a small epsilon for Eq/Ne so a decoded value matches its intended threshold.
 */

#include <cmath>
#include <utility>
#include <vector>

namespace grove {
namespace mapview {

class Filter {
public:
    enum class Op { Always, Gt, Ge, Lt, Le, Eq, Ne, And, Or, Not };

    static Filter always() { return Filter{Op::Always, 0.0, {}}; }
    static Filter cmp(Op op, double threshold) { return Filter{op, threshold, {}}; }
    static Filter all(std::vector<Filter> children) { return Filter{Op::And, 0.0, std::move(children)}; }
    static Filter any(std::vector<Filter> children) { return Filter{Op::Or, 0.0, std::move(children)}; }
    static Filter negate(Filter child) { return Filter{Op::Not, 0.0, {std::move(child)}}; }

    // Does `value` pass this predicate?
    bool eval(double value) const {
        switch (op_) {
            case Op::Always: return true;
            case Op::Gt:     return value >  threshold_;
            case Op::Ge:     return value >= threshold_;
            case Op::Lt:     return value <  threshold_;
            case Op::Le:     return value <= threshold_;
            case Op::Eq:     return std::fabs(value - threshold_) <= kEps;
            case Op::Ne:     return std::fabs(value - threshold_) >  kEps;
            case Op::And:
                for (const auto& c : children_) if (!c.eval(value)) return false;
                return true;
            case Op::Or:
                for (const auto& c : children_) if (c.eval(value)) return true;
                return false;
            case Op::Not:
                return children_.empty() ? true : !children_.front().eval(value);
        }
        return true;
    }

    Op op() const { return op_; }

private:
    Filter(Op op, double threshold, std::vector<Filter> children)
        : op_(op), threshold_(threshold), children_(std::move(children)) {}

    static constexpr double kEps = 1e-9;
    Op op_;
    double threshold_;
    std::vector<Filter> children_;
};

} // namespace mapview
} // namespace grove
