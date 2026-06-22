#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace grove::uibind {

using json = nlohmann::json;

/**
 * @brief A data SCOPE — the foundation shared by data-binding (in) and events (out) for the JSON UI.
 *
 * QUOI : un scope = la donnée de ce niveau (`data`) + un parent optionnel (`parent`). POURQUOI : c'est le
 *   socle commun du moteur de templates — un binding `{{champ}}` (donnée→prop) ET un event `{{champ}}`
 *   (prop→payload) résolvent leurs chemins contre LE MÊME scope du widget. Les repeaters créeront des
 *   scopes ENFANTS (l'item courant) dont le parent est le scope englobant ; le scope racine a parent=null.
 * COMMENT : un chemin `a.b.c` descend dans la donnée json (objets + index d'array `fleet.0.name`). Les
 *   préfixes `$root.` (scope du sommet) et `$parent.` (scope parent) permettent de remonter la chaîne —
 *   sinon un chemin nu résout dans CE scope. Pas de langage d'expression : chemins + interpolation, point.
 */
struct Scope {
    const json* data = nullptr;     // this scope's data (root object, or a repeater item)
    const Scope* parent = nullptr;  // enclosing scope (for $root / $parent); null at the root
};

/// True if `s` contains at least one `{{...}}` placeholder (so a parser knows the prop is bound).
bool hasBindings(const std::string& s);

/// Resolve a dotted data path against a scope (honours the `$root.`/`$parent.` prefixes). Returns the
/// json leaf, or nullptr if any segment is absent / can't be descended.
const json* resolvePath(const Scope& scope, const std::string& path);

/// Interpolate every `{{path}}` in `tmpl` against `scope` (literal text kept; a missing path -> ""). A
/// string with no placeholders is returned unchanged. This is the STRING-prop resolution (e.g. `text`).
std::string interpolate(const Scope& scope, const std::string& tmpl);

/// Typed single-binding resolution for NON-string props (`value`/`width`/`visible`/...). `tmpl` is either
/// exactly `"{{path}}"` or a literal; falls back to `def` when missing/unconvertible.
double resolveNumber(const Scope& scope, const std::string& tmpl, double def = 0.0);
bool   resolveBool(const Scope& scope, const std::string& tmpl, bool def = false);

/// Stringify a json leaf the way interpolation does (string as-is, number compact, bool "true"/"false",
/// null/absent -> ""). Exposed for reuse (e.g. event-arg payloads).
std::string leafToString(const json& leaf);

} // namespace grove::uibind
