#include "hpvr/hp1_gesture.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

struct ClassUsage {
    std::string qualified_class_name;
    std::size_t actor_count{};
    std::uint64_t serialized_bytes{};
    std::vector<std::string> sample_objects;
};

[[nodiscard]] std::string display_path(
    const std::vector<std::string>& components) {
    std::string result;
    for (const auto& component : components) {
        if (!result.empty()) {
            result.push_back('.');
        }
        result += component;
    }
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: hpvr_hp1_actor_census <map-package>\n";
        return EXIT_FAILURE;
    }
    const std::filesystem::path map_package(argv[1]);
    const auto level = hpvr::wand::inspect_hp1_level_handles(map_package);
    if (level.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "level_status=" << static_cast<int>(level.status)
                  << " error=" << level.error << '\n';
        return EXIT_FAILURE;
    }
    const auto table = hpvr::wand::inspect_hp1_package_link_table(map_package);
    if (table.status != hpvr::wand::Hp1ProfileStatus::ok) {
        std::cerr << "table_status=" << static_cast<int>(table.status)
                  << " error=" << table.error << '\n';
        return EXIT_FAILURE;
    }
    std::map<std::int32_t, const hpvr::wand::Hp1PackageExport*> exports;
    for (const auto& object : table.exports) {
        if (!exports.emplace(object.reference, &object).second) {
            std::cerr << "duplicate_export_reference=" << object.reference
                      << '\n';
            return EXIT_FAILURE;
        }
    }

    std::map<std::string, ClassUsage> classes;
    std::size_t nonnull_count = 0;
    std::size_t missing_count = 0;
    std::uint64_t serialized_bytes = 0;
    for (const auto reference : level.actor_references) {
        if (reference == 0) {
            continue;
        }
        ++nonnull_count;
        const auto found = exports.find(reference);
        if (found == exports.end()) {
            ++missing_count;
            continue;
        }
        const auto& object = *found->second;
        auto& usage = classes[object.qualified_class_name];
        usage.qualified_class_name = object.qualified_class_name;
        ++usage.actor_count;
        usage.serialized_bytes += object.serialized_bytes;
        serialized_bytes += object.serialized_bytes;
        if (usage.sample_objects.size() < 5) {
            usage.sample_objects.push_back(display_path(object.object_path));
        }
    }
    std::vector<const ClassUsage*> ordered;
    ordered.reserve(classes.size());
    for (const auto& [name, usage] : classes) {
        static_cast<void>(name);
        ordered.push_back(&usage);
    }
    std::ranges::sort(ordered, [](const auto* left, const auto* right) {
        if (left->actor_count != right->actor_count) {
            return left->actor_count > right->actor_count;
        }
        return left->qualified_class_name < right->qualified_class_name;
    });

    std::cout << "actor_census_status=ok"
              << " map=" << map_package.string()
              << " actor_slots=" << level.actor_references.size()
              << " nonnull_actors=" << nonnull_count
              << " null_slots=" << level.null_actor_count
              << " classes=" << classes.size()
              << " serialized_bytes=" << serialized_bytes
              << " missing_exports=" << missing_count << '\n';
    for (const auto* usage : ordered) {
        std::cout << "class=" << usage->qualified_class_name
                  << " actors=" << usage->actor_count
                  << " serialized_bytes=" << usage->serialized_bytes
                  << " samples=";
        for (std::size_t index = 0; index < usage->sample_objects.size();
             ++index) {
            if (index != 0) {
                std::cout << ',';
            }
            std::cout << usage->sample_objects[index];
        }
        std::cout << '\n';
    }
    return missing_count == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
