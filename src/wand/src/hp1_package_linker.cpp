#include "hpvr/hp1_package_linker.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace hpvr::wand {
namespace {

constexpr std::size_t kMaximumResolvedImports{1'000'000};
constexpr std::uint32_t kLightmapGutter{1};

[[nodiscard]] float dot(Hp1BspVector a, Hp1BspVector b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] Hp1BspVector cross(Hp1BspVector a, Hp1BspVector b) noexcept {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

[[nodiscard]] std::optional<Hp1BspVector> solve_plane_coordinates(
    Hp1BspVector u, Hp1BspVector v, Hp1BspVector n,
    float u_value, float v_value, float n_value) {
    const auto vx_n = cross(v, n);
    const auto nx_u = cross(n, u);
    const auto ux_v = cross(u, v);
    const float determinant = dot(u, vx_n);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-10F) {
        return std::nullopt;
    }
    const float inverse = 1.0F / determinant;
    return Hp1BspVector{
        (u_value * vx_n.x + v_value * nx_u.x + n_value * ux_v.x) * inverse,
        (u_value * vx_n.y + v_value * nx_u.y + n_value * ux_v.y) * inverse,
        (u_value * vx_n.z + v_value * nx_u.z + n_value * ux_v.z) * inverse};
}

[[nodiscard]] std::array<float, 3> hsb_light_color(
    std::uint8_t hue, std::uint8_t saturation) {
    const float h = static_cast<float>(hue) * 6.0F / 255.0F;
    const float s = 1.0F - static_cast<float>(saturation) / 255.0F;
    const float x = s * (1.0F - std::abs(std::fmod(h, 2.0F) - 1.0F));
    std::array<float, 3> rgb{};
    if (h < 1.0F) rgb = {s, x, 0.0F};
    else if (h < 2.0F) rgb = {x, s, 0.0F};
    else if (h < 3.0F) rgb = {0.0F, s, x};
    else if (h < 4.0F) rgb = {0.0F, x, s};
    else if (h < 5.0F) rgb = {x, 0.0F, s};
    else rgb = {s, 0.0F, x};
    for (float& channel : rgb) channel += 1.0F - s;
    return rgb;
}

[[nodiscard]] std::uint8_t encode_linear(float value) noexcept {
    value = std::clamp(value, 0.0F, 1.0F);
    const float srgb = value <= 0.0031308F
                           ? value * 12.92F
                           : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
    return static_cast<std::uint8_t>(std::lround(srgb * 255.0F));
}

class LinkException final : public std::runtime_error {
public:
    LinkException(Hp1PackageLinkStatus status, std::string message)
        : std::runtime_error(std::move(message)), status_(status) {}
    [[nodiscard]] Hp1PackageLinkStatus status() const noexcept {
        return status_;
    }

private:
    Hp1PackageLinkStatus status_;
};

[[noreturn]] void link_fail(Hp1PackageLinkStatus status, std::string message) {
    throw LinkException(status, std::move(message));
}

[[nodiscard]] std::string ascii_fold(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

[[nodiscard]] bool ascii_equal_fold(std::string_view left,
                                    std::string_view right) {
    return ascii_fold(left) == ascii_fold(right);
}

[[nodiscard]] std::string path_key(
    const std::vector<std::string>& components,
    std::size_t first = 0) {
    std::string result;
    for (std::size_t index = first; index < components.size(); ++index) {
        const auto folded = ascii_fold(components[index]);
        result += std::to_string(folded.size());
        result.push_back(':');
        result += folded;
        result.push_back(';');
    }
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

[[nodiscard]] std::string export_identity_key(
    std::string_view qualified_class_name,
    const std::vector<std::string>& object_path) {
    return ascii_fold(qualified_class_name) + "|" + path_key(object_path);
}

[[nodiscard]] std::string identity_key(
    std::string_view qualified_class_name,
    const std::vector<std::string>& object_path) {
    return ascii_fold(qualified_class_name) + "|" + path_key(object_path);
}

[[nodiscard]] std::vector<std::string> normalized_export_path(
    const Hp1PackageExport& object) {
    return object.object_path;
}

struct PackageRecord {
    const Hp1ResolvedPackage* package{};
    Hp1PackageLinkTable table;
    std::map<std::string, const Hp1PackageExport*> exports;
};

[[nodiscard]] std::map<std::string, PackageRecord> load_records(
    const Hp1PackageGraph& graph) {
    std::map<std::string, PackageRecord> records;
    for (const auto& package : graph.packages) {
        const auto key = ascii_fold(package.package_name);
        if (records.contains(key)) {
            link_fail(Hp1PackageLinkStatus::invalid_link_table,
                      "resolved graph contains duplicate package identity '" +
                          package.package_name + "'");
        }
        PackageRecord record;
        record.package = &package;
        if (package.kind == Hp1ResolvedPackageKind::data_package) {
            record.table = inspect_hp1_package_link_table(package.path);
            if (record.table.status != Hp1ProfileStatus::ok) {
                link_fail(Hp1PackageLinkStatus::invalid_link_table,
                          "package '" + package.package_name +
                              "' link table failed validation: " +
                              record.table.error);
            }
            for (const auto& object : record.table.exports) {
                const auto identity = export_identity_key(
                    object.qualified_class_name,
                    object.object_path);
                if (!record.exports.emplace(identity, &object).second) {
                    link_fail(Hp1PackageLinkStatus::ambiguous_target_export,
                              "package '" + package.package_name +
                                  "' contains duplicate export identity");
                }
            }
        }
        records.emplace(key, std::move(record));
    }
    return records;
}

class Linker {
public:
    explicit Linker(Hp1PackageGraph graph) : graph_(std::move(graph)) {}

    [[nodiscard]] Hp1PackageLinkResult run() {
        auto records = load_records(graph_);
        Hp1PackageLinkResult result;
        result.imports.reserve(total_import_count(records));
        for (const auto& [source_key, source] : records) {
            static_cast<void>(source_key);
            if (source.package->kind != Hp1ResolvedPackageKind::data_package) {
                continue;
            }
            std::vector<VisitState> states(
                source.table.imports.size(), VisitState::unvisited);
            std::vector<std::optional<Hp1ResolvedImport>> resolved(
                source.table.imports.size());
            for (std::size_t index = 0;
                 index < source.table.imports.size();
                 ++index) {
                if (result.imports.size() >= kMaximumResolvedImports) {
                    link_fail(Hp1PackageLinkStatus::resource_limit,
                              "link closure exceeds the import limit");
                }
                result.imports.push_back(resolve_import(
                    source, index, records, states, resolved));
                switch (result.imports.back().target_kind) {
                    case Hp1ImportTargetKind::package_root:
                        ++result.package_root_count;
                        break;
                    case Hp1ImportTargetKind::package_group:
                        ++result.package_group_count;
                        break;
                    case Hp1ImportTargetKind::export_object:
                        ++result.export_object_count;
                        break;
                    case Hp1ImportTargetKind::native_object:
                        ++result.native_object_count;
                        break;
                    case Hp1ImportTargetKind::native_module:
                        ++result.native_module_count;
                        break;
                }
            }
        }
        result.status = Hp1PackageLinkStatus::ok;
        result.graph = std::move(graph_);
        return result;
    }

private:
    enum class VisitState { unvisited, visiting, complete };

    [[nodiscard]] static std::size_t total_import_count(
        const std::map<std::string, PackageRecord>& records) {
        std::size_t total = 0;
        for (const auto& [key, record] : records) {
            static_cast<void>(key);
            if (record.table.imports.size() >
                kMaximumResolvedImports - total) {
                link_fail(Hp1PackageLinkStatus::resource_limit,
                          "link closure exceeds the import limit");
            }
            total += record.table.imports.size();
        }
        return total;
    }

    [[nodiscard]] static Hp1ResolvedImport resolve_import(
        const PackageRecord& source,
        std::size_t import_index,
        const std::map<std::string, PackageRecord>& records,
        std::vector<VisitState>& states,
        std::vector<std::optional<Hp1ResolvedImport>>& resolved_imports) {
        if (import_index >= source.table.imports.size()) {
            link_fail(Hp1PackageLinkStatus::invalid_import_path,
                      "import outer reference is outside its package table");
        }
        if (states[import_index] == VisitState::complete) {
            return *resolved_imports[import_index];
        }
        if (states[import_index] == VisitState::visiting) {
            link_fail(Hp1PackageLinkStatus::invalid_import_path,
                      "import outer chain contains a cycle");
        }
        states[import_index] = VisitState::visiting;
        const auto& import = source.table.imports[import_index];
        if (import.object_path.empty()) {
            link_fail(Hp1PackageLinkStatus::invalid_import_path,
                      "package '" + source.package->package_name +
                          "' contains an import with an empty object path");
        }
        const auto& target_name = import.object_path.front();
        const auto target = records.find(ascii_fold(target_name));
        if (target == records.end()) {
            link_fail(Hp1PackageLinkStatus::missing_target_package,
                      "package '" + source.package->package_name +
                          "' imports target outside the resolved graph '" +
                          target_name + "'");
        }

        Hp1ResolvedImport resolved;
        resolved.source_package = source.package->package_name;
        resolved.source_reference = import.reference;
        resolved.qualified_class_name = import.qualified_class_name;
        resolved.source_object_path = import.object_path;
        resolved.target_package = target->second.package->package_name;

        if (import.root_package) {
            if (import.object_path.size() != 1) {
                link_fail(Hp1PackageLinkStatus::invalid_import_path,
                          "root package import has a nested object path");
            }
            resolved.target_kind =
                target->second.package->kind ==
                        Hp1ResolvedPackageKind::native_module
                    ? Hp1ImportTargetKind::native_module
                    : Hp1ImportTargetKind::package_root;
            return complete_import(
                import_index, std::move(resolved), states, resolved_imports);
        }
        if (import.object_path.size() < 2 || import.outer_reference >= 0) {
            link_fail(Hp1PackageLinkStatus::invalid_import_path,
                      "non-package import has no imported outer object");
        }
        const auto outer_wide =
            -static_cast<std::int64_t>(import.outer_reference) - 1;
        if (outer_wide < 0 ||
            static_cast<std::size_t>(outer_wide) >=
                source.table.imports.size()) {
            link_fail(Hp1PackageLinkStatus::invalid_import_path,
                      "import outer reference is outside its package table");
        }
        const auto outer = resolve_import(
            source,
            static_cast<std::size_t>(outer_wide),
            records,
            states,
            resolved_imports);
        if (!ascii_equal_fold(outer.target_package,
                              target->second.package->package_name)) {
            link_fail(Hp1PackageLinkStatus::invalid_import_path,
                      "import outer resolves into a different package");
        }

        auto target_path = outer.target_object_path;
        target_path.push_back(import.object_path.back());
        if (target->second.package->kind ==
            Hp1ResolvedPackageKind::native_module) {
            resolved.target_kind = Hp1ImportTargetKind::native_module;
            resolved.target_object_path = std::move(target_path);
            return complete_import(
                import_index, std::move(resolved), states, resolved_imports);
        }

        const auto identity =
            identity_key(import.qualified_class_name, target_path);
        const auto object = target->second.exports.find(identity);
        if (ascii_equal_fold(import.qualified_class_name, "Core.Package")) {
            resolved.target_kind = Hp1ImportTargetKind::package_group;
            if (object != target->second.exports.end()) {
                resolved.target_reference = object->second->reference;
                resolved.target_object_path =
                    normalized_export_path(*object->second);
            } else if (!outer.target_object_path.empty() &&
                       ascii_equal_fold(
                           outer.target_object_path.back(),
                           import.object_path.back())) {
                resolved.target_object_path = outer.target_object_path;
            } else {
                resolved.target_object_path = std::move(target_path);
            }
            return complete_import(
                import_index, std::move(resolved), states, resolved_imports);
        }
        if (object == target->second.exports.end()) {
            std::string path_matches;
            std::string leaf_matches;
            const auto wanted_path = path_key(target_path);
            for (const auto& candidate : target->second.table.exports) {
                if (path_key(candidate.object_path) == wanted_path) {
                    if (!path_matches.empty()) {
                        path_matches += ",";
                    }
                    path_matches += candidate.qualified_class_name;
                }
                if (!candidate.object_path.empty() &&
                    ascii_equal_fold(candidate.object_path.back(),
                                     import.object_path.back())) {
                    if (!leaf_matches.empty()) {
                        leaf_matches += ",";
                    }
                    leaf_matches +=
                        display_path(candidate.object_path) + ":" +
                        candidate.qualified_class_name;
                }
            }
            if (target->second.package->native_companion) {
                resolved.target_kind = Hp1ImportTargetKind::native_object;
                resolved.target_object_path = std::move(target_path);
                return complete_import(
                    import_index, std::move(resolved), states, resolved_imports);
            }
            link_fail(Hp1PackageLinkStatus::missing_target_export,
                      "package '" + source.package->package_name +
                          "' has no exact target export in package '" +
                          target_name + "' for import reference " +
                          std::to_string(import.reference) + " path '" +
                          display_path(import.object_path) + "' class '" +
                          import.qualified_class_name +
                          "' path_match_classes='" + path_matches +
                          "' leaf_matches='" + leaf_matches + "'");
        }
        resolved.target_kind = Hp1ImportTargetKind::export_object;
        resolved.target_reference = object->second->reference;
        resolved.target_object_path = normalized_export_path(*object->second);
        return complete_import(
            import_index, std::move(resolved), states, resolved_imports);
    }

    [[nodiscard]] static Hp1ResolvedImport complete_import(
        std::size_t import_index,
        Hp1ResolvedImport resolved,
        std::vector<VisitState>& states,
        std::vector<std::optional<Hp1ResolvedImport>>& resolved_imports) {
        states[import_index] = VisitState::complete;
        resolved_imports[import_index] = resolved;
        return resolved;
    }

    Hp1PackageGraph graph_;
};

}  // namespace

Hp1PackageLinkResult link_hp1_package_graph(
    const std::filesystem::path& data_root,
    const std::filesystem::path& entry_package) {
    Hp1PackageLinkResult result;
    try {
        auto graph = resolve_hp1_package_graph(data_root, entry_package);
        if (graph.status != Hp1PackageGraphStatus::ok) {
            result.status = Hp1PackageLinkStatus::graph_error;
            result.error = graph.error;
            return result;
        }
        return Linker(std::move(graph)).run();
    } catch (const LinkException& exception) {
        result = {};
        result.status = exception.status();
        result.error = exception.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.status = Hp1PackageLinkStatus::resource_limit;
        result.error = "allocation failed while linking package graph";
    } catch (const std::exception& exception) {
        result = {};
        result.status = Hp1PackageLinkStatus::invalid_link_table;
        result.error = exception.what();
    }
    return result;
}

Hp1CharacterManifest build_hp1_character_manifest(
    const std::filesystem::path& data_root,
    const std::filesystem::path& map_package,
    std::int32_t excluded_actor_reference,
    const std::vector<Hp1ActorVisual>& additional_actors) {
    Hp1CharacterManifest result;
    try {
        auto census = inspect_hp1_actor_visuals(map_package);
        if (census.status != Hp1ProfileStatus::ok) {
            result.status = census.status;
            result.error = census.error;
            return result;
        }
        for (const auto& actor : additional_actors) {
            if (actor.actor_reference <= 0 || std::ranges::any_of(census.actors,
                    [&](const auto& a) { return a.actor_reference == actor.actor_reference; })) {
                result.error = "additional character identity is invalid or duplicated";
                return result;
            }
            census.actors.push_back(actor);
        }
        const auto linked = link_hp1_package_graph(data_root, map_package);
        if (linked.status != Hp1PackageLinkStatus::ok) {
            result.error = "character package link failed: " + linked.error;
            return result;
        }

        struct PackageView {
            std::string name;
            std::filesystem::path path;
            Hp1PackageLinkTable table;
        };
        std::map<std::string, PackageView> packages;
        for (const auto& package : linked.graph.packages) {
            if (package.kind != Hp1ResolvedPackageKind::data_package) {
                continue;
            }
            PackageView view{
                package.package_name,
                package.path,
                inspect_hp1_package_link_table(package.path),
            };
            if (view.table.status != Hp1ProfileStatus::ok) {
                result.error = "character package table failed for '" +
                               package.package_name + "': " +
                               view.table.error;
                return result;
            }
            packages.emplace(ascii_fold(package.package_name),
                             std::move(view));
        }
        std::map<std::pair<std::string, std::int32_t>,
                 const Hp1ResolvedImport*>
            imports;
        for (const auto& imported : linked.imports) {
            imports.emplace(
                std::pair{ascii_fold(imported.source_package),
                          imported.source_reference},
                &imported);
        }

        struct ResolvedReference {
            std::string package_name;
            std::filesystem::path package_path;
            std::int32_t reference{};
            std::string qualified_class_name;
            std::vector<std::string> object_path;
        };
        const auto resolve_reference =
            [&packages, &imports](std::string_view source_package,
                                  std::int32_t reference)
            -> std::optional<ResolvedReference> {
            if (reference == 0) {
                return std::nullopt;
            }
            if (reference < 0) {
                const auto found = imports.find(
                    std::pair{ascii_fold(source_package), reference});
                if (found == imports.end() ||
                    found->second->target_kind !=
                        Hp1ImportTargetKind::export_object ||
                    found->second->target_reference <= 0) {
                    return std::nullopt;
                }
                const auto package = packages.find(
                    ascii_fold(found->second->target_package));
                if (package == packages.end()) {
                    return std::nullopt;
                }
                return ResolvedReference{
                    found->second->target_package,
                    package->second.path,
                    found->second->target_reference,
                    found->second->qualified_class_name,
                    found->second->target_object_path,
                };
            }
            const auto package = packages.find(ascii_fold(source_package));
            if (package == packages.end()) {
                return std::nullopt;
            }
            const auto object = std::ranges::find_if(
                package->second.table.exports,
                [reference](const auto& candidate) {
                    return candidate.reference == reference;
                });
            if (object == package->second.table.exports.end()) {
                return std::nullopt;
            }
            return ResolvedReference{
                package->second.name,
                package->second.path,
                reference,
                object->qualified_class_name,
                object->object_path,
            };
        };
        const auto is_character_base = [](std::string_view package_name,
                                          const auto& object_path) {
            if (!ascii_equal_fold(package_name, "HPBase") ||
                object_path.empty()) {
                return false;
            }
            const auto& leaf = object_path.back();
            return ascii_equal_fold(leaf, "baseChar") ||
                   ascii_equal_fold(leaf, "baseHarry");
        };
        const auto path_is_character_base = [](const auto& object_path) {
            return object_path.size() >= 2 &&
                   ascii_equal_fold(object_path.front(), "HPBase") &&
                   (ascii_equal_fold(object_path.back(), "baseChar") ||
                    ascii_equal_fold(object_path.back(), "baseHarry"));
        };

        const auto map_source = map_package.stem().string();
        result.inspected_actor_count = census.actors.size();
        result.actors.reserve(census.actors.size());
        for (const auto& actor : census.actors) {
            if (actor.actor_reference == excluded_actor_reference) {
                ++result.excluded_actor_count;
                continue;
            }
            if (!actor.location_serialized) {
                ++result.missing_location_count;
                continue;
            }
            auto current_class = resolve_reference(
                map_source, actor.class_reference);
            if (!current_class.has_value() ||
                !ascii_equal_fold(current_class->qualified_class_name,
                                  "Core.Class")) {
                ++result.unresolved_class_count;
                continue;
            }

            auto mesh = actor.mesh_serialized
                            ? resolve_reference(map_source,
                                                actor.mesh_reference)
                            : std::optional<ResolvedReference>{};
            bool character = false;
            std::map<std::size_t, Hp1CharacterSkinOverride> skins;
            const auto collect_skins = [&](const auto& properties, const std::string& source) {
                for (const auto& property : properties) {
                    if (!ascii_equal_fold(property.name, "MultiSkins") ||
                        !property.object_reference_serialized || property.object_reference == 0) continue;
                    const auto slot=static_cast<std::size_t>(std::max<std::int64_t>(0,property.array_index));
                    if (slot >= 8 || skins.contains(slot)) continue;
                    const auto texture=resolve_reference(source,property.object_reference);
                    if (texture && ascii_equal_fold(texture->qualified_class_name,"Engine.Texture"))
                        skins.emplace(slot,Hp1CharacterSkinOverride{slot,texture->package_path,texture->reference});
                }
            };
            collect_skins(actor.serialized_properties,map_source);
            bool resolution_failed = false;
            bool hidden = actor.hidden_serialized && actor.hidden;
            bool hidden_known = actor.hidden_serialized;
            float draw_scale = actor.draw_scale;
            bool draw_scale_known = actor.draw_scale_serialized;
            std::optional<std::uint8_t> draw_type =
                actor.draw_type_serialized
                    ? std::optional<std::uint8_t>{actor.draw_type}
                    : std::nullopt;
            for (std::size_t depth = 0; depth < 32; ++depth) {
                character = character || is_character_base(
                                             current_class->package_name,
                                             current_class->object_path);
                const auto defaults = inspect_hp1_class_visual_defaults(
                    current_class->package_path,
                    current_class->reference);
                if (defaults.status != Hp1ProfileStatus::ok) {
                    resolution_failed = true;
                    break;
                }
                character = character ||
                            path_is_character_base(defaults.super_object_path);
                collect_skins(defaults.serialized_properties,current_class->package_name);
                if (!mesh.has_value() && defaults.mesh_serialized) {
                    mesh = resolve_reference(current_class->package_name,
                                             defaults.mesh_reference);
                }
                if (!draw_scale_known && defaults.draw_scale_serialized) {
                    draw_scale = defaults.draw_scale;
                    draw_scale_known = true;
                }
                if (!hidden_known && defaults.hidden_serialized) {
                    hidden = defaults.hidden;
                    hidden_known = true;
                }
                if (!draw_type.has_value() &&
                    defaults.draw_type_serialized) {
                    draw_type = defaults.draw_type;
                }
                if (character && mesh.has_value()) {
                    break;
                }
                if (defaults.super_reference == 0) {
                    break;
                }
                current_class = resolve_reference(
                    current_class->package_name,
                    defaults.super_reference);
                if (!current_class.has_value() ||
                    !ascii_equal_fold(current_class->qualified_class_name,
                                      "Core.Class")) {
                    resolution_failed = true;
                    break;
                }
            }
            if (resolution_failed) {
                ++result.unresolved_class_count;
                continue;
            }
            if (!character) {
                ++result.non_character_actor_count;
                continue;
            }
            if (hidden || (draw_type.has_value() && *draw_type != 2U)) {
                ++result.hidden_actor_count;
                continue;
            }
            if (!mesh.has_value() || mesh->reference <= 0 ||
                !ascii_equal_fold(mesh->qualified_class_name,
                                  "Engine.SkeletalMesh")) {
                ++result.missing_mesh_count;
                continue;
            }
            if (!std::isfinite(draw_scale) || draw_scale <= 0.0F) {
                ++result.missing_mesh_count;
                continue;
            }
            result.actors.push_back({
                actor.actor_reference,
                actor.class_reference,
                actor.actor_slot_index,
                actor.object_name,
                actor.qualified_class_name,
                actor.location_unreal,
                actor.rotation_units,
                draw_scale,
                mesh->package_path,
                mesh->package_name,
                mesh->reference,
                display_path(mesh->object_path),
                {},
            });
            for(const auto& [slot,skin]:skins) result.actors.back().skins.push_back(skin);
        }
        result.status = Hp1ProfileStatus::ok;
    } catch (const std::bad_alloc&) {
        result = {};
        result.error = "allocation failed while building character manifest";
    } catch (const std::exception& exception) {
        result = {};
        result.error = exception.what();
    }
    return result;
}

Hp1TexturedBspScene build_hp1_textured_bsp_scene(
    const std::filesystem::path& data_root,
    const std::filesystem::path& map_package,
    float meters_per_unreal_unit,
    std::uint32_t maximum_triangle_count,
    std::int32_t brush_model_reference) {
    Hp1TexturedBspScene result;
    try {
        if (maximum_triangle_count == 0) {
            result.error = "textured BSP triangle limit is zero";
            return result;
        }
        const auto topology = brush_model_reference > 0
            ? load_hp1_brush_topology(map_package, brush_model_reference)
            : load_hp1_bsp_topology(map_package);
        if (topology.status != Hp1ProfileStatus::ok) {
            result.status = topology.status;
            result.error = topology.error;
            return result;
        }
        const auto mesh = build_hp1_bsp_triangle_mesh(
            topology, meters_per_unreal_unit);
        if (mesh.status != Hp1ProfileStatus::ok) {
            result.status = mesh.status;
            result.error = mesh.error;
            return result;
        }
        const auto linked = link_hp1_package_graph(data_root, map_package);
        if (linked.status != Hp1PackageLinkStatus::ok) {
            result.error = "package link failed: " + linked.error;
            return result;
        }

        const auto selected_count = std::min<std::size_t>(
            mesh.triangles.size(), maximum_triangle_count);
        result.available_triangle_count = mesh.triangles.size();
        result.selected_triangle_count = selected_count;
        result.omitted_triangle_count = mesh.triangles.size() - selected_count;

        std::map<std::string, std::filesystem::path> package_paths;
        for (const auto& package : linked.graph.packages) {
            if (package.kind == Hp1ResolvedPackageKind::data_package) {
                package_paths.emplace(
                    ascii_fold(package.package_name), package.path);
            }
        }
        const auto source_name = ascii_fold(map_package.stem().string());
        std::map<std::int32_t, const Hp1ResolvedImport*> imports;
        for (const auto& imported : linked.imports) {
            if (ascii_fold(imported.source_package) == source_name) {
                imports.emplace(imported.source_reference, &imported);
            }
        }

        const auto actors = inspect_hp1_actor_visuals(map_package);
        if (actors.status != Hp1ProfileStatus::ok) {
            result.status = actors.status;
            result.error = "light actor census failed: " + actors.error;
            return result;
        }
        std::map<std::int32_t, const Hp1ActorVisual*> light_actors;
        for (const auto& actor : actors.actors) {
            if (actor.location_serialized && actor.light_brightness > 0 &&
                actor.light_radius > 0) {
                light_actors.emplace(actor.actor_reference, &actor);
            }
        }

        struct LightmapPlacement {
            std::uint32_t x{};
            std::uint32_t y{};
            std::int32_t surface{-1};
        };
        std::vector<LightmapPlacement> placements(topology.light_maps.size());
        for (std::size_t index = 0; index < selected_count; ++index) {
            const auto& triangle = mesh.triangles[index];
            const auto& surface = topology.surfaces[triangle.surface_index];
            if (surface.light_map_index >= 0) {
                placements[static_cast<std::size_t>(surface.light_map_index)]
                    .surface = static_cast<std::int32_t>(
                        triangle.surface_index);
            }
        }
        for (const std::uint32_t atlas_size : {512U, 1024U, 2048U, 4096U}) {
            std::uint32_t cursor_x = kLightmapGutter;
            std::uint32_t cursor_y = kLightmapGutter;
            std::uint32_t row_height = 0;
            bool fits = true;
            for (std::size_t index = 0; index < placements.size(); ++index) {
                if (placements[index].surface < 0) continue;
                const auto& light_map = topology.light_maps[index];
                const auto width = static_cast<std::uint32_t>(
                    light_map.u_clamp) + kLightmapGutter * 2U;
                const auto height = static_cast<std::uint32_t>(
                    light_map.v_clamp) + kLightmapGutter * 2U;
                if (cursor_x + width > atlas_size) {
                    cursor_x = kLightmapGutter;
                    cursor_y += row_height;
                    row_height = 0;
                }
                if (cursor_y + height > atlas_size) {
                    fits = false;
                    break;
                }
                placements[index].x = cursor_x + kLightmapGutter;
                placements[index].y = cursor_y + kLightmapGutter;
                cursor_x += width;
                row_height = std::max(row_height, height);
            }
            if (fits) {
                result.lightmap_width = atlas_size;
                result.lightmap_height = atlas_size;
                break;
            }
        }
        if (result.lightmap_width == 0) {
            result.error = "BSP light maps do not fit the 4096 atlas cap";
            return result;
        }
        result.lightmap_rgba8.assign(
            static_cast<std::size_t>(result.lightmap_width) *
                result.lightmap_height * 4U,
            0U);

        struct Material {
            bool masked=false;
            std::uint32_t layer{};
            std::uint32_t width{1};
            std::uint32_t height{1};
            bool fallback{true};
            std::vector<std::uint8_t> rgba8;
        };
        std::map<std::int32_t, Material> materials;
        for (std::size_t index = 0; index < selected_count; ++index) {
            const auto& triangle = mesh.triangles[index];
            const auto& surface = topology.surfaces[triangle.surface_index];
            auto& material=materials[surface.texture_reference];
            material.masked=material.masked||(surface.polygon_flags&2U)!=0;
        }

        std::uint32_t next_layer = 1;
        for (auto& [reference, material] : materials) {
            if (reference >= 0) {
                continue;
            }
            const auto imported = imports.find(reference);
            if (imported == imports.end()) {
                result.error = "BSP texture import is absent from the linked map";
                return result;
            }
            const auto& target = *imported->second;
            if (target.target_kind != Hp1ImportTargetKind::export_object ||
                target.target_reference <= 0 ||
                !ascii_equal_fold(target.qualified_class_name,
                                  "Engine.Texture")) {
                continue;
            }
            const auto path = package_paths.find(
                ascii_fold(target.target_package));
            if (path == package_paths.end()) {
                result.error = "linked BSP texture package has no file path";
                return result;
            }
            auto texture = load_hp1_p8_texture(
                path->second, target.target_reference,material.masked);
            if (texture.status != Hp1ProfileStatus::ok ||
                texture.mips.empty()) {
                result.status = texture.status;
                result.error = "P8 texture decode failed for " +
                               target.target_package + ": " + texture.error;
                return result;
            }
            if (next_layer == std::numeric_limits<std::uint32_t>::max()) {
                result.error = "texture layer count exceeds uint32";
                return result;
            }
            material.layer = next_layer++;
            if(result.texture_layer_names.size()<=material.layer)result.texture_layer_names.resize(material.layer+1);
            if(!target.target_object_path.empty())result.texture_layer_names[material.layer]=ascii_fold(target.target_object_path.back());
            material.width = texture.mips.front().width;
            material.height = texture.mips.front().height;
            material.fallback = false;
            material.rgba8 = std::move(texture.rgba8);
            result.texture_layer_width = std::max(
                result.texture_layer_width, material.width);
            result.texture_layer_height = std::max(
                result.texture_layer_height, material.height);
            ++result.decoded_texture_count;
        }
        result.texture_layer_width = std::max(
            result.texture_layer_width, 1U);
        result.texture_layer_height = std::max(
            result.texture_layer_height, 1U);
        result.texture_layer_count = next_layer;
        result.fallback_material_count = static_cast<std::size_t>(
            std::ranges::count_if(materials, [](const auto& entry) {
                return entry.second.fallback;
            }));

        const std::size_t layer_pixels =
            static_cast<std::size_t>(result.texture_layer_width) *
            result.texture_layer_height;
        if (layer_pixels > std::numeric_limits<std::size_t>::max() / 4U ||
            layer_pixels * 4U >
                std::numeric_limits<std::size_t>::max() /
                    result.texture_layer_count) {
            result.error = "texture array byte size overflowed";
            return result;
        }
        result.texture_rgba8.resize(
            layer_pixels * 4U * result.texture_layer_count);
        const auto write_pixel = [&](std::uint32_t layer,
                                     std::uint32_t x,
                                     std::uint32_t y,
                                     const std::uint8_t* rgba) {
            const auto pixel =
                (static_cast<std::size_t>(layer) * layer_pixels +
                 static_cast<std::size_t>(y) * result.texture_layer_width + x) *
                4U;
            std::copy_n(rgba, 4, result.texture_rgba8.begin() + pixel);
        };
        for (std::uint32_t y = 0; y < result.texture_layer_height; ++y) {
            for (std::uint32_t x = 0; x < result.texture_layer_width; ++x) {
                const bool bright = ((x / 16U) ^ (y / 16U)) % 2U != 0;
                const std::array<std::uint8_t, 4> diagnostic{
                    static_cast<std::uint8_t>(bright ? 255 : 24),
                    static_cast<std::uint8_t>(bright ? 32 : 0),
                    static_cast<std::uint8_t>(bright ? 220 : 24),
                    255,
                };
                write_pixel(0, x, y, diagnostic.data());
            }
        }
        for (const auto& [reference, material] : materials) {
            static_cast<void>(reference);
            if (material.fallback) {
                continue;
            }
            for (std::uint32_t y = 0; y < result.texture_layer_height; ++y) {
                for (std::uint32_t x = 0; x < result.texture_layer_width; ++x) {
                    const auto source =
                        (static_cast<std::size_t>(y % material.height) *
                             material.width +
                         x % material.width) *
                        4U;
                    write_pixel(material.layer, x, y,
                                material.rgba8.data() + source);
                }
            }
        }

        const auto write_lightmap_pixel =
            [&result](std::uint32_t x, std::uint32_t y,
                      const std::array<std::uint8_t, 4>& rgba) {
                const auto offset =
                    (static_cast<std::size_t>(y) * result.lightmap_width + x) *
                    4U;
                std::copy(rgba.begin(), rgba.end(),
                          result.lightmap_rgba8.begin() + offset);
            };
        for (std::size_t map_index = 0; map_index < placements.size();
             ++map_index) {
            const auto& placement = placements[map_index];
            if (placement.surface < 0) continue;
            const auto& light_map = topology.light_maps[map_index];
            const auto& surface = topology.surfaces[
                static_cast<std::size_t>(placement.surface)];
            const auto& base = topology.points[
                static_cast<std::size_t>(surface.base_point_index)];
            const auto& texture_u = topology.vectors[
                static_cast<std::size_t>(surface.texture_u_vector_index)];
            const auto& texture_v = topology.vectors[
                static_cast<std::size_t>(surface.texture_v_vector_index)];
            const auto& normal = topology.vectors[
                static_cast<std::size_t>(surface.normal_vector_index)];
            const float normal_length = std::sqrt(std::max(dot(normal, normal),
                                                            1.0e-12F));
            const Hp1BspVector unit_normal{
                normal.x / normal_length,
                normal.y / normal_length,
                normal.z / normal_length};
            const float base_u = dot(texture_u, base) + light_map.pan.x -
                                 0.5F * light_map.u_scale;
            const float base_v = dot(texture_v, base) + light_map.pan.y -
                                 0.5F * light_map.v_scale;
            const float plane = dot(normal, base);
            const std::size_t width =
                static_cast<std::size_t>(light_map.u_clamp);
            const std::size_t height =
                static_cast<std::size_t>(light_map.v_clamp);
            const std::size_t pitch = (width + 7U) / 8U;
            const std::size_t bytes_per_light = pitch * height;
            std::vector<std::array<std::uint8_t, 4>> pixels(width * height);

            const auto visibility = [&](std::size_t light_ordinal,
                                        int x, int y) {
                static constexpr int kernel[3][3] = {
                    {1, 2, 1}, {2, 4, 2}, {1, 2, 1}};
                int sum = 0;
                for (int ky = -1; ky <= 1; ++ky) {
                    for (int kx = -1; kx <= 1; ++kx) {
                        const int sx = std::clamp(x + kx, 0,
                                                 light_map.u_clamp - 1);
                        const int sy = std::clamp(y + ky, 0,
                                                 light_map.v_clamp - 1);
                        const auto byte = static_cast<std::size_t>(
                            light_map.data_offset) +
                            light_ordinal * bytes_per_light +
                            static_cast<std::size_t>(sy) * pitch +
                            static_cast<std::size_t>(sx >> 3);
                        if ((topology.light_bits[byte] &
                             (1U << static_cast<unsigned>(sx & 7))) != 0) {
                            sum += kernel[ky + 1][kx + 1];
                        }
                    }
                }
                return static_cast<float>(sum) / 16.0F;
            };
            for (std::size_t y = 0; y < height; ++y) {
                for (std::size_t x = 0; x < width; ++x) {
                    std::array<float, 3> lighting{0.085F, 0.080F, 0.070F};
                    const auto world = solve_plane_coordinates(
                        texture_u, texture_v, normal,
                        base_u + (static_cast<float>(x) + 0.5F) *
                                     light_map.u_scale,
                        base_v + (static_cast<float>(y) + 0.5F) *
                                     light_map.v_scale,
                        plane);
                    if (world.has_value() &&
                        light_map.light_actor_index >= 0) {
                        std::size_t ordinal = 0;
                        for (std::size_t reference_index =
                                 static_cast<std::size_t>(
                                     light_map.light_actor_index);
                             reference_index <
                                 topology.light_references.size() &&
                             topology.light_references[reference_index] != 0;
                             ++reference_index, ++ordinal) {
                            const auto found = light_actors.find(
                                topology.light_references[reference_index]);
                            if (found == light_actors.end()) continue;
                            const auto& actor = *found->second;
                            const float dx = actor.location_unreal.x - world->x;
                            const float dy = actor.location_unreal.y - world->y;
                            const float dz = actor.location_unreal.z - world->z;
                            const float distance_squared =
                                dx * dx + dy * dy + dz * dz;
                            const float radius =
                                static_cast<float>(actor.light_radius) * 25.0F;
                            if (!(distance_squared < radius * radius)) continue;
                            const float distance = std::sqrt(
                                std::max(distance_squared, 1.0e-8F));
                            const float ratio = distance / radius;
                            const float smooth = std::min(
                                (1.0F + 2.0F * ratio * ratio * ratio -
                                 3.0F * ratio * ratio) /
                                    std::max(ratio, 1.0e-4F),
                                1.0F);
                            const float angle = std::abs(
                                (dx * unit_normal.x + dy * unit_normal.y +
                                 dz * unit_normal.z) /
                                distance);
                            const float amount = visibility(
                                ordinal, static_cast<int>(x),
                                static_cast<int>(y)) *
                                std::max(smooth, 0.0F) * angle *
                                (static_cast<float>(actor.light_brightness) /
                                 255.0F) * 1.55F;
                            const auto color = hsb_light_color(
                                actor.light_hue, actor.light_saturation);
                            for (std::size_t channel = 0; channel < 3;
                                 ++channel) {
                                lighting[channel] += color[channel] * amount;
                            }
                        }
                    }
                    pixels[y * width + x] = {
                        encode_linear(lighting[0]),
                        encode_linear(lighting[1]),
                        encode_linear(lighting[2]), 255};
                }
            }
            for (int y = -1; y <= light_map.v_clamp; ++y) {
                for (int x = -1; x <= light_map.u_clamp; ++x) {
                    const auto source_x = static_cast<std::size_t>(
                        std::clamp(x, 0, light_map.u_clamp - 1));
                    const auto source_y = static_cast<std::size_t>(
                        std::clamp(y, 0, light_map.v_clamp - 1));
                    write_lightmap_pixel(
                        static_cast<std::uint32_t>(
                            static_cast<int>(placement.x) + x),
                        static_cast<std::uint32_t>(
                            static_cast<int>(placement.y) + y),
                        pixels[source_y * width + source_x]);
                }
            }
            ++result.decoded_lightmap_count;
            result.lightmap_texel_count += width * height;
            if (light_map.light_actor_index >= 0) {
                for (std::size_t index = static_cast<std::size_t>(
                         light_map.light_actor_index);
                     index < topology.light_references.size() &&
                     topology.light_references[index] != 0; ++index) {
                    result.lightmap_light_count +=
                        light_actors.contains(
                            topology.light_references[index])
                            ? 1U
                            : 0U;
                }
            }
        }

        result.vertices.reserve(selected_count * 3U);
        for (std::size_t index = 0; index < selected_count; ++index) {
            const auto& triangle = mesh.triangles[index];
            const auto& surface = topology.surfaces[triangle.surface_index];
            const auto material = materials.find(surface.texture_reference);
            if (material == materials.end()) {
                result.error = "selected BSP material disappeared";
                return result;
            }
            if (material->second.fallback) {
                ++result.fallback_triangle_count;
            }
            const bool has_lightmap = surface.light_map_index >= 0 &&
                placements[static_cast<std::size_t>(surface.light_map_index)]
                        .surface >= 0;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                std::array<float, 2> lightmap_uv{};
                if (has_lightmap) {
                    const auto& light_map = topology.light_maps[
                        static_cast<std::size_t>(surface.light_map_index)];
                    const auto& placement = placements[
                        static_cast<std::size_t>(surface.light_map_index)];
                    const float u =
                        (triangle.texel_uv[corner][0] - surface.pan_u -
                         light_map.pan.x + 0.5F * light_map.u_scale) /
                        light_map.u_scale;
                    const float v =
                        (triangle.texel_uv[corner][1] - surface.pan_v -
                         light_map.pan.y + 0.5F * light_map.v_scale) /
                        light_map.v_scale;
                    lightmap_uv = {
                        (static_cast<float>(placement.x) + u) /
                            static_cast<float>(result.lightmap_width),
                        (static_cast<float>(placement.y) + v) /
                            static_cast<float>(result.lightmap_height)};
                }
                result.vertices.push_back({
                    triangle.positions_m[corner],
                    triangle.normal,
                    {
                        triangle.texel_uv[corner][0] /
                            static_cast<float>(result.texture_layer_width),
                        triangle.texel_uv[corner][1] /
                            static_cast<float>(result.texture_layer_height),
                    },
                    lightmap_uv,
                    material->second.layer,
                    surface.polygon_flags|((material->second.masked&&(surface.polygon_flags&8U))?2U:0U),
                    has_lightmap ? 1U : 0U,
                    triangle.node_index,
                    triangle.surface_index,
                });
            }
        }
        if (std::ranges::any_of(result.vertices, [](const auto& vertex) {
                return !std::isfinite(vertex.texture_uv[0]) ||
                       !std::isfinite(vertex.texture_uv[1]) ||
                       !std::isfinite(vertex.lightmap_uv[0]) ||
                       !std::isfinite(vertex.lightmap_uv[1]);
            })) {
            result.error = "textured BSP contains a non-finite normalized UV";
            return result;
        }
        result.status = Hp1ProfileStatus::ok;
    } catch (const std::bad_alloc&) {
        result = {};
        result.error = "allocation failed while building textured BSP scene";
    } catch (const std::exception& exception) {
        result = {};
        result.error = exception.what();
    }
    return result;
}

}  // namespace hpvr::wand
