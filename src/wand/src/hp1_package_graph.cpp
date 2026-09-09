#include "hpvr/hp1_package_graph.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace hpvr::wand {
namespace {

constexpr std::size_t kMaximumIndexedFiles{10'000};
constexpr std::size_t kMaximumResolvedPackages{4'096};
constexpr std::size_t kMaximumDependencyEdges{65'536};

struct SearchPath {
    std::string_view directory;
    std::string_view extension;
};

constexpr std::array kSearchPaths{
    SearchPath{"system", ".u"},
    SearchPath{"maps", ".unr"},
    SearchPath{"textures", ".utx"},
    SearchPath{"sounds", ".uax"},
    SearchPath{"music", ".umx"},
};

// These UE1 modules provide native implementations in the shipped Windows
// build. Standalone ports must retain that identity even though the platform
// DLLs themselves are deliberately not imported as game data.
constexpr std::array<std::string_view, 12> kHp1NativeModuleNames{
    "core",   "d3ddrv", "editor",  "engine", "fire",  "galaxy",
    "ipdrv",  "render", "softdrv", "uweb",   "window", "windrv",
};

class GraphException final : public std::runtime_error {
public:
    GraphException(Hp1PackageGraphStatus status, std::string message)
        : std::runtime_error(std::move(message)), status_(status) {}
    [[nodiscard]] Hp1PackageGraphStatus status() const noexcept {
        return status_;
    }

private:
    Hp1PackageGraphStatus status_;
};

[[noreturn]] void graph_fail(Hp1PackageGraphStatus status,
                             std::string message) {
    throw GraphException(status, std::move(message));
}

[[nodiscard]] std::string ascii_fold(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

[[nodiscard]] std::string path_key(const std::filesystem::path& path) {
    return ascii_fold(path.generic_string());
}

[[nodiscard]] bool extension_is(const std::filesystem::path& path,
                                std::string_view extension) {
    return ascii_fold(path.extension().string()) == extension;
}

[[nodiscard]] std::filesystem::path canonical_existing(
    const std::filesystem::path& path,
    Hp1PackageGraphStatus status,
    std::string_view label) {
    std::error_code error;
    const auto result = std::filesystem::canonical(path, error);
    if (error) {
        graph_fail(status,
                   std::string(label) + " does not resolve to an existing path");
    }
    return result;
}

[[nodiscard]] bool is_within(const std::filesystem::path& child,
                             const std::filesystem::path& root) {
    std::error_code error;
    const auto relative = std::filesystem::relative(child, root, error);
    if (error || relative.empty() || relative.is_absolute()) {
        return false;
    }
    for (const auto& component : relative) {
        if (component == "..") {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::filesystem::path find_child_directory(
    const std::filesystem::path& root,
    std::string_view wanted) {
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(root, error), end;
         !error && iterator != end;
         iterator.increment(error)) {
        const bool directory = iterator->is_directory(error);
        if (!error && directory &&
            ascii_fold(iterator->path().filename().string()) == wanted) {
            return iterator->path();
        }
    }
    if (error) {
        graph_fail(Hp1PackageGraphStatus::invalid_data_root,
                   "could not enumerate data root directories");
    }
    return {};
}

struct IndexedFile {
    std::filesystem::path path;
    std::size_t search_rank{};
};

struct FileIndex {
    std::map<std::string, std::vector<IndexedFile>> packages;
    std::map<std::string, std::filesystem::path> native_modules;
    std::set<std::string> package_paths;
};

[[nodiscard]] FileIndex build_index(const std::filesystem::path& root) {
    FileIndex result;
    for (const auto module_name : kHp1NativeModuleNames) {
        result.native_modules.emplace(std::string(module_name),
                                      std::filesystem::path{});
    }
    std::size_t file_count = 0;
    for (std::size_t rank = 0; rank < kSearchPaths.size(); ++rank) {
        const auto& search = kSearchPaths[rank];
        const auto directory = find_child_directory(root, search.directory);
        if (directory.empty()) {
            continue;
        }
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(directory, error), end;
             !error && iterator != end;
             iterator.increment(error)) {
            const bool regular = iterator->is_regular_file(error);
            if (error || !regular) {
                continue;
            }
            const auto path = canonical_existing(
                iterator->path(), Hp1PackageGraphStatus::invalid_data_root,
                "indexed package");
            if (!is_within(path, root)) {
                graph_fail(Hp1PackageGraphStatus::invalid_data_root,
                           "search path contains a file outside the data root");
            }
            if (extension_is(path, search.extension)) {
                result.packages[ascii_fold(path.stem().string())].push_back(
                    {path, rank});
                result.package_paths.insert(path_key(path));
                ++file_count;
            } else if (rank == 0 && extension_is(path, ".dll")) {
                result.native_modules.insert_or_assign(
                    ascii_fold(path.stem().string()), path);
                ++file_count;
            }
            if (file_count > kMaximumIndexedFiles) {
                graph_fail(Hp1PackageGraphStatus::resource_limit,
                           "data root exceeds the indexed-file limit");
            }
        }
        if (error) {
            graph_fail(Hp1PackageGraphStatus::invalid_data_root,
                       "could not enumerate a package search directory");
        }
    }
    for (auto& [name, candidates] : result.packages) {
        static_cast<void>(name);
        std::ranges::sort(candidates, [](const auto& left, const auto& right) {
            if (left.search_rank != right.search_rank) {
                return left.search_rank < right.search_rank;
            }
            return path_key(left.path) < path_key(right.path);
        });
    }
    return result;
}

class GraphBuilder {
public:
    GraphBuilder(FileIndex index, std::filesystem::path entry)
        : index_(std::move(index)), entry_(std::move(entry)) {}

    [[nodiscard]] Hp1PackageGraph build() {
        if (!index_.package_paths.contains(path_key(entry_))) {
            graph_fail(Hp1PackageGraphStatus::invalid_entry_package,
                       "entry package is outside the shipped UE1 search paths");
        }
        visit_data(entry_.stem().string(), entry_);
        graph_.status = Hp1PackageGraphStatus::ok;
        std::ranges::sort(graph_.packages, [](const auto& left,
                                             const auto& right) {
            const auto left_name = ascii_fold(left.package_name);
            const auto right_name = ascii_fold(right.package_name);
            if (left_name != right_name) {
                return left_name < right_name;
            }
            return path_key(left.path) < path_key(right.path);
        });
        std::ranges::sort(graph_.dependencies, [](const auto& left,
                                                 const auto& right) {
            return std::pair{ascii_fold(left.from_package),
                             ascii_fold(left.to_package)} <
                   std::pair{ascii_fold(right.from_package),
                             ascii_fold(right.to_package)};
        });
        return graph_;
    }

private:
    enum class VisitState { visiting, complete };

    void add_edge(std::string_view from,
                  std::string_view to,
                  Hp1ResolvedPackageKind kind) {
        if (graph_.dependencies.size() >= kMaximumDependencyEdges) {
            graph_fail(Hp1PackageGraphStatus::resource_limit,
                       "dependency graph exceeds the edge limit");
        }
        graph_.dependencies.push_back(
            {std::string(from), std::string(to), kind});
    }

    void visit_native(std::string_view package_name,
                      const std::filesystem::path& path) {
        const auto key = "native:" + ascii_fold(package_name);
        if (states_.contains(key)) {
            return;
        }
        enforce_package_limit();
        states_[key] = VisitState::complete;
        Hp1ResolvedPackage node;
        node.package_name = std::string(package_name);
        node.path = path;
        node.kind = Hp1ResolvedPackageKind::native_module;
        graph_.packages.push_back(std::move(node));
    }

    void visit_data(std::string package_name,
                    const std::filesystem::path& path) {
        const auto key = path_key(path);
        if (const auto found = states_.find(key); found != states_.end()) {
            if (found->second == VisitState::visiting) {
                ++graph_.cycle_edge_count;
            }
            return;
        }
        enforce_package_limit();
        states_[key] = VisitState::visiting;

        auto summary = inspect_hp1_package(path);
        if (summary.status != Hp1ProfileStatus::ok) {
            graph_fail(Hp1PackageGraphStatus::package_error,
                       "package '" + package_name + "' failed validation: " +
                           summary.error);
        }

        Hp1ResolvedPackage node;
        node.package_name = package_name;
        node.path = path;
        node.summary = std::move(summary);
        const auto folded_name = ascii_fold(package_name);
        node.native_companion = index_.native_modules.contains(folded_name);
        if (const auto candidates = index_.packages.find(folded_name);
            candidates != index_.packages.end()) {
            const auto selected_key = path_key(path);
            for (const auto& candidate : candidates->second) {
                if (path_key(candidate.path) != selected_key) {
                    node.shadowed_candidates.push_back(candidate.path);
                    ++graph_.shadowed_candidate_count;
                }
            }
        }
        const auto dependencies = node.summary.imported_packages;
        graph_.packages.push_back(std::move(node));

        for (const auto& dependency : dependencies) {
            const auto dependency_key = ascii_fold(dependency);
            if (const auto packages = index_.packages.find(dependency_key);
                packages != index_.packages.end() && !packages->second.empty()) {
                if (packages->second.size() > 1 &&
                    packages->second[0].search_rank ==
                        packages->second[1].search_rank) {
                    graph_fail(
                        Hp1PackageGraphStatus::ambiguous_dependency,
                        "package '" + package_name +
                            "' imports case-insensitively ambiguous dependency '" +
                            dependency + "'");
                }
                const auto& selected = packages->second.front().path;
                add_edge(package_name, dependency,
                         Hp1ResolvedPackageKind::data_package);
                visit_data(dependency, selected);
            } else if (const auto native =
                           index_.native_modules.find(dependency_key);
                       native != index_.native_modules.end()) {
                add_edge(package_name, dependency,
                         Hp1ResolvedPackageKind::native_module);
                visit_native(dependency, native->second);
            } else {
                graph_fail(Hp1PackageGraphStatus::missing_dependency,
                           "package '" + package_name +
                               "' imports missing dependency '" + dependency +
                               "'");
            }
        }
        states_[key] = VisitState::complete;
    }

    void enforce_package_limit() const {
        if (states_.size() >= kMaximumResolvedPackages) {
            graph_fail(Hp1PackageGraphStatus::resource_limit,
                       "dependency graph exceeds the package limit");
        }
    }

    FileIndex index_;
    std::filesystem::path entry_;
    std::map<std::string, VisitState> states_;
    Hp1PackageGraph graph_;
};

}  // namespace

Hp1PackageGraph resolve_hp1_package_graph(
    const std::filesystem::path& data_root,
    const std::filesystem::path& entry_package) {
    Hp1PackageGraph result;
    try {
        const auto root = canonical_existing(
            data_root, Hp1PackageGraphStatus::invalid_data_root, "data root");
        std::error_code error;
        if (!std::filesystem::is_directory(root, error) || error) {
            graph_fail(Hp1PackageGraphStatus::invalid_data_root,
                       "data root is not a directory");
        }
        const auto entry = canonical_existing(
            entry_package, Hp1PackageGraphStatus::invalid_entry_package,
            "entry package");
        if (!is_within(entry, root)) {
            graph_fail(Hp1PackageGraphStatus::invalid_entry_package,
                       "entry package is outside the data root");
        }
        return GraphBuilder(build_index(root), entry).build();
    } catch (const GraphException& exception) {
        result.status = exception.status();
        result.error = exception.what();
    } catch (const std::bad_alloc&) {
        result.status = Hp1PackageGraphStatus::resource_limit;
        result.error = "allocation failed while resolving package graph";
    } catch (const std::exception& exception) {
        result.status = Hp1PackageGraphStatus::invalid_data_root;
        result.error = exception.what();
    }
    return result;
}

}  // namespace hpvr::wand
