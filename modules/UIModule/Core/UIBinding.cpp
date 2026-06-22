#include "UIBinding.h"
#include <cctype>

namespace grove::uibind {

// ----------------------------------------------------------------------------
// Small helpers
// ----------------------------------------------------------------------------

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

static const Scope* rootOf(const Scope& s) {
    const Scope* c = &s;
    while (c->parent) c = c->parent;
    return c;
}

std::string leafToString(const json& leaf) {
    // A string drops its quotes; a number/bool prints compactly; null/object/array -> "" (objects rarely
    // interpolate). dump() gives nlohmann's shortest round-trippable number form (0.8 -> "0.8").
    if (leaf.is_string())  return leaf.get<std::string>();
    if (leaf.is_boolean()) return leaf.get<bool>() ? "true" : "false";
    if (leaf.is_number())  return leaf.dump();
    return "";
}

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------

bool hasBindings(const std::string& s) {
    return s.find("{{") != std::string::npos;
}

const json* resolvePath(const Scope& scope, const std::string& path) {
    // Pick the target scope from an optional prefix, then strip it off the path.
    const Scope* target = &scope;
    std::string p = path;
    if (p.rfind("$root.", 0) == 0) {
        target = rootOf(scope);
        p = p.substr(6);
    } else if (p.rfind("$parent.", 0) == 0) {
        target = scope.parent ? scope.parent : &scope;
        p = p.substr(8);
    }
    if (!target->data) return nullptr;
    if (p.empty()) return target->data;   // the scope itself

    // Walk the dotted path: object keys + numeric array indices.
    const json* cur = target->data;
    size_t start = 0;
    while (true) {
        size_t dot = p.find('.', start);
        const std::string seg = (dot == std::string::npos) ? p.substr(start) : p.substr(start, dot - start);
        if (seg.empty()) return nullptr;

        if (cur->is_object()) {
            auto it = cur->find(seg);
            if (it == cur->end()) return nullptr;
            cur = &(*it);
        } else if (cur->is_array()) {
            try {
                size_t idx = std::stoul(seg);
                if (idx >= cur->size()) return nullptr;
                cur = &(*cur)[idx];
            } catch (...) { return nullptr; }
        } else {
            return nullptr;   // can't descend into a scalar
        }

        if (dot == std::string::npos) break;
        start = dot + 1;
    }
    return cur;
}

std::string interpolate(const Scope& scope, const std::string& tmpl) {
    if (!hasBindings(tmpl)) return tmpl;   // fast path: no placeholders -> verbatim

    std::string out;
    size_t i = 0;
    while (i < tmpl.size()) {
        const size_t open = tmpl.find("{{", i);
        if (open == std::string::npos) { out += tmpl.substr(i); break; }
        out += tmpl.substr(i, open - i);                 // literal text before the placeholder

        const size_t close = tmpl.find("}}", open + 2);
        if (close == std::string::npos) { out += tmpl.substr(open); break; }   // unclosed -> keep literal

        const std::string path = trim(tmpl.substr(open + 2, close - (open + 2)));
        const json* leaf = resolvePath(scope, path);
        out += leaf ? leafToString(*leaf) : "";          // missing path -> empty substitution
        i = close + 2;
    }
    return out;
}

// Extract the first {{path}} inner from a single-binding template (or "" if none).
static std::string singleBindingPath(const std::string& tmpl) {
    const size_t open = tmpl.find("{{");
    if (open == std::string::npos) return "";
    const size_t close = tmpl.find("}}", open + 2);
    if (close == std::string::npos) return "";
    return trim(tmpl.substr(open + 2, close - (open + 2)));
}

double resolveNumber(const Scope& scope, const std::string& tmpl, double def) {
    if (hasBindings(tmpl)) {
        const json* leaf = resolvePath(scope, singleBindingPath(tmpl));
        if (!leaf) return def;
        if (leaf->is_number())  return leaf->get<double>();
        if (leaf->is_boolean()) return leaf->get<bool>() ? 1.0 : 0.0;
        if (leaf->is_string())  { try { return std::stod(leaf->get<std::string>()); } catch (...) { return def; } }
        return def;
    }
    try { return std::stod(tmpl); } catch (...) { return def; }   // a literal number prop
}

bool resolveBool(const Scope& scope, const std::string& tmpl, bool def) {
    if (hasBindings(tmpl)) {
        const json* leaf = resolvePath(scope, singleBindingPath(tmpl));
        if (!leaf) return def;
        if (leaf->is_boolean()) return leaf->get<bool>();
        if (leaf->is_number())  return leaf->get<double>() != 0.0;
        if (leaf->is_string())  { const std::string v = leaf->get<std::string>(); return v == "true" || v == "1"; }
        return def;
    }
    return tmpl == "true" || tmpl == "1";
}

} // namespace grove::uibind
