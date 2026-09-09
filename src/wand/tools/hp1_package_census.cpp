#include "hpvr/hp1_gesture.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: hpvr_hp1_package_census <package> [package ...]\n";
        return EXIT_FAILURE;
    }

    bool all_ok = true;
    for (int argument = 1; argument < argc; ++argument) {
        const std::filesystem::path path(argv[argument]);
        auto summary = hpvr::wand::inspect_hp1_package(path);
        if (summary.status != hpvr::wand::Hp1ProfileStatus::ok) {
            std::cerr << "package_status=" << static_cast<int>(summary.status)
                      << " path=" << path.string()
                      << " error=" << summary.error << '\n';
            all_ok = false;
            continue;
        }
        std::ranges::sort(
            summary.classes,
            [](const auto& left, const auto& right) {
                if (left.export_count != right.export_count) {
                    return left.export_count > right.export_count;
                }
                return left.qualified_class_name < right.qualified_class_name;
            });
        std::cout << "package_status=ok"
                  << " path=" << path.string()
                  << " version=" << summary.package_version
                  << " licensee=" << summary.licensee_version
                  << " file_bytes=" << summary.file_bytes
                  << " names=" << summary.name_count
                  << " imports=" << summary.import_count
                  << " exports=" << summary.export_count
                  << " serialized_bytes=" << summary.serialized_bytes
                  << " dependencies=" << summary.imported_packages.size()
                  << " distinct_export_classes=" << summary.classes.size()
                  << '\n';
        for (const auto& package : summary.imported_packages) {
            std::cout << "dependency=" << package << '\n';
        }
        for (const auto& entry : summary.classes) {
            std::cout << "class=" << entry.qualified_class_name
                      << " exports=" << entry.export_count
                      << " serialized_bytes=" << entry.serialized_bytes
                      << '\n';
        }
    }
    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
