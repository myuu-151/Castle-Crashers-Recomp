#include "as/names.h"

#include <algorithm>

#include "as/builtins.h"

namespace as {

Names::Names() {
    // ID 0 is the empty string; the built-ins keep castle.exe's IDs, and new
    // names are numbered after the last of them.
    uint16_t last = 0;
    for (const auto& b : kBuiltinNames) last = std::max(last, b.id);
    strings_.resize(size_t(last) + 1);
    for (const auto& b : kBuiltinNames) {
        strings_[b.id] = b.name;
        ids_.emplace(strings_[b.id], b.id);
    }
    ids_.emplace("", 0);
}

NameId Names::intern(std::string_view s) {
    if (s.empty()) return 0;
    auto it = ids_.find(s);
    if (it != ids_.end()) return it->second;
    NameId id = NameId(strings_.size());
    strings_.emplace_back(s);
    ids_.emplace(strings_.back(), id);
    return id;
}

NameId Names::find(std::string_view s) const {
    auto it = ids_.find(s);
    return it == ids_.end() ? 0 : it->second;
}

const std::string& Names::str(NameId id) const {
    static const std::string empty;
    return id < strings_.size() ? strings_[id] : empty;
}

Names& names() {
    static Names table;
    return table;
}

}  // namespace as
