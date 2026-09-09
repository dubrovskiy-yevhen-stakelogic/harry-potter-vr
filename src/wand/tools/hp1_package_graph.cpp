#include "hpvr/hp1_package_graph.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: hpvr_hp1_package_graph <data-root> <entry-package>\n";
        return EXIT_FAILURE;
    }

    const auto graph = hpvr::wand::resolve_hp1_package_graph(
        std::filesystem::path(argv[1]), std::filesystem::path(argv[2]));
    if (graph.status != hpvr::wand::Hp1PackageGraphStatus::ok) {
        std::cerr << "graph_status=" << static_cast<int>(graph.status)
                  << " error=" << graph.error << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "graph_status=ok"
              << " packages=" << graph.packages.size()
              << " dependencies=" << graph.dependencies.size()
              << " cycle_edges=" << graph.cycle_edge_count
              << " shadowed_candidates=" << graph.shadowed_candidate_count
              << '\n';
    for (const auto& package : graph.packages) {
        std::cout << "package=" << package.package_name
                  << " kind="
                  << (package.kind ==
                              hpvr::wand::Hp1ResolvedPackageKind::data_package
                          ? "data"
                          : "native")
                  << " path=" << package.path.string();
        if (package.kind ==
            hpvr::wand::Hp1ResolvedPackageKind::data_package) {
            std::cout << " version=" << package.summary.package_version
                      << " direct_dependencies="
                      << package.summary.imported_packages.size()
                      << " native_companion="
                      << (package.native_companion ? 1 : 0);
        }
        std::cout << '\n';
        for (const auto& shadowed : package.shadowed_candidates) {
            std::cout << "shadowed_by=" << package.path.string()
                      << " candidate=" << shadowed.string() << '\n';
        }
    }
    for (const auto& edge : graph.dependencies) {
        std::cout << "dependency=" << edge.from_package << "->"
                  << edge.to_package << " kind="
                  << (edge.to_kind ==
                              hpvr::wand::Hp1ResolvedPackageKind::data_package
                          ? "data"
                          : "native")
                  << '\n';
    }
    return EXIT_SUCCESS;
}
