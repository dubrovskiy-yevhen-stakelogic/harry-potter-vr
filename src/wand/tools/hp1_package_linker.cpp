#include "hpvr/hp1_package_linker.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::string ascii_fold(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

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
    if (argc < 3 || argc > 4) {
        std::cerr << "usage: hpvr_hp1_package_linker <data-root> "
                     "<entry-package> [detail-filter]\n";
        return EXIT_FAILURE;
    }

    const auto result = hpvr::wand::link_hp1_package_graph(
        std::filesystem::path(argv[1]), std::filesystem::path(argv[2]));
    if (result.status != hpvr::wand::Hp1PackageLinkStatus::ok) {
        std::cerr << "link_status=" << static_cast<int>(result.status)
                  << " error=" << result.error << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "link_status=ok"
              << " packages=" << result.graph.packages.size()
              << " package_edges=" << result.graph.dependencies.size()
              << " imports=" << result.imports.size()
              << " package_roots=" << result.package_root_count
              << " package_groups=" << result.package_group_count
              << " export_objects=" << result.export_object_count
              << " native_objects=" << result.native_object_count
              << " native_modules=" << result.native_module_count
              << '\n';

    std::map<std::string, std::array<std::size_t, 5>> by_source;
    std::map<std::string, std::size_t> native_by_target;
    for (const auto& import : result.imports) {
        auto& counts = by_source[import.source_package];
        switch (import.target_kind) {
            case hpvr::wand::Hp1ImportTargetKind::package_root:
                ++counts[0];
                break;
            case hpvr::wand::Hp1ImportTargetKind::package_group:
                ++counts[1];
                break;
            case hpvr::wand::Hp1ImportTargetKind::export_object:
                ++counts[2];
                break;
            case hpvr::wand::Hp1ImportTargetKind::native_object:
                ++counts[3];
                ++native_by_target[import.target_package];
                break;
            case hpvr::wand::Hp1ImportTargetKind::native_module:
                ++counts[4];
                break;
        }
    }
    for (const auto& [package, counts] : by_source) {
        std::cout << "source=" << package
                  << " package_roots=" << counts[0]
                  << " package_groups=" << counts[1]
                  << " export_objects=" << counts[2]
                  << " native_objects=" << counts[3]
                  << " native_modules=" << counts[4] << '\n';
    }
    for (const auto& [package, count] : native_by_target) {
        std::cout << "native_target=" << package
                  << " object_requirements=" << count << '\n';
    }
    if (argc == 4) {
        const auto filter = ascii_fold(argv[3]);
        for (const auto& import : result.imports) {
            const auto source_path = display_path(import.source_object_path);
            const auto target_path = display_path(import.target_object_path);
            const auto searchable = ascii_fold(
                import.source_package + "." + source_path + " " +
                import.target_package + "." + target_path + " " +
                import.qualified_class_name);
            if (searchable.find(filter) == std::string::npos) {
                continue;
            }
            std::cout << "detail_source=" << import.source_package
                      << " source_ref=" << import.source_reference
                      << " source_path=" << source_path
                      << " class=" << import.qualified_class_name
                      << " target_package=" << import.target_package
                      << " target_ref=" << import.target_reference
                      << " target_path=" << target_path
                      << " target_kind="
                      << static_cast<unsigned>(import.target_kind) << '\n';
        }
    }
    return EXIT_SUCCESS;
}
