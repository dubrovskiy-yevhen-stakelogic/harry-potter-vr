#include "hpvr/hp1_gesture.h"
#include "hpvr/hp1_gesture_c.h"
#include "hpvr/hp1_package_graph.h"
#include "hpvr/hp1_package_linker.h"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using hpvr::wand::Hp1GestureScoreStatus;
using hpvr::wand::Hp1PackageGraphStatus;
using hpvr::wand::Hp1PackageLinkStatus;
using hpvr::wand::Hp1ProfileStatus;
using hpvr::wand::Hp1ImportTargetKind;
using hpvr::wand::Hp1ResolvedPackageKind;
using hpvr::wand::Vec2;

[[noreturn]] void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        fail(message);
    }
}

void expect_near(float actual, float expected, std::string_view message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 1.0e-5F) {
        std::cerr << "FAIL: " << message << " (actual=" << actual
                  << ", expected=" << expected << ")\n";
        std::exit(EXIT_FAILURE);
    }
}

void append_u8(Bytes& bytes, std::uint8_t value) {
    bytes.push_back(value);
}

void append_u16(Bytes& bytes, std::uint16_t value) {
    append_u8(bytes, static_cast<std::uint8_t>(value));
    append_u8(bytes, static_cast<std::uint8_t>(value >> 8U));
}

void append_u32(Bytes& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        append_u8(bytes, static_cast<std::uint8_t>(value >> shift));
    }
}

void append_i32(Bytes& bytes, std::int32_t value) {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

void append_u64(Bytes& bytes, std::uint64_t value) {
    append_u32(bytes, static_cast<std::uint32_t>(value));
    append_u32(bytes, static_cast<std::uint32_t>(value >> 32U));
}

void append_f32(Bytes& bytes, float value) {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

void append_compact(Bytes& bytes, std::int32_t signed_value) {
    const bool negative = signed_value < 0;
    std::uint32_t value = negative
                              ? static_cast<std::uint32_t>(
                                    -static_cast<std::int64_t>(signed_value))
                              : static_cast<std::uint32_t>(signed_value);
    std::uint8_t first = static_cast<std::uint8_t>(value & 0x3FU);
    value >>= 6U;
    if (negative) {
        first |= 0x80U;
    }
    if (value != 0) {
        first |= 0x40U;
    }
    append_u8(bytes, first);
    while (value != 0) {
        std::uint8_t next = static_cast<std::uint8_t>(value & 0x7FU);
        value >>= 7U;
        if (value != 0) {
            next |= 0x80U;
        }
        append_u8(bytes, next);
    }
}

void patch_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    expect(offset + 4 <= bytes.size(), "header patch is in range");
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes[offset + shift / 8] =
            static_cast<std::uint8_t>(value >> shift);
    }
}

void append_name(Bytes& bytes, std::string_view value) {
    append_compact(bytes, static_cast<std::int32_t>(value.size() + 1));
    bytes.insert(bytes.end(), value.begin(), value.end());
    append_u8(bytes, 0);
    append_u32(bytes, 0);
}

void append_legacy_name(Bytes& bytes, std::string_view value) {
    bytes.insert(bytes.end(), value.begin(), value.end());
    append_u8(bytes, 0);
    append_u32(bytes, 0);
}

void append_name_for_version(Bytes& bytes,
                             std::string_view value,
                             std::uint16_t package_version) {
    if (package_version >= 64) {
        append_name(bytes, value);
    } else {
        append_legacy_name(bytes, value);
    }
}

void append_float_tag(Bytes& bytes,
                      std::int32_t name,
                      float value,
                      int array_index = -1) {
    append_compact(bytes, name);
    append_u8(bytes,
              static_cast<std::uint8_t>(0x24U |
                                        (array_index >= 0 ? 0x80U : 0U)));
    if (array_index >= 0) {
        expect(array_index < 128, "synthetic array index fits one byte");
        append_u8(bytes, static_cast<std::uint8_t>(array_index));
    }
    append_f32(bytes, value);
}

void append_array_tag(Bytes& bytes,
                      std::int32_t name,
                      const Bytes& value) {
    expect(value.size() < 256, "synthetic array payload fits one-byte size");
    append_compact(bytes, name);
    append_u8(bytes, 0x59U);
    append_u8(bytes, static_cast<std::uint8_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

Bytes make_header(std::uint16_t package_version = 76) {
    Bytes bytes;
    append_u32(bytes, 0x9E2A83C1U);
    append_u16(bytes, package_version);
    append_u16(bytes, 0);
    append_u32(bytes, 0);
    for (int index = 0; index < 6; ++index) {
        append_u32(bytes, 0);
    }
    expect(bytes.size() == 36, "synthetic package header size");
    return bytes;
}

void finish_header(Bytes& bytes,
                   std::uint32_t name_count,
                   std::uint32_t name_offset,
                   std::uint32_t export_count,
                   std::uint32_t export_offset,
                   std::uint32_t import_count,
                   std::uint32_t import_offset) {
    patch_u32(bytes, 12, name_count);
    patch_u32(bytes, 16, name_offset);
    patch_u32(bytes, 20, export_count);
    patch_u32(bytes, 24, export_offset);
    patch_u32(bytes, 28, import_count);
    patch_u32(bytes, 32, import_offset);
}

void append_import(Bytes& bytes,
                   std::int32_t class_package,
                   std::int32_t class_name,
                   std::int32_t object_name,
                   std::int32_t outer = 0) {
    append_compact(bytes, class_package);
    append_compact(bytes, class_name);
    append_i32(bytes, outer);
    append_compact(bytes, object_name);
}

void append_export(Bytes& bytes,
                   std::int32_t class_reference,
                   std::int32_t object_name,
                   std::uint32_t object_flags,
                   std::size_t serial_offset,
                   std::size_t serial_size) {
    append_compact(bytes, class_reference);
    append_compact(bytes, 0);
    append_i32(bytes, 0);
    append_compact(bytes, object_name);
    append_u32(bytes, object_flags);
    append_compact(bytes, static_cast<std::int32_t>(serial_size));
    if (serial_size != 0) {
        append_compact(bytes, static_cast<std::int32_t>(serial_offset));
    }
}

Bytes make_base_package(std::uint16_t package_version = 76) {
    enum Name : std::int32_t {
        none,
        gesture,
        flip_pattern,
        spell_learn_trigger,
        points,
        segments,
        accuracy,
        very_good,
        very_bad,
        draw_time,
        pass_mark,
        core,
        class_object,
        package_object,
        engine,
    };
    constexpr std::array names{
        "None", "Gesture", "FlipPattern", "SpellLearnTrigger", "Points",
        "Segments", "fAccuracy", "fVGoodThreshold", "fVBadThreshold",
        "DrawTime", "PassMark", "Core", "Class", "Package", "Engine",
    };

    Bytes bytes = make_header(package_version);
    const auto name_offset = bytes.size();
    for (const std::string_view name : names) {
        append_name_for_version(bytes, name, package_version);
    }
    const auto import_offset = bytes.size();
    append_import(bytes, core, package_object, engine);
    append_import(bytes, core, class_object, gesture, -1);

    Bytes gesture_payload;
    Bytes point_array;
    append_compact(point_array, 3);
    for (const Vec2 point : std::array<Vec2, 3>{
             Vec2{0.15F, 0.20F}, Vec2{0.65F, 0.35F}, Vec2{0.80F, 0.90F}}) {
        append_f32(point_array, point.x);
        append_f32(point_array, point.y);
        append_f32(point_array, 7.0F);
    }
    append_array_tag(gesture_payload, points, point_array);
    Bytes segment_array;
    append_compact(segment_array, 4);
    for (const std::int32_t value : std::array{0, 1, 1, 2}) {
        append_i32(segment_array, value);
    }
    append_array_tag(gesture_payload, segments, segment_array);
    append_compact(gesture_payload, none);
    const auto gesture_offset = bytes.size();
    bytes.insert(bytes.end(), gesture_payload.begin(), gesture_payload.end());

    Bytes class_payload;
    for (int index = 0; index < 4; ++index) {
        append_compact(class_payload, 0);
    }
    append_compact(class_payload, none);
    append_u32(class_payload, 0);
    append_u32(class_payload, 0);
    append_u32(class_payload, 0);
    append_u64(class_payload, 0);
    append_u64(class_payload, 0);
    append_u16(class_payload, 0);
    append_u32(class_payload, 0);
    append_u32(class_payload, 0);
    class_payload.insert(class_payload.end(), 16, 0);
    append_compact(class_payload, 0);
    append_compact(class_payload, 0);
    append_compact(class_payload, 0);
    append_compact(class_payload, none);
    append_float_tag(class_payload, accuracy, 0.125F);
    append_float_tag(class_payload, very_good, 0.875F);
    append_float_tag(class_payload, very_bad, 0.375F);
    append_float_tag(class_payload, draw_time, 5.5F);
    for (int index = 0; index < 10; ++index) {
        append_float_tag(class_payload,
                         pass_mark,
                         0.05F + static_cast<float>(index) * 0.075F,
                         index);
    }
    append_compact(class_payload, none);
    const auto class_offset = bytes.size();
    bytes.insert(bytes.end(), class_payload.begin(), class_payload.end());

    const auto export_offset = bytes.size();
    append_export(bytes, -2, flip_pattern, 0, gesture_offset,
                  gesture_payload.size());
    append_export(bytes, 0, spell_learn_trigger, 0, class_offset,
                  class_payload.size());
    finish_header(bytes,
                  static_cast<std::uint32_t>(names.size()),
                  static_cast<std::uint32_t>(name_offset),
                  2,
                  static_cast<std::uint32_t>(export_offset),
                  2,
                  static_cast<std::uint32_t>(import_offset));
    return bytes;
}

Bytes make_lesson_package(std::uint16_t package_version = 76) {
    enum Name : std::int32_t {
        none,
        spell_learn_trigger,
        actor,
        spell,
        spell_flip,
        draw_time,
        core,
        class_object,
        package_object,
        hpbase,
    };
    constexpr std::array names{
        "None", "SpellLearnTrigger", "SpellLearnTrigger0", "Spell",
        "spellFlip", "DrawTime", "Core", "Class", "Package", "HPBase",
    };

    Bytes bytes = make_header(package_version);
    const auto name_offset = bytes.size();
    for (const std::string_view name : names) {
        append_name_for_version(bytes, name, package_version);
    }
    const auto import_offset = bytes.size();
    append_import(bytes, core, package_object, hpbase);
    append_import(bytes, core, class_object, spell_learn_trigger, -1);
    append_import(bytes, core, class_object, spell_flip, -1);

    Bytes payload;
    append_compact(payload, 0);
    append_compact(payload, 0);
    append_u64(payload, 0);
    append_i32(payload, 0);
    append_compact(payload, spell);
    append_u8(payload, 0x05U);
    append_compact(payload, -3);
    append_float_tag(payload, draw_time, 11.25F);
    append_compact(payload, none);
    const auto payload_offset = bytes.size();
    bytes.insert(bytes.end(), payload.begin(), payload.end());

    const auto export_offset = bytes.size();
    append_export(bytes, -2, actor, 0x02000000U, payload_offset,
                  payload.size());
    finish_header(bytes,
                  static_cast<std::uint32_t>(names.size()),
                  static_cast<std::uint32_t>(name_offset),
                  1,
                  static_cast<std::uint32_t>(export_offset),
                  3,
                  static_cast<std::uint32_t>(import_offset));
    return bytes;
}

Bytes make_dependency_package(std::string_view dependency,
                              std::uint16_t package_version = 76) {
    const std::array<std::string_view, 4> names{
        "None", "Core", "Package", dependency,
    };
    Bytes bytes = make_header(package_version);
    const auto name_offset = bytes.size();
    for (const auto name : names) {
        append_name_for_version(bytes, name, package_version);
    }
    const auto import_offset = bytes.size();
    append_import(bytes, 1, 2, 3);
    const auto export_offset = bytes.size();
    finish_header(bytes,
                  static_cast<std::uint32_t>(names.size()),
                  static_cast<std::uint32_t>(name_offset),
                  0,
                  static_cast<std::uint32_t>(export_offset),
                  1,
                  static_cast<std::uint32_t>(import_offset));
    return bytes;
}

Bytes make_empty_package(std::uint16_t package_version = 76) {
    Bytes bytes = make_header(package_version);
    const auto name_offset = bytes.size();
    append_name_for_version(bytes, "None", package_version);
    const auto table_offset = bytes.size();
    finish_header(bytes,
                  1,
                  static_cast<std::uint32_t>(name_offset),
                  0,
                  static_cast<std::uint32_t>(table_offset),
                  0,
                  static_cast<std::uint32_t>(table_offset));
    return bytes;
}

Bytes make_object_import_package(std::string_view target_package,
                                 std::string_view target_object,
                                 std::uint16_t package_version = 76) {
    const std::array<std::string_view, 6> names{
        "None", "Core", "Package", "Class", target_package, target_object,
    };
    Bytes bytes = make_header(package_version);
    const auto name_offset = bytes.size();
    for (const auto name : names) {
        append_name_for_version(bytes, name, package_version);
    }
    const auto import_offset = bytes.size();
    append_import(bytes, 1, 2, 4);
    append_import(bytes, 1, 3, 5, -1);
    const auto export_offset = bytes.size();
    finish_header(bytes,
                  static_cast<std::uint32_t>(names.size()),
                  static_cast<std::uint32_t>(name_offset),
                  0,
                  static_cast<std::uint32_t>(export_offset),
                  2,
                  static_cast<std::uint32_t>(import_offset));
    return bytes;
}

Bytes make_group_import_package(std::string_view target_package,
                                std::string_view group_name,
                                std::uint16_t package_version = 76) {
    const std::array<std::string_view, 5> names{
        "None", "Core", "Package", target_package, group_name,
    };
    Bytes bytes = make_header(package_version);
    const auto name_offset = bytes.size();
    for (const auto name : names) {
        append_name_for_version(bytes, name, package_version);
    }
    const auto import_offset = bytes.size();
    append_import(bytes, 1, 2, 3);
    append_import(bytes, 1, 2, 4, -1);
    const auto export_offset = bytes.size();
    finish_header(bytes,
                  static_cast<std::uint32_t>(names.size()),
                  static_cast<std::uint32_t>(name_offset),
                  0,
                  static_cast<std::uint32_t>(export_offset),
                  2,
                  static_cast<std::uint32_t>(import_offset));
    return bytes;
}

Bytes make_grouped_object_import_package(
    std::string_view target_package,
    std::string_view group_name,
    std::string_view object_name,
    std::uint16_t package_version = 76) {
    const std::array<std::string_view, 7> names{
        "None", "Core", "Package", "Class", target_package, group_name,
        object_name,
    };
    Bytes bytes = make_header(package_version);
    const auto name_offset = bytes.size();
    for (const auto name : names) {
        append_name_for_version(bytes, name, package_version);
    }
    const auto import_offset = bytes.size();
    append_import(bytes, 1, 2, 4);
    append_import(bytes, 1, 2, 5, -1);
    append_import(bytes, 1, 2, 5, -2);
    append_import(bytes, 1, 3, 6, -3);
    const auto export_offset = bytes.size();
    finish_header(bytes,
                  static_cast<std::uint32_t>(names.size()),
                  static_cast<std::uint32_t>(name_offset),
                  0,
                  static_cast<std::uint32_t>(export_offset),
                  4,
                  static_cast<std::uint32_t>(import_offset));
    return bytes;
}

Bytes make_grouped_class_export_package(
    std::string_view group_name,
    std::string_view object_name,
    std::uint16_t package_version = 76) {
    const std::array<std::string_view, 3> names{
        "None", group_name, object_name,
    };
    Bytes bytes = make_header(package_version);
    const auto name_offset = bytes.size();
    for (const auto name : names) {
        append_name_for_version(bytes, name, package_version);
    }
    const auto table_offset = bytes.size();
    append_export(bytes, 0, 1, 0, 0, 0);
    append_compact(bytes, 0);
    append_compact(bytes, 0);
    append_i32(bytes, 1);
    append_compact(bytes, 2);
    append_u32(bytes, 0);
    append_compact(bytes, 0);
    finish_header(bytes,
                  static_cast<std::uint32_t>(names.size()),
                  static_cast<std::uint32_t>(name_offset),
                  2,
                  static_cast<std::uint32_t>(table_offset),
                  0,
                  static_cast<std::uint32_t>(table_offset));
    return bytes;
}

Bytes make_class_export_package(std::string_view object_name,
                                std::size_t copies = 1,
                                std::uint16_t package_version = 76) {
    const std::array<std::string_view, 2> names{"None", object_name};
    Bytes bytes = make_header(package_version);
    const auto name_offset = bytes.size();
    for (const auto name : names) {
        append_name_for_version(bytes, name, package_version);
    }
    const auto table_offset = bytes.size();
    for (std::size_t index = 0; index < copies; ++index) {
        append_export(bytes, 0, 1, 0, 0, 0);
    }
    finish_header(bytes,
                  static_cast<std::uint32_t>(names.size()),
                  static_cast<std::uint32_t>(name_offset),
                  static_cast<std::uint32_t>(copies),
                  static_cast<std::uint32_t>(table_offset),
                  0,
                  static_cast<std::uint32_t>(table_offset));
    return bytes;
}

Bytes make_p8_texture_package(std::uint16_t package_version = 76, int wet_source = 0) {
    enum Name : std::int32_t {
        none,
        core,
        package_object,
        class_object,
        engine,
        texture_class,
        palette_name,
        format_name,
        texture_name,
        palette_object,
        fire,
        wet_class,
        source_name,
        wet_name,
    };
    constexpr std::array names{
        "None", "Core", "Package", "Class", "Engine", "Texture",
        "Palette", "Format", "SyntheticTexture", "SyntheticPalette",
        "Fire", "WetTexture", "SourceTexture", "SyntheticWater",
    };
    Bytes bytes = make_header(package_version);
    const auto name_offset = bytes.size();
    for (const std::string_view name : names) {
        append_name_for_version(bytes, name, package_version);
    }
    const auto import_offset = bytes.size();
    append_import(bytes, core, package_object, engine);
    append_import(bytes, core, class_object, texture_class, -1);
    append_import(bytes, core, class_object, palette_name, -1);
    append_import(bytes, core, package_object, fire);
    append_import(bytes, core, class_object, wet_class, -4);

    Bytes palette_payload;
    append_compact(palette_payload, none);
    append_compact(palette_payload, 256);
    for (int index = 0; index < 256; ++index) {
        append_u8(palette_payload, static_cast<std::uint8_t>(index));
        append_u8(palette_payload, static_cast<std::uint8_t>(index + 1));
        append_u8(palette_payload, static_cast<std::uint8_t>(index + 2));
        append_u8(palette_payload, static_cast<std::uint8_t>(255 - index));
    }
    const auto palette_offset = bytes.size();
    bytes.insert(bytes.end(), palette_payload.begin(), palette_payload.end());

    Bytes texture_payload;
    append_compact(texture_payload, format_name);
    append_u8(texture_payload, 0x01U);
    append_u8(texture_payload, 0);
    append_compact(texture_payload, palette_name);
    append_u8(texture_payload, 0x05U);
    append_compact(texture_payload, 2);
    append_compact(texture_payload, none);
    append_compact(texture_payload, 1);
    const auto skip_offset_patch = texture_payload.size();
    append_u32(texture_payload, 0);
    append_compact(texture_payload, 4);
    for (const std::uint8_t index : std::array<std::uint8_t, 4>{0, 1, 2, 3}) {
        append_u8(texture_payload, index);
    }
    const auto after_mip_data = texture_payload.size();
    append_i32(texture_payload, 2);
    append_i32(texture_payload, 2);
    append_u8(texture_payload, 1);
    append_u8(texture_payload, 1);
    const auto texture_offset = bytes.size();
    patch_u32(texture_payload,
              skip_offset_patch,
              static_cast<std::uint32_t>(
                  texture_offset + after_mip_data));
    bytes.insert(bytes.end(), texture_payload.begin(), texture_payload.end());

    Bytes wet_payload;
    append_compact(wet_payload, source_name);
    append_u8(wet_payload, 0x05U);
    append_compact(wet_payload, wet_source);
    append_compact(wet_payload, none);
    const auto wet_offset=bytes.size();
    if(wet_source)bytes.insert(bytes.end(),wet_payload.begin(),wet_payload.end());
    const auto export_offset = bytes.size();
    append_export(bytes, -2, texture_name, 0, texture_offset,
                  texture_payload.size());
    append_export(bytes, -3, palette_object, 0, palette_offset,
                  palette_payload.size());
    if(wet_source)append_export(bytes,-5,wet_name,0,wet_offset,wet_payload.size());
    finish_header(bytes,
                  static_cast<std::uint32_t>(names.size()),
                  static_cast<std::uint32_t>(name_offset),
                  wet_source ? 3 : 2,
                  static_cast<std::uint32_t>(export_offset),
                  5,
                  static_cast<std::uint32_t>(import_offset));
    return bytes;
}

void append_serialized_string(Bytes& bytes, std::string_view value) {
    append_compact(bytes, static_cast<std::int32_t>(value.size() + 1));
    bytes.insert(bytes.end(), value.begin(), value.end());
    append_u8(bytes, 0);
}

Bytes make_level_package(bool imported_actor = false,
                         bool wrong_model_class = false,
                         std::uint16_t package_version = 76,
                         bool truncate_model = false,
                         bool invalid_active_vertex = false,
                         bool include_player_start = false,
                         const Bytes& actor_properties = {}) {
    enum Name : std::int32_t {
        none,
        core,
        package_object,
        class_object,
        engine,
        level_class,
        model_class,
        actor_class,
        polys_class,
        level_name,
        model_name,
        actor_zero,
        actor_one,
        polys_name,
        player_start_class,
        location,
        rotation,
        vector_struct,
        rotator_struct,
        player_start_name,
    };
    constexpr std::array names{
        "None", "Core", "Package", "Class", "Engine", "Level",
        "Model", "Actor", "Polys", "SyntheticLevel", "SyntheticModel",
        "Actor0", "Actor1", "SyntheticPolys", "PlayerStart", "Location",
        "Rotation", "Vector", "Rotator", "PlayerStart0",
        "CutCast", "CutLoc", "cast", "Locs",
    };
    Bytes bytes = make_header(package_version);
    const auto name_offset = bytes.size();
    for (const std::string_view name : names) {
        append_name_for_version(bytes, name, package_version);
    }
    const auto import_offset = bytes.size();
    append_import(bytes, core, package_object, engine);
    append_import(bytes, core, class_object, level_class, -1);
    append_import(bytes, core, class_object, model_class, -1);
    append_import(bytes, core, class_object, actor_class, -1);
    append_import(bytes, core, class_object, polys_class, -1);
    append_import(bytes, core, class_object, player_start_class, -1);

    Bytes payload;
    append_compact(payload, none);
    append_i32(payload, include_player_start ? 5 : 4);
    append_i32(payload, include_player_start ? 5 : 4);
    append_compact(payload, imported_actor ? -4 : 3);
    append_compact(payload, 0);
    append_compact(payload, 4);
    if (include_player_start) {
        append_compact(payload, 6);
    }
    append_compact(payload, 0);
    append_serialized_string(payload, "unreal");
    append_serialized_string(payload, "");
    append_serialized_string(payload, "Synthetic");
    append_serialized_string(payload, "");
    append_compact(payload, 1);
    append_serialized_string(payload, "Difficulty=1");
    append_i32(payload, 7777);
    append_i32(payload, 1);
    append_compact(payload, wrong_model_class ? 3 : 2);
    const auto payload_offset = bytes.size();
    bytes.insert(bytes.end(), payload.begin(), payload.end());

    Bytes model_payload;
    append_compact(model_payload, none);
    model_payload.insert(model_payload.end(), 41, 0);
    append_compact(model_payload, 2);
    for (const std::array<float, 3> value :
         std::array{std::array{0.0F, 0.0F, 1.0F},
                    std::array{1.0F, 0.0F, 0.0F}}) {
        for (const float component : value) {
            append_f32(model_payload, component);
        }
    }
    append_compact(model_payload, 3);
    for (const std::array<float, 3> value :
         std::array{std::array{0.0F, 0.0F, 0.0F},
                    std::array{100.0F, 0.0F, 0.0F},
                    std::array{100.0F, 100.0F, 0.0F}}) {
        for (const float component : value) {
            append_f32(model_payload, component);
        }
    }
    append_compact(model_payload, 1);
    append_f32(model_payload, 0.0F);
    append_f32(model_payload, 0.0F);
    append_f32(model_payload, 1.0F);
    append_f32(model_payload, 0.0F);
    model_payload.insert(model_payload.end(), 9, 0);
    // Vertex pool, surface, back child, front child, coplanar link,
    // deferred bound field, collision bound.
    for (const std::int32_t value :
         std::array{0, 0, -1, -1, -1, 999, 0}) {
        append_compact(model_payload, value);
    }
    append_u8(model_payload, 0);
    append_u8(model_payload, 0);
    append_u8(model_payload, 3);
    append_i32(model_payload, -1);
    append_i32(model_payload, -1);
    append_compact(model_payload, 1);
    append_compact(model_payload, 0);
    model_payload.insert(model_payload.end(), 4, 0);
    for (const std::int32_t value : std::array{0, 0, 0, 0, 0, 999}) {
        append_compact(model_payload, value);
    }
    model_payload.insert(model_payload.end(), 4, 0);
    append_compact(model_payload, 3);
    append_compact(model_payload, 4);
    for (const std::int32_t value :
         std::array{0, 0, invalid_active_vertex ? 3 : 1, 2, 2, 3, 3, 6}) {
        append_compact(model_payload, value);
    }
    append_i32(model_payload, 6);
    append_i32(model_payload, 2);
    for (int zone = 0; zone < 2; ++zone) {
        append_compact(model_payload, 0);
        model_payload.insert(model_payload.end(), 16, 0);
    }
    append_compact(model_payload, 5);
    append_compact(model_payload, 1);
    append_i32(model_payload, 0);
    append_f32(model_payload, 0.0F);
    append_f32(model_payload, 0.0F);
    append_f32(model_payload, 0.0F);
    append_compact(model_payload, 1);
    append_compact(model_payload, 1);
    append_f32(model_payload, 16.0F);
    append_f32(model_payload, 16.0F);
    append_i32(model_payload, 0);
    append_compact(model_payload, 4);
    model_payload.insert(model_payload.end(), 4, 0x5aU);
    append_compact(model_payload, 1);
    model_payload.insert(model_payload.end(), 25, 0);
    append_compact(model_payload, 3);
    model_payload.insert(model_payload.end(), 12, 0);
    append_compact(model_payload, 1);
    append_compact(model_payload, 0);
    append_compact(model_payload, 1);
    append_compact(model_payload, 2);
    model_payload.insert(model_payload.end(), 8, 0);
    append_compact(model_payload, 3);
    append_compact(model_payload, 3);
    append_compact(model_payload, 0);
    append_compact(model_payload, 4);
    append_i32(model_payload, 1);
    append_i32(model_payload, 1);
    if (truncate_model) {
        model_payload.pop_back();
    }
    const auto model_payload_offset = bytes.size();
    bytes.insert(bytes.end(), model_payload.begin(), model_payload.end());

    Bytes player_start_payload;
    if (include_player_start) {
        append_compact(player_start_payload, location);
        append_u8(player_start_payload, 0x3AU);
        append_compact(player_start_payload, vector_struct);
        append_f32(player_start_payload, 123.0F);
        append_f32(player_start_payload, -456.0F);
        append_f32(player_start_payload, 789.0F);
        append_compact(player_start_payload, rotation);
        append_u8(player_start_payload, 0x3AU);
        append_compact(player_start_payload, rotator_struct);
        append_i32(player_start_payload, 1024);
        append_i32(player_start_payload, 2048);
        append_i32(player_start_payload, -4096);
        append_compact(player_start_payload, none);
    }
    const auto player_start_payload_offset = bytes.size();
    bytes.insert(bytes.end(), player_start_payload.begin(),
                 player_start_payload.end());

    const auto actor_payload_offset = bytes.size();
    bytes.insert(bytes.end(), actor_properties.begin(), actor_properties.end());

    const auto export_offset = bytes.size();
    append_export(bytes, -2, level_name, 0, payload_offset, payload.size());
    append_export(bytes, -3, model_name, 0, model_payload_offset,
                  model_payload.size());
    append_export(bytes, -4, actor_zero, 0, actor_payload_offset,
                  actor_properties.size());
    append_export(bytes, -4, actor_one, 0, 0, 0);
    append_export(bytes, -5, polys_name, 0, 0, 0);
    if (include_player_start) {
        append_export(bytes, -6, player_start_name, 0,
                      player_start_payload_offset,
                      player_start_payload.size());
    }
    finish_header(bytes,
                  static_cast<std::uint32_t>(names.size()),
                  static_cast<std::uint32_t>(name_offset),
                  include_player_start ? 6U : 5U,
                  static_cast<std::uint32_t>(export_offset),
                  6,
                  static_cast<std::uint32_t>(import_offset));
    return bytes;
}

Bytes make_skeletal_mesh_package(bool corrupt_first_lazy_offset = false) {
    enum Name : std::int32_t {
        none, core, class_object, package_object, engine, skeletal_mesh,
        mesh_name,
    };
    constexpr std::array names{
        "None", "Core", "Class", "Package", "Engine", "SkeletalMesh",
        "SyntheticMesh",
    };
    Bytes bytes = make_header();
    const auto name_offset = bytes.size();
    for (const std::string_view name : names) append_name(bytes, name);
    const auto import_offset = bytes.size();
    append_import(bytes, core, package_object, engine);
    append_import(bytes, core, class_object, skeletal_mesh, -1);

    const auto payload_offset = bytes.size();
    Bytes payload;
    append_compact(payload, none);
    payload.insert(payload.end(), 25 + 16, 0);
    const auto append_empty_lazy = [&](bool corrupt) {
        const auto patch = payload.size();
        append_i32(payload, 0);
        append_compact(payload, 0);
        const auto end = payload_offset + payload.size() + (corrupt ? 1U : 0U);
        patch_u32(payload, patch, static_cast<std::uint32_t>(end));
    };
    append_empty_lazy(corrupt_first_lazy_offset);
    append_empty_lazy(false);
    append_compact(payload, 0);
    append_empty_lazy(false);
    payload.insert(payload.end(), 25 + 16, 0);
    append_empty_lazy(false);
    append_compact(payload, 1);
    append_compact(payload, 0);
    append_compact(payload, 0);
    append_compact(payload, 0);
    append_i32(payload, 3);
    append_i32(payload, 0);
    append_u32(payload, 0);
    append_u32(payload, 0);
    for (float value : std::array{1.0F, 1.0F, 1.0F}) append_f32(payload, value);
    for (int index = 0; index < 3; ++index) append_f32(payload, 0.0F);
    for (int index = 0; index < 5; ++index) append_i32(payload, 0);
    append_compact(payload, 0);

    append_compact(payload, 3);
    for (std::uint16_t value : std::array<std::uint16_t, 3>{0, 1, 2}) {
        append_u16(payload, value);
    }
    append_compact(payload, 1);
    append_u16(payload, 0);
    append_compact(payload, 1);
    for (std::uint16_t value : std::array<std::uint16_t, 4>{0, 1, 2, 0}) {
        append_u16(payload, value);
    }
    append_compact(payload, 3);
    for (std::uint16_t value : std::array<std::uint16_t, 3>{0, 1, 2}) {
        append_u16(payload, value);
    }
    append_compact(payload, 3);
    for (std::uint16_t vertex = 0; vertex < 3; ++vertex) {
        append_u16(payload, vertex);
        append_u8(payload, vertex == 1 ? 255 : 0);
        append_u8(payload, vertex == 2 ? 255 : 0);
    }
    append_compact(payload, 1);
    append_u32(payload, 0);
    append_i32(payload, 0);
    append_compact(payload, 0);
    append_i32(payload, 3);
    append_i32(payload, 0);
    append_f32(payload, 1.0F);
    append_f32(payload, 0.0F);
    append_f32(payload, 1.0F);
    append_i32(payload, 0);
    append_f32(payload, 0.0F);
    append_f32(payload, 0.0F);
    append_compact(payload, 0);
    append_i32(payload, 0);

    append_compact(payload, 0);
    append_compact(payload, 3);
    for (const auto& point : std::array{
             std::array{0.0F, 0.0F, 0.0F},
             std::array{1.0F, 0.0F, 0.0F},
             std::array{0.0F, 0.0F, 1.0F}}) {
        for (float value : point) append_f32(payload, value);
    }
    append_compact(payload, 1);
    append_compact(payload, none);
    append_u32(payload, 0);
    for (float value : std::array{0.0F, 0.0F, 0.0F, 1.0F}) {
        append_f32(payload, value);
    }
    for (int index = 0; index < 7; ++index) append_f32(payload, 0.0F);
    append_i32(payload, 0);
    append_i32(payload, 0);
    append_compact(payload, 1);
    append_u16(payload, 0);
    append_u16(payload, 3);
    append_u16(payload, 0);
    append_u16(payload, 0);
    append_compact(payload, 3);
    for (std::uint16_t point = 0; point < 3; ++point) {
        append_u16(payload, point);
        append_u16(payload, 65535);
    }
    append_compact(payload, 3);
    for (const auto& point : std::array{
             std::array{0.0F, 0.0F, 0.0F},
             std::array{1.0F, 0.0F, 0.0F},
             std::array{0.0F, 0.0F, 1.0F}}) {
        for (float value : point) append_f32(payload, value);
    }
    append_i32(payload, 1);
    append_compact(payload, 0);
    append_i32(payload, -1);
    for (int index = 0; index < 12; ++index) append_f32(payload, 0.0F);

    bytes.insert(bytes.end(), payload.begin(), payload.end());
    const auto export_offset = bytes.size();
    append_export(bytes, -2, mesh_name, 0, payload_offset, payload.size());
    finish_header(bytes, static_cast<std::uint32_t>(names.size()),
                  static_cast<std::uint32_t>(name_offset), 1,
                  static_cast<std::uint32_t>(export_offset), 2,
                  static_cast<std::uint32_t>(import_offset));
    return bytes;
}

class TemporaryGraphRoot {
public:
    TemporaryGraphRoot() {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        root_ = std::filesystem::temp_directory_path() /
                ("hpvr-hp1-graph-test-" + std::to_string(nonce));
        std::filesystem::create_directories(root_ / "system");
        std::filesystem::create_directories(root_ / "maps");
        std::filesystem::create_directories(root_ / "textures");
        std::filesystem::create_directories(root_ / "sounds");
        std::filesystem::create_directories(root_ / "music");
    }

    ~TemporaryGraphRoot() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    TemporaryGraphRoot(const TemporaryGraphRoot&) = delete;
    TemporaryGraphRoot& operator=(const TemporaryGraphRoot&) = delete;

    [[nodiscard]] const std::filesystem::path& root() const noexcept {
        return root_;
    }

    [[nodiscard]] std::filesystem::path write(std::string_view relative,
                                              const Bytes& bytes) const {
        const auto path = root_ / std::filesystem::path(relative);
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            fail("could not write synthetic graph package");
        }
        return path;
    }

private:
    std::filesystem::path root_;
};

class TemporaryPackages {
public:
    explicit TemporaryPackages(std::uint16_t package_version = 76) {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        directory_ = std::filesystem::temp_directory_path() /
                     ("hpvr-hp1-gesture-test-" + std::to_string(nonce));
        std::filesystem::create_directories(directory_);
        base_ = directory_ / "SyntheticBase.u";
        lesson_ = directory_ / "SyntheticLesson.unr";
        write(base_, make_base_package(package_version));
        write(lesson_, make_lesson_package(package_version));
    }

    ~TemporaryPackages() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    TemporaryPackages(const TemporaryPackages&) = delete;
    TemporaryPackages& operator=(const TemporaryPackages&) = delete;

    [[nodiscard]] const std::filesystem::path& base() const noexcept {
        return base_;
    }
    [[nodiscard]] const std::filesystem::path& lesson() const noexcept {
        return lesson_;
    }

private:
    static void write(const std::filesystem::path& path, const Bytes& bytes) {
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            fail("could not write synthetic package");
        }
    }

    std::filesystem::path directory_;
    std::filesystem::path base_;
    std::filesystem::path lesson_;
};

void synthetic_profile_loads_and_merges_lesson_override() {
    const TemporaryPackages packages;
    const auto profile = hpvr::wand::load_hp1_spell_profile(
        packages.base(), packages.lesson(), "FlipPattern", "spellFlip");
    expect(profile.status == Hp1ProfileStatus::ok,
           "synthetic profile loads");
    expect(profile.error.empty(), "successful profile has no error");
    expect(profile.base_package_version == 76, "base version is retained");
    expect(profile.lesson_package_version == 76,
           "lesson version is retained");
    expect(profile.gesture_name == "FlipPattern", "gesture name is retained");
    expect(profile.lesson_actor_name == "SpellLearnTrigger0",
           "matching lesson actor is retained");
    expect(profile.template_points.size() == 3, "three points are loaded");
    expect(profile.segments == std::vector<std::int32_t>({0, 1, 1, 2}),
           "segments are parsed but remain scorer-independent");
    expect_near(profile.template_points[1].x, 0.65F, "middle point x");
    expect_near(profile.template_points[1].y, 0.35F, "middle point y");
    expect_near(profile.accuracy_radius, 0.125F, "accuracy default");
    expect_near(profile.very_good_threshold, 0.875F, "very-good default");
    expect_near(profile.very_bad_threshold, 0.375F, "very-bad default");
    expect_near(profile.default_draw_time_seconds, 5.5F,
                "class draw-time default");
    expect_near(profile.draw_time_seconds, 11.25F,
                "actor draw-time override");
    expect(profile.pass_mark_count == 10, "all pass marks are loaded");
    expect_near(profile.pass_marks[0], 0.05F, "first pass mark");
    expect_near(profile.pass_marks[9], 0.725F, "last pass mark");
}

void scoped_package_reads_reuse_and_invalidate(){
    const TemporaryPackages packages;
    {
        hpvr::wand::Hp1PackageReadScope scope;
        expect(hpvr::wand::inspect_hp1_package(packages.base()).status==Hp1ProfileStatus::ok,"cached source loads");
        const auto first=scope.stats();
        expect(hpvr::wand::inspect_hp1_package(packages.base()).status==Hp1ProfileStatus::ok,"cached source reloads");
        expect(scope.stats().reads==first.reads&&scope.stats().hits==first.hits+1,"package reused without read/copy");
        {hpvr::wand::Hp1PackageReadScope nested;
            (void)hpvr::wand::inspect_hp1_package(packages.base());
            expect(nested.stats().hits==first.hits+2,"nested load shares cache");}
        {std::ofstream out(packages.base(),std::ios::binary|std::ios::trunc);out.put('x');}
        expect(hpvr::wand::inspect_hp1_package(packages.base()).status!=Hp1ProfileStatus::ok,"changed package invalidates cache");
        expect(scope.stats().retained_bytes<=96U*1024U*1024U,"read cache is bounded");
    }
    hpvr::wand::Hp1PackageReadScope fresh;
    expect(fresh.stats().hits==0&&fresh.stats().retained_bytes==0,"scope releases all cached data");
}

void package_census_validates_tables_and_aggregates_classes() {
    const TemporaryPackages packages;
    const auto summary = hpvr::wand::inspect_hp1_package(packages.base());
    expect(summary.status == Hp1ProfileStatus::ok,
           "synthetic package census succeeds");
    expect(summary.error.empty(), "successful census has no error");
    expect(summary.package_version == 76, "census retains package version");
    expect(summary.licensee_version == 0, "census retains licensee version");
    expect(summary.file_bytes == std::filesystem::file_size(packages.base()),
           "census retains exact file size");
    expect(summary.name_count == 15, "census counts names");
    expect(summary.import_count == 2, "census counts imports");
    expect(summary.export_count == 2, "census counts exports");
    expect(summary.serialized_bytes > 0, "census sums payload bytes");
    expect(summary.imported_packages == std::vector<std::string>({"Engine"}),
           "census finds deterministic root package dependencies");
    expect(summary.classes.size() == 2, "census finds two export classes");
    const auto gesture = std::ranges::find_if(
        summary.classes,
        [](const auto& entry) {
            return entry.qualified_class_name == "Engine.Gesture";
        });
    expect(gesture != summary.classes.end(),
           "census resolves a qualified imported class");
    expect(gesture->export_count == 1, "gesture class count is exact");
    expect(gesture->serialized_bytes > 0, "gesture payload bytes are counted");
    const auto class_object = std::ranges::find_if(
        summary.classes,
        [](const auto& entry) {
            return entry.qualified_class_name == "Core.Class";
        });
    expect(class_object != summary.classes.end(),
           "zero class reference is identified as Core.Class");
    expect(class_object->export_count == 1, "class export count is exact");

    const auto absent = hpvr::wand::inspect_hp1_package(
        packages.base().parent_path() / "absent-package.unr");
    expect(absent.status == Hp1ProfileStatus::io_error,
           "census fails closed for an absent package");
    expect(absent.classes.empty(),
           "failed census does not return partial class aggregates");
}

void legacy_package_names_load_without_a_compact_length() {
    const TemporaryPackages packages(61);
    const auto summary = hpvr::wand::inspect_hp1_package(packages.base());
    expect(summary.status == Hp1ProfileStatus::ok,
           "synthetic v61 package census succeeds");
    expect(summary.package_version == 61,
           "legacy census retains package version");
    expect(summary.name_count == 15,
           "legacy null-terminated names are counted");
    const auto profile = hpvr::wand::load_hp1_spell_profile(
        packages.base(), packages.lesson(), "FlipPattern", "spellFlip");
    expect(profile.status == Hp1ProfileStatus::ok,
           "synthetic v61 profile crosses the complete package reader");
    expect(profile.base_package_version == 61 &&
               profile.lesson_package_version == 61,
           "legacy profile retains both package versions");
}

void unobserved_package_version_is_rejected_before_table_parsing() {
    const TemporaryPackages packages(62);
    const auto summary = hpvr::wand::inspect_hp1_package(packages.base());
    expect(summary.status == Hp1ProfileStatus::unsupported_package,
           "synthetic unobserved version is rejected");
    expect(summary.classes.empty() && summary.imported_packages.empty(),
           "unsupported census returns no partial aggregates");
}

void malformed_packages_fail_closed() {
    const TemporaryPackages packages;
    const auto missing = hpvr::wand::load_hp1_spell_profile(
        packages.base(), packages.lesson(), "MissingPattern", "spellFlip");
    expect(missing.status == Hp1ProfileStatus::object_not_found,
           "missing gesture is reported without fallback data");

    const auto missing_spell = hpvr::wand::load_hp1_spell_profile(
        packages.base(), packages.lesson(), "FlipPattern", "spellMissing");
    expect(missing_spell.status == Hp1ProfileStatus::object_not_found,
           "missing spell actor is reported without fallback data");

    const auto absent_file = hpvr::wand::load_hp1_spell_profile(
        packages.base().parent_path() / "absent.u",
        packages.lesson(),
        "FlipPattern",
        "spellFlip");
    expect(absent_file.status == Hp1ProfileStatus::io_error,
           "absent package reports an IO error");
}

void package_graph_uses_shipped_search_order() {
    const TemporaryGraphRoot files;
    const auto entry =
        files.write("maps/Root.unr", make_dependency_package("HPBase"));
    static_cast<void>(
        files.write("system/HPBase.u", make_dependency_package("Engine")));
    static_cast<void>(
        files.write("textures/HPBase.utx", make_empty_package()));
    static_cast<void>(files.write("system/Engine.u", make_empty_package()));
    static_cast<void>(files.write("system/Engine.dll", Bytes{0x4dU}));

    const auto graph =
        hpvr::wand::resolve_hp1_package_graph(files.root(), entry);
    expect(graph.status == Hp1PackageGraphStatus::ok,
           "package graph resolves a complete closure");
    expect(graph.error.empty(), "successful package graph has no error");
    expect(graph.packages.size() == 3,
           "package graph retains each selected data package once");
    expect(graph.dependencies.size() == 2,
           "package graph retains both transitive edges");
    expect(graph.cycle_edge_count == 0, "acyclic graph has no back edges");
    expect(graph.shadowed_candidate_count == 1,
           "lower-priority basename collision is reported");

    const auto hpbase = std::ranges::find_if(
        graph.packages,
        [](const auto& package) { return package.package_name == "HPBase"; });
    expect(hpbase != graph.packages.end(), "HPBase node is present");
    expect(hpbase->path.extension() == ".u",
           "System package wins before Textures package");
    expect(hpbase->shadowed_candidates.size() == 1 &&
               hpbase->shadowed_candidates.front().extension() == ".utx",
           "shadowed texture package remains diagnostic evidence");

    const auto engine = std::ranges::find_if(
        graph.packages,
        [](const auto& package) { return package.package_name == "Engine"; });
    expect(engine != graph.packages.end() && engine->native_companion,
           "data package records its same-name native companion");
}

void package_graph_retains_dll_only_native_requirements() {
    const TemporaryGraphRoot files;
    const auto entry =
        files.write("maps/Root.unr", make_dependency_package("Window"));
    static_cast<void>(files.write("system/Window.dll", Bytes{0x4dU}));

    const auto graph =
        hpvr::wand::resolve_hp1_package_graph(files.root(), entry);
    expect(graph.status == Hp1PackageGraphStatus::ok,
           "DLL-only dependency is a resolved terminal requirement");
    expect(graph.packages.size() == 2 && graph.dependencies.size() == 1,
           "native requirement participates in the graph");
    const auto native = std::ranges::find_if(
        graph.packages,
        [](const auto& package) {
            return package.kind == Hp1ResolvedPackageKind::native_module;
        });
    expect(native != graph.packages.end() && native->package_name == "Window",
           "native requirement retains module identity");
}

void package_graph_retains_intrinsic_native_requirements_without_dlls() {
    {
        const TemporaryGraphRoot files;
        const auto entry =
            files.write("maps/Root.unr", make_dependency_package("Core"));
        static_cast<void>(
            files.write("system/Core.u", make_empty_package()));

        const auto graph =
            hpvr::wand::resolve_hp1_package_graph(files.root(), entry);
        expect(graph.status == Hp1PackageGraphStatus::ok,
               "Core data package resolves without importing Core.dll");
        const auto core = std::ranges::find_if(
            graph.packages,
            [](const auto& package) { return package.package_name == "Core"; });
        expect(core != graph.packages.end() && core->native_companion,
               "Core retains its intrinsic native companion identity");
    }
    {
        const TemporaryGraphRoot files;
        const auto entry =
            files.write("maps/Root.unr", make_dependency_package("Window"));

        const auto graph =
            hpvr::wand::resolve_hp1_package_graph(files.root(), entry);
        expect(graph.status == Hp1PackageGraphStatus::ok,
               "known DLL-only module resolves without copying Windows DLL");
        const auto native = std::ranges::find_if(
            graph.packages,
            [](const auto& package) {
                return package.kind == Hp1ResolvedPackageKind::native_module;
            });
        expect(native != graph.packages.end() &&
                   native->package_name == "Window" && native->path.empty(),
               "virtual native requirement has identity but no host DLL path");
    }
}

void package_graph_detects_cycles_and_fails_closed_on_missing_data() {
    {
        const TemporaryGraphRoot files;
        const auto entry =
            files.write("maps/A.unr", make_dependency_package("B"));
        static_cast<void>(
            files.write("system/B.u", make_dependency_package("A")));
        const auto graph =
            hpvr::wand::resolve_hp1_package_graph(files.root(), entry);
        expect(graph.status == Hp1PackageGraphStatus::ok,
               "cyclic package graph still resolves");
        expect(graph.packages.size() == 2 && graph.dependencies.size() == 2,
               "cyclic graph remains finite and complete");
        expect(graph.cycle_edge_count == 1,
               "cycle is reported as one deterministic back edge");
    }
    {
        const TemporaryGraphRoot files;
        const auto entry =
            files.write("maps/Root.unr", make_dependency_package("Absent"));
        const auto graph =
            hpvr::wand::resolve_hp1_package_graph(files.root(), entry);
        expect(graph.status == Hp1PackageGraphStatus::missing_dependency,
               "missing transitive package fails closed");
        expect(graph.packages.empty() && graph.dependencies.empty(),
               "failed graph exposes no partial closure");
        expect(graph.error.find("Absent") != std::string::npos,
               "missing dependency is named in the diagnostic");
    }
}

void package_link_table_retains_exact_object_identities() {
    const TemporaryGraphRoot files;
    const auto entry = files.write(
        "maps/Source.unr",
        make_object_import_package("Target", "Thing"));
    const auto table = hpvr::wand::inspect_hp1_package_link_table(entry);
    expect(table.status == Hp1ProfileStatus::ok,
           "synthetic link table validates");
    expect(table.imports.size() == 2 && table.exports.empty(),
           "link table retains both imports and no synthetic exports");
    expect(table.imports[0].reference == -1 &&
               table.imports[0].qualified_class_name == "Core.Package" &&
               table.imports[0].root_package &&
               table.imports[0].object_path ==
                   std::vector<std::string>({"Target"}),
           "root package import identity is exact");
    expect(table.imports[1].reference == -2 &&
               table.imports[1].outer_reference == -1 &&
               table.imports[1].qualified_class_name == "Core.Class" &&
               !table.imports[1].root_package &&
               table.imports[1].object_path ==
                   std::vector<std::string>({"Target", "Thing"}),
           "nested object import retains class, outer, and path");
}

void package_linker_resolves_exact_export_identity() {
    const TemporaryGraphRoot files;
    const auto entry = files.write(
        "maps/Source.unr",
        make_object_import_package("Target", "Thing"));
    static_cast<void>(
        files.write("system/Target.u", make_class_export_package("Thing")));

    const auto linked =
        hpvr::wand::link_hp1_package_graph(files.root(), entry);
    expect(linked.status == Hp1PackageLinkStatus::ok,
           "synthetic cross-package link succeeds");
    expect(linked.graph.packages.size() == 2,
           "link result retains the complete package graph");
    expect(linked.imports.size() == 2 &&
               linked.package_root_count == 1 &&
               linked.package_group_count == 0 &&
               linked.export_object_count == 1 &&
               linked.native_object_count == 0 &&
               linked.native_module_count == 0,
           "root and object imports receive exact target kinds");
    const auto object = std::ranges::find_if(
        linked.imports,
        [](const auto& import) {
            return import.target_kind == Hp1ImportTargetKind::export_object;
        });
    expect(object != linked.imports.end() &&
               object->source_reference == -2 &&
               object->target_package == "Target" &&
               object->target_reference == 1 &&
               object->target_object_path ==
                   std::vector<std::string>({"Thing"}),
           "object import links to the concrete target export reference");
}

void package_linker_keeps_same_named_top_level_export() {
    const TemporaryGraphRoot files;
    const auto entry = files.write(
        "maps/Source.unr",
        make_object_import_package("Song", "Song"));
    static_cast<void>(
        files.write("music/Song.umx", make_class_export_package("Song")));

    const auto linked =
        hpvr::wand::link_hp1_package_graph(files.root(), entry);
    expect(linked.status == Hp1PackageLinkStatus::ok &&
               linked.export_object_count == 1,
           "same-named top-level export resolves exactly");
    const auto object = std::ranges::find_if(
        linked.imports,
        [](const auto& import) {
            return import.target_kind == Hp1ImportTargetKind::export_object;
        });
    expect(object != linked.imports.end() &&
               object->target_object_path ==
                   std::vector<std::string>({"Song"}),
           "same-named object component is not mistaken for package root");
}

void package_linker_retains_virtual_package_groups() {
    const TemporaryGraphRoot files;
    const auto entry = files.write(
        "maps/Source.unr",
        make_group_import_package("Target", "Icons"));
    static_cast<void>(
        files.write("textures/Target.utx", make_empty_package()));

    const auto linked =
        hpvr::wand::link_hp1_package_graph(files.root(), entry);
    expect(linked.status == Hp1PackageLinkStatus::ok,
           "virtual package group does not require a target export");
    expect(linked.package_root_count == 1 &&
               linked.package_group_count == 1 &&
               linked.export_object_count == 0,
           "package group remains distinct from roots and objects");
    const auto group = std::ranges::find_if(
        linked.imports,
        [](const auto& import) {
            return import.target_kind == Hp1ImportTargetKind::package_group;
        });
    expect(group != linked.imports.end() &&
               group->target_object_path ==
                   std::vector<std::string>({"Icons"}),
           "package group retains its namespace path");
}

void package_linker_collapses_observed_same_name_group_alias() {
    const TemporaryGraphRoot files;
    const auto entry = files.write(
        "maps/Source.unr",
        make_grouped_object_import_package("Target", "Icons", "Station"));
    static_cast<void>(files.write(
        "textures/Target.utx",
        make_grouped_class_export_package("Icons", "Station")));

    const auto linked =
        hpvr::wand::link_hp1_package_graph(files.root(), entry);
    expect(linked.status == Hp1PackageLinkStatus::ok,
           "same-name package group alias resolves");
    expect(linked.package_root_count == 1 &&
               linked.package_group_count == 2 &&
               linked.export_object_count == 1,
           "group alias chain retains both imports and one object link");
    const auto object = std::ranges::find_if(
        linked.imports,
        [](const auto& import) {
            return import.target_kind == Hp1ImportTargetKind::export_object;
        });
    expect(object != linked.imports.end() &&
               object->target_object_path ==
                   std::vector<std::string>({"Icons", "Station"}) &&
               object->target_reference == 2,
           "duplicate group alias normalizes to the concrete grouped export");
}

void package_linker_retains_native_object_requirements() {
    const TemporaryGraphRoot files;
    const auto entry = files.write(
        "maps/Source.unr",
        make_object_import_package("Target", "NativeThing"));
    static_cast<void>(
        files.write("system/Target.u", make_empty_package()));
    static_cast<void>(files.write("system/Target.dll", Bytes{0x4dU}));

    const auto linked =
        hpvr::wand::link_hp1_package_graph(files.root(), entry);
    expect(linked.status == Hp1PackageLinkStatus::ok,
           "missing export in a native package becomes a native requirement");
    expect(linked.package_root_count == 1 &&
               linked.native_object_count == 1 &&
               linked.export_object_count == 0,
           "native object remains distinct from package and export links");
    const auto native = std::ranges::find_if(
        linked.imports,
        [](const auto& import) {
            return import.target_kind == Hp1ImportTargetKind::native_object;
        });
    expect(native != linked.imports.end() &&
               native->target_package == "Target" &&
               native->target_object_path ==
                   std::vector<std::string>({"NativeThing"}),
           "native object retains the exact implementation identity");
}

void package_linker_fails_closed_on_missing_or_ambiguous_export() {
    {
        const TemporaryGraphRoot files;
        const auto entry = files.write(
            "maps/Source.unr",
            make_object_import_package("Target", "Thing"));
        static_cast<void>(
            files.write("system/Target.u", make_empty_package()));
        const auto linked =
            hpvr::wand::link_hp1_package_graph(files.root(), entry);
        expect(linked.status == Hp1PackageLinkStatus::missing_target_export,
               "missing target export fails closed");
        expect(linked.imports.empty() && linked.graph.packages.empty(),
               "missing export exposes no partial linker result");
    }
    {
        const TemporaryGraphRoot files;
        const auto entry = files.write(
            "maps/Source.unr",
            make_object_import_package("Target", "Thing"));
        static_cast<void>(files.write(
            "system/Target.u", make_class_export_package("Thing", 2)));
        const auto linked =
            hpvr::wand::link_hp1_package_graph(files.root(), entry);
        expect(linked.status == Hp1PackageLinkStatus::ambiguous_target_export,
               "duplicate target export identity fails closed");
        expect(linked.imports.empty() && linked.graph.packages.empty(),
               "ambiguous export exposes no partial linker result");
    }
}

void level_handles_decode_actor_slots_and_world_model() {
    const TemporaryGraphRoot files;
    const auto map = files.write("maps/Synthetic.unr", make_level_package());
    const auto level = hpvr::wand::inspect_hp1_level_handles(map);
    expect(level.status == Hp1ProfileStatus::ok,
           "synthetic Level handles decode");
    expect(level.package_version == 76, "Level reports package version");
    expect(level.level_reference == 1, "Level export identity is retained");
    expect(level.world_model_reference == 2,
           "Level world model identity is retained");
    expect(level.actor_references == std::vector<std::int32_t>{3, 0, 4, 0},
           "Level actor slots retain order and null holes");
    expect(level.null_actor_count == 2, "Level null actor slots are counted");

    const auto imported = files.write(
        "maps/ImportedActor.unr", make_level_package(true, false));
    expect(hpvr::wand::inspect_hp1_level_handles(imported).status ==
               Hp1ProfileStatus::invalid_profile,
           "Level rejects imported actor ownership");

    const auto wrong_model = files.write(
        "maps/WrongModel.unr", make_level_package(false, true));
    expect(hpvr::wand::inspect_hp1_level_handles(wrong_model).status ==
               Hp1ProfileStatus::invalid_profile,
           "Level rejects a non-Model world reference");
}

void player_start_census_decodes_only_direct_level_actor_properties() {
    const TemporaryGraphRoot files;
    const auto map = files.write(
        "maps/PlayerStart.unr",
        make_level_package(false, false, 76, false, false, true));
    const auto starts = hpvr::wand::inspect_hp1_player_starts(map);
    expect(starts.status == Hp1ProfileStatus::ok &&
               starts.package_version == 76 &&
               starts.player_starts.size() == 1,
           "direct Engine.PlayerStart actor is selected from Level slots");
    const auto& start = starts.player_starts.front();
    expect(start.actor_reference == 6 && start.actor_slot_index == 3 &&
               start.object_name == "PlayerStart0",
           "PlayerStart retains actor reference, slot, and object identity");
    expect(start.location_serialized && start.rotation_serialized,
           "PlayerStart reports explicitly serialized transform fields");
    expect_near(start.location_unreal.x, 123.0F, "PlayerStart location X");
    expect_near(start.location_unreal.y, -456.0F, "PlayerStart location Y");
    expect_near(start.location_unreal.z, 789.0F, "PlayerStart location Z");
    expect(start.rotation_units ==
               std::array<std::int32_t, 3>{1024, 2048, -4096},
           "PlayerStart retains raw Unreal rotator units");

    const auto without =
        files.write("maps/NoPlayerStart.unr", make_level_package());
    const auto empty = hpvr::wand::inspect_hp1_player_starts(without);
    expect(empty.status == Hp1ProfileStatus::ok && empty.player_starts.empty(),
           "map without a direct PlayerStart returns an empty successful census");
}

void actor_visual_census_retains_slots_and_instance_overrides() {
    const TemporaryGraphRoot files;
    const auto map = files.write(
        "maps/ActorVisuals.unr",
        make_level_package(false, false, 76, false, false, true));
    const auto actors = hpvr::wand::inspect_hp1_actor_visuals(map);
    expect(actors.status == Hp1ProfileStatus::ok &&
               actors.package_version == 76 && actors.actors.size() == 3,
           "actor visual census accepts actors with no instance payload");
    const auto start = std::ranges::find_if(
        actors.actors,
        [](const auto& actor) { return actor.actor_reference == 6; });
    expect(start != actors.actors.end() && start->actor_slot_index == 3 &&
               start->class_reference == -6 &&
               start->qualified_class_name == "Engine.PlayerStart",
           "actor visual census retains Level slot and class identity");
    expect(start->location_serialized && start->rotation_serialized &&
               !start->mesh_serialized && start->draw_scale == 1.0F,
           "actor visual census decodes transform and leaves defaults explicit");
}

void actor_visual_census_accepts_empty_cutscene_aliases() {
    const TemporaryGraphRoot files;
    for (const auto structure : {20, 21}) {
        for (const auto count : {0, 1, 5, -1, 50}) {
            Bytes value;
            append_compact(value, 4);
            append_compact(value, count);
            if (count == 1) append_u8(value, 0);
            if (count == 5) {
                for (const auto c : std::string_view("Hero")) append_u8(value, c);
                append_u8(value, 0);
            }
            value.insert(value.end(), 12, 0);
            Bytes properties;
            append_compact(properties, structure + 2);
            append_u8(properties, 0x5a);
            append_compact(properties, structure);
            append_u8(properties, static_cast<std::uint8_t>(value.size()));
            properties.insert(properties.end(), value.begin(), value.end());
            append_compact(properties, 0);
            const auto map = files.write(
                "maps/CutAlias" + std::to_string(structure) + "_" +
                    std::to_string(count) + ".unr",
                make_level_package(false, false, 76, false, false, false,
                                   properties));
            const auto census = hpvr::wand::inspect_hp1_actor_visuals(map);
            if (count < 0 || count == 50) {
                expect(census.status == Hp1ProfileStatus::invalid_profile,
                       "negative or out-of-bounds cutscene alias fails closed");
                continue;
            }
            expect(census.status == Hp1ProfileStatus::ok &&
                       census.actors.front().serialized_properties.size() == 1,
                   "empty and named cutscene aliases retain actor properties");
            const auto& alias = census.actors.front().serialized_properties.front();
            expect(alias.object_reference == 4 && alias.object_reference_serialized &&
                       alias.text_value_serialized &&
                       alias.text_value == (count == 5 ? "Hero" : "") &&
                       alias.value == value,
                   "cutscene alias keeps reference, text, and trailing struct fields");
        }
    }
}

void model_census_decodes_only_bounded_collection_framing() {
    const TemporaryGraphRoot files;
    const auto map = files.write("maps/Synthetic.unr", make_level_package());
    const auto model = hpvr::wand::inspect_hp1_model_census(map);
    expect(model.status == Hp1ProfileStatus::ok,
           "synthetic Model census validates");
    expect(model.package_version == 76 && model.model_reference == 2 &&
               model.polys_reference == 5,
           "Model census retains package and object identities");
    expect(model.vector_count == 2 && model.point_count == 3 &&
               model.node_count == 1 && model.surface_count == 1 &&
               model.vertex_count == 4 && model.shared_side_count == 6,
           "Model census retains primary BSP collection counts");
    expect(model.zone_count == 2 && model.light_map_count == 1 &&
               model.light_bit_bytes == 4 && model.bound_count == 1 &&
               model.leaf_hull_count == 3 && model.leaf_count == 1,
           "Model census retains auxiliary collection counts");
    expect(model.light_count == 3 && model.null_light_count == 1 &&
               model.root_outside && model.linked,
           "Model census retains light holes and terminal flags");

    const auto truncated = files.write(
        "maps/Truncated.unr", make_level_package(false, false, 76, true));
    expect(hpvr::wand::inspect_hp1_model_census(truncated).status ==
               Hp1ProfileStatus::invalid_package,
           "truncated Model payload fails closed");

    const auto legacy = files.write(
        "maps/Legacy.unr", make_level_package(false, false, 61));
    expect(hpvr::wand::inspect_hp1_model_census(legacy).status ==
               Hp1ProfileStatus::unsupported_package,
           "legacy indirect UModel layout is rejected explicitly");
}

void skeletal_mesh_census_decodes_exact_ue1_framing() {
    const TemporaryGraphRoot files;
    const auto mesh_path = files.write(
        "system/SyntheticMesh.u", make_skeletal_mesh_package());
    const auto mesh = hpvr::wand::inspect_hp1_skeletal_mesh_census(
        mesh_path, 1);
    expect(mesh.status == Hp1ProfileStatus::ok,
           "synthetic SkeletalMesh census validates");
    expect(mesh.package_version == 76 && mesh.mesh_reference == 1 &&
               mesh.object_name == "SyntheticMesh",
           "SkeletalMesh census retains package and object identity");
    expect(mesh.point_count == 3 && mesh.lod_wedge_count == 3 &&
               mesh.face_count == 1 && mesh.material_count == 1,
           "SkeletalMesh census retains direct geometry counts");
    expect(mesh.bone_count == 1 && mesh.bone_index_count == 1 &&
               mesh.bone_weight_count == 3 && mesh.skeletal_depth == 1,
           "SkeletalMesh census retains bounded skeleton counts");
    const auto triangles = hpvr::wand::build_hp1_skeletal_triangle_mesh(
        mesh_path, 1, 0.02F);
    expect(triangles.status == Hp1ProfileStatus::ok &&
               triangles.vertices.size() == 3,
           "synthetic SkeletalMesh builds one direct triangle");
    expect_near(triangles.vertices[0].position_m.x, 0.0F,
                "legacy corner swap retains first mapped X");
    expect_near(triangles.vertices[0].position_m.z, 0.02F,
                "legacy direct mesh maps Unreal X to OpenXR positive Z");
    expect_near(triangles.vertices[2].position_m.y, 0.02F,
                "legacy direct mesh maps Unreal Z to OpenXR positive Y");
    expect(triangles.vertices[0].texture_uv ==
               std::array<float, 2>{1.0F, 0.0F} &&
               triangles.vertices[0].material_index == 0,
           "direct triangle retains byte UV and material identity");

    const auto invalid = files.write(
        "system/InvalidMesh.u", make_skeletal_mesh_package(true));
    expect(hpvr::wand::inspect_hp1_skeletal_mesh_census(invalid, 1).status ==
               Hp1ProfileStatus::invalid_profile,
           "SkeletalMesh lazy-array offset mismatch fails closed");
    expect(hpvr::wand::inspect_hp1_skeletal_mesh_census(mesh_path, -1).status ==
               Hp1ProfileStatus::invalid_profile,
           "SkeletalMesh import reference is rejected");
}

void bsp_topology_decodes_and_validates_active_cross_indices() {
    const TemporaryGraphRoot files;
    const auto map = files.write("maps/Synthetic.unr", make_level_package());
    const auto topology = hpvr::wand::load_hp1_bsp_topology(map);
    expect(topology.status == Hp1ProfileStatus::ok,
           "synthetic BSP topology validates");
    expect(topology.package_version == 76 &&
               topology.model_reference == 2,
           "BSP topology retains package and model identity");
    expect(topology.vectors.size() == 2 && topology.points.size() == 3 &&
               topology.nodes.size() == 1 &&
               topology.surfaces.size() == 1 &&
               topology.vertices.size() == 3,
           "BSP topology retains only the selected structural arrays");
    expect(topology.nodes[0].vertex_pool_index == 0 &&
               topology.nodes[0].vertex_count == 3 &&
               topology.nodes[0].surface_index == 0 &&
               topology.nodes[0].front_node_index == -1 &&
               topology.nodes[0].back_node_index == -1 &&
               topology.nodes[0].coplanar_node_index == -1,
           "node topology and remapped active vertex span are exact");
    expect(topology.vertices[0].point_index == 0 &&
               topology.vertices[1].point_index == 1 &&
               topology.vertices[1].side_index == 2 &&
               topology.vertices[2].point_index == 2,
           "active vertex references retain point and side identities");
    expect(topology.surfaces[0].base_point_index == 0 &&
               topology.surfaces[0].normal_vector_index == 0 &&
               topology.surfaces[0].light_map_index == 0,
           "surface topology retains validated point and vector links");

    const auto invalid = files.write(
        "maps/InvalidVertex.unr",
        make_level_package(false, false, 76, false, true));
    expect(hpvr::wand::load_hp1_bsp_topology(invalid).status ==
               Hp1ProfileStatus::invalid_profile,
           "out-of-range active vertex point fails closed");
}

void bsp_triangle_mesh_maps_axes_winding_and_degenerates() {
    const TemporaryGraphRoot files;
    const auto map = files.write("maps/Synthetic.unr", make_level_package());
    auto topology = hpvr::wand::load_hp1_bsp_topology(map);
    topology.vectors[0] = {0.0F, 1.0F, 0.0F};
    topology.vectors[1] = {1.0F, 0.0F, 0.0F};
    topology.surfaces[0].texture_u_vector_index = 1;
    topology.surfaces[0].texture_v_vector_index = 0;
    topology.surfaces[0].pan_u = 4;
    topology.surfaces[0].pan_v = -3;
    const auto mesh = hpvr::wand::build_hp1_bsp_triangle_mesh(topology, 0.01F);
    expect(mesh.status == Hp1ProfileStatus::ok,
           "synthetic BSP triangle mesh builds");
    expect(mesh.source_polygon_count == 1 && mesh.triangles.size() == 1 &&
               mesh.short_polygon_count == 0 &&
               mesh.degenerate_triangle_count == 0 &&
               mesh.reversed_winding_count == 1,
           "one left-to-right handed triangle has its winding reversed");
    const auto& triangle = mesh.triangles.front();
    expect_near(triangle.positions_m[0].x, 0.0F,
                "triangle origin maps to XR X");
    expect_near(triangle.positions_m[1].x, 1.0F,
                "Unreal +Y maps to OpenXR +X");
    expect_near(triangle.positions_m[1].z, -1.0F,
                "Unreal +X maps to OpenXR -Z");
    expect_near(triangle.normal.x, 0.0F, "triangle normal X");
    expect_near(triangle.normal.y, 1.0F,
                "Unreal +Z normal maps to OpenXR +Y");
    expect_near(triangle.normal.z, 0.0F, "triangle normal Z");
    expect_near(triangle.texel_uv[0][0], 4.0F, "triangle origin texture U");
    expect_near(triangle.texel_uv[0][1], -3.0F, "triangle origin texture V");
    expect_near(triangle.texel_uv[1][0], 104.0F,
                "texture U follows the winding-swapped corner");
    expect_near(triangle.texel_uv[1][1], 97.0F,
                "texture V follows the winding-swapped corner");
    expect_near(triangle.texel_uv[2][0], 104.0F,
                "third corner texture U remains attached");
    expect_near(triangle.texel_uv[2][1], -3.0F,
                "third corner texture V remains attached");
    expect(triangle.node_index == 0 && triangle.surface_index == 0,
           "triangle retains node and surface provenance");

    const auto unit_mesh =
        hpvr::wand::build_hp1_bsp_triangle_mesh(topology, 1.0F);
    expect(unit_mesh.status == Hp1ProfileStatus::ok &&
               unit_mesh.triangles.size() == mesh.triangles.size() &&
               unit_mesh.degenerate_triangle_count ==
                   mesh.degenerate_triangle_count &&
               unit_mesh.reversed_winding_count ==
                   mesh.reversed_winding_count,
           "triangle selection and winding are invariant across world scale");
    expect_near(unit_mesh.triangles.front().positions_m[1].x, 100.0F,
                "world scale changes only emitted metric positions");
    expect_near(unit_mesh.triangles.front().normal.y, triangle.normal.y,
                "world scale does not change the emitted unit normal");
    expect_near(unit_mesh.triangles.front().texel_uv[1][0],
                triangle.texel_uv[1][0],
                "world scale does not change source texture coordinates");

    expect(hpvr::wand::build_hp1_bsp_triangle_mesh(topology, 0.0F).status ==
               Hp1ProfileStatus::invalid_profile,
           "zero world scale fails closed");

    auto invalid = topology;
    invalid.vertices[1].point_index = 3;
    const auto invalid_mesh =
        hpvr::wand::build_hp1_bsp_triangle_mesh(invalid, 0.01F);
    expect(invalid_mesh.status == Hp1ProfileStatus::invalid_profile &&
               invalid_mesh.triangles.empty(),
           "mutated out-of-range mesh input fails without partial output");

    auto degenerate = topology;
    degenerate.points[2] = degenerate.points[1];
    const auto rejected =
        hpvr::wand::build_hp1_bsp_triangle_mesh(degenerate, 0.01F);
    expect(rejected.status == Hp1ProfileStatus::ok &&
               rejected.triangles.empty() &&
               rejected.degenerate_triangle_count == 1,
           "degenerate fan triangle is counted but never emitted");
}

void c_abi_loads_profile_without_cpp_ownership_crossing_boundary() {
    const TemporaryPackages packages;
    const auto base = packages.base().u8string();
    const auto lesson = packages.lesson().u8string();
    const std::string base_utf8(base.begin(), base.end());
    const std::string lesson_utf8(lesson.begin(), lesson.end());
    std::array<hpvr_wand_vec2, HPVR_HP1_GESTURE_MAX_TEMPLATE_POINTS> points{};
    std::array<std::int32_t, HPVR_HP1_GESTURE_MAX_SEGMENTS> segments{};
    hpvr_hp1_spell_profile_report report{};
    const auto status = hpvr_hp1_load_spell_profile_utf8(
        base_utf8.c_str(),
        lesson_utf8.c_str(),
        "FlipPattern",
        "spellFlip",
        points.data(),
        static_cast<std::uint32_t>(points.size()),
        segments.data(),
        static_cast<std::uint32_t>(segments.size()),
        &report);
    expect(status == HPVR_HP1_PROFILE_OK, "C ABI profile load succeeds");
    expect(report.status == status, "C ABI mirrors profile status");
    expect(report.abi_version == HPVR_HP1_GESTURE_ABI_VERSION,
           "C ABI profile version matches");
    expect(report.template_point_count == 3, "C ABI writes three points");
    expect(report.segment_count == 4, "C ABI writes four segment values");
    expect_near(points[1].x, 0.65F, "C ABI middle point x");
    expect(segments[3] == 2, "C ABI final segment value");
    expect_near(report.draw_time_seconds, 11.25F,
                "C ABI carries actor draw-time override");
}

void c_abi_short_buffers_do_not_receive_partial_profile_data() {
    const TemporaryPackages packages;
    const auto base = packages.base().u8string();
    const auto lesson = packages.lesson().u8string();
    const std::string base_utf8(base.begin(), base.end());
    const std::string lesson_utf8(lesson.begin(), lesson.end());
    std::array<hpvr_wand_vec2, 2> points{{{77.0F, 88.0F}, {99.0F, 111.0F}}};
    std::array<std::int32_t, 3> segments{{77, 88, 99}};
    hpvr_hp1_spell_profile_report report{};
    const auto status = hpvr_hp1_load_spell_profile_utf8(
        base_utf8.c_str(),
        lesson_utf8.c_str(),
        "FlipPattern",
        "spellFlip",
        points.data(),
        static_cast<std::uint32_t>(points.size()),
        segments.data(),
        static_cast<std::uint32_t>(segments.size()),
        &report);
    expect(status == HPVR_HP1_PROFILE_BUFFER_TOO_SMALL,
           "C ABI reports both required capacities");
    expect(report.template_point_count == 3, "required point count is reported");
    expect(report.segment_count == 4, "required segment count is reported");
    expect_near(points[0].x, 77.0F, "short point buffer is not partially written");
    expect(segments[0] == 77, "short segment buffer is not partially written");
}

void c_abi_bsp_slice_is_bounded_and_uses_two_call_ownership() {
    const TemporaryGraphRoot files;
    const auto map = files.write("maps/Synthetic.unr", make_level_package());
    const auto map_u8 = map.u8string();
    const std::string map_utf8(map_u8.begin(), map_u8.end());

    hpvr_hp1_bsp_slice_report query{};
    const auto query_status = hpvr_hp1_load_bsp_slice_utf8(
        map_utf8.c_str(), 0.01F, 1, nullptr, 0, &query);
    expect(query_status == HPVR_HP1_PROFILE_BUFFER_TOO_SMALL &&
               query.status == query_status &&
               query.abi_version == HPVR_HP1_BSP_SLICE_ABI_VERSION,
           "BSP slice size query reports the stable ABI and short buffer");
    expect(query.required_vertex_count == 3 &&
               query.selected_triangle_count == 1 &&
               query.available_triangle_count == 1 &&
               query.omitted_triangle_count == 0,
           "BSP slice query reports exact bounded capacities");
    expect(query.zero_flag_triangle_count == 1 &&
               query.nonzero_flag_triangle_count == 0 &&
               query.no_texture_triangle_count == 1 &&
               query.local_actor_triangle_count == 1,
           "BSP slice query classifies only raw flags and reference kinds");
    expect_near(query.bounds_min_m[2], -1.0F,
                "BSP slice query reports converted minimum Z");
    expect_near(query.bounds_max_m[0], 1.0F,
                "BSP slice query reports converted maximum X");

    std::array<hpvr_hp1_bsp_slice_vertex, 2> short_vertices{};
    short_vertices[0].position_m[0] = 77.0F;
    hpvr_hp1_bsp_slice_report short_report{};
    const auto short_status = hpvr_hp1_load_bsp_slice_utf8(
        map_utf8.c_str(), 0.01F, 1, short_vertices.data(),
        static_cast<std::uint32_t>(short_vertices.size()), &short_report);
    expect(short_status == HPVR_HP1_PROFILE_BUFFER_TOO_SMALL &&
               short_report.written_vertex_count == 0,
           "short BSP slice buffer receives no partial output");
    expect_near(short_vertices[0].position_m[0], 77.0F,
                "short BSP vertex buffer remains unchanged");

    std::array<hpvr_hp1_bsp_slice_vertex, 3> vertices{};
    hpvr_hp1_bsp_slice_report report{};
    const auto status = hpvr_hp1_load_bsp_slice_utf8(
        map_utf8.c_str(), 0.01F, 1, vertices.data(),
        static_cast<std::uint32_t>(vertices.size()), &report);
    expect(status == HPVR_HP1_PROFILE_OK &&
               report.written_vertex_count == vertices.size(),
           "exact BSP slice buffer is filled successfully");
    expect_near(vertices[1].position_m[0], 1.0F,
                "BSP C ABI preserves converted triangle position");
    expect_near(vertices[1].normal[1], 1.0F,
                "BSP C ABI preserves CCW triangle normal");
    expect(vertices[1].node_index == 0 &&
               vertices[1].surface_index == 0 &&
               vertices[1].polygon_flags == 0,
           "BSP C ABI retains metadata without object payload");
}

void c_abi_player_start_retains_identity_and_uses_bsp_axes() {
    const TemporaryGraphRoot files;
    const auto map = files.write(
        "maps/PlayerStart.unr",
        make_level_package(false, false, 76, false, false, true));
    const auto map_u8 = map.u8string();
    const std::string map_utf8(map_u8.begin(), map_u8.end());

    hpvr_hp1_player_start_report report{};
    const auto status = hpvr_hp1_load_player_start_utf8(
        map_utf8.c_str(), 0.01F, 0, &report);
    expect(status == HPVR_HP1_PROFILE_OK && report.status == status &&
               report.abi_version == HPVR_HP1_PLAYER_START_ABI_VERSION &&
               report.available_player_start_count == 1 &&
               report.selected_ordinal == 0,
           "PlayerStart C ABI selects one stable ordinal");
    expect(report.actor_reference == 6 && report.actor_slot_index == 3 &&
               std::string_view(report.object_name) == "PlayerStart0",
           "PlayerStart C ABI retains object and Level-slot identity");
    expect_near(report.position_m[0], -4.56F,
                "PlayerStart Unreal Y maps to OpenXR X");
    expect_near(report.position_m[1], 7.89F,
                "PlayerStart Unreal Z maps to OpenXR Y");
    expect_near(report.position_m[2], -1.23F,
                "PlayerStart Unreal X maps to negative OpenXR Z");
    expect(report.rotation_units[0] == 1024 &&
               report.rotation_units[1] == 2048 &&
               report.rotation_units[2] == -4096 &&
               report.location_serialized == 1 &&
               report.rotation_serialized == 1,
           "PlayerStart C ABI retains raw serialized transform evidence");

    hpvr_hp1_player_start_report missing{};
    const auto missing_status = hpvr_hp1_load_player_start_utf8(
        map_utf8.c_str(), 0.01F, 1, &missing);
    expect(missing_status == HPVR_HP1_PROFILE_OBJECT_NOT_FOUND &&
               missing.available_player_start_count == 1,
           "out-of-range PlayerStart ordinal fails with the available count");
}

void c_abi_actor_visual_selects_exact_level_reference() {
    const TemporaryGraphRoot files;
    const auto map = files.write(
        "maps/ActorVisual.unr",
        make_level_package(false, false, 76, false, false, true));
    const auto map_u8 = map.u8string();
    const std::string map_utf8(map_u8.begin(), map_u8.end());

    hpvr_hp1_actor_visual_report report{};
    const auto status = hpvr_hp1_load_actor_visual_utf8(
        map_utf8.c_str(), 0.01F, 6, &report);
    expect(status == HPVR_HP1_PROFILE_OK && report.status == status &&
               report.abi_version == HPVR_HP1_ACTOR_VISUAL_ABI_VERSION,
           "actor visual C ABI selects one exact Level reference");
    expect(report.actor_reference == 6 && report.actor_slot_index == 3 &&
               std::string_view(report.object_name) == "PlayerStart0",
           "actor visual C ABI retains object and Level-slot identity");
    expect_near(report.position_m[0], -4.56F,
                "actor visual Unreal Y maps to OpenXR X");
    expect_near(report.position_m[1], 7.89F,
                "actor visual Unreal Z maps to OpenXR Y");
    expect_near(report.position_m[2], -1.23F,
                "actor visual Unreal X maps to negative OpenXR Z");
    expect(report.rotation_units[0] == 1024 &&
               report.rotation_units[1] == 2048 &&
               report.rotation_units[2] == -4096 &&
               report.location_serialized == 1 &&
               report.rotation_serialized == 1 &&
               report.draw_scale == 1.0F,
           "actor visual C ABI retains serialized placement evidence");

    hpvr_hp1_actor_visual_report missing{};
    const auto missing_status = hpvr_hp1_load_actor_visual_utf8(
        map_utf8.c_str(), 0.01F, 7, &missing);
    expect(missing_status == HPVR_HP1_PROFILE_OBJECT_NOT_FOUND,
           "missing exact actor reference fails closed");
}

void p8_texture_decoder_validates_mips_palette_and_rgba() {
    const TemporaryGraphRoot files;
    const auto package =
        files.write("textures/Synthetic.utx", make_p8_texture_package());
    const auto texture = hpvr::wand::load_hp1_p8_texture(package, 1);
    expect(texture.status == Hp1ProfileStatus::ok &&
               texture.package_version == 76 &&
               texture.texture_reference == 1 &&
               texture.palette_reference == 2 &&
               texture.object_name == "SyntheticTexture",
           "P8 decoder retains texture and palette identity");
    expect(texture.format_serialized && texture.format == 0 &&
               !texture.compressed_mips_serialized &&
               texture.mips.size() == 1,
           "P8 decoder validates format and one regular mip");
    expect(texture.mips[0].width == 2 &&
               texture.mips[0].height == 2 &&
               texture.mips[0].width_bits == 1 &&
               texture.mips[0].height_bits == 1 &&
               texture.mips[0].indexed_bytes == 4,
           "P8 decoder retains exact mip metadata");
    const std::array<std::uint8_t, 16> expected{
        0, 1, 2, 255,
        1, 2, 3, 254,
        2, 3, 4, 253,
        3, 4, 5, 252,
    };
    expect(std::ranges::equal(texture.rgba8, expected),
           "P8 palette indices expand to RGBA in UE1 channel order");

    for (const auto package_version :
         std::array<std::uint16_t, 2>{69, 73}) {
        const auto legacy_package = files.write(
            "textures/Synthetic" + std::to_string(package_version) + ".utx",
            make_p8_texture_package(package_version));
        const auto legacy_texture =
            hpvr::wand::load_hp1_p8_texture(legacy_package, 1);
        expect(legacy_texture.status == Hp1ProfileStatus::ok &&
                   legacy_texture.package_version == package_version &&
                   std::ranges::equal(legacy_texture.rgba8, expected),
               "P8 decoder covers each observed v62+ texture layout");
    }

    const auto wrong_export = hpvr::wand::load_hp1_p8_texture(package, 2);
    expect(wrong_export.status == Hp1ProfileStatus::invalid_profile,
           "Palette export cannot be decoded as a Texture");
    const auto imported = hpvr::wand::load_hp1_p8_texture(package, -2);
    expect(imported.status == Hp1ProfileStatus::invalid_profile,
           "import reference cannot be decoded as a local Texture");
    for(const auto source: {1,2,3,-2}) {
        const auto wet_package=files.write("textures/Water"+std::to_string(source)+".utx",
                                           make_p8_texture_package(76,source));
        const auto water=hpvr::wand::load_hp1_p8_texture(wet_package,3);
        if(source==1) {
            expect(water.status==Hp1ProfileStatus::ok && water.texture_reference==3 &&
                   water.object_name=="SyntheticWater" && water.rgba8==texture.rgba8,
                   "WetTexture uses authored source pixels and retains wrapper identity");
        } else {
            expect(water.status!=Hp1ProfileStatus::ok && water.rgba8.empty(),
                   "WetTexture rejects palette sources, cyclic sources, and unresolved imports");
        }
    }
}

std::vector<Vec2> dense_line_template() {
    std::vector<Vec2> result;
    for (int index = 0; index <= 16; ++index) {
        result.push_back({static_cast<float>(index) / 16.0F, 0.5F});
    }
    return result;
}

void scorer_matches_coverage_contract() {
    const std::array<Vec2, 3> authored{{
        {0.0F, 0.5F}, {0.5F, 0.5F}, {1.0F, 0.5F},
    }};
    const auto dense = dense_line_template();
    const auto perfect =
        hpvr::wand::compare_hp1_projected_gesture(dense, authored, 0.03F);
    expect(perfect.status == Hp1GestureScoreStatus::ok,
           "perfect gesture scores");
    expect_near(perfect.score, 1.0F, "perfect coverage score");
    expect(perfect.input_point_count == 17, "all input points are counted");
    expect(perfect.unique_input_point_count == 17,
           "distinct input points remain");
    expect(perfect.dense_template_point_count == 17,
           "three authored points densify to seventeen");

    const std::array<Vec2, 3> duplicated{{
        {0.0F, 0.5F}, {0.0009F, 0.5009F}, {1.0F, 0.5F},
    }};
    const auto duplicate_score =
        hpvr::wand::compare_hp1_projected_gesture(duplicated, authored, 0.03F);
    expect(duplicate_score.unique_input_point_count == 2,
           "strict per-axis epsilon removes global XY duplicates");
    expect(duplicate_score.score < 0.1F,
           "fewer than ten unique points receives the legacy penalty");

    std::vector<Vec2> far;
    for (int index = 0; index < 10; ++index) {
        far.push_back({4.0F + static_cast<float>(index) * 0.01F, 4.0F});
    }
    const auto rejected =
        hpvr::wand::compare_hp1_projected_gesture(far, authored, 0.03F);
    expect_near(rejected.score, 0.0F, "far coverage scores zero");
}

void scorer_rejects_invalid_values() {
    const std::array<Vec2, 2> valid{{{0.0F, 0.0F}, {1.0F, 1.0F}}};
    expect(hpvr::wand::compare_hp1_projected_gesture(valid, valid, 0.0F).status ==
               Hp1GestureScoreStatus::invalid_accuracy,
           "zero accuracy is rejected");
    const std::array<Vec2, 1> invalid{{
        {std::numeric_limits<float>::quiet_NaN(), 0.0F},
    }};
    expect(hpvr::wand::compare_hp1_projected_gesture(invalid, valid, 0.1F).status ==
               Hp1GestureScoreStatus::invalid_input,
           "non-finite drawn input is rejected");
    expect(hpvr::wand::compare_hp1_projected_gesture(valid, invalid, 0.1F).status ==
               Hp1GestureScoreStatus::invalid_template,
           "non-finite template input is rejected");

    const std::array<hpvr::wand::Vec3, 3> legacy{{
        {99.0F, 99.0F, -1.0F},
        {0.0F, 0.0F, 0.0F},
        {1.0F, 1.0F, 0.0F},
    }};
    const auto sentinel = hpvr::wand::compare_hp1_gesture(
        legacy, valid, 0.1F);
    expect(sentinel.input_point_count == 3,
           "legacy scorer counts submitted slots before filtering");
    expect(sentinel.unique_input_point_count == 2,
           "legacy Z=-1 slot is removed before XY deduplication");
}

void lesson_period_uses_all_five_hundred_slots() {
    expect(hpvr::wand::hp1_lesson_resampling_period_ns(12.0F) == 24'000'000,
           "twelve seconds maps to a 24ms slot period");
    expect(hpvr::wand::hp1_lesson_resampling_period_ns(0.0F) == 0,
           "zero draw time is rejected");
    expect(hpvr::wand::hp1_lesson_resampling_period_ns(
               std::numeric_limits<float>::infinity()) == 0,
           "infinite draw time is rejected");
}

void hp1_animation_key_codec_matches_retail_contract() {
    const auto identity =
        hpvr::wand::decode_hp1_animation_orientation_key({0, 0, 0});
    expect_near(identity.x, 0.0F, "zero quaternion X");
    expect_near(identity.y, 0.0F, "zero quaternion Y");
    expect_near(identity.z, 0.0F, "zero quaternion Z");
    expect_near(identity.w, 1.0F, "zero quaternion reconstructs W");

    const auto quarter_turn =
        hpvr::wand::decode_hp1_animation_orientation_key({32767, 0, 0});
    expect_near(quarter_turn.x, 1.0F, "maximum quaternion X");
    expect_near(quarter_turn.w, 0.0F, "maximum quaternion leaves zero W");

    const auto position =
        hpvr::wand::decode_hp1_animation_position_key(
            {32767, -32767, 16384}, 12.0F);
    expect_near(position.x, 12.0F, "position X uses per-track scale");
    expect_near(position.y, -12.0F, "position Y keeps sign");
    expect_near(position.z, 6.000183F, "position Z retains quantization");
    expect_near(
        hpvr::wand::decode_hp1_animation_time_key(255, 0.25F),
        63.75F,
        "time key uses per-track scale");
}

}  // namespace

void mpeg_loader_excludes_object_tail_and_incomplete_frames() {
    const auto package_with = [](const Bytes& payload) {
        constexpr std::array names{"None","Core","Package","Class","Engine","Sound","SyntheticVoice"};
        Bytes bytes=make_header();
        const auto names_at=bytes.size();
        for(const auto name:names) append_name(bytes,name);
        const auto imports_at=bytes.size();
        append_import(bytes,1,2,4);
        append_import(bytes,1,3,5,-1);
        const auto data_at=bytes.size();
        bytes.insert(bytes.end(),payload.begin(),payload.end());
        const auto exports_at=bytes.size();
        append_export(bytes,-2,6,0,data_at,payload.size());
        finish_header(bytes,static_cast<std::uint32_t>(names.size()),
            static_cast<std::uint32_t>(names_at),1,static_cast<std::uint32_t>(exports_at),
            2,static_cast<std::uint32_t>(imports_at));
        return bytes;
    };
    // MPEG2 Layer II, 64 kbps, 22050 Hz: 417 bytes per unpadded frame.
    Bytes frame(417,0x55);
    frame[0]=0xFF; frame[1]=0xF5; frame[2]=0x80; frame[3]=0xC0;
    Bytes payload{0,0,0,0};
    payload.insert(payload.end(),frame.begin(),frame.end());
    payload.insert(payload.end(),frame.begin(),frame.end());
    payload.insert(payload.end(),32,0);
    TemporaryGraphRoot root;
    auto path=root.write("sounds/Voice.uax",package_with(payload));
    auto decoded=hpvr::wand::load_hp1_mpeg_sound(path,1);
    expect(decoded.status==Hp1ProfileStatus::ok,"MPEG complete frames load");
    expect(decoded.encoded_bytes.size()==834,"MPEG object metadata tail excluded");
    expect(decoded.sample_rate==22050 && decoded.channel_count==1,"MPEG format retained");
    frame.resize(20);
    path=root.write("sounds/Truncated.uax",package_with(frame));
    decoded=hpvr::wand::load_hp1_mpeg_sound(path,1);
    expect(decoded.status!=Hp1ProfileStatus::ok,"incomplete first MPEG frame rejected");
}

int main() {
    scoped_package_reads_reuse_and_invalidate();
    mpeg_loader_excludes_object_tail_and_incomplete_frames();
    synthetic_profile_loads_and_merges_lesson_override();
    package_census_validates_tables_and_aggregates_classes();
    legacy_package_names_load_without_a_compact_length();
    unobserved_package_version_is_rejected_before_table_parsing();
    malformed_packages_fail_closed();
    package_graph_uses_shipped_search_order();
    package_graph_retains_dll_only_native_requirements();
    package_graph_retains_intrinsic_native_requirements_without_dlls();
    package_graph_detects_cycles_and_fails_closed_on_missing_data();
    package_link_table_retains_exact_object_identities();
    package_linker_resolves_exact_export_identity();
    package_linker_keeps_same_named_top_level_export();
    package_linker_retains_virtual_package_groups();
    package_linker_collapses_observed_same_name_group_alias();
    package_linker_retains_native_object_requirements();
    package_linker_fails_closed_on_missing_or_ambiguous_export();
    level_handles_decode_actor_slots_and_world_model();
    player_start_census_decodes_only_direct_level_actor_properties();
    actor_visual_census_retains_slots_and_instance_overrides();
    actor_visual_census_accepts_empty_cutscene_aliases();
    model_census_decodes_only_bounded_collection_framing();
    skeletal_mesh_census_decodes_exact_ue1_framing();
    bsp_topology_decodes_and_validates_active_cross_indices();
    bsp_triangle_mesh_maps_axes_winding_and_degenerates();
    c_abi_loads_profile_without_cpp_ownership_crossing_boundary();
    c_abi_short_buffers_do_not_receive_partial_profile_data();
    c_abi_bsp_slice_is_bounded_and_uses_two_call_ownership();
    c_abi_player_start_retains_identity_and_uses_bsp_axes();
    c_abi_actor_visual_selects_exact_level_reference();
    p8_texture_decoder_validates_mips_palette_and_rgba();
    scorer_matches_coverage_contract();
    scorer_rejects_invalid_values();
    lesson_period_uses_all_five_hundred_slots();
    hp1_animation_key_codec_matches_retail_contract();
    std::cout << "hpvr_hp1_gesture_tests: all checks passed\n";
    return EXIT_SUCCESS;
}
