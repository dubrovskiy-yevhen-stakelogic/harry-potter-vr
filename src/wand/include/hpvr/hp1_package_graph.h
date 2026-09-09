#pragma once

#include "hpvr/hp1_gesture.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace hpvr::wand {

enum class Hp1PackageGraphStatus {
    ok,
    invalid_data_root,
    invalid_entry_package,
    package_error,
    missing_dependency,
    ambiguous_dependency,
    resource_limit,
};

enum class Hp1ResolvedPackageKind {
    data_package,
    native_module,
};

struct Hp1ResolvedPackage {
    std::string package_name;
    std::filesystem::path path;
    Hp1ResolvedPackageKind kind{Hp1ResolvedPackageKind::data_package};
    Hp1PackageSummary summary;
    bool native_companion{};
    std::vector<std::filesystem::path> shadowed_candidates;
};

struct Hp1PackageDependency {
    std::string from_package;
    std::string to_package;
    Hp1ResolvedPackageKind to_kind{Hp1ResolvedPackageKind::data_package};
};

struct Hp1PackageGraph {
    Hp1PackageGraphStatus status{Hp1PackageGraphStatus::invalid_data_root};
    std::string error;
    std::vector<Hp1ResolvedPackage> packages;
    std::vector<Hp1PackageDependency> dependencies;
    std::size_t cycle_edge_count{};
    std::size_t shadowed_candidate_count{};
};

// Resolves the dependency closure using the shipped Core.System search order:
// System/*.u, Maps/*.unr, Textures/*.utx, Sounds/*.uax, Music/*.umx.
// The data root and entry package are explicit; no process working-directory or
// global Unreal configuration is consulted. DLL-only imports are retained as
// terminal native-module requirements and are never loaded or executed.
[[nodiscard]] Hp1PackageGraph resolve_hp1_package_graph(
    const std::filesystem::path& data_root,
    const std::filesystem::path& entry_package);

}  // namespace hpvr::wand
