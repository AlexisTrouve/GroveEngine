#pragma once

/**
 * grove::mapview::Filter — a minimal composable, optionally cross-field predicate (S1c + S1g, §5).
 *
 * WHAT  : A small predicate tree: leaf comparisons (>, >=, <, <=, ==, !=) against a threshold, composed with
 *         AND / OR / NOT, plus Always. A leaf compares EITHER the layer's primary field (the value passed to
 *         eval) OR a NAMED field resolved through a sampler — so cross-field rules like "elevation > 0 AND
 *         biome == 2" are expressible. eval -> bool decides whether a cell is drawn by a layer.
 *
 * WHY   : The modular "show only where…" the user asked for. Deliberately a MINIMAL predicate set, not a
 *         Turing-complete DSL (mapview.md §5). Cross-field support (S1g) keeps it a plain data tree (trivially
 *         serializable) — a leaf just carries an optional field name. A named field that is ABSENT at a cell
 *         makes its leaf fail (fail-franc: the condition can't be evaluated, so the cell is excluded — never
 *         silently treated as some default).
 *
 * HOW   : Header-only, std-only. eval is a TEMPLATE over the sampler callable (no std::function, no per-cell
 *         allocation, fully inlinable in the orchestrator's hot loop). A leaf with an empty field name reads
 *         the primary value; a named leaf calls sample(name, out) which returns false if the field is absent.
 *         The single-argument eval(value) is the convenience for primary-only filters (no named fields).
 */

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace grove {
namespace mapview {

class Filter {
public:
    enum class Op { Always, Gt, Ge, Lt, Le, Eq, Ne, And, Or, Not };

    static Filter always() { return Filter{Op::Always, std::string{}, 0.0, {}}; }
    // Compare the layer's PRIMARY field (the value passed to eval).
    static Filter cmp(Op op, double threshold) { return Filter{op, std::string{}, threshold, {}}; }
    // Compare a NAMED field (resolved via the sampler) — cross-field.
    static Filter cmpField(std::string field, Op op, double threshold) {
        return Filter{op, std::move(field), threshold, {}};
    }
    static Filter all(std::vector<Filter> children) { return Filter{Op::And, std::string{}, 0.0, std::move(children)}; }
    static Filter any(std::vector<Filter> children) { return Filter{Op::Or, std::string{}, 0.0, std::move(children)}; }
    static Filter negate(Filter child) { return Filter{Op::Not, std::string{}, 0.0, {std::move(child)}}; }

    // Full eval: sample(field, out) resolves a named field (returns false if absent at this cell); a leaf
    // with an empty field name uses `primaryValue`.
    template <class Sample>
    bool eval(Sample&& sample, double primaryValue) const {
        switch (op_) {
            case Op::Always:
                return true;
            case Op::Gt: case Op::Ge: case Op::Lt:
            case Op::Le: case Op::Eq: case Op::Ne: {
                double v;
                if (field_.empty()) {
                    v = primaryValue;
                } else if (!sample(field_, v)) {
                    return false;  // named field absent -> fail-franc
                }
                switch (op_) {
                    case Op::Gt: return v >  threshold_;
                    case Op::Ge: return v >= threshold_;
                    case Op::Lt: return v <  threshold_;
                    case Op::Le: return v <= threshold_;
                    case Op::Eq: return std::fabs(v - threshold_) <= kEps;
                    case Op::Ne: return std::fabs(v - threshold_) >  kEps;
                    default:     return true;
                }
            }
            case Op::And:
                for (const auto& c : children_) if (!c.eval(sample, primaryValue)) return false;
                return true;
            case Op::Or:
                for (const auto& c : children_) if (c.eval(sample, primaryValue)) return true;
                return false;
            case Op::Not:
                return children_.empty() ? true : !children_.front().eval(sample, primaryValue);
        }
        return true;
    }

    // Convenience for primary-only filters (no named fields to resolve).
    bool eval(double primaryValue) const {
        return eval([](const std::string&, double&) { return false; }, primaryValue);
    }

    Op op() const { return op_; }

private:
    Filter(Op op, std::string field, double threshold, std::vector<Filter> children)
        : op_(op), field_(std::move(field)), threshold_(threshold), children_(std::move(children)) {}

    static constexpr double kEps = 1e-9;
    Op op_;
    std::string field_;          // empty => the layer's primary field; else a named field
    double threshold_;
    std::vector<Filter> children_;
};

} // namespace mapview
} // namespace grove
