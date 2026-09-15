#pragma once

#include "hpvr/hp1_gesture.h"
#include <string_view>

namespace hpvr::quest {

inline bool EqualResourceName(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    const auto fold = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; };
    for (std::size_t i = 0; i < left.size(); ++i)
        if (fold(left[i]) != fold(right[i])) return false;
    return true;
}

// Export numbers are package-local and can change between compatible builds.
// Require one root object with the expected type; never choose an arbitrary match.
inline std::int32_t FindUniqueRootExport(const wand::Hp1PackageLinkTable& table,
                                        std::string_view name, std::string_view type) {
    if (table.status != wand::Hp1ProfileStatus::ok || name.empty() || type.empty()) return 0;
    std::int32_t found = 0;
    for (const auto& item : table.exports) {
        if (item.outer_reference != 0 || item.object_path.size() != 1 ||
            !EqualResourceName(item.object_path.front(), name) ||
            !EqualResourceName(item.qualified_class_name, type)) continue;
        if (found != 0 || item.reference <= 0) return 0;
        found = item.reference;
    }
    return found;
}

} // namespace hpvr::quest
