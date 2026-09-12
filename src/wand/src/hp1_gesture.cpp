#include "hpvr/hp1_gesture.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace hpvr::wand {
namespace {

constexpr std::uint32_t kPackageMagic{0x9E2A83C1U};
constexpr std::uint16_t kCompactNameLengthVersion{64};
constexpr std::size_t kMaximumPackageBytes{512U * 1024U * 1024U};
constexpr std::size_t kMaximumTableEntries{1'000'000};
constexpr std::size_t kMaximumNameBytes{64U * 1024U};
constexpr std::size_t kMaximumSerializedStringUnits{1024U * 1024U};
constexpr std::size_t kMaximumUrlOptions{4096};
constexpr std::size_t kMaximumTextureMips{32};
constexpr std::uint32_t kMaximumTextureDimension{4096};
constexpr std::size_t kMaximumTexturePixels{
    static_cast<std::size_t>(kMaximumTextureDimension) *
    kMaximumTextureDimension};
constexpr std::size_t kPaletteEntryCount{256};
constexpr float kDuplicateEpsilon{0.001F};
constexpr std::uint32_t kHasStackFlag{0x0200'0000U};

[[nodiscard]] constexpr bool supported_package_version(
    std::uint16_t version) noexcept {
    switch (version) {
        case 61:
        case 68:
        case 69:
        case 72:
        case 73:
        case 75:
        case 76:
            return true;
        default:
            return false;
    }
}

class ProfileException final : public std::runtime_error {
public:
    ProfileException(Hp1ProfileStatus status, std::string message)
        : std::runtime_error(std::move(message)), status_(status) {}

    [[nodiscard]] Hp1ProfileStatus status() const noexcept { return status_; }

private:
    Hp1ProfileStatus status_;
};

[[noreturn]] void fail(Hp1ProfileStatus status, std::string_view message) {
    throw ProfileException(status, std::string(message));
}

class Cursor {
public:
    explicit Cursor(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::size_t position() const noexcept { return position_; }
    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }

    [[nodiscard]] std::uint8_t read_u8() {
        return take(1).front();
    }

    [[nodiscard]] std::uint16_t read_u16() {
        const auto bytes = take(2);
        return static_cast<std::uint16_t>(bytes[0]) |
               static_cast<std::uint16_t>(bytes[1]) << 8U;
    }

    [[nodiscard]] std::int16_t read_i16() {
        return std::bit_cast<std::int16_t>(read_u16());
    }

    [[nodiscard]] std::uint32_t read_u32() {
        const auto bytes = take(4);
        return static_cast<std::uint32_t>(bytes[0]) |
               static_cast<std::uint32_t>(bytes[1]) << 8U |
               static_cast<std::uint32_t>(bytes[2]) << 16U |
               static_cast<std::uint32_t>(bytes[3]) << 24U;
    }

    [[nodiscard]] std::int32_t read_i32() {
        return std::bit_cast<std::int32_t>(read_u32());
    }

    [[nodiscard]] std::uint64_t read_u64() {
        const auto low = static_cast<std::uint64_t>(read_u32());
        const auto high = static_cast<std::uint64_t>(read_u32());
        return low | high << 32U;
    }

    [[nodiscard]] float read_f32() {
        return std::bit_cast<float>(read_u32());
    }

    [[nodiscard]] std::int32_t read_compact_index() {
        const std::uint8_t first = read_u8();
        const bool negative = (first & 0x80U) != 0;
        std::uint32_t value = first & 0x3FU;
        if ((first & 0x40U) != 0) {
            unsigned shift = 6;
            bool complete = false;
            for (unsigned part = 1; part <= 4; ++part) {
                const std::uint8_t next = read_u8();
                if (part == 4 && (next & 0x60U) != 0) {
                    fail(Hp1ProfileStatus::invalid_package,
                         "UE1 compact index has noncanonical high bits");
                }
                const std::uint8_t mask = part == 4 ? 0x1FU : 0x7FU;
                value |= static_cast<std::uint32_t>(next & mask) << shift;
                if ((next & 0x80U) == 0) {
                    complete = true;
                    break;
                }
                shift += 7;
            }
            if (!complete) {
                fail(Hp1ProfileStatus::invalid_package,
                     "unterminated UE1 compact index");
            }
        }
        if (negative && value == 0) {
            fail(Hp1ProfileStatus::invalid_package,
                 "UE1 compact index encodes negative zero");
        }
        if (value > static_cast<std::uint32_t>(
                        std::numeric_limits<std::int32_t>::max())) {
            fail(Hp1ProfileStatus::invalid_package,
                 "UE1 compact index exceeds signed range");
        }
        const auto signed_value = static_cast<std::int32_t>(value);
        return negative ? -signed_value : signed_value;
    }

    [[nodiscard]] std::size_t read_array_index() {
        const std::uint8_t first = read_u8();
        if ((first & 0xC0U) == 0xC0U) {
            return static_cast<std::size_t>(first & 0x3FU) << 24U |
                   static_cast<std::size_t>(read_u8()) << 16U |
                   static_cast<std::size_t>(read_u8()) << 8U |
                   static_cast<std::size_t>(read_u8());
        }
        if ((first & 0x80U) != 0) {
            return static_cast<std::size_t>(first & 0x7FU) << 8U |
                   static_cast<std::size_t>(read_u8());
        }
        return first;
    }

    [[nodiscard]] std::span<const std::uint8_t> take(std::size_t count) {
        if (count > remaining()) {
            fail(Hp1ProfileStatus::invalid_package,
                 "serialized object extends beyond its checked range");
        }
        const auto result = bytes_.subspan(position_, count);
        position_ += count;
        return result;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t position_{};
};

[[nodiscard]] bool ascii_equal_fold(std::string_view left,
                                    std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto fold = [](char value) {
            if (value >= 'A' && value <= 'Z') {
                return static_cast<char>(value + ('a' - 'A'));
            }
            return value;
        };
        if (fold(left[index]) != fold(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::size_t checked_count(std::int32_t value,
                                        std::size_t maximum,
                                        std::string_view field) {
    if (value < 0 || static_cast<std::size_t>(value) > maximum) {
        fail(Hp1ProfileStatus::invalid_package,
             std::string(field) + " has an invalid count " +
                 std::to_string(value));
    }
    return static_cast<std::size_t>(value);
}

struct CachedPackageBytes {
    std::filesystem::file_time_type modified;
    std::uintmax_t size=0;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    std::size_t used=0;
};
thread_local unsigned package_read_depth=0;
thread_local std::map<std::filesystem::path,CachedPackageBytes> package_read_cache;
thread_local Hp1PackageReadStats package_read_stats;
thread_local std::size_t package_read_clock=0;
constexpr std::size_t kPackageReadCacheLimit=96U*1024U*1024U;

[[nodiscard]] std::shared_ptr<const std::vector<std::uint8_t>> read_package_file(
    const std::filesystem::path& path) {
    const auto key=std::filesystem::absolute(path).lexically_normal();
    std::error_code error;
    const auto modified=std::filesystem::last_write_time(key,error);
    std::error_code size_error;
    const auto size=std::filesystem::file_size(key,size_error);
    const bool cacheable=package_read_depth&&!error&&!size_error&&size<=kPackageReadCacheLimit;
    if(cacheable){
        const auto cached=package_read_cache.find(key);
        if(cached!=package_read_cache.end()){
            if(cached->second.modified==modified&&cached->second.size==size){
                ++package_read_stats.hits;cached->second.used=++package_read_clock;
                return cached->second.bytes;
            }
            package_read_stats.retained_bytes-=cached->second.bytes->size();
            package_read_cache.erase(cached);
        }
    }
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        fail(Hp1ProfileStatus::io_error, "could not open package read-only");
    }
    const auto end = stream.tellg();
    if (end <= std::streampos{0}) {
        fail(Hp1ProfileStatus::invalid_package,
             "package size is outside the supported range");
    }
    const auto end_offset = static_cast<std::streamoff>(end);
    if (end_offset <= 0 ||
        static_cast<std::uint64_t>(end_offset) > kMaximumPackageBytes) {
        fail(Hp1ProfileStatus::invalid_package,
             "package size is outside the supported range");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end_offset));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        fail(Hp1ProfileStatus::io_error, "could not read complete package");
    }
    auto storage=std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
    if(package_read_depth)++package_read_stats.reads;
    if(cacheable&&storage->size()==size){
        while(!package_read_cache.empty()&&(package_read_cache.size()>=8||
              package_read_stats.retained_bytes+storage->size()>kPackageReadCacheLimit)){
            const auto oldest=std::min_element(package_read_cache.begin(),package_read_cache.end(),
                [](const auto& a,const auto& b){return a.second.used<b.second.used;});
            package_read_stats.retained_bytes-=oldest->second.bytes->size();
            package_read_cache.erase(oldest);
        }
        package_read_stats.retained_bytes+=storage->size();
        package_read_cache.emplace(key,CachedPackageBytes{modified,size,storage,++package_read_clock});
    }
    return storage;
}

struct ImportEntry {
    std::size_t class_package{};
    std::size_t class_name{};
    std::int32_t outer{};
    std::size_t object_name{};
};

struct ExportEntry {
    std::int32_t class_reference{};
    std::int32_t super_reference{};
    std::int32_t outer{};
    std::size_t object_name{};
    std::uint32_t object_flags{};
    std::size_t serial_offset{};
    std::size_t serial_size{};
};

class Package {
public:
    explicit Package(const std::filesystem::path& path)
        : bytes_storage_(read_package_file(path)), bytes_(*bytes_storage_) {
        parse();
    }

    [[nodiscard]] std::uint16_t version() const noexcept { return version_; }
    [[nodiscard]] std::uint16_t licensee_version() const noexcept {
        return licensee_version_;
    }
    [[nodiscard]] std::size_t file_size() const noexcept { return bytes_.size(); }
    [[nodiscard]] std::size_t name_count() const noexcept {
        return names_.size();
    }
    [[nodiscard]] std::size_t import_count() const noexcept {
        return imports_.size();
    }
    [[nodiscard]] const ImportEntry& import_at(std::size_t index) const {
        if (index >= imports_.size()) {
            fail(Hp1ProfileStatus::invalid_package,
                 "import index is outside the package table");
        }
        return imports_[index];
    }
    [[nodiscard]] std::size_t export_count() const noexcept {
        return exports_.size();
    }
    [[nodiscard]] const ExportEntry& export_at(std::size_t index) const {
        if (index >= exports_.size()) {
            fail(Hp1ProfileStatus::invalid_package,
                 "export index is outside the package table");
        }
        return exports_[index];
    }
    [[nodiscard]] std::string_view export_name(std::size_t index) const {
        return names_[export_at(index).object_name];
    }
    [[nodiscard]] bool reference_is_class(
        std::int32_t reference,
        std::string_view package_name,
        std::string_view object_name) const {
        if (reference >= 0) {
            return false;
        }
        const auto index = static_cast<std::size_t>(
            -static_cast<std::int64_t>(reference) - 1);
        if (index >= imports_.size()) {
            fail(Hp1ProfileStatus::invalid_package,
                 "import reference is outside the package table");
        }
        const auto& entry = imports_[index];
        if (!ascii_equal_fold(names_[entry.class_package], "Core") ||
            !ascii_equal_fold(names_[entry.class_name], "Class")) {
            return false;
        }
        const auto path = reference_path(reference);
        return path.size() == 2 &&
               ascii_equal_fold(path[0], package_name) &&
               ascii_equal_fold(path[1], object_name);
    }
    [[nodiscard]] std::span<const std::uint8_t> export_bytes(
        std::size_t index) const {
        const auto& entry = export_at(index);
        return std::span<const std::uint8_t>(bytes_)
            .subspan(entry.serial_offset, entry.serial_size);
    }
    [[nodiscard]] std::string_view name(std::int32_t index) const {
        if (index < 0 || static_cast<std::size_t>(index) >= names_.size()) {
            fail(Hp1ProfileStatus::invalid_package,
                 "name index is outside the package table");
        }
        return names_[static_cast<std::size_t>(index)];
    }
    [[nodiscard]] std::string class_name_for_export(std::size_t index) const {
        const auto reference = export_at(index).class_reference;
        if (reference == 0) {
            return "Core.Class";
        }
        const auto path = reference_path(reference);
        if (path.empty()) {
            fail(Hp1ProfileStatus::invalid_package,
                 "export class reference has an empty object path");
        }
        std::string result;
        for (const auto component : path) {
            if (!result.empty()) {
                result.push_back('.');
            }
            result.append(component);
        }
        return result;
    }
    [[nodiscard]] std::string qualified_class_name_for_import(
        std::size_t index) const {
        const auto& entry = import_at(index);
        return std::string(names_[entry.class_package]) + "." +
               names_[entry.class_name];
    }
    [[nodiscard]] std::string qualified_class_name_for_export(
        std::size_t index,
        std::string_view current_package_name) const {
        const auto reference = export_at(index).class_reference;
        if (reference == 0) {
            return "Core.Class";
        }
        auto result = class_name_for_export(index);
        if (reference > 0) {
            result = std::string(current_package_name) + "." + result;
        }
        return result;
    }
    [[nodiscard]] std::vector<std::string> object_path(
        std::int32_t reference) const {
        const auto views = reference_path(reference);
        std::vector<std::string> result;
        result.reserve(views.size());
        for (const auto component : views) {
            result.emplace_back(component);
        }
        return result;
    }
    [[nodiscard]] std::vector<std::string> imported_packages() const {
        std::set<std::string> unique;
        for (const auto& entry : imports_) {
            if (entry.outer == 0 &&
                ascii_equal_fold(names_[entry.class_package], "Core") &&
                ascii_equal_fold(names_[entry.class_name], "Package")) {
                unique.emplace(names_[entry.object_name]);
            }
        }
        return {unique.begin(), unique.end()};
    }
    void require_valid_reference(std::int32_t reference) const {
        validate_reference(reference);
    }

private:
    [[nodiscard]] std::vector<std::string_view> reference_path(
        std::int32_t reference) const {
        std::vector<std::string_view> reversed;
        const auto maximum_depth = imports_.size() + exports_.size() + 1;
        while (reference != 0) {
            if (reversed.size() >= maximum_depth) {
                fail(Hp1ProfileStatus::invalid_package,
                     "object outer chain contains a cycle");
            }
            if (reference > 0) {
                const auto index = static_cast<std::size_t>(reference - 1);
                const auto& entry = export_at(index);
                reversed.push_back(names_[entry.object_name]);
                reference = entry.outer;
            } else {
                const auto index = static_cast<std::size_t>(
                    -static_cast<std::int64_t>(reference) - 1);
                if (index >= imports_.size()) {
                    fail(Hp1ProfileStatus::invalid_package,
                         "import reference is outside the package table");
                }
                const auto& entry = imports_[index];
                reversed.push_back(names_[entry.object_name]);
                reference = entry.outer;
            }
        }
        std::reverse(reversed.begin(), reversed.end());
        return reversed;
    }

    void parse() {
        Cursor header(bytes_);
        if (header.read_u32() != kPackageMagic) {
            fail(Hp1ProfileStatus::invalid_package,
                 "file does not have the UE1 package magic");
        }
        version_ = header.read_u16();
        licensee_version_ = header.read_u16();
        if (!supported_package_version(version_) || licensee_version_ != 0) {
            fail(Hp1ProfileStatus::unsupported_package,
                 "package is not one of the observed shipped UE1 versions "
                 "61/68/69/72/73/75/76 with licensee version 0");
        }
        static_cast<void>(header.read_u32());
        const auto name_count = checked_count(
            header.read_i32(), kMaximumTableEntries, "name table");
        const auto name_offset = checked_offset(header.read_i32(), "name table");
        const auto export_count = checked_count(
            header.read_i32(), kMaximumTableEntries, "export table");
        const auto export_offset =
            checked_offset(header.read_i32(), "export table");
        const auto import_count = checked_count(
            header.read_i32(), kMaximumTableEntries, "import table");
        const auto import_offset =
            checked_offset(header.read_i32(), "import table");

        parse_names(name_offset, name_count);
        parse_imports(import_offset, import_count);
        parse_exports(export_offset, export_count);

        for (const auto& entry : imports_) {
            validate_reference(entry.outer);
        }
        for (const auto& entry : exports_) {
            validate_reference(entry.class_reference);
            validate_reference(entry.super_reference);
            validate_reference(entry.outer);
        }
    }

    [[nodiscard]] std::size_t checked_offset(std::int32_t value,
                                             std::string_view field) const {
        if (value < 0 || static_cast<std::size_t>(value) > bytes_.size()) {
            fail(Hp1ProfileStatus::invalid_package,
                 std::string(field) + " has an invalid offset");
        }
        return static_cast<std::size_t>(value);
    }

    void parse_names(std::size_t offset, std::size_t count) {
        Cursor cursor(std::span<const std::uint8_t>(bytes_).subspan(offset));
        names_.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            if (version_ >= kCompactNameLengthVersion) {
                const auto encoded_length = cursor.read_compact_index();
                if (encoded_length <= 0 ||
                    static_cast<std::size_t>(encoded_length) >
                        kMaximumNameBytes) {
                    fail(Hp1ProfileStatus::unsupported_package,
                         "package uses an unsupported name encoding");
                }
                const auto raw =
                    cursor.take(static_cast<std::size_t>(encoded_length));
                if (raw.empty() || raw.back() != 0) {
                    fail(Hp1ProfileStatus::invalid_package,
                         "package name is not null terminated");
                }
                names_.emplace_back(
                    reinterpret_cast<const char*>(raw.data()),
                    raw.size() - 1);
            } else {
                std::string name;
                name.reserve(32);
                bool terminated = false;
                for (std::size_t byte = 0; byte < kMaximumNameBytes; ++byte) {
                    const auto value = cursor.read_u8();
                    if (value == 0) {
                        terminated = true;
                        break;
                    }
                    name.push_back(static_cast<char>(value));
                }
                if (!terminated || name.empty()) {
                    fail(Hp1ProfileStatus::invalid_package,
                         "legacy package name is empty or unterminated");
                }
                names_.push_back(std::move(name));
            }
            static_cast<void>(cursor.read_u32());
        }
        if (std::ranges::none_of(
                names_,
                [](const auto& name) { return ascii_equal_fold(name, "None"); })) {
            fail(Hp1ProfileStatus::invalid_package,
                 "package name table has no None terminator name");
        }
    }

    [[nodiscard]] std::size_t checked_name(std::int32_t value) const {
        if (value < 0 || static_cast<std::size_t>(value) >= names_.size()) {
            fail(Hp1ProfileStatus::invalid_package,
                 "serialized name reference is out of range");
        }
        return static_cast<std::size_t>(value);
    }

    void parse_imports(std::size_t offset, std::size_t count) {
        Cursor cursor(std::span<const std::uint8_t>(bytes_).subspan(offset));
        imports_.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto class_package =
                checked_name(cursor.read_compact_index());
            const auto class_name = checked_name(cursor.read_compact_index());
            const auto outer = cursor.read_i32();
            const auto object_name = checked_name(cursor.read_compact_index());
            imports_.push_back(
                {class_package, class_name, outer, object_name});
        }
    }

    void parse_exports(std::size_t offset, std::size_t count) {
        Cursor cursor(std::span<const std::uint8_t>(bytes_).subspan(offset));
        exports_.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto class_reference = cursor.read_compact_index();
            const auto super_reference = cursor.read_compact_index();
            const auto outer = cursor.read_i32();
            const auto object_name = checked_name(cursor.read_compact_index());
            const auto object_flags = cursor.read_u32();
            const auto signed_size = cursor.read_compact_index();
            if (signed_size < 0) {
                fail(Hp1ProfileStatus::invalid_package,
                     "export has a negative serial size");
            }
            const auto serial_size = static_cast<std::size_t>(signed_size);
            std::size_t serial_offset = 0;
            if (serial_size != 0) {
                serial_offset =
                    checked_offset(cursor.read_compact_index(), "export payload");
                if (serial_size > bytes_.size() - serial_offset) {
                    fail(Hp1ProfileStatus::invalid_package,
                         "export payload exceeds the package");
                }
            }
            exports_.push_back({
                class_reference,
                super_reference,
                outer,
                object_name,
                object_flags,
                serial_offset,
                serial_size,
            });
        }
    }

    void validate_reference(std::int32_t reference) const {
        if (reference > 0 &&
            static_cast<std::size_t>(reference - 1) >= exports_.size()) {
            fail(Hp1ProfileStatus::invalid_package,
                 "export reference is out of range");
        }
        if (reference < 0) {
            const auto wide = -static_cast<std::int64_t>(reference) - 1;
            if (static_cast<std::size_t>(wide) >= imports_.size()) {
                fail(Hp1ProfileStatus::invalid_package,
                     "import reference is out of range");
            }
        }
    }

    std::shared_ptr<const std::vector<std::uint8_t>> bytes_storage_;
    std::span<const std::uint8_t> bytes_;
    std::uint16_t version_{};
    std::uint16_t licensee_version_{};
    std::vector<std::string> names_;
    std::vector<ImportEntry> imports_;
    std::vector<ExportEntry> exports_;
};

enum class PropertyKind : std::uint8_t {
    byte = 1,
    integer = 2,
    boolean = 3,
    floating = 4,
    object = 5,
    name = 6,
    string = 7,
    class_object = 8,
    array = 9,
    structure = 10,
    vector = 11,
    rotator = 12,
    text_string = 13,
};

struct PropertyTag {
    std::string_view name;
    PropertyKind kind{};
    std::optional<std::string_view> structure_name;
    std::optional<std::size_t> array_index;
    std::optional<bool> boolean_value;
    std::span<const std::uint8_t> value;
};

[[nodiscard]] std::int32_t property_reference(const PropertyTag& tag);

[[nodiscard]] Hp1ClassDefaultProperty retain_property(
    const Package& package,
    const PropertyTag& tag) {
    Hp1ClassDefaultProperty property;
    property.name = tag.name;
    property.kind = static_cast<std::uint8_t>(tag.kind);
    if (tag.structure_name.has_value()) {
        property.structure_name = *tag.structure_name;
    }
    if (tag.array_index.has_value()) {
        property.array_index = static_cast<std::int64_t>(*tag.array_index);
    }
    if (tag.boolean_value.has_value()) {
        property.boolean_value = *tag.boolean_value;
        property.boolean_value_serialized = true;
    }
    property.value.assign(tag.value.begin(), tag.value.end());
    if (tag.kind == PropertyKind::object ||
        tag.kind == PropertyKind::class_object) {
        property.object_reference = property_reference(tag);
        package.require_valid_reference(property.object_reference);
        property.object_path = package.object_path(property.object_reference);
        property.object_reference_serialized = true;
    } else if (tag.kind == PropertyKind::name) {
        Cursor value(tag.value);
        property.text_value = package.name(value.read_compact_index());
        if (value.remaining() != 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Name property has trailing bytes");
        }
        property.text_value_serialized = true;
    } else if (tag.kind == PropertyKind::string ||
               tag.kind == PropertyKind::text_string) {
        Cursor value(tag.value);
        const auto count = value.read_compact_index();
        if (count <= 0 || static_cast<std::size_t>(count) > value.remaining()) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "String property has an invalid byte count");
        }
        const auto bytes = value.take(static_cast<std::size_t>(count));
        if (value.remaining() != 0 || bytes.empty() || bytes.back() != 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "String property has invalid termination");
        }
        property.text_value.assign(
            reinterpret_cast<const char*>(bytes.data()), bytes.size() - 1U);
        property.text_value_serialized = true;
    } else if (tag.kind == PropertyKind::structure &&
               tag.structure_name.has_value() &&
               (ascii_equal_fold(*tag.structure_name, "CutCast") ||
                ascii_equal_fold(*tag.structure_name, "CutLoc"))) {
        Cursor value(tag.value);
        property.object_reference = value.read_compact_index();
        package.require_valid_reference(property.object_reference);
        property.object_path = package.object_path(property.object_reference);
        property.object_reference_serialized = true;
        const auto count = value.read_compact_index();
        if (count < 0 || static_cast<std::size_t>(count) > value.remaining()) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "CutScene structure alias has an invalid byte count");
        }
        if (count == 0) {
            // An unassigned cast/marker alias is serialized as an empty FString.
            property.text_value_serialized = true;
            return property;
        }
        const auto bytes = value.take(static_cast<std::size_t>(count));
        if (bytes.empty() || bytes.back() != 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "CutScene structure alias is not terminated");
        }
        property.text_value.assign(
            reinterpret_cast<const char*>(bytes.data()), bytes.size() - 1U);
        property.text_value_serialized = true;
    }
    return property;
}

[[nodiscard]] std::optional<PropertyTag> read_property_tag(
    const Package& package,
    Cursor& cursor) {
    const auto name = package.name(cursor.read_compact_index());
    if (ascii_equal_fold(name, "None")) {
        return std::nullopt;
    }
    const std::uint8_t info = cursor.read_u8();
    const auto raw_kind = static_cast<std::uint8_t>(info & 0x0FU);
    if (raw_kind == 0 || raw_kind > 15) {
        fail(Hp1ProfileStatus::invalid_package,
             "property tag has an invalid kind");
    }
    const auto kind = static_cast<PropertyKind>(raw_kind);
    std::optional<std::string_view> structure_name;
    if (kind == PropertyKind::structure) {
        structure_name = package.name(cursor.read_compact_index());
    }
    std::size_t size = 0;
    switch ((info >> 4U) & 0x07U) {
        case 0:
            size = 1;
            break;
        case 1:
            size = 2;
            break;
        case 2:
            size = 4;
            break;
        case 3:
            size = 12;
            break;
        case 4:
            size = 16;
            break;
        case 5:
            size = cursor.read_u8();
            break;
        case 6:
            size = cursor.read_u16();
            break;
        case 7:
            size = cursor.read_u32();
            break;
        default:
            break;
    }
    if (kind == PropertyKind::boolean) {
        size = 0;
    }
    const std::optional<bool> boolean_value =
        kind == PropertyKind::boolean
            ? std::optional<bool>{(info & 0x80U) != 0}
            : std::nullopt;
    std::optional<std::size_t> array_index;
    if ((info & 0x80U) != 0 && kind != PropertyKind::boolean) {
        array_index = cursor.read_array_index();
    }
    return PropertyTag{name, kind, structure_name, array_index, boolean_value,
                       cursor.take(size)};
}

[[nodiscard]] Hp1BspVector property_vector(const PropertyTag& tag,
                                           std::string_view field) {
    if (tag.kind != PropertyKind::structure ||
        !tag.structure_name.has_value() ||
        !ascii_equal_fold(*tag.structure_name, "Vector") ||
        tag.array_index.has_value() || tag.value.size() != 12) {
        fail(Hp1ProfileStatus::invalid_profile,
             std::string(field) + " is not one scalar Vector property");
    }
    Cursor cursor(tag.value);
    const Hp1BspVector value{
        cursor.read_f32(), cursor.read_f32(), cursor.read_f32()};
    if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
        !std::isfinite(value.z)) {
        fail(Hp1ProfileStatus::invalid_profile,
             std::string(field) + " contains a non-finite component");
    }
    return value;
}

[[nodiscard]] std::array<std::int32_t, 3> property_rotator(
    const PropertyTag& tag,
    std::string_view field) {
    if (tag.kind != PropertyKind::structure ||
        !tag.structure_name.has_value() ||
        !ascii_equal_fold(*tag.structure_name, "Rotator") ||
        tag.array_index.has_value() || tag.value.size() != 12) {
        fail(Hp1ProfileStatus::invalid_profile,
             std::string(field) + " is not one scalar Rotator property");
    }
    Cursor cursor(tag.value);
    return {cursor.read_i32(), cursor.read_i32(), cursor.read_i32()};
}

void skip_object_stack(Cursor& cursor, std::uint32_t object_flags) {
    if ((object_flags & kHasStackFlag) == 0) {
        return;
    }
    const auto function = cursor.read_compact_index();
    static_cast<void>(cursor.read_compact_index());
    static_cast<void>(cursor.read_u64());
    static_cast<void>(cursor.read_i32());
    if (function != 0) {
        static_cast<void>(cursor.read_compact_index());
    }
}

void skip_serialized_string(Cursor& cursor) {
    const auto start = cursor.position();
    const auto signed_count = cursor.read_compact_index();
    if (signed_count == 0) {
        return;
    }
    const auto wide_count = signed_count < 0
                                ? -static_cast<std::int64_t>(signed_count)
                                : static_cast<std::int64_t>(signed_count);
    if (wide_count <= 0 ||
        static_cast<std::uint64_t>(wide_count) >
            kMaximumSerializedStringUnits) {
        fail(Hp1ProfileStatus::invalid_profile,
             "Level FURL string has an invalid length");
    }
    const auto count = static_cast<std::size_t>(wide_count);
    const auto bytes_per_unit = signed_count < 0 ? 2U : 1U;
    if (count > std::numeric_limits<std::size_t>::max() / bytes_per_unit) {
        fail(Hp1ProfileStatus::invalid_profile,
             "Level FURL string byte size overflowed");
    }
    const auto bytes = cursor.take(count * bytes_per_unit);
    if (bytes_per_unit == 1U) {
        if (bytes.back() != 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Level FURL ANSI string is not null terminated");
        }
    } else if (bytes[bytes.size() - 2] != 0 || bytes.back() != 0) {
        fail(Hp1ProfileStatus::invalid_profile,
             "Level FURL Unicode string is not null terminated at byte " +
                 std::to_string(start) + " with count " +
                 std::to_string(signed_count));
    }
}

void skip_level_url(Cursor& cursor) {
    skip_serialized_string(cursor);  // protocol
    skip_serialized_string(cursor);  // host
    skip_serialized_string(cursor);  // map
    skip_serialized_string(cursor);  // portal
    const auto option_count = checked_count(
        cursor.read_compact_index(), kMaximumUrlOptions, "Level FURL options");
    for (std::size_t index = 0; index < option_count; ++index) {
        skip_serialized_string(cursor);
    }
    static_cast<void>(cursor.read_i32());  // port
    static_cast<void>(cursor.read_i32());  // valid
}

[[nodiscard]] std::size_t read_serialized_count(Cursor& cursor,
                                                std::string_view field) {
    return checked_count(cursor.read_compact_index(), kMaximumTableEntries,
                         field);
}

void skip_repeated_bytes(Cursor& cursor,
                         std::size_t count,
                         std::size_t element_size,
                         std::string_view field) {
    if (count > std::numeric_limits<std::size_t>::max() / element_size) {
        fail(Hp1ProfileStatus::invalid_package,
             std::string(field) + " byte size overflowed");
    }
    static_cast<void>(cursor.take(count * element_size));
}

[[nodiscard]] std::size_t read_lazy_fixed_array(
    Cursor& cursor,
    const ExportEntry& export_entry,
    std::size_t element_size,
    std::string_view field) {
    const auto skip_position = cursor.read_i32();
    const auto count = read_serialized_count(cursor, field);
    if (skip_position < 0 ||
        export_entry.serial_offset >
            std::numeric_limits<std::size_t>::max() - cursor.position()) {
        fail(Hp1ProfileStatus::invalid_package,
             std::string(field) + " has an invalid lazy-array end offset");
    }
    const auto data_position = export_entry.serial_offset + cursor.position();
    if (count > std::numeric_limits<std::size_t>::max() / element_size ||
        data_position > std::numeric_limits<std::size_t>::max() -
                            count * element_size ||
        static_cast<std::size_t>(skip_position) !=
            data_position + count * element_size) {
        fail(Hp1ProfileStatus::invalid_profile,
             std::string(field) + " lazy-array end offset disagrees with count");
    }
    skip_repeated_bytes(cursor, count, element_size, field);
    return count;
}

[[nodiscard]] float read_finite_geometry_float(Cursor& cursor,
                                               std::string_view field);

void skip_box(Cursor& cursor, std::string_view field) {
    for (int component = 0; component < 6; ++component) {
        static_cast<void>(read_finite_geometry_float(cursor, field));
    }
    static_cast<void>(cursor.read_u8());
}

void skip_sphere(Cursor& cursor, std::string_view field) {
    for (int component = 0; component < 4; ++component) {
        static_cast<void>(read_finite_geometry_float(cursor, field));
    }
}

void skip_mesh_animation_sequence(const Package& package,
                                  Cursor& cursor) {
    static_cast<void>(package.name(cursor.read_compact_index()));
    static_cast<void>(package.name(cursor.read_compact_index()));
    static_cast<void>(cursor.read_i32());
    static_cast<void>(cursor.read_i32());
    const auto notify_count =
        read_serialized_count(cursor, "SkeletalMesh animation notifies");
    for (std::size_t index = 0; index < notify_count; ++index) {
        static_cast<void>(read_finite_geometry_float(
            cursor, "SkeletalMesh animation notify time"));
        static_cast<void>(package.name(cursor.read_compact_index()));
    }
    static_cast<void>(read_finite_geometry_float(
        cursor, "SkeletalMesh animation rate"));
}

[[nodiscard]] std::size_t find_top_level_level(const Package& package) {
    std::optional<std::size_t> level_index;
    for (std::size_t index = 0; index < package.export_count(); ++index) {
        const auto& entry = package.export_at(index);
        if (entry.outer == 0 &&
            package.reference_is_class(
                entry.class_reference, "Engine", "Level")) {
            if (level_index.has_value()) {
                fail(Hp1ProfileStatus::ambiguous_object,
                     "map contains more than one top-level Engine.Level");
            }
            level_index = index;
        }
    }
    if (!level_index.has_value()) {
        fail(Hp1ProfileStatus::object_not_found,
             "top-level Engine.Level export was not found");
    }
    return *level_index;
}

void skip_properties(const Package& package,
                     Cursor& cursor,
                     std::string_view object_name) {
    while (cursor.remaining() != 0) {
        if (!read_property_tag(package, cursor).has_value()) {
            return;
        }
    }
    fail(Hp1ProfileStatus::invalid_profile,
         std::string(object_name) + " properties have no terminator");
}

[[nodiscard]] std::int32_t read_level_model_reference(
    const Package& package,
    std::size_t level_index) {
    const auto& level = package.export_at(level_index);
    Cursor cursor(package.export_bytes(level_index));
    skip_object_stack(cursor, level.object_flags);
    skip_properties(package, cursor, "Level");
    const auto actor_count = checked_count(
        cursor.read_i32(), kMaximumTableEntries, "Level Actors");
    const auto serialized_actor_count = checked_count(
        cursor.read_i32(), kMaximumTableEntries, "Level serialized Actors");
    if (serialized_actor_count != actor_count) {
        fail(Hp1ProfileStatus::invalid_profile,
             "Level actor transaction counts disagree");
    }
    for (std::size_t index = 0; index < actor_count; ++index) {
        const auto reference = cursor.read_compact_index();
        package.require_valid_reference(reference);
        if (reference < 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Level Actors contains a non-local import reference");
        }
    }
    skip_level_url(cursor);
    const auto model_reference = cursor.read_compact_index();
    package.require_valid_reference(model_reference);
    if (model_reference <= 0) {
        fail(Hp1ProfileStatus::invalid_profile,
             "Level world model is not a local export reference");
    }
    const auto model_index = static_cast<std::size_t>(model_reference - 1);
    if (!ascii_equal_fold(
            package.class_name_for_export(model_index), "Engine.Model")) {
        fail(Hp1ProfileStatus::invalid_profile,
             "Level world model reference is not an Engine.Model export");
    }
    return model_reference;
}

[[nodiscard]] float read_finite_geometry_float(Cursor& cursor,
                                               std::string_view field) {
    const float value = cursor.read_f32();
    if (!std::isfinite(value)) {
        fail(Hp1ProfileStatus::invalid_profile,
             std::string(field) + " contains a non-finite value");
    }
    return value;
}

[[nodiscard]] Hp1BspVector read_bsp_vector(Cursor& cursor,
                                           std::string_view field) {
    return {
        read_finite_geometry_float(cursor, field),
        read_finite_geometry_float(cursor, field),
        read_finite_geometry_float(cursor, field),
    };
}

void require_topology_index(std::int32_t value,
                            std::size_t count,
                            bool allow_none,
                            std::string_view field) {
    if ((allow_none && value == -1) ||
        (value >= 0 && static_cast<std::size_t>(value) < count)) {
        return;
    }
    fail(Hp1ProfileStatus::invalid_profile,
         std::string(field) + " " + std::to_string(value) +
             " is outside target count " + std::to_string(count));
}

[[nodiscard]] Hp1BspTopology decode_bsp_topology(
    const Package& package,
    std::int32_t model_reference) {
    Hp1BspTopology result;
    const auto model_index = static_cast<std::size_t>(model_reference - 1);
    const auto& model = package.export_at(model_index);
    Cursor cursor(package.export_bytes(model_index));
    skip_object_stack(cursor, model.object_flags);
    skip_properties(package, cursor, "Model");
    static_cast<void>(cursor.take(25));  // UPrimitive FBox
    static_cast<void>(cursor.take(16));  // post-v61 UPrimitive FSphere

    const auto vector_count = read_serialized_count(cursor, "Model Vectors");
    result.vectors.reserve(vector_count);
    for (std::size_t index = 0; index < vector_count; ++index) {
        result.vectors.push_back(read_bsp_vector(cursor, "Model Vectors"));
    }
    const auto point_count = read_serialized_count(cursor, "Model Points");
    result.points.reserve(point_count);
    for (std::size_t index = 0; index < point_count; ++index) {
        result.points.push_back(read_bsp_vector(cursor, "Model Points"));
    }

    const auto node_count = read_serialized_count(cursor, "Model Nodes");
    result.nodes.reserve(node_count);
    for (std::size_t index = 0; index < node_count; ++index) {
        Hp1BspNode node;
        for (float& component : node.plane) {
            component = read_finite_geometry_float(cursor, "Model Nodes plane");
        }
        node.zone_mask = cursor.read_u64();
        node.flags = cursor.read_u8();
        node.vertex_pool_index = cursor.read_compact_index();
        node.surface_index = cursor.read_compact_index();
        // Authored zone-actor samples establish back/front/coplanar order.
        node.back_node_index = cursor.read_compact_index();
        node.front_node_index = cursor.read_compact_index();
        node.coplanar_node_index = cursor.read_compact_index();
        // Serialized node field +0x2C is not exposed; owned data proves it is
        // not a safe index into the serialized Nodes array when Linked is 0.
        static_cast<void>(cursor.read_compact_index());
        node.collision_bound_index = cursor.read_compact_index();
        node.zone_indices[0] = cursor.read_u8();
        node.zone_indices[1] = cursor.read_u8();
        node.vertex_count = cursor.read_u8();
        node.leaf_indices[0] = cursor.read_i32();
        node.leaf_indices[1] = cursor.read_i32();
        result.nodes.push_back(node);
    }

    const auto surface_count = read_serialized_count(cursor, "Model Surfs");
    result.surfaces.reserve(surface_count);
    for (std::size_t index = 0; index < surface_count; ++index) {
        Hp1BspSurface surface;
        surface.texture_reference = cursor.read_compact_index();
        package.require_valid_reference(surface.texture_reference);
        surface.polygon_flags = cursor.read_u32();
        surface.base_point_index = cursor.read_compact_index();
        surface.normal_vector_index = cursor.read_compact_index();
        surface.texture_u_vector_index = cursor.read_compact_index();
        surface.texture_v_vector_index = cursor.read_compact_index();
        surface.light_map_index = cursor.read_compact_index();
        // Serialized surface field +0x1C targets the deferred Polys payload.
        static_cast<void>(cursor.read_compact_index());
        surface.pan_u = cursor.read_i16();
        surface.pan_v = cursor.read_i16();
        surface.actor_reference = cursor.read_compact_index();
        package.require_valid_reference(surface.actor_reference);
        result.surfaces.push_back(surface);
    }

    const auto vertex_count = read_serialized_count(cursor, "Model Verts");
    std::vector<Hp1BspVertex> serialized_vertices;
    serialized_vertices.reserve(vertex_count);
    for (std::size_t index = 0; index < vertex_count; ++index) {
        serialized_vertices.push_back({
            cursor.read_compact_index(),
            cursor.read_compact_index(),
        });
    }

    result.shared_side_count = checked_count(
        cursor.read_i32(), kMaximumTableEntries, "Model shared sides");
    result.zone_count = checked_count(
        cursor.read_i32(), kMaximumTableEntries, "Model Zones");
    result.zone_actor_references.reserve(result.zone_count);
    for (std::size_t index = 0; index < result.zone_count; ++index) {
        const auto actor_reference = cursor.read_compact_index();
        package.require_valid_reference(actor_reference);
        result.zone_actor_references.push_back(actor_reference);
        static_cast<void>(cursor.take(16));
    }

    const auto polys_reference = cursor.read_compact_index();
    result.polys_reference = polys_reference;
    package.require_valid_reference(polys_reference);
    if (polys_reference != 0 &&
        (polys_reference < 0 ||
         !ascii_equal_fold(package.class_name_for_export(
                               static_cast<std::size_t>(polys_reference - 1)),
                           "Engine.Polys"))) {
        fail(Hp1ProfileStatus::invalid_profile,
             "Model Polys reference is not a local Engine.Polys export");
    }

    const auto light_map_count =
        read_serialized_count(cursor, "Model LightMap");
    result.light_maps.reserve(light_map_count);
    for (std::size_t index = 0; index < light_map_count; ++index) {
        Hp1LightMapIndex light_map;
        light_map.data_offset = cursor.read_i32();
        light_map.pan = read_bsp_vector(cursor, "Model LightMap pan");
        light_map.u_clamp = cursor.read_compact_index();
        light_map.v_clamp = cursor.read_compact_index();
        light_map.u_scale =
            read_finite_geometry_float(cursor, "Model LightMap U scale");
        light_map.v_scale =
            read_finite_geometry_float(cursor, "Model LightMap V scale");
        light_map.light_actor_index = cursor.read_i32();
        if (light_map.data_offset < 0 || light_map.u_clamp <= 0 ||
            light_map.v_clamp <= 0 || light_map.u_clamp > 4096 ||
            light_map.v_clamp > 4096 || !(light_map.u_scale > 0.0F) ||
            !(light_map.v_scale > 0.0F)) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Model LightMap contains invalid dimensions or scale");
        }
        result.light_maps.push_back(light_map);
    }
    const auto light_bit_bytes =
        read_serialized_count(cursor, "Model LightBits");
    const auto light_bits = cursor.take(light_bit_bytes);
    result.light_bits.assign(light_bits.begin(), light_bits.end());
    result.bound_count = read_serialized_count(cursor, "Model Bounds");
    skip_repeated_bytes(cursor, result.bound_count, 25, "Model Bounds");
    const auto leaf_hull_count =
        read_serialized_count(cursor, "Model LeafHulls");
    skip_repeated_bytes(cursor, leaf_hull_count, 4, "Model LeafHulls");
    result.leaf_count = read_serialized_count(cursor, "Model Leaves");
    for (std::size_t index = 0; index < result.leaf_count; ++index) {
        static_cast<void>(cursor.read_compact_index());
        static_cast<void>(cursor.read_compact_index());
        static_cast<void>(cursor.read_compact_index());
        static_cast<void>(cursor.take(8));
    }
    const auto light_count = read_serialized_count(cursor, "Model Lights");
    result.light_references.reserve(light_count);
    for (std::size_t index = 0; index < light_count; ++index) {
        const auto reference = cursor.read_compact_index();
        package.require_valid_reference(reference);
        result.light_references.push_back(reference);
    }
    static_cast<void>(cursor.read_i32());  // RootOutside
    static_cast<void>(cursor.read_i32());  // Linked
    if (cursor.remaining() != 0) {
        fail(Hp1ProfileStatus::invalid_profile,
             "Model payload has trailing bytes");
    }

    for (const auto& node : result.nodes) {
        require_topology_index(node.surface_index, result.surfaces.size(),
                               false, "BSP node surface index");
        require_topology_index(node.front_node_index, result.nodes.size(),
                               true, "BSP node front index");
        require_topology_index(node.back_node_index, result.nodes.size(),
                               true, "BSP node back index");
        require_topology_index(node.collision_bound_index, result.bound_count,
                               true, "BSP node collision-bound index");
        // Unzoned mover brush Models have no zone table and encode zone 0.
        const auto zone_limit = std::max<std::size_t>(1, result.zone_count);
        require_topology_index(node.zone_indices[0], zone_limit, false,
                               "BSP node front-zone index");
        require_topology_index(node.zone_indices[1], zone_limit, false,
                               "BSP node back-zone index");
        require_topology_index(node.leaf_indices[0], result.leaf_count, true,
                               "BSP node front-leaf index");
        require_topology_index(node.leaf_indices[1], result.leaf_count, true,
                               "BSP node back-leaf index");
        if (node.vertex_pool_index < 0 ||
            static_cast<std::size_t>(node.vertex_pool_index) >
                serialized_vertices.size() ||
            static_cast<std::size_t>(node.vertex_count) >
                serialized_vertices.size() -
                    static_cast<std::size_t>(node.vertex_pool_index)) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "BSP node vertex span is outside Model Verts");
        }
    }
    for (const auto& surface : result.surfaces) {
        require_topology_index(surface.base_point_index, result.points.size(),
                               false, "BSP surface base-point index");
        require_topology_index(surface.normal_vector_index,
                               result.vectors.size(), false,
                               "BSP surface normal-vector index");
        require_topology_index(surface.texture_u_vector_index,
                               result.vectors.size(), false,
                               "BSP surface texture-U index");
        require_topology_index(surface.texture_v_vector_index,
                               result.vectors.size(), false,
                               "BSP surface texture-V index");
        require_topology_index(surface.light_map_index, result.light_maps.size(),
                               true, "BSP surface light-map index");
    }
    for (const auto& light_map : result.light_maps) {
        if (light_map.light_actor_index < -1 ||
            (light_map.light_actor_index >= 0 &&
             static_cast<std::size_t>(light_map.light_actor_index) >=
                 result.light_references.size())) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Model LightMap light span is outside Model Lights");
        }
        std::size_t lights_for_map = 0;
        for (std::size_t index = light_map.light_actor_index < 0
                                     ? result.light_references.size()
                                     : static_cast<std::size_t>(
                                           light_map.light_actor_index);
             index < result.light_references.size() &&
             result.light_references[index] != 0;
             ++index) {
            ++lights_for_map;
        }
        const auto pitch =
            (static_cast<std::size_t>(light_map.u_clamp) + 7U) / 8U;
        const auto bytes_per_light =
            pitch * static_cast<std::size_t>(light_map.v_clamp);
        const auto offset = static_cast<std::size_t>(light_map.data_offset);
        if (lights_for_map > 0 &&
            (offset > result.light_bits.size() ||
             bytes_per_light > result.light_bits.size() ||
             lights_for_map >
                 (result.light_bits.size() - offset) / bytes_per_light)) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Model LightMap visibility span exceeds Model LightBits");
        }
    }
    std::size_t active_vertex_count = 0;
    for (const auto& node : result.nodes) {
        if (node.vertex_count > kMaximumTableEntries - active_vertex_count) {
            fail(Hp1ProfileStatus::invalid_package,
                 "active BSP vertex count exceeds the safety cap");
        }
        active_vertex_count += node.vertex_count;
    }
    result.vertices.reserve(active_vertex_count);
    for (auto& node : result.nodes) {
        const auto serialized_start =
            static_cast<std::size_t>(node.vertex_pool_index);
        node.vertex_pool_index =
            static_cast<std::int32_t>(result.vertices.size());
        for (std::size_t offset = 0; offset < node.vertex_count; ++offset) {
            const auto& vertex = serialized_vertices[serialized_start + offset];
            require_topology_index(vertex.point_index, result.points.size(),
                                   false, "active BSP vertex point index");
            require_topology_index(vertex.side_index, result.shared_side_count,
                                   true, "active BSP vertex side index");
            result.vertices.push_back(vertex);
        }
    }

    result.status = Hp1ProfileStatus::ok;
    result.package_version = package.version();
    result.model_reference = model_reference;
    return result;
}

[[nodiscard]] float property_float(const PropertyTag& tag) {
    if (tag.kind != PropertyKind::floating || tag.value.size() != 4) {
        fail(Hp1ProfileStatus::invalid_profile,
             "expected a four-byte floating property");
    }
    Cursor cursor(tag.value);
    const float value = cursor.read_f32();
    if (!std::isfinite(value)) {
        fail(Hp1ProfileStatus::invalid_profile,
             "profile contains a non-finite float");
    }
    return value;
}

[[nodiscard]] std::int32_t property_reference(const PropertyTag& tag) {
    if ((tag.kind != PropertyKind::object &&
         tag.kind != PropertyKind::class_object) ||
        tag.value.empty()) {
        fail(Hp1ProfileStatus::invalid_profile,
             "expected an object-reference property");
    }
    Cursor cursor(tag.value);
    const auto reference = cursor.read_compact_index();
    if (cursor.remaining() != 0) {
        fail(Hp1ProfileStatus::invalid_profile,
             "object-reference property has trailing bytes");
    }
    return reference;
}

[[nodiscard]] std::uint8_t property_byte(const PropertyTag& tag,
                                         std::string_view field) {
    if (tag.kind != PropertyKind::byte || tag.array_index.has_value() ||
        tag.value.size() != 1) {
        fail(Hp1ProfileStatus::invalid_profile,
             std::string(field) + " is not one scalar byte property");
    }
    return tag.value[0];
}

[[nodiscard]] std::string property_name(const Package& package,
                                        const PropertyTag& tag,
                                        std::string_view field) {
    if (tag.kind != PropertyKind::name || tag.array_index.has_value() ||
        tag.value.empty()) {
        fail(Hp1ProfileStatus::invalid_profile,
             std::string(field) + " is not one scalar name property");
    }
    Cursor value(tag.value);
    const auto result = std::string(package.name(value.read_compact_index()));
    if (value.remaining() != 0) {
        fail(Hp1ProfileStatus::invalid_profile,
             std::string(field) + " has trailing bytes");
    }
    return result;
}

[[nodiscard]] bool property_boolean(const PropertyTag& tag,
                                    std::string_view field) {
    if (tag.kind != PropertyKind::boolean ||
        !tag.boolean_value.has_value() || tag.array_index.has_value()) {
        fail(Hp1ProfileStatus::invalid_profile,
             std::string(field) + " is not one scalar boolean property");
    }
    return *tag.boolean_value;
}

void require_property_end(const Cursor& cursor) {
    if (cursor.remaining() != 0) {
        fail(Hp1ProfileStatus::invalid_profile,
             "target object has unsupported trailing serialized data");
    }
}

struct AuthoredGesture {
    std::vector<Vec2> points;
    std::vector<std::int32_t> segments;
};

[[nodiscard]] std::vector<Vec2> read_points(std::span<const std::uint8_t> value) {
    Cursor cursor(value);
    const auto count = checked_count(cursor.read_compact_index(),
                                     kHp1NativeGesturePointLimit,
                                     "gesture Points");
    if (count < 2 || cursor.remaining() != count * 12U) {
        fail(Hp1ProfileStatus::invalid_profile,
             "gesture Points payload has an invalid size");
    }
    std::vector<Vec2> points;
    points.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const float x = cursor.read_f32();
        const float y = cursor.read_f32();
        const float z = cursor.read_f32();
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "gesture contains a non-finite point");
        }
        points.push_back({x, y});
    }
    return points;
}

[[nodiscard]] std::vector<std::int32_t> read_segments(
    std::span<const std::uint8_t> value) {
    Cursor cursor(value);
    const auto count = checked_count(cursor.read_compact_index(), 4096,
                                     "gesture Segments");
    if (cursor.remaining() != count * sizeof(std::int32_t)) {
        fail(Hp1ProfileStatus::invalid_profile,
             "gesture Segments payload has an invalid size");
    }
    std::vector<std::int32_t> segments;
    segments.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        segments.push_back(cursor.read_i32());
    }
    return segments;
}

[[nodiscard]] AuthoredGesture load_gesture(const Package& package,
                                           std::string_view gesture_name) {
    std::optional<std::size_t> match;
    for (std::size_t index = 0; index < package.export_count(); ++index) {
        const auto& entry = package.export_at(index);
        if (entry.outer == 0 &&
            ascii_equal_fold(package.export_name(index), gesture_name) &&
            package.reference_is_class(
                entry.class_reference, "Engine", "Gesture")) {
            if (match.has_value()) {
                fail(Hp1ProfileStatus::ambiguous_object,
                     "package contains more than one matching Gesture export");
            }
            match = index;
        }
    }
    if (!match.has_value()) {
        fail(Hp1ProfileStatus::object_not_found,
             "named Gesture export was not found");
    }

    const auto& entry = package.export_at(*match);
    Cursor cursor(package.export_bytes(*match));
    skip_object_stack(cursor, entry.object_flags);
    AuthoredGesture gesture;
    bool terminated = false;
    bool segments_seen = false;
    while (cursor.remaining() != 0) {
        const auto tag = read_property_tag(package, cursor);
        if (!tag.has_value()) {
            terminated = true;
            break;
        }
        if (ascii_equal_fold(tag->name, "Points")) {
            if (tag->kind != PropertyKind::array || !gesture.points.empty()) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "gesture has an invalid Points property");
            }
            gesture.points = read_points(tag->value);
        } else if (ascii_equal_fold(tag->name, "Segments")) {
            if (tag->kind != PropertyKind::array || segments_seen) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "gesture has an invalid Segments property");
            }
            segments_seen = true;
            gesture.segments = read_segments(tag->value);
        }
    }
    if (!terminated || gesture.points.empty()) {
        fail(Hp1ProfileStatus::invalid_profile,
             "gesture is missing its required Points property");
    }
    require_property_end(cursor);
    return gesture;
}

struct LessonPolicy {
    std::array<float, kHp1PassMarkCount> pass_marks{};
    std::array<bool, kHp1PassMarkCount> pass_mark_present{};
    std::optional<float> accuracy_radius;
    std::optional<float> very_good_threshold;
    std::optional<float> very_bad_threshold;
    std::optional<float> draw_time_seconds;
};

void apply_policy_property(LessonPolicy& policy, const PropertyTag& tag) {
    if (ascii_equal_fold(tag.name, "fAccuracy")) {
        policy.accuracy_radius = property_float(tag);
    } else if (ascii_equal_fold(tag.name, "fVGoodThreshold")) {
        policy.very_good_threshold = property_float(tag);
    } else if (ascii_equal_fold(tag.name, "fVBadThreshold")) {
        policy.very_bad_threshold = property_float(tag);
    } else if (ascii_equal_fold(tag.name, "DrawTime")) {
        policy.draw_time_seconds = property_float(tag);
    } else if (ascii_equal_fold(tag.name, "PassMark")) {
        const std::size_t index = tag.array_index.value_or(0);
        if (index >= kHp1PassMarkCount || policy.pass_mark_present[index]) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "PassMark contains an invalid or duplicate array element");
        }
        policy.pass_marks[index] = property_float(tag);
        policy.pass_mark_present[index] = true;
    }
}

[[nodiscard]] LessonPolicy load_class_policy(const Package& package) {
    std::optional<std::size_t> match;
    for (std::size_t index = 0; index < package.export_count(); ++index) {
        const auto& entry = package.export_at(index);
        if (entry.class_reference == 0 && entry.outer == 0 &&
            ascii_equal_fold(package.export_name(index), "SpellLearnTrigger")) {
            if (match.has_value()) {
                fail(Hp1ProfileStatus::ambiguous_object,
                     "multiple SpellLearnTrigger class exports were found");
            }
            match = index;
        }
    }
    if (!match.has_value()) {
        fail(Hp1ProfileStatus::object_not_found,
             "SpellLearnTrigger class export was not found");
    }

    const auto& entry = package.export_at(*match);
    Cursor cursor(package.export_bytes(*match));
    skip_object_stack(cursor, entry.object_flags);
    static_cast<void>(cursor.read_compact_index());  // UField base
    static_cast<void>(cursor.read_compact_index());  // UField next
    static_cast<void>(cursor.read_compact_index());  // script text
    static_cast<void>(cursor.read_compact_index());  // first child
    static_cast<void>(package.name(cursor.read_compact_index()));
    static_cast<void>(cursor.read_u32());  // source line
    static_cast<void>(cursor.read_u32());  // source text position
    if (cursor.read_u32() != 0) {
        fail(Hp1ProfileStatus::unsupported_package,
             "SpellLearnTrigger class-level bytecode is not empty");
    }
    static_cast<void>(cursor.read_u64());  // probe mask
    static_cast<void>(cursor.read_u64());  // ignore mask
    static_cast<void>(cursor.read_u16());  // label table offset
    static_cast<void>(cursor.read_u32());  // state flags
    static_cast<void>(cursor.read_u32());  // class flags
    static_cast<void>(cursor.take(16));    // class GUID

    const auto dependency_count = checked_count(
        cursor.read_compact_index(), kMaximumTableEntries, "class dependencies");
    for (std::size_t index = 0; index < dependency_count; ++index) {
        static_cast<void>(cursor.read_compact_index());
        static_cast<void>(cursor.read_u32());
        static_cast<void>(cursor.read_u32());
    }
    const auto package_import_count = checked_count(
        cursor.read_compact_index(), kMaximumTableEntries, "class package imports");
    for (std::size_t index = 0; index < package_import_count; ++index) {
        static_cast<void>(cursor.read_compact_index());
    }
    static_cast<void>(cursor.read_compact_index());
    static_cast<void>(package.name(cursor.read_compact_index()));

    LessonPolicy policy;
    bool terminated = false;
    while (cursor.remaining() != 0) {
        const auto tag = read_property_tag(package, cursor);
        if (!tag.has_value()) {
            terminated = true;
            break;
        }
        apply_policy_property(policy, *tag);
    }
    if (!terminated) {
        fail(Hp1ProfileStatus::invalid_profile,
             "SpellLearnTrigger defaults have no property terminator");
    }
    require_property_end(cursor);
    return policy;
}

struct LessonMatch {
    std::string actor_name;
    LessonPolicy overrides;
};

[[nodiscard]] LessonMatch load_lesson_overrides(
    const Package& package,
    std::string_view spell_class_name) {
    std::optional<LessonMatch> match;
    for (std::size_t index = 0; index < package.export_count(); ++index) {
        const auto& entry = package.export_at(index);
        if (!package.reference_is_class(
                entry.class_reference, "HPBase", "SpellLearnTrigger")) {
            continue;
        }
        Cursor cursor(package.export_bytes(index));
        skip_object_stack(cursor, entry.object_flags);
        LessonPolicy overrides;
        std::optional<std::int32_t> spell_reference;
        bool terminated = false;
        while (cursor.remaining() != 0) {
            const auto tag = read_property_tag(package, cursor);
            if (!tag.has_value()) {
                terminated = true;
                break;
            }
            if (ascii_equal_fold(tag->name, "Spell")) {
                spell_reference = property_reference(*tag);
            }
            apply_policy_property(overrides, *tag);
        }
        if (!terminated) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "SpellLearnTrigger actor has no property terminator");
        }
        require_property_end(cursor);
        if (!spell_reference.has_value() ||
            !package.reference_is_class(
                *spell_reference, "HPBase", spell_class_name)) {
            continue;
        }
        if (match.has_value()) {
            fail(Hp1ProfileStatus::ambiguous_object,
                 "lesson package has multiple actors for the requested spell");
        }
        match = LessonMatch{std::string(package.export_name(index)), overrides};
    }
    if (!match.has_value()) {
        fail(Hp1ProfileStatus::object_not_found,
             "lesson actor for the requested spell was not found");
    }
    return *match;
}

void merge_policy(LessonPolicy& base, const LessonPolicy& overrides) {
    if (overrides.accuracy_radius.has_value()) {
        base.accuracy_radius = overrides.accuracy_radius;
    }
    if (overrides.very_good_threshold.has_value()) {
        base.very_good_threshold = overrides.very_good_threshold;
    }
    if (overrides.very_bad_threshold.has_value()) {
        base.very_bad_threshold = overrides.very_bad_threshold;
    }
    if (overrides.draw_time_seconds.has_value()) {
        base.draw_time_seconds = overrides.draw_time_seconds;
    }
    for (std::size_t index = 0; index < kHp1PassMarkCount; ++index) {
        if (overrides.pass_mark_present[index]) {
            base.pass_marks[index] = overrides.pass_marks[index];
            base.pass_mark_present[index] = true;
        }
    }
}

[[nodiscard]] float nearest_distance(Vec2 point,
                                     std::span<const Vec2> candidates) {
    float nearest = 1.0F;
    for (const Vec2 candidate : candidates) {
        const float x = point.x - candidate.x;
        const float y = point.y - candidate.y;
        nearest = std::min(nearest, std::sqrt(x * x + y * y));
    }
    return nearest;
}

[[nodiscard]] bool finite(Vec2 point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

[[nodiscard]] bool finite(Vec3 point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y) &&
           std::isfinite(point.z);
}

[[nodiscard]] std::int32_t capped_rounded_weight(float value,
                                                 std::int32_t maximum) {
    if (!std::isfinite(value) || value >= static_cast<float>(maximum)) {
        return maximum;
    }
    return std::min(static_cast<std::int32_t>(std::round(value)), maximum);
}

}  // namespace

Hp1PackageReadScope::Hp1PackageReadScope(){
    if(package_read_depth++==0){package_read_cache.clear();package_read_stats={};package_read_clock=0;}
}
Hp1PackageReadScope::~Hp1PackageReadScope(){
    if(--package_read_depth==0){package_read_cache.clear();package_read_stats.retained_bytes=0;}
}
Hp1PackageReadStats Hp1PackageReadScope::stats()const{return package_read_stats;}

Hp1PackageSummary inspect_hp1_package(
    const std::filesystem::path& package_path) {
    Hp1PackageSummary result;
    try {
        const Package package(package_path);
        std::map<std::string, std::pair<std::size_t, std::uint64_t>>
            class_totals;
        std::uint64_t serialized_bytes = 0;
        for (std::size_t index = 0; index < package.export_count(); ++index) {
            const auto& entry = package.export_at(index);
            const auto entry_bytes =
                static_cast<std::uint64_t>(entry.serial_size);
            if (entry_bytes >
                std::numeric_limits<std::uint64_t>::max() - serialized_bytes) {
                fail(Hp1ProfileStatus::invalid_package,
                     "serialized export byte total overflowed");
            }
            serialized_bytes += entry_bytes;
            auto& [count, bytes] =
                class_totals[package.class_name_for_export(index)];
            ++count;
            if (entry_bytes >
                std::numeric_limits<std::uint64_t>::max() - bytes) {
                fail(Hp1ProfileStatus::invalid_package,
                     "serialized class byte total overflowed");
            }
            bytes += entry_bytes;
        }

        result.status = Hp1ProfileStatus::ok;
        result.package_version = package.version();
        result.licensee_version = package.licensee_version();
        result.file_bytes = static_cast<std::uint64_t>(package.file_size());
        result.name_count = package.name_count();
        result.import_count = package.import_count();
        result.export_count = package.export_count();
        result.serialized_bytes = serialized_bytes;
        result.imported_packages = package.imported_packages();
        result.classes.reserve(class_totals.size());
        for (auto& [name, totals] : class_totals) {
            result.classes.push_back({
                std::move(name),
                totals.first,
                totals.second,
            });
        }
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_package;
        result.error = "allocation failed while inspecting package";
    } catch (const std::exception& error) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_package;
        result.error = error.what();
    }
    return result;
}

Hp1PackageLinkTable inspect_hp1_package_link_table(
    const std::filesystem::path& package_path) {
    Hp1PackageLinkTable result;
    try {
        const Package package(package_path);
        const auto package_name = package_path.stem().string();
        result.package_version = package.version();
        result.licensee_version = package.licensee_version();
        result.imports.reserve(package.import_count());
        for (std::size_t index = 0; index < package.import_count(); ++index) {
            const auto& entry = package.import_at(index);
            const auto reference =
                -static_cast<std::int32_t>(index) - 1;
            result.imports.push_back({
                reference,
                entry.outer,
                package.qualified_class_name_for_import(index),
                package.object_path(reference),
                entry.outer == 0 &&
                    ascii_equal_fold(
                        package.name(
                            static_cast<std::int32_t>(entry.class_package)),
                        "Core") &&
                    ascii_equal_fold(
                        package.name(
                            static_cast<std::int32_t>(entry.class_name)),
                        "Package"),
            });
        }
        result.exports.reserve(package.export_count());
        for (std::size_t index = 0; index < package.export_count(); ++index) {
            const auto& entry = package.export_at(index);
            const auto reference = static_cast<std::int32_t>(index) + 1;
            result.exports.push_back({
                reference,
                entry.outer,
                package.qualified_class_name_for_export(index, package_name),
                package.object_path(reference),
                entry.object_flags,
                static_cast<std::uint64_t>(entry.serial_size),
            });
        }
        result.status = Hp1ProfileStatus::ok;
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_package;
        result.error = "allocation failed while inspecting package link table";
    } catch (const std::exception& error) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_package;
        result.error = error.what();
    }
    return result;
}

Hp1LevelHandles inspect_hp1_level_handles(
    const std::filesystem::path& map_package) {
    Hp1LevelHandles result;
    try {
        const Package package(map_package);
        std::optional<std::size_t> level_index;
        for (std::size_t index = 0; index < package.export_count(); ++index) {
            const auto& entry = package.export_at(index);
            if (entry.outer == 0 &&
                package.reference_is_class(
                    entry.class_reference, "Engine", "Level")) {
                if (level_index.has_value()) {
                    fail(Hp1ProfileStatus::ambiguous_object,
                         "map contains more than one top-level Engine.Level");
                }
                level_index = index;
            }
        }
        if (!level_index.has_value()) {
            fail(Hp1ProfileStatus::object_not_found,
                 "top-level Engine.Level export was not found");
        }

        const auto& level = package.export_at(*level_index);
        Cursor cursor(package.export_bytes(*level_index));
        skip_object_stack(cursor, level.object_flags);
        bool properties_terminated = false;
        while (cursor.remaining() != 0) {
            if (!read_property_tag(package, cursor).has_value()) {
                properties_terminated = true;
                break;
            }
        }
        if (!properties_terminated) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Level properties have no terminator");
        }
        const auto actor_count = checked_count(
            cursor.read_i32(), kMaximumTableEntries, "Level Actors");
        const auto serialized_actor_count = checked_count(
            cursor.read_i32(), kMaximumTableEntries,
            "Level serialized Actors");
        if (serialized_actor_count != actor_count) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Level actor transaction counts disagree");
        }
        result.actor_references.reserve(actor_count);
        for (std::size_t index = 0; index < actor_count; ++index) {
            const auto reference = cursor.read_compact_index();
            package.require_valid_reference(reference);
            if (reference < 0) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "Level Actors contains a non-local import reference");
            }
            result.actor_references.push_back(reference);
            if (reference == 0) {
                ++result.null_actor_count;
            }
        }

        skip_level_url(cursor);
        const auto model_reference = cursor.read_compact_index();
        package.require_valid_reference(model_reference);
        if (model_reference <= 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Level world model is not a local export reference");
        }
        const auto model_index =
            static_cast<std::size_t>(model_reference - 1);
        if (!ascii_equal_fold(
                package.class_name_for_export(model_index), "Engine.Model")) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Level world model reference is not an Engine.Model export");
        }

        result.status = Hp1ProfileStatus::ok;
        result.package_version = package.version();
        result.level_reference =
            static_cast<std::int32_t>(*level_index) + 1;
        result.world_model_reference = model_reference;
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = "allocation failed while inspecting Level handles";
    } catch (const std::exception& error) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = error.what();
    }
    return result;
}

Hp1PlayerStartCensus inspect_hp1_player_starts(
    const std::filesystem::path& map_package) {
    Hp1PlayerStartCensus result;
    try {
        const auto level = inspect_hp1_level_handles(map_package);
        if (level.status != Hp1ProfileStatus::ok) {
            fail(level.status, level.error);
        }
        const Package package(map_package);
        if (package.version() != level.package_version) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Level and PlayerStart package versions disagree");
        }

        for (std::size_t slot = 0; slot < level.actor_references.size();
             ++slot) {
            const auto reference = level.actor_references[slot];
            if (reference <= 0) {
                continue;
            }
            const auto export_index = static_cast<std::size_t>(reference - 1);
            const auto& actor = package.export_at(export_index);
            if (!package.reference_is_class(
                    actor.class_reference, "Engine", "PlayerStart")) {
                continue;
            }

            Hp1PlayerStart player_start;
            player_start.actor_reference = reference;
            player_start.actor_slot_index = slot;
            player_start.object_name = package.export_name(export_index);
            Cursor cursor(package.export_bytes(export_index));
            skip_object_stack(cursor, actor.object_flags);
            bool properties_terminated = false;
            while (cursor.remaining() != 0) {
                const auto tag = read_property_tag(package, cursor);
                if (!tag.has_value()) {
                    properties_terminated = true;
                    break;
                }
                if (ascii_equal_fold(tag->name, "Location")) {
                    if (player_start.location_serialized) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "PlayerStart serializes Location more than once");
                    }
                    player_start.location_unreal =
                        property_vector(*tag, "PlayerStart Location");
                    player_start.location_serialized = true;
                } else if (ascii_equal_fold(tag->name, "Rotation")) {
                    if (player_start.rotation_serialized) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "PlayerStart serializes Rotation more than once");
                    }
                    player_start.rotation_units =
                        property_rotator(*tag, "PlayerStart Rotation");
                    player_start.rotation_serialized = true;
                }
            }
            if (!properties_terminated || cursor.remaining() != 0) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "PlayerStart payload has unsupported trailing data");
            }
            if (!player_start.location_serialized) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "PlayerStart has no serialized Location property");
            }
            result.player_starts.push_back(std::move(player_start));
        }

        result.status = Hp1ProfileStatus::ok;
        result.package_version = package.version();
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = "allocation failed while inspecting PlayerStart actors";
    } catch (const std::exception& error) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = error.what();
    }
    return result;
}

Hp1ActorVisualCensus inspect_hp1_actor_visuals(
    const std::filesystem::path& map_package) {
    Hp1ActorVisualCensus result;
    try {
        const auto level = inspect_hp1_level_handles(map_package);
        if (level.status != Hp1ProfileStatus::ok) {
            fail(level.status, level.error);
        }
        const Package package(map_package);
        if (package.version() != level.package_version) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Level and actor-visual package versions disagree");
        }
        result.actors.reserve(
            level.actor_references.size() - level.null_actor_count);
        for (std::size_t slot = 0; slot < level.actor_references.size();
             ++slot) {
            const auto reference = level.actor_references[slot];
            if (reference <= 0) {
                continue;
            }
            const auto export_index = static_cast<std::size_t>(reference - 1);
            const auto& object = package.export_at(export_index);
            Hp1ActorVisual actor;
            actor.actor_reference = reference;
            actor.class_reference = object.class_reference;
            actor.actor_slot_index = slot;
            actor.object_name = package.export_name(export_index);
            actor.qualified_class_name =
                package.class_name_for_export(export_index);
            Cursor cursor(package.export_bytes(export_index));
            skip_object_stack(cursor, object.object_flags);
            bool terminated = cursor.remaining() == 0;
            while (cursor.remaining() != 0) {
                const auto tag = read_property_tag(package, cursor);
                if (!tag.has_value()) {
                    terminated = true;
                    break;
                }
                ++actor.property_count;
                actor.serialized_properties.push_back(
                    retain_property(package, *tag));
                if (ascii_equal_fold(tag->name, "Location")) {
                    if (actor.location_serialized) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "actor has duplicate Location properties");
                    }
                    actor.location_unreal =
                        property_vector(*tag, "Actor Location");
                    actor.location_serialized = true;
                } else if (ascii_equal_fold(tag->name, "Rotation")) {
                    if (actor.rotation_serialized) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "actor has duplicate Rotation properties");
                    }
                    actor.rotation_units =
                        property_rotator(*tag, "Actor Rotation");
                    actor.rotation_serialized = true;
                } else if (ascii_equal_fold(tag->name, "DrawScale")) {
                    if (actor.draw_scale_serialized ||
                        tag->array_index.has_value()) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "actor has invalid DrawScale property");
                    }
                    actor.draw_scale = property_float(*tag);
                    actor.draw_scale_serialized = true;
                } else if (ascii_equal_fold(tag->name, "Mesh")) {
                    if (actor.mesh_serialized ||
                        tag->array_index.has_value()) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "actor has invalid Mesh property");
                    }
                    actor.mesh_reference = property_reference(*tag);
                    package.require_valid_reference(actor.mesh_reference);
                    actor.mesh_serialized = true;
                } else if (ascii_equal_fold(tag->name, "Skin") ||
                           ascii_equal_fold(tag->name, "MultiSkins")) {
                    const auto skin = property_reference(*tag);
                    package.require_valid_reference(skin);
                    actor.skin_references.push_back(skin);
                } else if (ascii_equal_fold(tag->name, "AnimSequence")) {
                    if (actor.animation_sequence_serialized ||
                        tag->kind != PropertyKind::name ||
                        tag->array_index.has_value()) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "actor has invalid AnimSequence property");
                    }
                    Cursor value(tag->value);
                    actor.animation_sequence = std::string(
                        package.name(value.read_compact_index()));
                    if (value.remaining() != 0) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "actor AnimSequence has trailing bytes");
                    }
                    actor.animation_sequence_serialized = true;
                } else if (ascii_equal_fold(tag->name, "Tag")) {
                    if (actor.tag_serialized) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "actor has duplicate Tag properties");
                    }
                    actor.tag = property_name(package, *tag, "Actor Tag");
                    actor.tag_serialized = true;
                } else if (ascii_equal_fold(tag->name, "Event")) {
                    if (actor.event_serialized) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "actor has duplicate Event properties");
                    }
                    actor.event = property_name(package, *tag, "Actor Event");
                    actor.event_serialized = true;
                } else if (ascii_equal_fold(tag->name, "InitialState")) {
                    if (actor.initial_state_serialized) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "actor has duplicate InitialState properties");
                    }
                    actor.initial_state =
                        property_name(package, *tag, "Actor InitialState");
                    actor.initial_state_serialized = true;
                } else if (ascii_equal_fold(tag->name, "AmbientSound")) {
                    if (actor.ambient_sound_serialized ||
                        tag->array_index.has_value()) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "actor has invalid AmbientSound property");
                    }
                    actor.ambient_sound_reference = property_reference(*tag);
                    package.require_valid_reference(
                        actor.ambient_sound_reference);
                    actor.ambient_sound_serialized = true;
                } else if (ascii_equal_fold(tag->name, "CollisionRadius")) {
                    actor.collision_radius = property_float(*tag);
                    actor.collision_radius_serialized = true;
                } else if (ascii_equal_fold(tag->name, "CollisionHeight")) {
                    actor.collision_height = property_float(*tag);
                    actor.collision_height_serialized = true;
                } else if (ascii_equal_fold(tag->name, "bCollideActors")) {
                    actor.collide_actors =
                        property_boolean(*tag, "Actor bCollideActors");
                    actor.collide_actors_serialized = true;
                } else if (ascii_equal_fold(tag->name, "bBlockActors")) {
                    actor.block_actors =
                        property_boolean(*tag, "Actor bBlockActors");
                    actor.block_actors_serialized = true;
                } else if (ascii_equal_fold(tag->name, "bBlockPlayers")) {
                    actor.block_players =
                        property_boolean(*tag, "Actor bBlockPlayers");
                    actor.block_players_serialized = true;
                } else if (ascii_equal_fold(tag->name, "SoundVolume")) {
                    actor.sound_volume =
                        property_byte(*tag, "Actor SoundVolume");
                    actor.sound_volume_serialized = true;
                } else if (ascii_equal_fold(tag->name, "SoundRadius")) {
                    actor.sound_radius =
                        property_byte(*tag, "Actor SoundRadius");
                    actor.sound_radius_serialized = true;
                } else if (ascii_equal_fold(tag->name, "SoundPitch")) {
                    actor.sound_pitch =
                        property_byte(*tag, "Actor SoundPitch");
                    actor.sound_pitch_serialized = true;
                } else if (ascii_equal_fold(tag->name, "LightBrightness")) {
                    actor.light_brightness =
                        property_byte(*tag, "Actor LightBrightness");
                    actor.light_brightness_serialized = true;
                } else if (ascii_equal_fold(tag->name, "LightHue")) {
                    actor.light_hue = property_byte(*tag, "Actor LightHue");
                    actor.light_hue_serialized = true;
                } else if (ascii_equal_fold(tag->name, "LightSaturation")) {
                    actor.light_saturation =
                        property_byte(*tag, "Actor LightSaturation");
                    actor.light_saturation_serialized = true;
                } else if (ascii_equal_fold(tag->name, "LightRadius")) {
                    actor.light_radius =
                        property_byte(*tag, "Actor LightRadius");
                    actor.light_radius_serialized = true;
                } else if (ascii_equal_fold(tag->name, "LightType")) {
                    actor.light_type =
                        property_byte(*tag, "Actor LightType");
                    actor.light_type_serialized = true;
                } else if (ascii_equal_fold(tag->name, "LightEffect")) {
                    actor.light_effect =
                        property_byte(*tag, "Actor LightEffect");
                    actor.light_effect_serialized = true;
                } else if (ascii_equal_fold(tag->name, "DrawType")) {
                    if (actor.draw_type_serialized ||
                        tag->kind != PropertyKind::byte ||
                        tag->array_index.has_value() ||
                        tag->value.size() != 1) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "actor has invalid DrawType property");
                    }
                    actor.draw_type = tag->value[0];
                    actor.draw_type_serialized = true;
                } else if (ascii_equal_fold(tag->name, "bHidden")) {
                    if (actor.hidden_serialized ||
                        tag->kind != PropertyKind::boolean ||
                        !tag->boolean_value.has_value()) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "actor has invalid bHidden property");
                    }
                    actor.hidden = *tag->boolean_value;
                    actor.hidden_serialized = true;
                }
            }
            if (!terminated) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "actor properties have no terminator");
            }
            require_property_end(cursor);
            result.actors.push_back(std::move(actor));
        }
        result.status = Hp1ProfileStatus::ok;
        result.package_version = package.version();
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.error = "allocation failed while inspecting actor visuals";
    } catch (const std::exception& error) {
        result = {};
        result.error = error.what();
    }
    return result;
}

Hp1ClassVisualDefaults inspect_hp1_class_visual_defaults(
    const std::filesystem::path& package_path,
    std::int32_t class_reference) {
    Hp1ClassVisualDefaults result;
    try {
        const Package package(package_path);
        if (class_reference <= 0 ||
            static_cast<std::size_t>(class_reference) >
                package.export_count()) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "class reference is not a local export");
        }
        const auto export_index =
            static_cast<std::size_t>(class_reference - 1);
        const auto& entry = package.export_at(export_index);
        if (entry.class_reference != 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "requested export is not a UClass");
        }

        result.package_version = package.version();
        result.class_reference = class_reference;
        result.super_reference = entry.super_reference;
        result.object_name = package.export_name(export_index);
        result.super_object_path = package.object_path(entry.super_reference);

        Cursor cursor(package.export_bytes(export_index));
        skip_object_stack(cursor, entry.object_flags);
        static_cast<void>(cursor.read_compact_index());  // UField base
        static_cast<void>(cursor.read_compact_index());  // UField next
        static_cast<void>(cursor.read_compact_index());  // script text
        static_cast<void>(cursor.read_compact_index());  // first child
        static_cast<void>(package.name(cursor.read_compact_index()));
        static_cast<void>(cursor.read_u32());  // source line
        static_cast<void>(cursor.read_u32());  // source text position
        if (cursor.read_u32() != 0) {
            fail(Hp1ProfileStatus::unsupported_package,
                 "class bytecode is non-empty and remains outside gate B10");
        }
        static_cast<void>(cursor.read_u64());  // probe mask
        static_cast<void>(cursor.read_u64());  // ignore mask
        static_cast<void>(cursor.read_u16());  // label table offset
        static_cast<void>(cursor.read_u32());  // state flags
        static_cast<void>(cursor.read_u32());  // class flags
        static_cast<void>(cursor.take(16));    // class GUID

        const auto dependency_count = checked_count(
            cursor.read_compact_index(), kMaximumTableEntries,
            "class dependencies");
        for (std::size_t index = 0; index < dependency_count; ++index) {
            static_cast<void>(cursor.read_compact_index());
            static_cast<void>(cursor.read_u32());
            static_cast<void>(cursor.read_u32());
        }
        const auto package_import_count = checked_count(
            cursor.read_compact_index(), kMaximumTableEntries,
            "class package imports");
        for (std::size_t index = 0; index < package_import_count; ++index) {
            static_cast<void>(cursor.read_compact_index());
        }
        static_cast<void>(cursor.read_compact_index());  // Within
        static_cast<void>(package.name(cursor.read_compact_index()));

        bool terminated = false;
        while (cursor.remaining() != 0) {
            const auto tag = read_property_tag(package, cursor);
            if (!tag.has_value()) {
                terminated = true;
                break;
            }
            ++result.property_count;
            result.serialized_properties.push_back(
                retain_property(package, *tag));
            if (ascii_equal_fold(tag->name, "DrawScale")) {
                if (result.draw_scale_serialized ||
                    tag->array_index.has_value()) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "class has invalid DrawScale default");
                }
                result.draw_scale = property_float(*tag);
                result.draw_scale_serialized = true;
            } else if (ascii_equal_fold(tag->name, "Mesh")) {
                if (result.mesh_serialized || tag->array_index.has_value()) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "class has invalid Mesh default");
                }
                result.mesh_reference = property_reference(*tag);
                package.require_valid_reference(result.mesh_reference);
                result.mesh_object_path =
                    package.object_path(result.mesh_reference);
                result.mesh_serialized = true;
            } else if (ascii_equal_fold(tag->name, "Skin") ||
                       ascii_equal_fold(tag->name, "MultiSkins")) {
                const auto skin = property_reference(*tag);
                package.require_valid_reference(skin);
                result.skin_references.push_back(skin);
            } else if (ascii_equal_fold(tag->name, "AnimSequence")) {
                if (result.animation_sequence_serialized ||
                    tag->kind != PropertyKind::name ||
                    tag->array_index.has_value()) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "class has invalid AnimSequence default");
                }
                Cursor value(tag->value);
                result.animation_sequence = std::string(
                    package.name(value.read_compact_index()));
                if (value.remaining() != 0) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "class AnimSequence has trailing bytes");
                }
                result.animation_sequence_serialized = true;
            } else if (ascii_equal_fold(tag->name, "BaseEyeHeight")) {
                if (result.base_eye_height_serialized ||
                    tag->array_index.has_value()) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "class has invalid BaseEyeHeight default");
                }
                result.base_eye_height = property_float(*tag);
                result.base_eye_height_serialized = true;
            } else if (ascii_equal_fold(tag->name, "CollisionHeight")) {
                if (result.collision_height_serialized ||
                    tag->array_index.has_value()) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "class has invalid CollisionHeight default");
                }
                result.collision_height = property_float(*tag);
                result.collision_height_serialized = true;
            } else if (ascii_equal_fold(tag->name, "DrawType")) {
                if (result.draw_type_serialized ||
                    tag->kind != PropertyKind::byte ||
                    tag->array_index.has_value() || tag->value.size() != 1) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "class has invalid DrawType default");
                }
                result.draw_type = tag->value[0];
                result.draw_type_serialized = true;
            } else if (ascii_equal_fold(tag->name, "bHidden")) {
                if (result.hidden_serialized ||
                    tag->kind != PropertyKind::boolean ||
                    !tag->boolean_value.has_value()) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "class has invalid bHidden default");
                }
                result.hidden = *tag->boolean_value;
                result.hidden_serialized = true;
            }
        }
        if (!terminated) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "class defaults have no property terminator");
        }
        require_property_end(cursor);
        result.status = Hp1ProfileStatus::ok;
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.error = "allocation failed while inspecting class defaults";
    } catch (const std::exception& error) {
        result = {};
        result.error = error.what();
    }
    return result;
}

static Hp1P8Texture load_hp1_p8_texture_impl(
    const std::filesystem::path& texture_package,
    std::int32_t texture_reference, bool palette_zero_transparent, unsigned source_depth) {
    Hp1P8Texture result;
    try {
        const Package package(texture_package);
        if (package.version() < 62) {
            fail(Hp1ProfileStatus::unsupported_package,
                 "Gate B9 P8 decoding requires the v62+ lazy-array layout");
        }
        if (texture_reference <= 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "P8 texture reference is not a local export");
        }
        const auto texture_index =
            static_cast<std::size_t>(texture_reference - 1);
        const auto& texture = package.export_at(texture_index);
        const bool wet_texture = package.reference_is_class(
            texture.class_reference, "Fire", "WetTexture");
        if (!wet_texture && !package.reference_is_class(
                texture.class_reference, "Engine", "Texture")) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "selected export is not an Engine.Texture or Fire.WetTexture");
        }

        result.texture_reference = texture_reference;
        result.object_name = package.export_name(texture_index);
        Cursor cursor(package.export_bytes(texture_index));
        skip_object_stack(cursor, texture.object_flags);
        bool properties_terminated = false;
        bool has_compressed_mips = false;
        std::int32_t source_texture_reference = 0;
        while (cursor.remaining() != 0) {
            const auto tag = read_property_tag(package, cursor);
            if (!tag.has_value()) {
                properties_terminated = true;
                break;
            }
            if (wet_texture && ascii_equal_fold(tag->name, "SourceTexture")) {
                if (source_texture_reference != 0 || tag->kind != PropertyKind::object ||
                    tag->array_index.has_value())
                    fail(Hp1ProfileStatus::invalid_profile, "WetTexture SourceTexture is not one object property");
                Cursor value(tag->value);
                source_texture_reference = value.read_compact_index();
                if (value.remaining() != 0 || source_texture_reference <= 0)
                    fail(Hp1ProfileStatus::unsupported_package, "WetTexture requires a local SourceTexture");
                package.require_valid_reference(source_texture_reference);
            } else if (ascii_equal_fold(tag->name, "Format")) {
                if (result.format_serialized ||
                    tag->kind != PropertyKind::byte ||
                    tag->array_index.has_value() ||
                    tag->value.size() != 1) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "Texture Format is not one scalar byte property");
                }
                result.format = tag->value[0];
                result.format_serialized = true;
            } else if (ascii_equal_fold(tag->name, "Palette")) {
                if (result.palette_reference != 0 ||
                    tag->kind != PropertyKind::object ||
                    tag->array_index.has_value()) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "Texture Palette is not one scalar object property");
                }
                Cursor value(tag->value);
                result.palette_reference = value.read_compact_index();
                if (value.remaining() != 0) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "Texture Palette property has trailing bytes");
                }
                package.require_valid_reference(result.palette_reference);
            } else if (ascii_equal_fold(tag->name, "PolyFlags")) {
                if(tag->value.size()!=4 || tag->array_index.has_value())
                    fail(Hp1ProfileStatus::invalid_profile,"Texture PolyFlags is not one scalar integer");
                Cursor value(tag->value);
                result.polygon_flags=value.read_u32();
                palette_zero_transparent=palette_zero_transparent||(result.polygon_flags&2U)!=0;
            } else if (ascii_equal_fold(tag->name, "bHasComp")) {
                if (tag->kind != PropertyKind::boolean ||
                    !tag->boolean_value.has_value()) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "Texture bHasComp is not one boolean property");
                }
                has_compressed_mips = *tag->boolean_value;
            }
        }
        if (!properties_terminated) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Texture properties have no terminator");
        }
        if (wet_texture) {
            if (source_depth >= 8 || source_texture_reference <= 0)
                fail(Hp1ProfileStatus::invalid_profile, "WetTexture SourceTexture is missing or cyclic");
            auto source = load_hp1_p8_texture_impl(texture_package, source_texture_reference,
                                                  palette_zero_transparent, source_depth + 1);
            if (source.status != Hp1ProfileStatus::ok) return source;
            source.texture_reference = texture_reference;
            source.object_name = result.object_name;
            source.polygon_flags |= result.polygon_flags;
            return source;
        }
        if (result.format_serialized && result.format != 0) {
            fail(Hp1ProfileStatus::unsupported_package,
                 "Gate B9 supports only legacy P8 texture format zero");
        }
        if (result.palette_reference <= 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "P8 Texture has no local serialized Palette reference");
        }
        result.compressed_mips_serialized = has_compressed_mips;

        struct ParsedMip {
            Hp1TextureMipInfo info;
            std::span<const std::uint8_t> indices;
        };
        const auto parse_mips =
            [&](Cursor& mip_cursor,
                bool retain,
                std::string_view field) -> std::vector<ParsedMip> {
            const auto mip_count = checked_count(
                mip_cursor.read_compact_index(), kMaximumTextureMips, field);
            std::vector<ParsedMip> parsed;
            parsed.reserve(mip_count);
            for (std::size_t mip_index = 0; mip_index < mip_count;
                 ++mip_index) {
                const auto skip_offset = mip_cursor.read_i32();
                const auto byte_count = checked_count(
                    mip_cursor.read_compact_index(),
                    kMaximumTexturePixels,
                    "Texture mip indexed bytes");
                const auto indices = mip_cursor.take(byte_count);
                const auto absolute_after_data =
                    texture.serial_offset + mip_cursor.position();
                if (skip_offset <= 0 ||
                    static_cast<std::size_t>(skip_offset) !=
                        absolute_after_data) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "Texture mip lazy-array skip offset disagrees");
                }
                const auto width_signed = mip_cursor.read_i32();
                const auto height_signed = mip_cursor.read_i32();
                const auto width_bits = mip_cursor.read_u8();
                const auto height_bits = mip_cursor.read_u8();
                if (width_signed <= 0 || height_signed <= 0 ||
                    width_signed >
                        static_cast<std::int32_t>(kMaximumTextureDimension) ||
                    height_signed >
                        static_cast<std::int32_t>(kMaximumTextureDimension) ||
                    width_bits >= 31 || height_bits >= 31 ||
                    (1U << width_bits) !=
                        static_cast<std::uint32_t>(width_signed) ||
                    (1U << height_bits) !=
                        static_cast<std::uint32_t>(height_signed)) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "Texture mip dimensions or bit counts are invalid");
                }
                const auto pixel_count =
                    static_cast<std::size_t>(width_signed) *
                    static_cast<std::size_t>(height_signed);
                if (retain && byte_count != pixel_count) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "P8 mip byte count does not match its dimensions");
                }
                if (retain) {
                    parsed.push_back({
                        {
                            static_cast<std::uint32_t>(width_signed),
                            static_cast<std::uint32_t>(height_signed),
                            width_bits,
                            height_bits,
                            byte_count,
                        },
                        indices,
                    });
                }
            }
            return parsed;
        };

        if (has_compressed_mips) {
            static_cast<void>(
                parse_mips(cursor, false, "Texture compressed Mips"));
        }
        const auto parsed_mips =
            parse_mips(cursor, true, "Texture Mips");
        if (parsed_mips.empty()) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "P8 Texture has no mip levels");
        }
        if (cursor.remaining() != 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Texture payload has trailing bytes");
        }

        const auto palette_index =
            static_cast<std::size_t>(result.palette_reference - 1);
        const auto& palette = package.export_at(palette_index);
        if (!package.reference_is_class(
                palette.class_reference, "Engine", "Palette")) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Texture Palette reference is not a direct Engine.Palette");
        }
        Cursor palette_cursor(package.export_bytes(palette_index));
        skip_object_stack(palette_cursor, palette.object_flags);
        skip_properties(package, palette_cursor, "Palette");
        const auto palette_count = checked_count(
            palette_cursor.read_compact_index(),
            kPaletteEntryCount,
            "Palette Colors");
        if (palette_count != kPaletteEntryCount) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "P8 Palette does not contain exactly 256 colors");
        }
        std::array<std::array<std::uint8_t, 4>, kPaletteEntryCount>
            palette_rgba{};
        for (auto& color : palette_rgba) {
            color[0] = palette_cursor.read_u8();
            color[1] = palette_cursor.read_u8();
            color[2] = palette_cursor.read_u8();
            color[3] = palette_cursor.read_u8();
        }
        if (palette_cursor.remaining() != 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Palette payload has trailing bytes");
        }

        result.mips.reserve(parsed_mips.size());
        for (const auto& mip : parsed_mips) {
            result.mips.push_back(mip.info);
        }
        const auto& top = parsed_mips.front();
        if (top.info.indexed_bytes >
            std::numeric_limits<std::size_t>::max() / 4U) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "P8 texture RGBA size overflowed");
        }
        result.rgba8.reserve(top.info.indexed_bytes * 4U);
        for (const auto palette_index_value : top.indices) {
            const auto color = palette_zero_transparent && palette_index_value==0
                ? std::array<std::uint8_t,4>{0,0,0,0} : palette_rgba[palette_index_value];
            result.rgba8.insert(
                result.rgba8.end(), color.begin(), color.end());
        }

        result.status = Hp1ProfileStatus::ok;
        result.package_version = package.version();
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = "allocation failed while decoding P8 Texture";
    } catch (const std::exception& error) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = error.what();
    }
    return result;
}

Hp1P8Texture load_hp1_p8_texture(
    const std::filesystem::path& texture_package,
    std::int32_t texture_reference, bool palette_zero_transparent) {
    return load_hp1_p8_texture_impl(texture_package, texture_reference,
                                   palette_zero_transparent, 0);
}

namespace {

[[nodiscard]] std::optional<std::span<const std::uint8_t>> find_riff_wave(
    std::span<const std::uint8_t> bytes) {
    constexpr std::array<std::uint8_t, 4> riff{'R', 'I', 'F', 'F'};
    constexpr std::array<std::uint8_t, 4> wave{'W', 'A', 'V', 'E'};
    for (std::size_t offset = 0; offset + 12 <= bytes.size(); ++offset) {
        if (!std::equal(riff.begin(), riff.end(), bytes.begin() + offset) ||
            !std::equal(wave.begin(), wave.end(), bytes.begin() + offset + 8)) {
            continue;
        }
        const std::uint32_t payload_size =
            static_cast<std::uint32_t>(bytes[offset + 4]) |
            (static_cast<std::uint32_t>(bytes[offset + 5]) << 8U) |
            (static_cast<std::uint32_t>(bytes[offset + 6]) << 16U) |
            (static_cast<std::uint32_t>(bytes[offset + 7]) << 24U);
        const std::uint64_t total_size =
            static_cast<std::uint64_t>(payload_size) + 8U;
        if (total_size >= 12U && total_size <= bytes.size() - offset) {
            return bytes.subspan(offset, static_cast<std::size_t>(total_size));
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::uint16_t wave_u16(
    std::span<const std::uint8_t> bytes,
    std::size_t offset,
    std::string_view field) {
    if (offset > bytes.size() || bytes.size() - offset < 2) {
        fail(Hp1ProfileStatus::invalid_profile,
             std::string(field) + " exceeds WAVE payload");
    }
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1] << 8U);
}

[[nodiscard]] std::uint32_t wave_u32(
    std::span<const std::uint8_t> bytes,
    std::size_t offset,
    std::string_view field) {
    if (offset > bytes.size() || bytes.size() - offset < 4) {
        fail(Hp1ProfileStatus::invalid_profile,
             std::string(field) + " exceeds WAVE payload");
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

}  // namespace

Hp1SoundCensus inspect_hp1_sound_assets(
    const std::filesystem::path& sound_package) {
    Hp1SoundCensus result;
    try {
        const Package package(sound_package);
        for (std::size_t index = 0; index < package.export_count(); ++index) {
            const auto& entry = package.export_at(index);
            if (!package.reference_is_class(
                    entry.class_reference, "Engine", "Sound")) {
                continue;
            }
            const auto wave = find_riff_wave(package.export_bytes(index));
            Hp1SoundAsset sound{
                static_cast<std::int32_t>(index + 1),
                std::string(package.export_name(index)),
                entry.serial_size,
                wave.has_value() ? wave->size() : 0U};
            const auto bytes = package.export_bytes(index);
            sound.payload_prefix_bytes =
                std::min(bytes.size(), sound.payload_prefix.size());
            std::copy_n(bytes.begin(), sound.payload_prefix_bytes,
                        sound.payload_prefix.begin());
            result.sounds.push_back(std::move(sound));
        }
        result.status = Hp1ProfileStatus::ok;
        result.package_version = package.version();
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.error = "allocation failed while inspecting Sound exports";
    } catch (const std::exception& error) {
        result = {};
        result.error = error.what();
    }
    return result;
}

Hp1PcmSound load_hp1_pcm_sound(
    const std::filesystem::path& sound_package,
    const std::int32_t sound_reference) {
    Hp1PcmSound result;
    try {
        const Package package(sound_package);
        package.require_valid_reference(sound_reference);
        if (sound_reference <= 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Sound reference is not a local export");
        }
        const auto index = static_cast<std::size_t>(sound_reference - 1);
        const auto& entry = package.export_at(index);
        if (!package.reference_is_class(
                entry.class_reference, "Engine", "Sound")) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "selected export is not a direct Engine.Sound");
        }
        const auto wave = find_riff_wave(package.export_bytes(index));
        if (!wave.has_value()) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Engine.Sound export has no bounded RIFF/WAVE payload");
        }

        std::optional<std::span<const std::uint8_t>> format;
        std::optional<std::span<const std::uint8_t>> samples;
        for (std::size_t offset = 12; offset + 8 <= wave->size();) {
            const auto chunk_size = wave_u32(*wave, offset + 4, "WAVE chunk");
            const std::uint64_t chunk_end = static_cast<std::uint64_t>(offset) +
                                            8U + chunk_size;
            if (chunk_end > wave->size()) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "WAVE chunk exceeds RIFF boundary");
            }
            const auto payload = wave->subspan(offset + 8, chunk_size);
            if ((*wave)[offset] == 'f' && (*wave)[offset + 1] == 'm' &&
                (*wave)[offset + 2] == 't' && (*wave)[offset + 3] == ' ') {
                format = payload;
            } else if ((*wave)[offset] == 'd' && (*wave)[offset + 1] == 'a' &&
                       (*wave)[offset + 2] == 't' &&
                       (*wave)[offset + 3] == 'a') {
                samples = payload;
            }
            const std::uint64_t next = chunk_end + (chunk_size & 1U);
            if (next > wave->size()) break;
            offset = static_cast<std::size_t>(next);
        }
        if (!format.has_value() || format->size() < 16 ||
            !samples.has_value()) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "WAVE is missing fmt or data chunk");
        }
        const auto encoding = wave_u16(*format, 0, "WAVE encoding");
        const auto channels = wave_u16(*format, 2, "WAVE channels");
        const auto sample_rate = wave_u32(*format, 4, "WAVE sample rate");
        const auto bits = wave_u16(*format, 14, "WAVE sample bits");
        if (encoding != 1 || (channels != 1 && channels != 2) ||
            sample_rate < 4000 || sample_rate > 192000 ||
            (bits != 8 && bits != 16)) {
            fail(Hp1ProfileStatus::unsupported_package,
                 "Sound WAVE format is not supported PCM8/PCM16 mono/stereo");
        }
        const std::size_t bytes_per_sample = bits / 8U;
        if (samples->size() % (bytes_per_sample * channels) != 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "WAVE data is not frame aligned");
        }
        result.samples.reserve(samples->size() / bytes_per_sample);
        if (bits == 8) {
            for (const auto value : *samples) {
                result.samples.push_back(static_cast<std::int16_t>(
                    (static_cast<std::int32_t>(value) - 128) * 256));
            }
        } else {
            for (std::size_t offset = 0; offset < samples->size(); offset += 2) {
                result.samples.push_back(static_cast<std::int16_t>(
                    wave_u16(*samples, offset, "WAVE PCM16 sample")));
            }
        }
        result.status = Hp1ProfileStatus::ok;
        result.package_version = package.version();
        result.sound_reference = sound_reference;
        result.object_name = package.export_name(index);
        result.sample_rate = sample_rate;
        result.channel_count = channels;
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.error = "allocation failed while decoding Sound PCM";
    } catch (const std::exception& error) {
        result = {};
        result.error = error.what();
    }
    return result;
}

Hp1ExportPayload load_hp1_export_payload(const std::filesystem::path& path,
                                        std::int32_t reference) {
    Hp1ExportPayload result;
    try {
        const Package package(path);
        package.require_valid_reference(reference);
        if (reference <= 0) fail(Hp1ProfileStatus::invalid_profile, "Expected local export");
        const auto bytes=package.export_bytes(static_cast<std::size_t>(reference-1));
        if (bytes.size()>64U*1024U*1024U) fail(Hp1ProfileStatus::invalid_profile, "Export exceeds adapter limit");
        result.bytes.assign(bytes.begin(),bytes.end());
        result.status=Hp1ProfileStatus::ok;
    } catch(const std::exception& e) { result.error=e.what(); }
    return result;
}

Hp1MpegSound load_hp1_mpeg_sound(
    const std::filesystem::path& sound_package,
    const std::int32_t sound_reference) {
    Hp1MpegSound result;
    try {
        const Package package(sound_package);
        package.require_valid_reference(sound_reference);
        if (sound_reference <= 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Sound reference is not a local export");
        }
        const auto index = static_cast<std::size_t>(sound_reference - 1);
        const auto& entry = package.export_at(index);
        if (!package.reference_is_class(entry.class_reference, "Engine", "Sound") &&
            !package.reference_is_class(entry.class_reference, "Engine", "Music")) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "selected export is not Sound or Music");
        }
        const auto bytes = package.export_bytes(index);
        std::optional<std::size_t> stream_offset;
        std::uint32_t sample_rate = 0;
        std::uint16_t channels = 0;
        constexpr std::array<std::uint32_t, 3> kMpeg1Rates{
            44100, 48000, 32000};
        constexpr std::array<std::uint32_t, 3> kMpeg2Rates{
            22050, 24000, 16000};
        constexpr std::array<std::uint32_t, 3> kMpeg25Rates{
            11025, 12000, 8000};
        for (std::size_t offset = 0; offset + 4 <= bytes.size(); ++offset) {
            const std::uint32_t header =
                (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
                (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U) |
                (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U) |
                static_cast<std::uint32_t>(bytes[offset + 3]);
            if ((header & 0xFFE00000U) != 0xFFE00000U) continue;
            const unsigned version = (header >> 19U) & 3U;
            const unsigned layer = (header >> 17U) & 3U;
            const unsigned bitrate = (header >> 12U) & 15U;
            const unsigned rate = (header >> 10U) & 3U;
            if (version == 1 || layer != 2 || bitrate == 0 || bitrate == 15 ||
                rate == 3) continue;
            stream_offset = offset;
            sample_rate = version == 3 ? kMpeg1Rates[rate]
                         : version == 2 ? kMpeg2Rates[rate]
                                        : kMpeg25Rates[rate];
            channels = ((header >> 6U) & 3U) == 3U ? 1U : 2U;
            break;
        }
        if (!stream_offset.has_value()) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Engine.Sound export has no MPEG Layer II stream");
        }
        // Engine.Sound stores fields after the encoded stream. Walk complete
        // Layer II frames; never pass the serialized object tail to a codec.
        constexpr std::array<unsigned, 16> rates1{
            0,32,48,56,64,80,96,112,128,160,192,224,256,320,384,0};
        constexpr std::array<unsigned, 16> rates2{
            0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
        std::size_t stream_end = *stream_offset;
        while (stream_end + 4 <= bytes.size()) {
            const auto* p = bytes.data() + stream_end;
            const std::uint32_t h = (std::uint32_t(p[0]) << 24U) |
                (std::uint32_t(p[1]) << 16U) | (std::uint32_t(p[2]) << 8U) | p[3];
            const unsigned version = (h >> 19U) & 3U;
            const unsigned rate_index = (h >> 10U) & 3U;
            if ((h & 0xFFE00000U) != 0xFFE00000U || version == 1 ||
                ((h >> 17U) & 3U) != 2 || rate_index == 3) break;
            const auto rate = version == 3 ? kMpeg1Rates[rate_index]
                : version == 2 ? kMpeg2Rates[rate_index] : kMpeg25Rates[rate_index];
            const unsigned kbps = (version == 3 ? rates1 : rates2)[(h >> 12U) & 15U];
            if (kbps == 0 || rate != sample_rate) break;
            const std::size_t length = 144000ULL * kbps / rate + ((h >> 9U) & 1U);
            if (length < 4 || length > bytes.size() - stream_end) break;
            stream_end += length;
        }
        if (stream_end == *stream_offset)
            fail(Hp1ProfileStatus::invalid_profile, "MPEG stream has no complete frames");
        result.encoded_bytes.assign(bytes.begin() + *stream_offset,
                                    bytes.begin() + stream_end);
        result.status = Hp1ProfileStatus::ok;
        result.package_version = package.version();
        result.sound_reference = sound_reference;
        result.object_name = package.export_name(index);
        result.sample_rate = sample_rate;
        result.channel_count = channels;
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.error = "allocation failed while loading MPEG Sound";
    } catch (const std::exception& error) {
        result = {};
        result.error = error.what();
    }
    return result;
}

Hp1ModelCensus inspect_hp1_model_census(
    const std::filesystem::path& map_package) {
    Hp1ModelCensus result;
    try {
        const Package package(map_package);
        if (package.version() <= 61) {
            fail(Hp1ProfileStatus::unsupported_package,
                 "legacy UModel object-array references are not supported");
        }
        const auto level_index = find_top_level_level(package);
        const auto model_reference =
            read_level_model_reference(package, level_index);
        const auto model_index =
            static_cast<std::size_t>(model_reference - 1);
        const auto& model = package.export_at(model_index);
        Cursor cursor(package.export_bytes(model_index));
        skip_object_stack(cursor, model.object_flags);
        skip_properties(package, cursor, "Model");

        // UPrimitive::Serialize: FBox (6 floats + validity byte) followed by
        // the post-v61 FSphere (4 floats).
        static_cast<void>(cursor.take(25));
        static_cast<void>(cursor.take(16));

        result.vector_count = read_serialized_count(cursor, "Model Vectors");
        skip_repeated_bytes(cursor, result.vector_count, 12, "Model Vectors");
        result.point_count = read_serialized_count(cursor, "Model Points");
        skip_repeated_bytes(cursor, result.point_count, 12, "Model Points");

        result.node_count = read_serialized_count(cursor, "Model Nodes");
        for (std::size_t index = 0; index < result.node_count; ++index) {
            static_cast<void>(cursor.take(24));
            static_cast<void>(cursor.read_u8());
            for (int field = 0; field < 7; ++field) {
                static_cast<void>(cursor.read_compact_index());
            }
            static_cast<void>(cursor.take(3));
            static_cast<void>(cursor.take(8));
        }

        result.surface_count =
            read_serialized_count(cursor, "Model Surfs");
        for (std::size_t index = 0; index < result.surface_count; ++index) {
            const auto texture_reference = cursor.read_compact_index();
            package.require_valid_reference(texture_reference);
            static_cast<void>(cursor.take(4));
            for (int field = 0; field < 6; ++field) {
                static_cast<void>(cursor.read_compact_index());
            }
            static_cast<void>(cursor.take(4));
            const auto actor_reference = cursor.read_compact_index();
            package.require_valid_reference(actor_reference);
        }

        result.vertex_count = read_serialized_count(cursor, "Model Verts");
        for (std::size_t index = 0; index < result.vertex_count; ++index) {
            static_cast<void>(cursor.read_compact_index());
            static_cast<void>(cursor.read_compact_index());
        }

        result.shared_side_count = checked_count(
            cursor.read_i32(), kMaximumTableEntries, "Model shared sides");
        result.zone_count = checked_count(
            cursor.read_i32(), kMaximumTableEntries, "Model Zones");
        for (std::size_t index = 0; index < result.zone_count; ++index) {
            const auto actor_reference = cursor.read_compact_index();
            package.require_valid_reference(actor_reference);
            static_cast<void>(cursor.take(16));
        }

        result.polys_reference = cursor.read_compact_index();
        package.require_valid_reference(result.polys_reference);
        if (result.polys_reference != 0 &&
            (result.polys_reference < 0 ||
             !ascii_equal_fold(package.class_name_for_export(
                                   static_cast<std::size_t>(
                                       result.polys_reference - 1)),
                               "Engine.Polys"))) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Model Polys reference is not a local Engine.Polys export");
        }

        result.light_map_count =
            read_serialized_count(cursor, "Model LightMap");
        for (std::size_t index = 0; index < result.light_map_count; ++index) {
            static_cast<void>(cursor.take(16));
            static_cast<void>(cursor.read_compact_index());
            static_cast<void>(cursor.read_compact_index());
            static_cast<void>(cursor.take(12));
        }
        result.light_bit_bytes =
            read_serialized_count(cursor, "Model LightBits");
        static_cast<void>(cursor.take(result.light_bit_bytes));
        result.bound_count = read_serialized_count(cursor, "Model Bounds");
        skip_repeated_bytes(cursor, result.bound_count, 25, "Model Bounds");
        result.leaf_hull_count =
            read_serialized_count(cursor, "Model LeafHulls");
        skip_repeated_bytes(
            cursor, result.leaf_hull_count, 4, "Model LeafHulls");
        result.leaf_count = read_serialized_count(cursor, "Model Leaves");
        for (std::size_t index = 0; index < result.leaf_count; ++index) {
            static_cast<void>(cursor.read_compact_index());
            static_cast<void>(cursor.read_compact_index());
            static_cast<void>(cursor.read_compact_index());
            static_cast<void>(cursor.take(8));
        }
        result.light_count = read_serialized_count(cursor, "Model Lights");
        for (std::size_t index = 0; index < result.light_count; ++index) {
            const auto light_reference = cursor.read_compact_index();
            package.require_valid_reference(light_reference);
            if (light_reference == 0) {
                ++result.null_light_count;
            }
        }
        result.root_outside = cursor.read_i32() != 0;
        result.linked = cursor.read_i32() != 0;
        if (cursor.remaining() != 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Model payload has trailing bytes");
        }

        result.status = Hp1ProfileStatus::ok;
        result.package_version = package.version();
        result.model_reference = model_reference;
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = "allocation failed while inspecting Model census";
    } catch (const std::exception& error) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = error.what();
    }
    return result;
}

namespace {

struct SkeletalWedgeCapture {
    std::uint16_t point_index{};
    std::array<float, 2> texture_uv{};
};

struct SkeletalFaceCapture {
    std::array<std::uint16_t, 3> wedge_indices{};
    std::uint16_t material_index{};
};

struct SkeletalMaterialCapture {
    std::uint32_t polygon_flags{};
    std::int32_t texture_index{};
};

struct SkeletalGeometryCapture {
    std::vector<Hp1BspVector> points;
    std::vector<SkeletalWedgeCapture> wedges;
    std::vector<SkeletalFaceCapture> faces;
    std::vector<SkeletalMaterialCapture> materials;
    std::vector<Hp1SkeletalBone> bones;
    std::vector<Hp1SkeletalBoneWeightSpan> bone_weight_spans;
    std::vector<Hp1SkeletalBoneWeight> bone_weights;
    std::vector<Hp1BspVector> local_points;
};

Hp1SkeletalMeshCensus decode_hp1_skeletal_mesh_census(
    const std::filesystem::path& mesh_package,
    std::int32_t mesh_reference,
    SkeletalGeometryCapture* geometry) {
    Hp1SkeletalMeshCensus result;
    try {
        const Package package(mesh_package);
        if (package.version() <= 61) {
            fail(Hp1ProfileStatus::unsupported_package,
                 "legacy SkeletalMesh lazy arrays are not supported");
        }
        package.require_valid_reference(mesh_reference);
        if (mesh_reference <= 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "SkeletalMesh reference is not a local export");
        }
        const auto mesh_index = static_cast<std::size_t>(mesh_reference - 1);
        const auto& mesh = package.export_at(mesh_index);
        if (!package.reference_is_class(
                mesh.class_reference, "Engine", "SkeletalMesh")) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "requested export is not a direct Engine.SkeletalMesh");
        }

        Cursor cursor(package.export_bytes(mesh_index));
        skip_object_stack(cursor, mesh.object_flags);
        skip_properties(package, cursor, "SkeletalMesh");

        // UPrimitive::Serialize.
        skip_box(cursor, "SkeletalMesh primitive box");
        skip_sphere(cursor, "SkeletalMesh primitive sphere");

        // UMesh::Serialize (UE1).
        result.packed_vertex_count = read_lazy_fixed_array(
            cursor, mesh, 4, "SkeletalMesh packed vertices");
        result.legacy_triangle_count = read_lazy_fixed_array(
            cursor, mesh, 20, "SkeletalMesh legacy triangles");
        result.animation_sequence_count =
            read_serialized_count(cursor, "SkeletalMesh animation sequences");
        for (std::size_t index = 0;
             index < result.animation_sequence_count;
             ++index) {
            skip_mesh_animation_sequence(package, cursor);
        }
        result.vertex_connect_count = read_lazy_fixed_array(
            cursor, mesh, 8, "SkeletalMesh vertex connects");
        skip_box(cursor, "SkeletalMesh repeated box");
        skip_sphere(cursor, "SkeletalMesh repeated sphere");
        static_cast<void>(read_lazy_fixed_array(
            cursor, mesh, 4, "SkeletalMesh vertex links"));

        result.texture_count =
            read_serialized_count(cursor, "SkeletalMesh textures");
        result.texture_references.reserve(result.texture_count);
        for (std::size_t index = 0; index < result.texture_count; ++index) {
            const auto reference = cursor.read_compact_index();
            package.require_valid_reference(reference);
            result.texture_references.push_back(reference);
        }
        result.bounding_box_count =
            read_serialized_count(cursor, "SkeletalMesh bounding boxes");
        for (std::size_t index = 0;
             index < result.bounding_box_count;
             ++index) {
            skip_box(cursor, "SkeletalMesh bounding boxes");
        }
        result.bounding_sphere_count =
            read_serialized_count(cursor, "SkeletalMesh bounding spheres");
        for (std::size_t index = 0;
             index < result.bounding_sphere_count;
             ++index) {
            skip_sphere(cursor, "SkeletalMesh bounding spheres");
        }
        result.vertex_count = cursor.read_i32();
        result.frame_count = cursor.read_i32();
        if (result.vertex_count < 0 || result.frame_count < 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "SkeletalMesh frame or vertex count is negative");
        }
        static_cast<void>(cursor.read_u32());  // AndFlags
        static_cast<void>(cursor.read_u32());  // OrFlags
        result.mesh_scale = read_bsp_vector(cursor, "SkeletalMesh scale");
        result.mesh_origin = read_bsp_vector(cursor, "SkeletalMesh origin");
        result.rotation_origin = {
            cursor.read_i32(), cursor.read_i32(), cursor.read_i32()};
        static_cast<void>(cursor.read_i32());  // CurPoly
        static_cast<void>(cursor.read_i32());  // CurVertex
        const auto texture_lod_count =
            read_serialized_count(cursor, "SkeletalMesh texture LOD");
        for (std::size_t index = 0; index < texture_lod_count; ++index) {
            static_cast<void>(read_finite_geometry_float(
                cursor, "SkeletalMesh texture LOD"));
        }

        // ULodMesh::Serialize (UE1).
        result.collapse_point_count =
            read_serialized_count(cursor, "SkeletalMesh collapse points");
        skip_repeated_bytes(cursor, result.collapse_point_count, 2,
                            "SkeletalMesh collapse points");
        result.face_level_count =
            read_serialized_count(cursor, "SkeletalMesh face levels");
        skip_repeated_bytes(cursor, result.face_level_count, 2,
                            "SkeletalMesh face levels");

        result.face_count =
            read_serialized_count(cursor, "SkeletalMesh faces");
        std::vector<std::array<std::uint16_t, 4>> faces;
        faces.reserve(result.face_count);
        for (std::size_t index = 0; index < result.face_count; ++index) {
            faces.push_back({cursor.read_u16(), cursor.read_u16(),
                             cursor.read_u16(), cursor.read_u16()});
        }

        result.collapse_wedge_count =
            read_serialized_count(cursor, "SkeletalMesh collapse wedges");
        skip_repeated_bytes(cursor, result.collapse_wedge_count, 2,
                            "SkeletalMesh collapse wedges");
        result.lod_wedge_count =
            read_serialized_count(cursor, "SkeletalMesh LOD wedges");
        std::vector<std::uint16_t> lod_wedge_vertices;
        std::vector<SkeletalWedgeCapture> captured_wedges;
        lod_wedge_vertices.reserve(result.lod_wedge_count);
        captured_wedges.reserve(result.lod_wedge_count);
        for (std::size_t index = 0; index < result.lod_wedge_count; ++index) {
            const auto point_index = cursor.read_u16();
            const auto u = cursor.read_u8();
            const auto v = cursor.read_u8();
            lod_wedge_vertices.push_back(point_index);
            captured_wedges.push_back({
                point_index,
                {static_cast<float>(u) / 255.0F,
                 static_cast<float>(v) / 255.0F},
            });
        }

        result.material_count =
            read_serialized_count(cursor, "SkeletalMesh materials");
        std::vector<std::int32_t> material_texture_indices;
        std::vector<SkeletalMaterialCapture> captured_materials;
        material_texture_indices.reserve(result.material_count);
        captured_materials.reserve(result.material_count);
        for (std::size_t index = 0; index < result.material_count; ++index) {
            const auto polygon_flags = cursor.read_u32();
            const auto texture_index = cursor.read_i32();
            material_texture_indices.push_back(texture_index);
            captured_materials.push_back({polygon_flags, texture_index});
        }
        result.material_texture_indices = material_texture_indices;
        result.special_face_count =
            read_serialized_count(cursor, "SkeletalMesh special faces");
        skip_repeated_bytes(cursor, result.special_face_count, 8,
                            "SkeletalMesh special faces");
        result.model_vertex_count = cursor.read_i32();
        result.special_vertex_count = cursor.read_i32();
        if (result.model_vertex_count < 0 || result.special_vertex_count < 0 ||
            result.special_vertex_count > result.model_vertex_count) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "SkeletalMesh LOD vertex counts are invalid");
        }
        for (int index = 0; index < 3; ++index) {
            static_cast<void>(read_finite_geometry_float(
                cursor, "SkeletalMesh LOD scalar"));
        }
        static_cast<void>(cursor.read_i32());  // LODMinVerts
        for (int index = 0; index < 2; ++index) {
            static_cast<void>(read_finite_geometry_float(
                cursor, "SkeletalMesh LOD scalar"));
        }
        result.remap_vertex_count =
            read_serialized_count(cursor, "SkeletalMesh remap vertices");
        skip_repeated_bytes(cursor, result.remap_vertex_count, 2,
                            "SkeletalMesh remap vertices");
        static_cast<void>(cursor.read_i32());  // OldFrameVerts

        // USkeletalMesh::SerializeSkelMesh1.
        result.skeletal_wedge_count =
            read_serialized_count(cursor, "SkeletalMesh float wedges");
        std::vector<std::uint16_t> skeletal_wedge_vertices;
        skeletal_wedge_vertices.reserve(result.skeletal_wedge_count);
        for (std::size_t index = 0;
             index < result.skeletal_wedge_count;
             ++index) {
            skeletal_wedge_vertices.push_back(cursor.read_u16());
            static_cast<void>(read_finite_geometry_float(
                cursor, "SkeletalMesh wedge U"));
            static_cast<void>(read_finite_geometry_float(
                cursor, "SkeletalMesh wedge V"));
        }
        result.point_count =
            read_serialized_count(cursor, "SkeletalMesh points");
        std::vector<Hp1BspVector> captured_points;
        captured_points.reserve(result.point_count);
        for (std::size_t index = 0; index < result.point_count; ++index) {
            captured_points.push_back(
                read_bsp_vector(cursor, "SkeletalMesh points"));
        }

        result.bone_count =
            read_serialized_count(cursor, "SkeletalMesh bones");
        std::vector<std::int32_t> bone_parents;
        bone_parents.reserve(result.bone_count);
        std::vector<Hp1SkeletalBone> captured_bones;
        captured_bones.reserve(result.bone_count);
        for (std::size_t index = 0; index < result.bone_count; ++index) {
            Hp1SkeletalBone bone;
            bone.name =
                std::string(package.name(cursor.read_compact_index()));
            bone.flags = cursor.read_u32();
            bone.orientation = {
                read_finite_geometry_float(
                    cursor, "SkeletalMesh bone orientation"),
                read_finite_geometry_float(
                    cursor, "SkeletalMesh bone orientation"),
                read_finite_geometry_float(
                    cursor, "SkeletalMesh bone orientation"),
                read_finite_geometry_float(
                    cursor, "SkeletalMesh bone orientation"),
            };
            bone.position = read_bsp_vector(
                cursor, "SkeletalMesh bone position");
            bone.length = read_finite_geometry_float(
                cursor, "SkeletalMesh bone length");
            bone.size =
                read_bsp_vector(cursor, "SkeletalMesh bone size");
            bone.child_count = cursor.read_i32();
            bone.parent_index = cursor.read_i32();
            if (bone.child_count < 0) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "SkeletalMesh bone child count is negative");
            }
            bone_parents.push_back(bone.parent_index);
            captured_bones.push_back(std::move(bone));
        }

        result.bone_index_count =
            read_serialized_count(cursor, "SkeletalMesh bone indices");
        std::vector<Hp1SkeletalBoneWeightSpan> bone_weight_spans;
        bone_weight_spans.reserve(result.bone_index_count);
        for (std::size_t index = 0; index < result.bone_index_count; ++index) {
            bone_weight_spans.push_back({
                cursor.read_u16(),
                cursor.read_u16(),
                cursor.read_u16(),
                cursor.read_u16(),
            });
        }
        result.bone_weight_count =
            read_serialized_count(cursor, "SkeletalMesh bone weights");
        std::vector<std::uint16_t> weighted_points;
        std::vector<Hp1SkeletalBoneWeight> captured_weights;
        weighted_points.reserve(result.bone_weight_count);
        captured_weights.reserve(result.bone_weight_count);
        for (std::size_t index = 0; index < result.bone_weight_count; ++index) {
            Hp1SkeletalBoneWeight weight{
                cursor.read_u16(), cursor.read_u16()};
            weighted_points.push_back(weight.point_index);
            captured_weights.push_back(weight);
        }
        result.local_point_count =
            read_serialized_count(cursor, "SkeletalMesh local points");
        std::vector<Hp1BspVector> captured_local_points;
        captured_local_points.reserve(result.local_point_count);
        for (std::size_t index = 0;
             index < result.local_point_count;
             ++index) {
            captured_local_points.push_back(read_bsp_vector(
                cursor, "SkeletalMesh local points"));
        }
        if (result.local_point_count != result.bone_weight_count) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "SkeletalMesh local point count disagrees with bone weights");
        }
        result.skeletal_depth = cursor.read_i32();
        result.animation_reference = cursor.read_compact_index();
        package.require_valid_reference(result.animation_reference);
        static_cast<void>(cursor.read_i32());  // WeaponBoneIndex
        for (int vector = 0; vector < 4; ++vector) {
            static_cast<void>(read_bsp_vector(
                cursor, "SkeletalMesh weapon adjustment"));
        }
        if (cursor.remaining() != 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "SkeletalMesh payload has trailing bytes");
        }

        for (const auto& face : faces) {
            for (int corner = 0; corner < 3; ++corner) {
                if (face[corner] >= result.lod_wedge_count) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "SkeletalMesh face wedge index is out of range");
                }
            }
            if (face[3] >= result.material_count) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "SkeletalMesh face material index is out of range");
            }
        }
        for (const auto vertex : lod_wedge_vertices) {
            if (static_cast<std::size_t>(vertex) +
                    static_cast<std::size_t>(result.special_vertex_count) >=
                static_cast<std::size_t>(result.model_vertex_count)) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "SkeletalMesh LOD wedge vertex is out of range");
            }
        }
        for (const auto texture_index : material_texture_indices) {
            if (texture_index < 0 ||
                static_cast<std::size_t>(texture_index) >=
                    result.texture_count) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "SkeletalMesh material texture index is out of range");
            }
        }
        for (const auto vertex : skeletal_wedge_vertices) {
            if (vertex >= result.point_count) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "SkeletalMesh float wedge point index is out of range");
            }
        }
        if (result.bone_index_count != result.bone_count) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "SkeletalMesh bone index count disagrees with bone count");
        }
        for (const auto& span : bone_weight_spans) {
            if (span.weight_offset > result.bone_weight_count ||
                span.weight_count >
                    result.bone_weight_count - span.weight_offset ||
                span.detail_count_a > span.weight_count ||
                span.detail_count_b > span.weight_count) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "SkeletalMesh bone weight span is out of range");
            }
        }
        for (const auto point : weighted_points) {
            if (point >= result.point_count) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "SkeletalMesh weighted point is out of range");
            }
        }
        for (std::size_t index = 0; index < bone_parents.size(); ++index) {
            const auto parent = bone_parents[index];
            if (parent < 0 ||
                static_cast<std::size_t>(parent) >= result.bone_count) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "SkeletalMesh bone parent index is out of range");
            }
        }
        if (result.skeletal_depth < 0 ||
            static_cast<std::size_t>(result.skeletal_depth) >
                result.bone_count) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "SkeletalMesh depth is out of range");
        }

        if (geometry != nullptr) {
            geometry->points = std::move(captured_points);
            geometry->wedges = std::move(captured_wedges);
            geometry->materials = std::move(captured_materials);
            geometry->bones = std::move(captured_bones);
            geometry->bone_weight_spans = std::move(bone_weight_spans);
            geometry->bone_weights = std::move(captured_weights);
            geometry->local_points = std::move(captured_local_points);
            geometry->faces.reserve(faces.size());
            for (const auto& face : faces) {
                geometry->faces.push_back({
                    {face[0], face[1], face[2]}, face[3]});
            }
        }

        result.status = Hp1ProfileStatus::ok;
        result.package_version = package.version();
        result.mesh_reference = mesh_reference;
        result.object_name = std::string(package.export_name(mesh_index));
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = "allocation failed while inspecting SkeletalMesh";
    } catch (const std::exception& error) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = error.what();
    }
    return result;
}

}  // namespace

Hp1SkeletalMeshCensus inspect_hp1_skeletal_mesh_census(
    const std::filesystem::path& mesh_package,
    std::int32_t mesh_reference) {
    return decode_hp1_skeletal_mesh_census(
        mesh_package, mesh_reference, nullptr);
}

Hp1SkeletalSkin load_hp1_skeletal_skin(
    const std::filesystem::path& mesh_package,
    std::int32_t mesh_reference) {
    Hp1SkeletalSkin result;
    SkeletalGeometryCapture capture;
    result.census = decode_hp1_skeletal_mesh_census(
        mesh_package, mesh_reference, &capture);
    if (result.census.status != Hp1ProfileStatus::ok) {
        result.status = result.census.status;
        result.error = result.census.error;
        return result;
    }
    result.points = std::move(capture.points);
    result.bones = std::move(capture.bones);
    result.bone_weight_spans = std::move(capture.bone_weight_spans);
    result.bone_weights = std::move(capture.bone_weights);
    result.local_points = std::move(capture.local_points);
    result.status = Hp1ProfileStatus::ok;
    return result;
}

Hp1SkeletalTriangleMesh build_hp1_skeletal_triangle_mesh(
    const std::filesystem::path& mesh_package,
    std::int32_t mesh_reference,
    float meters_per_unreal_unit) {
    Hp1SkeletalTriangleMesh result;
    try {
        if (!std::isfinite(meters_per_unreal_unit) ||
            meters_per_unreal_unit <= 0.0F) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "SkeletalMesh scale must be finite and positive");
        }
        SkeletalGeometryCapture geometry;
        result.census = decode_hp1_skeletal_mesh_census(
            mesh_package, mesh_reference, &geometry);
        if (result.census.status != Hp1ProfileStatus::ok) {
            result.status = result.census.status;
            result.error = result.census.error;
            return result;
        }
        if (geometry.faces.size() > kMaximumTableEntries / 3U) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "SkeletalMesh triangle stream exceeds the safety cap");
        }
        result.vertices.reserve(geometry.faces.size() * 3U);
        bool have_bounds = false;
        for (std::size_t face_index = 0;
             face_index < geometry.faces.size();
             ++face_index) {
            const auto& face = geometry.faces[face_index];
            const auto& material = geometry.materials[face.material_index];
            // UE1 skeletal meshes are mirrored on X and swap the first two
            // face corners during the legacy conversion. Composed with the
            // repository's Unreal-to-OpenXR mapping this is (Y, Z, X).
            const std::array<std::size_t, 3> corner_order{1, 0, 2};
            for (const auto source_corner : corner_order) {
                const auto& wedge =
                    geometry.wedges[face.wedge_indices[source_corner]];
                const auto& point = geometry.points[wedge.point_index];
                const Hp1BspVector position{
                    point.y * meters_per_unreal_unit,
                    point.z * meters_per_unreal_unit,
                    point.x * meters_per_unreal_unit,
                };
                if (!have_bounds) {
                    result.bounds_min_m = position;
                    result.bounds_max_m = position;
                    have_bounds = true;
                } else {
                    result.bounds_min_m.x =
                        std::min(result.bounds_min_m.x, position.x);
                    result.bounds_min_m.y =
                        std::min(result.bounds_min_m.y, position.y);
                    result.bounds_min_m.z =
                        std::min(result.bounds_min_m.z, position.z);
                    result.bounds_max_m.x =
                        std::max(result.bounds_max_m.x, position.x);
                    result.bounds_max_m.y =
                        std::max(result.bounds_max_m.y, position.y);
                    result.bounds_max_m.z =
                        std::max(result.bounds_max_m.z, position.z);
                }
                result.vertices.push_back({
                    position,
                    wedge.texture_uv,
                    face.material_index,
                    material.polygon_flags,
                    static_cast<std::uint32_t>(face_index),
                    wedge.point_index,
                });
            }
        }
        if (!have_bounds) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "SkeletalMesh has no renderable faces");
        }
        result.status = Hp1ProfileStatus::ok;
        result.meters_per_unreal_unit = meters_per_unreal_unit;
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = "allocation failed while building SkeletalMesh triangles";
    } catch (const std::exception& error) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = error.what();
    }
    return result;
}

Hp1Animation load_hp1_animation(
    const std::filesystem::path& animation_package,
    std::int32_t animation_reference) {
    Hp1Animation result;
    try {
        const Package package(animation_package);
        package.require_valid_reference(animation_reference);
        if (animation_reference <= 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Animation reference is not a local export");
        }
        const auto animation_index =
            static_cast<std::size_t>(animation_reference - 1);
        const auto& animation = package.export_at(animation_index);
        if (!package.reference_is_class(
                animation.class_reference, "Engine", "Animation")) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "requested export is not a direct Engine.Animation");
        }
        result.package_version = package.version();
        result.animation_reference = animation_reference;
        result.object_name = package.export_name(animation_index);

        Cursor cursor(package.export_bytes(animation_index));
        skip_object_stack(cursor, animation.object_flags);
        skip_properties(package, cursor, "Animation");

        const auto bone_count =
            read_serialized_count(cursor, "Animation reference bones");
        result.bones.reserve(bone_count);
        for (std::size_t index = 0; index < bone_count; ++index) {
            const auto name =
                std::string(package.name(cursor.read_compact_index()));
            const auto flags = cursor.read_u32();
            const auto parent_index = cursor.read_i32();
            if (parent_index < 0 ||
                static_cast<std::size_t>(parent_index) >= bone_count) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "Animation bone parent is outside the reference skeleton");
            }
            result.bones.push_back({name, flags, parent_index});
        }

        std::size_t total_keys = 0;
        const auto read_track =
            [&cursor, &result, &total_keys](std::string_view label) {
            Hp1AnimationTrack track;
            track.flags = cursor.read_u32();
            const auto track_header = std::string(label) + " flags=" +
                                      std::to_string(track.flags) +
                                      " offset=" +
                                      std::to_string(cursor.position());
            track.orientation_count = read_serialized_count(
                cursor, track_header + " orientation keys");
            if (track.orientation_count > kMaximumTableEntries - total_keys) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "Animation key total exceeds the safety cap");
            }
            track.orientation_offset = result.orientation_key_count;
            total_keys += track.orientation_count;
            result.orientation_key_count += track.orientation_count;

            track.position_count = read_serialized_count(
                cursor, track_header + " position keys");
            if (track.position_count > kMaximumTableEntries - total_keys) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "Animation key total exceeds the safety cap");
            }
            track.position_offset = result.position_key_count;
            total_keys += track.position_count;
            result.position_key_count += track.position_count;

            track.time_count = read_serialized_count(
                cursor, track_header + " time keys");
            if (track.time_count > kMaximumTableEntries - total_keys) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "Animation key total exceeds the safety cap");
            }
            track.time_offset = result.time_key_count;
            total_keys += track.time_count;
            result.time_key_count += track.time_count;

            track.position_scale = read_finite_geometry_float(
                cursor, track_header + " position scale");
            track.time_scale = read_finite_geometry_float(
                cursor, track_header + " time scale");
            return track;
        };

        const auto move_count =
            read_serialized_count(cursor, "Animation moves");
        result.moves.reserve(move_count);
        for (std::size_t move_index = 0;
             move_index < move_count;
             ++move_index) {
            Hp1AnimationMove move;
            move.root_speed = read_bsp_vector(cursor, "Animation root speed");
            move.track_time = read_finite_geometry_float(
                cursor, "Animation track time");
            if (move.track_time < 0.0F) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "Animation track time is negative");
            }
            move.start_bone = cursor.read_i32();
            move.flags = cursor.read_u32();
            const auto bone_index_count = read_serialized_count(
                cursor, "Animation move bone indices");
            move.bone_indices.reserve(bone_index_count);
            for (std::size_t index = 0;
                 index < bone_index_count;
                 ++index) {
                const auto bone_index = cursor.read_i32();
                if (bone_index < -1 ||
                    (bone_index >= 0 &&
                     static_cast<std::size_t>(bone_index) >= bone_count)) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "Animation move bone index is outside the skeleton");
                }
                move.bone_indices.push_back(bone_index);
            }
            const auto track_count = read_serialized_count(
                cursor, "Animation move tracks");
            move.tracks.reserve(track_count);
            for (std::size_t track_index = 0;
                 track_index < track_count;
                 ++track_index) {
                move.tracks.push_back(read_track(
                    "Animation move=" + std::to_string(move_index) +
                    " track=" + std::to_string(track_index)));
            }
            result.moves.push_back(std::move(move));
        }

        const auto sequence_count = read_serialized_count(
            cursor, "Animation sequences");
        result.sequences.reserve(sequence_count);
        for (std::size_t index = 0; index < sequence_count; ++index) {
            Hp1AnimationSequence sequence;
            sequence.name =
                std::string(package.name(cursor.read_compact_index()));
            sequence.group =
                std::string(package.name(cursor.read_compact_index()));
            sequence.start_frame = cursor.read_i32();
            sequence.frame_count = cursor.read_i32();
            if (sequence.start_frame < 0 || sequence.frame_count < 0) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "Animation sequence frame range is negative");
            }
            sequence.notify_count = read_serialized_count(
                cursor, "Animation sequence notifications");
            for (std::size_t notify = 0;
                 notify < sequence.notify_count;
                 ++notify) {
                const auto time = read_finite_geometry_float(
                    cursor, "Animation notification time");
                if (time < 0.0F || time > 1.0F) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "Animation notification time is outside 0..1");
                }
                static_cast<void>(
                    package.name(cursor.read_compact_index()));
            }
            sequence.rate = read_finite_geometry_float(
                cursor, "Animation sequence rate");
            if (sequence.rate < 0.0F) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "Animation sequence rate is negative");
            }
            result.sequences.push_back(std::move(sequence));
        }

        const auto orientation_storage_count = read_serialized_count(
            cursor, "Animation compressed orientation storage");
        result.compressed_orientation_keys.reserve(orientation_storage_count);
        for (std::size_t index = 0;
             index < orientation_storage_count;
             ++index) {
            result.compressed_orientation_keys.push_back(
                {cursor.read_i16(), cursor.read_i16(), cursor.read_i16()});
        }
        const auto position_storage_count = read_serialized_count(
            cursor, "Animation compressed position storage");
        result.compressed_position_keys.reserve(position_storage_count);
        for (std::size_t index = 0;
             index < position_storage_count;
             ++index) {
            result.compressed_position_keys.push_back(
                {cursor.read_i16(), cursor.read_i16(), cursor.read_i16()});
        }
        const auto time_storage_count = read_serialized_count(
            cursor, "Animation compressed time storage");
        result.compressed_time_keys.reserve(time_storage_count);
        for (std::size_t index = 0; index < time_storage_count; ++index) {
            result.compressed_time_keys.push_back(cursor.read_u8());
        }
        if (result.orientation_key_count != orientation_storage_count ||
            result.position_key_count != position_storage_count ||
            result.time_key_count != time_storage_count) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Animation track references do not exactly cover compressed "
                 "key storage");
        }
        if (cursor.remaining() != 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Animation payload has trailing bytes");
        }
        result.status = Hp1ProfileStatus::ok;
    } catch (const ProfileException& error) {
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.error = "allocation failed while loading Animation";
    } catch (const std::exception& error) {
        result = {};
        result.error = error.what();
    }
    return result;
}

Hp1Quaternion decode_hp1_animation_orientation_key(
    std::array<std::int16_t, 3> key) noexcept {
    // The owned retail binary uses this float-rounded value of
    // (pi / 2) / 32767 before applying sin to each stored component.
    constexpr float radians_per_unit{4.7938363e-05F};
    const float x = std::sin(static_cast<float>(key[0]) * radians_per_unit);
    const float y = std::sin(static_cast<float>(key[1]) * radians_per_unit);
    const float z = std::sin(static_cast<float>(key[2]) * radians_per_unit);
    const float remaining =
        std::max(0.0F, 1.0F - x * x - y * y - z * z);
    return {x, y, z, std::sqrt(remaining)};
}

Hp1BspVector decode_hp1_animation_position_key(
    std::array<std::int16_t, 3> key,
    float track_scale) noexcept {
    // The shipped decoder multiplies the track scale by 1/32767 first.
    constexpr float signed_unit{3.051851e-05F};
    const float scale = track_scale * signed_unit;
    return {
        static_cast<float>(key[0]) * scale,
        static_cast<float>(key[1]) * scale,
        static_cast<float>(key[2]) * scale,
    };
}

float decode_hp1_animation_time_key(
    std::uint8_t key,
    float track_scale) noexcept {
    return static_cast<float>(key) * track_scale;
}

namespace {

Hp1Quaternion normalized_quaternion(
    Hp1Quaternion value,
    std::string_view label) {
    const float length_squared =
        value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w;
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-12F) {
        fail(Hp1ProfileStatus::invalid_profile,
             std::string(label) + " quaternion is invalid");
    }
    const float inverse_length = 1.0F / std::sqrt(length_squared);
    return {
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
        value.w * inverse_length,
    };
}

Hp1Quaternion multiply_quaternions(
    Hp1Quaternion left,
    Hp1Quaternion right) noexcept {
    return {
        left.w * right.x + left.x * right.w +
            left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z +
            left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y -
            left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x -
            left.y * right.y - left.z * right.z,
    };
}

Hp1BspVector rotate_by_inverse_quaternion(
    Hp1Quaternion rotation,
    Hp1BspVector point) noexcept {
    rotation.x = -rotation.x;
    rotation.y = -rotation.y;
    rotation.z = -rotation.z;
    const Hp1BspVector axis{rotation.x, rotation.y, rotation.z};
    const Hp1BspVector cross{
        axis.y * point.z - axis.z * point.y,
        axis.z * point.x - axis.x * point.z,
        axis.x * point.y - axis.y * point.x,
    };
    const Hp1BspVector second_cross{
        axis.y * cross.z - axis.z * cross.y,
        axis.z * cross.x - axis.x * cross.z,
        axis.x * cross.y - axis.y * cross.x,
    };
    return {
        point.x + 2.0F * (rotation.w * cross.x + second_cross.x),
        point.y + 2.0F * (rotation.w * cross.y + second_cross.y),
        point.z + 2.0F * (rotation.w * cross.z + second_cross.z),
    };
}

Hp1Quaternion interpolate_quaternions(
    Hp1Quaternion first,
    Hp1Quaternion second,
    float fraction) {
    first = normalized_quaternion(first, "Animation key");
    second = normalized_quaternion(second, "Animation key");
    float dot = first.x * second.x + first.y * second.y +
                first.z * second.z + first.w * second.w;
    if (dot < 0.0F) {
        second = {-second.x, -second.y, -second.z, -second.w};
        dot = -dot;
    }
    Hp1Quaternion value;
    if (dot > 0.9995F) {
        value = {
            first.x + (second.x - first.x) * fraction,
            first.y + (second.y - first.y) * fraction,
            first.z + (second.z - first.z) * fraction,
            first.w + (second.w - first.w) * fraction,
        };
    } else {
        const float angle = std::acos(std::clamp(dot, -1.0F, 1.0F));
        const float denominator = std::sin(angle);
        const float first_weight =
            std::sin((1.0F - fraction) * angle) / denominator;
        const float second_weight =
            std::sin(fraction * angle) / denominator;
        value = {
            first.x * first_weight + second.x * second_weight,
            first.y * first_weight + second.y * second_weight,
            first.z * first_weight + second.z * second_weight,
            first.w * first_weight + second.w * second_weight,
        };
    }
    return normalized_quaternion(value, "Interpolated animation");
}

struct AnimationKeyInterval {
    std::size_t first{};
    std::size_t second{};
    float fraction{};
};

AnimationKeyInterval animation_key_interval(
    const Hp1Animation& animation,
    const Hp1AnimationTrack& track,
    float sample_time,
    float track_time, bool looping) {
    if (track.time_count == 0) {
        return {};
    }
    float prior_time = 0.0F;
    for (std::size_t index = 1; index < track.time_count; ++index) {
        const float current_time =
            prior_time + decode_hp1_animation_time_key(
                             animation.compressed_time_keys[
                                 track.time_offset + index],
                             track.time_scale);
        if (sample_time <= current_time) {
            const float duration = current_time - prior_time;
            return {
                index - 1,
                index,
                duration > 0.0F
                    ? std::clamp(
                          (sample_time - prior_time) / duration, 0.0F, 1.0F)
                    : 0.0F,
            };
        }
        prior_time = current_time;
    }
    if(!looping)return {track.time_count-1,track.time_count-1,0.0F};
    const float wrap_duration = track_time - prior_time;
    return {
        track.time_count - 1,
        0,
        wrap_duration > 0.0F
            ? std::clamp(
                  (sample_time - prior_time) / wrap_duration, 0.0F, 1.0F)
            : 0.0F,
    };
}

}  // namespace

Hp1SkeletalPose sample_hp1_skeletal_animation(
    const Hp1SkeletalSkin& skin,
    const Hp1Animation& animation,
    std::size_t sequence_index,
    float elapsed_seconds, bool looping, bool stabilize_root_height) {
    Hp1SkeletalPose result;
    try {
        if (skin.status != Hp1ProfileStatus::ok ||
            animation.status != Hp1ProfileStatus::ok) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Skeletal skin and Animation must both be valid");
        }
        if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0F) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Animation elapsed time must be finite and non-negative");
        }
        if (sequence_index >= animation.sequences.size() ||
            sequence_index >= animation.moves.size()) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Animation sequence index is out of range");
        }
        if (skin.bones.size() != animation.bones.size()) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Animation skeleton bone count disagrees with mesh");
        }
        for (std::size_t bone = 0; bone < skin.bones.size(); ++bone) {
            if (!ascii_equal_fold(
                    skin.bones[bone].name, animation.bones[bone].name)) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "Animation skeleton identity disagrees with mesh at "
                     "bone=" + std::to_string(bone) +
                     " mesh_name=" + skin.bones[bone].name +
                     " animation_name=" + animation.bones[bone].name);
            }
        }
        const auto& move = animation.moves[sequence_index];
        if (move.track_time <= 0.0F ||
            move.bone_indices.size() != skin.bones.size()) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Animation move has an invalid track map");
        }
        const float sample_time = looping?std::fmod(elapsed_seconds, move.track_time):
            std::min(elapsed_seconds,move.track_time);

        struct BoneTransform {
            Hp1Quaternion orientation;
            Hp1BspVector position;
        };
        std::vector<BoneTransform> local_transforms(skin.bones.size());
        for (std::size_t bone = 0; bone < skin.bones.size(); ++bone) {
            Hp1Quaternion orientation = skin.bones[bone].orientation;
            Hp1BspVector position = skin.bones[bone].position;
            const auto track_index = move.bone_indices[bone];
            if (track_index >= 0) {
                if (static_cast<std::size_t>(track_index) >=
                    move.tracks.size()) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "Animation bone-to-track index is out of range");
                }
                const auto& track =
                    move.tracks[static_cast<std::size_t>(track_index)];
                if (track.orientation_count > 0) {
                    if (track.orientation_count != track.time_count) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "Orientation keys do not share track times");
                    }
                    const auto interval = animation_key_interval(
                        animation, track, sample_time, move.track_time, looping);
                    const auto first =
                        decode_hp1_animation_orientation_key(
                            animation.compressed_orientation_keys[
                                track.orientation_offset + interval.first]);
                    const auto second =
                        decode_hp1_animation_orientation_key(
                            animation.compressed_orientation_keys[
                                track.orientation_offset + interval.second]);
                    orientation = interpolate_quaternions(
                        first, second, interval.fraction);
                }
                if (track.position_count == 1) {
                    position = decode_hp1_animation_position_key(
                        animation.compressed_position_keys[
                            track.position_offset],
                        track.position_scale);
                } else if (track.position_count > 1) {
                    if (track.position_count != track.time_count) {
                        fail(Hp1ProfileStatus::invalid_profile,
                             "Dynamic position keys do not share track times");
                    }
                    const auto interval = animation_key_interval(
                        animation, track, sample_time, move.track_time, looping);
                    const auto first = decode_hp1_animation_position_key(
                        animation.compressed_position_keys[
                            track.position_offset + interval.first],
                        track.position_scale);
                    const auto second = decode_hp1_animation_position_key(
                        animation.compressed_position_keys[
                            track.position_offset + interval.second],
                        track.position_scale);
                    position = {
                        first.x + (second.x - first.x) * interval.fraction,
                        first.y + (second.y - first.y) * interval.fraction,
                        first.z + (second.z - first.z) * interval.fraction,
                    };
                }
            }
            // Scene locomotion owns the actor's vertical path. Optional VR
            // stabilization removes root bounce, retaining limb articulation.
            if(stabilize_root_height&&skin.bones[bone].parent_index==static_cast<std::int32_t>(bone))
                position.z=skin.bones[bone].position.z;
            orientation =
                normalized_quaternion(orientation, "Animation bone");
            local_transforms[bone] = {orientation, position};
        }

        // The mesh hierarchy is authoritative for skin weights/local points.
        // Animation.RefBones supplies ordered track identity; one shipped set
        // contains cyclic parent metadata despite matching bone names, so it
        // must not replace the validated mesh hierarchy.
        std::vector<BoneTransform> transforms(skin.bones.size());
        std::vector<std::uint8_t> transform_states(skin.bones.size());
        const auto resolve_transform =
            [&](auto&& resolve, std::size_t bone) -> void {
            if (transform_states[bone] == 2) {
                return;
            }
            if (transform_states[bone] == 1) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "Animation skeleton contains a parent cycle");
            }
            transform_states[bone] = 1;
            auto transform = local_transforms[bone];
            const auto parent_value = skin.bones[bone].parent_index;
            if (parent_value < 0 ||
                static_cast<std::size_t>(parent_value) >=
                    transforms.size()) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "Animation skeleton parent is out of range");
            }
            const auto parent_index =
                static_cast<std::size_t>(parent_value);
            if (parent_index != bone) {
                resolve(resolve, parent_index);
                const auto& parent = transforms[parent_index];
                const auto relative =
                    rotate_by_inverse_quaternion(
                        parent.orientation, transform.position);
                transform.position = {
                    parent.position.x + relative.x,
                    parent.position.y + relative.y,
                    parent.position.z + relative.z,
                };
                transform.orientation = normalized_quaternion(
                    multiply_quaternions(
                        transform.orientation, parent.orientation),
                    "Animation global bone");
            }
            transforms[bone] = transform;
            transform_states[bone] = 2;
        };
        for (std::size_t bone = 0; bone < skin.bones.size(); ++bone) {
            resolve_transform(resolve_transform, bone);
        }

        result.points.resize(skin.points.size());
        for (std::size_t bone = 0;
             bone < skin.bone_weight_spans.size();
             ++bone) {
            const auto& span = skin.bone_weight_spans[bone];
            const auto& transform = transforms[bone];
            for (std::size_t offset = 0;
                 offset < span.weight_count;
                 ++offset) {
                const auto weight_index = span.weight_offset + offset;
                const auto& influence = skin.bone_weights[weight_index];
                const auto local = rotate_by_inverse_quaternion(
                    transform.orientation,
                    skin.local_points[weight_index]);
                const float weight =
                    static_cast<float>(influence.encoded_weight) / 65535.0F;
                auto& target = result.points[influence.point_index];
                target.x += (local.x + transform.position.x) * weight;
                target.y += (local.y + transform.position.y) * weight;
                target.z += (local.z + transform.position.z) * weight;
            }
        }
        if (std::ranges::any_of(
                result.points,
                [](Hp1BspVector point) {
                    return !std::isfinite(point.x) ||
                           !std::isfinite(point.y) ||
                           !std::isfinite(point.z);
                })) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "Animation skinning produced a non-finite point");
        }
        result.status = Hp1ProfileStatus::ok;
        result.sequence_index = sequence_index;
        result.sample_time_seconds = sample_time;
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = "allocation failed while sampling skeletal animation";
    } catch (const std::exception& error) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = error.what();
    }
    return result;
}

Hp1BspTopology load_hp1_bsp_topology(
    const std::filesystem::path& map_package) {
    Hp1BspTopology result;
    try {
        const Package package(map_package);
        if (package.version() <= 61) {
            fail(Hp1ProfileStatus::unsupported_package,
                 "legacy UModel object-array references are not supported");
        }
        const auto level_index = find_top_level_level(package);
        const auto model_reference =
            read_level_model_reference(package, level_index);
        result = decode_bsp_topology(package, model_reference);
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = "allocation failed while loading BSP topology";
    } catch (const std::exception& error) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = error.what();
    }
    return result;
}

Hp1BspTopology load_hp1_brush_topology(
    const std::filesystem::path& map_package, std::int32_t model_reference) {
    Hp1BspTopology result;
    try {
        const Package package(map_package);
        package.require_valid_reference(model_reference);
        if (model_reference <= 0 || !ascii_equal_fold(
                package.class_name_for_export(static_cast<std::size_t>(model_reference - 1)),
                "Engine.Model")) {
            fail(Hp1ProfileStatus::invalid_profile, "Brush is not a local Model");
        }
        result = decode_bsp_topology(package, model_reference);
    } catch (const std::exception& error) {
        result = {};
        result.error = error.what();
    }
    return result;
}

Hp1BspTopology load_hp1_brush_polygon_topology(
    const std::filesystem::path& map_package, std::int32_t model_reference) {
    Hp1BspTopology result;
    try {
        const Package package(map_package);
        package.require_valid_reference(model_reference);
        if(model_reference<=0 || !ascii_equal_fold(package.class_name_for_export(
            static_cast<std::size_t>(model_reference-1)),"Engine.Model"))
            fail(Hp1ProfileStatus::invalid_profile,"Brush is not a local Model");
        const auto auxiliary=decode_bsp_topology(package,model_reference);
        const auto reference=auxiliary.polys_reference;
        if(reference<=0)fail(Hp1ProfileStatus::invalid_profile,"Brush has no authored Polys");
        const auto index=static_cast<std::size_t>(reference-1);
        Cursor cursor(package.export_bytes(index));
        skip_object_stack(cursor,package.export_at(index).object_flags);
        skip_properties(package,cursor,"Polys");
        const auto count=checked_count(cursor.read_i32(),65536,"Polys count");
        const auto capacity=checked_count(cursor.read_i32(),65536,"Polys capacity");
        if(!count||capacity<count)fail(Hp1ProfileStatus::invalid_profile,"Invalid Polys array");
        result.model_reference=model_reference;result.polys_reference=reference;
        result.package_version=auxiliary.package_version;
        for(std::size_t i=0;i<count;++i){
            const auto vertices=cursor.read_u8();
            if(vertices<3||vertices>64)fail(Hp1ProfileStatus::invalid_profile,"Invalid brush polygon vertex count");
            const auto base=read_bsp_vector(cursor,"Poly base");
            const auto normal=read_bsp_vector(cursor,"Poly normal");
            const auto u=read_bsp_vector(cursor,"Poly U");
            const auto v=read_bsp_vector(cursor,"Poly V");
            Hp1BspSurface surface;surface.base_point_index=static_cast<std::int32_t>(result.points.size());
            result.points.push_back(base);
            surface.normal_vector_index=static_cast<std::int32_t>(result.vectors.size());result.vectors.push_back(normal);
            surface.texture_u_vector_index=static_cast<std::int32_t>(result.vectors.size());result.vectors.push_back(u);
            surface.texture_v_vector_index=static_cast<std::int32_t>(result.vectors.size());result.vectors.push_back(v);
            surface.light_map_index=-1;
            Hp1BspNode node;node.vertex_count=vertices;node.surface_index=static_cast<std::int32_t>(i);
            node.vertex_pool_index=static_cast<std::int32_t>(result.vertices.size());
            node.front_node_index=node.back_node_index=node.coplanar_node_index=-1;
            node.collision_bound_index=-1;node.leaf_indices={-1,-1};
            node.plane={normal.x,normal.y,normal.z,normal.x*base.x+normal.y*base.y+normal.z*base.z};
            for(unsigned j=0;j<vertices;++j){
                const auto point=read_bsp_vector(cursor,"Poly vertex");
                const float plane=(point.x-base.x)*normal.x+(point.y-base.y)*normal.y+(point.z-base.z)*normal.z;
                if(!std::isfinite(plane)||std::abs(plane)>.125F)
                    fail(Hp1ProfileStatus::invalid_profile,"Non-planar authored brush polygon");
                result.vertices.push_back({static_cast<std::int32_t>(result.points.size()),-1});result.points.push_back(point);
            }
            surface.polygon_flags=cursor.read_u32();
            surface.actor_reference=cursor.read_compact_index();package.require_valid_reference(surface.actor_reference);
            surface.texture_reference=cursor.read_compact_index();package.require_valid_reference(surface.texture_reference);
            static_cast<void>(package.name(cursor.read_compact_index()));
            const auto link=cursor.read_compact_index();
            if(link < -1 || link>=static_cast<std::int32_t>(count))
                fail(Hp1ProfileStatus::invalid_profile,"Invalid authored polygon link");
            static_cast<void>(cursor.read_compact_index()); // Brush polygon bookkeeping index.
            surface.pan_u=cursor.read_i16();surface.pan_v=cursor.read_i16();
            result.surfaces.push_back(surface);result.nodes.push_back(node);
        }
        if(cursor.remaining()!=0)fail(Hp1ProfileStatus::invalid_profile,"Trailing authored polygon data");
        result.status=Hp1ProfileStatus::ok;
    }catch(const std::exception& error){result={};result.error=error.what();}
    return result;
}

Hp1BspTriangleMesh build_hp1_bsp_triangle_mesh(
    const Hp1BspTopology& topology,
    float meters_per_unreal_unit) {
    Hp1BspTriangleMesh result;
    try {
        if (topology.status != Hp1ProfileStatus::ok) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "BSP topology is not in the ok state");
        }
        if (!std::isfinite(meters_per_unreal_unit) ||
            meters_per_unreal_unit <= 0.0F) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "meters-per-Unreal-unit scale must be finite and positive");
        }
        if (topology.nodes.size() > kMaximumTableEntries ||
            topology.surfaces.size() > kMaximumTableEntries ||
            topology.vertices.size() > kMaximumTableEntries ||
            topology.points.size() > kMaximumTableEntries) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "BSP topology exceeds the triangle-builder safety cap");
        }

        const auto converted = [](Hp1BspVector point) {
            return Hp1BspVector{
                point.y,
                point.z,
                -point.x,
            };
        };
        const auto scaled = [meters_per_unreal_unit](Hp1BspVector point) {
            return Hp1BspVector{
                point.x * meters_per_unreal_unit,
                point.y * meters_per_unreal_unit,
                point.z * meters_per_unreal_unit,
            };
        };
        const auto finite_vector = [](Hp1BspVector value) {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z);
        };
        const auto subtract = [](Hp1BspVector left, Hp1BspVector right) {
            return Hp1BspVector{
                left.x - right.x,
                left.y - right.y,
                left.z - right.z,
            };
        };
        const auto cross = [](Hp1BspVector left, Hp1BspVector right) {
            return Hp1BspVector{
                left.y * right.z - left.z * right.y,
                left.z * right.x - left.x * right.z,
                left.x * right.y - left.y * right.x,
            };
        };
        const auto dot = [](Hp1BspVector left, Hp1BspVector right) {
            return static_cast<double>(left.x) * right.x +
                   static_cast<double>(left.y) * right.y +
                   static_cast<double>(left.z) * right.z;
        };

        std::size_t maximum_triangle_count = 0;
        for (const auto& node : topology.nodes) {
            if (node.vertex_count < 3) {
                ++result.short_polygon_count;
                continue;
            }
            ++result.source_polygon_count;
            const auto additions =
                static_cast<std::size_t>(node.vertex_count) - 2;
            if (additions > kMaximumTableEntries - maximum_triangle_count) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "BSP triangle count exceeds the safety cap");
            }
            maximum_triangle_count += additions;
        }
        result.triangles.reserve(maximum_triangle_count);

        for (std::size_t node_index = 0;
             node_index < topology.nodes.size(); ++node_index) {
            const auto& node = topology.nodes[node_index];
            if (node.vertex_count < 3) {
                continue;
            }
            require_topology_index(node.surface_index,
                                   topology.surfaces.size(), false,
                                   "triangle node surface index");
            const auto& surface = topology.surfaces[
                static_cast<std::size_t>(node.surface_index)];
            require_topology_index(surface.base_point_index,
                                   topology.points.size(), false,
                                   "triangle surface base point index");
            require_topology_index(surface.texture_u_vector_index,
                                   topology.vectors.size(), false,
                                   "triangle surface texture U vector index");
            require_topology_index(surface.texture_v_vector_index,
                                   topology.vectors.size(), false,
                                   "triangle surface texture V vector index");
            if (node.vertex_pool_index < 0 ||
                static_cast<std::size_t>(node.vertex_pool_index) >
                    topology.vertices.size() ||
                static_cast<std::size_t>(node.vertex_count) >
                    topology.vertices.size() -
                        static_cast<std::size_t>(node.vertex_pool_index)) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "triangle node vertex span is invalid");
            }

            Hp1BspVector desired_normal{
                node.plane[1], node.plane[2], -node.plane[0]};
            if (!finite_vector(desired_normal)) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "triangle node plane normal is non-finite");
            }
            const double desired_length_squared =
                dot(desired_normal, desired_normal);
            if (!(desired_length_squared > 0.0) ||
                !std::isfinite(desired_length_squared)) {
                fail(Hp1ProfileStatus::invalid_profile,
                     "triangle node plane normal has zero length");
            }
            const auto span_start =
                static_cast<std::size_t>(node.vertex_pool_index);
            const auto sample_at = [&](std::size_t offset) {
                const auto& vertex = topology.vertices[span_start + offset];
                require_topology_index(vertex.point_index,
                                       topology.points.size(), false,
                                       "triangle vertex point index");
                const auto source_position = topology.points[
                    static_cast<std::size_t>(vertex.point_index)];
                const auto position = converted(source_position);
                const auto relative = subtract(
                    source_position,
                    topology.points[static_cast<std::size_t>(
                        surface.base_point_index)]);
                const double u = dot(
                    relative,
                    topology.vectors[static_cast<std::size_t>(
                        surface.texture_u_vector_index)]) +
                    surface.pan_u;
                const double v = dot(
                    relative,
                    topology.vectors[static_cast<std::size_t>(
                        surface.texture_v_vector_index)]) +
                    surface.pan_v;
                if (!finite_vector(position) || !std::isfinite(u) ||
                    !std::isfinite(v) ||
                    std::abs(u) > std::numeric_limits<float>::max() ||
                    std::abs(v) > std::numeric_limits<float>::max()) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "triangle position or texture coordinate is invalid");
                }
                return std::pair{
                    position,
                    std::array{static_cast<float>(u), static_cast<float>(v)},
                };
            };
            const auto first = sample_at(0);
            for (std::size_t offset = 1;
                 offset + 1 < node.vertex_count; ++offset) {
                const auto second = sample_at(offset);
                const auto third = sample_at(offset + 1);
                std::array<Hp1BspVector, 3> positions{
                    first.first,
                    second.first,
                    third.first,
                };
                std::array<std::array<float, 2>, 3> texel_uv{
                    first.second,
                    second.second,
                    third.second,
                };
                const auto edge0 = subtract(positions[1], positions[0]);
                const auto edge1 = subtract(positions[2], positions[0]);
                auto geometric_normal = cross(edge0, edge1);
                const double edge_product =
                    dot(edge0, edge0) * dot(edge1, edge1);
                const double normal_length_squared =
                    dot(geometric_normal, geometric_normal);
                if (!(edge_product > 0.0) ||
                    !(normal_length_squared > edge_product * 1.0e-12) ||
                    !std::isfinite(normal_length_squared)) {
                    ++result.degenerate_triangle_count;
                    continue;
                }
                const double alignment =
                    dot(geometric_normal, desired_normal);
                if (alignment == 0.0 || !std::isfinite(alignment)) {
                    ++result.degenerate_triangle_count;
                    continue;
                }
                if (alignment < 0.0) {
                    std::swap(positions[1], positions[2]);
                    std::swap(texel_uv[1], texel_uv[2]);
                    geometric_normal.x = -geometric_normal.x;
                    geometric_normal.y = -geometric_normal.y;
                    geometric_normal.z = -geometric_normal.z;
                    ++result.reversed_winding_count;
                }
                const float inverse_length = static_cast<float>(
                    1.0 / std::sqrt(normal_length_squared));
                const Hp1BspVector normal{
                    geometric_normal.x * inverse_length,
                    geometric_normal.y * inverse_length,
                    geometric_normal.z * inverse_length,
                };
                if (!finite_vector(normal)) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "triangle normal is non-finite");
                }
                std::array<Hp1BspVector, 3> positions_m{
                    scaled(positions[0]),
                    scaled(positions[1]),
                    scaled(positions[2]),
                };
                if (std::ranges::any_of(
                        positions_m,
                        [&finite_vector](Hp1BspVector position) {
                            return !finite_vector(position);
                        })) {
                    fail(Hp1ProfileStatus::invalid_profile,
                         "scaled triangle position is non-finite");
                }
                result.triangles.push_back({
                    positions_m,
                    texel_uv,
                    normal,
                    static_cast<std::uint32_t>(node_index),
                    static_cast<std::uint32_t>(node.surface_index),
                });
            }
        }

        result.status = Hp1ProfileStatus::ok;
        result.meters_per_unreal_unit = meters_per_unreal_unit;
    } catch (const ProfileException& error) {
        result = {};
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = "allocation failed while building BSP triangles";
    } catch (const std::exception& error) {
        result = {};
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = error.what();
    }
    return result;
}

Hp1SpellProfile load_hp1_spell_profile(
    const std::filesystem::path& base_package,
    const std::filesystem::path& lesson_package,
    std::string_view gesture_name,
    std::string_view spell_class_name) {
    Hp1SpellProfile result;
    try {
        if (gesture_name.empty() || spell_class_name.empty()) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "gesture and spell names must be non-empty");
        }
        const Package base(base_package);
        const Package lesson(lesson_package);
        auto gesture = load_gesture(base, gesture_name);
        auto policy = load_class_policy(base);
        const auto lesson_match =
            load_lesson_overrides(lesson, spell_class_name);
        const float default_draw_time =
            policy.draw_time_seconds.value_or(std::numeric_limits<float>::quiet_NaN());
        merge_policy(policy, lesson_match.overrides);

        if (!policy.accuracy_radius.has_value() ||
            !policy.very_good_threshold.has_value() ||
            !policy.very_bad_threshold.has_value() ||
            !policy.draw_time_seconds.has_value() ||
            !std::all_of(policy.pass_mark_present.begin(),
                         policy.pass_mark_present.end(),
                         [](bool present) { return present; })) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "spell-learning policy is incomplete");
        }
        if (*policy.accuracy_radius <= 0.0F ||
            *policy.draw_time_seconds <= 0.0F ||
            !std::isfinite(default_draw_time) || default_draw_time <= 0.0F ||
            hp1_lesson_resampling_period_ns(*policy.draw_time_seconds) <= 0) {
            fail(Hp1ProfileStatus::invalid_profile,
                 "spell-learning policy contains invalid numeric values");
        }

        result.status = Hp1ProfileStatus::ok;
        result.gesture_name = std::string(gesture_name);
        result.lesson_actor_name = lesson_match.actor_name;
        result.template_points = std::move(gesture.points);
        result.segments = std::move(gesture.segments);
        result.pass_marks = policy.pass_marks;
        result.pass_mark_count = kHp1PassMarkCount;
        result.accuracy_radius = *policy.accuracy_radius;
        result.very_good_threshold = *policy.very_good_threshold;
        result.very_bad_threshold = *policy.very_bad_threshold;
        result.default_draw_time_seconds = default_draw_time;
        result.draw_time_seconds = *policy.draw_time_seconds;
        result.base_package_version = base.version();
        result.lesson_package_version = lesson.version();
    } catch (const ProfileException& error) {
        result.status = error.status();
        result.error = error.what();
    } catch (const std::bad_alloc&) {
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = "allocation failed while loading spell profile";
    } catch (const std::exception& error) {
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = error.what();
    } catch (...) {
        result.status = Hp1ProfileStatus::invalid_profile;
        result.error = "unknown spell-profile loader failure";
    }
    return result;
}

Hp1GestureScore compare_hp1_gesture(
    std::span<const Vec3> drawn_points,
    std::span<const Vec2> template_points,
    float accuracy_radius) {
    Hp1GestureScore result;
    result.input_point_count =
        std::min(drawn_points.size(), kHp1NativeGesturePointLimit);
    if (!std::isfinite(accuracy_radius) || accuracy_radius <= 0.0F) {
        result.status = Hp1GestureScoreStatus::invalid_accuracy;
        return result;
    }
    const auto authored_count =
        std::min(template_points.size(), kHp1NativeGesturePointLimit);
    if (authored_count == 0 ||
        !std::all_of(template_points.begin(),
                     template_points.begin() +
                         static_cast<std::ptrdiff_t>(authored_count),
                     [](Vec2 point) { return finite(point); })) {
        result.status = Hp1GestureScoreStatus::invalid_template;
        return result;
    }

    std::vector<Vec2> unique_drawn;
    unique_drawn.reserve(result.input_point_count);
    for (const Vec3 point : drawn_points.first(result.input_point_count)) {
        if (point.z == -1.0F) {
            continue;
        }
        if (!finite(point)) {
            result.status = Hp1GestureScoreStatus::invalid_input;
            return result;
        }
        const Vec2 xy{point.x, point.y};
        const bool duplicate = std::any_of(
            unique_drawn.begin(), unique_drawn.end(), [xy](Vec2 prior) {
                return std::abs(prior.x - xy.x) < kDuplicateEpsilon &&
                       std::abs(prior.y - xy.y) < kDuplicateEpsilon;
            });
        if (!duplicate) {
            unique_drawn.push_back(xy);
        }
    }
    result.unique_input_point_count = unique_drawn.size();

    std::vector<Vec2> dense_template;
    dense_template.reserve(authored_count * 8U - 7U);
    for (std::size_t index = 0; index < authored_count; ++index) {
        const Vec2 point = template_points[index];
        dense_template.push_back(point);
        if (index + 1 == authored_count) {
            break;
        }
        const Vec2 next = template_points[index + 1];
        for (int step = 1; step < 8; ++step) {
            const float fraction = static_cast<float>(step) / 8.0F;
            dense_template.push_back({
                point.x + (next.x - point.x) * fraction,
                point.y + (next.y - point.y) * fraction,
            });
        }
    }
    result.dense_template_point_count = dense_template.size();

    std::int32_t penalty = 0;
    std::int32_t total = 0;
    for (const Vec2 point : dense_template) {
        const float distance = nearest_distance(point, unique_drawn);
        if (distance > accuracy_radius) {
            const auto weight = capped_rounded_weight(
                distance / (accuracy_radius * 5.0F) + 1.0F, 2);
            penalty += weight;
            total += weight;
        } else {
            total += 2;
        }
    }
    for (const Vec2 point : unique_drawn) {
        const float distance = nearest_distance(point, dense_template);
        if (distance > accuracy_radius) {
            const auto weight =
                capped_rounded_weight(distance / accuracy_radius, 3);
            penalty += weight;
            total += weight;
        }
    }
    if (unique_drawn.size() < 10) {
        penalty += 500;
        total += 500;
    }
    result.status = Hp1GestureScoreStatus::ok;
    result.score = total == 0
                       ? 0.0F
                       : std::clamp(1.0F - static_cast<float>(penalty) /
                                               static_cast<float>(total),
                                    0.0F, 1.0F);
    return result;
}

Hp1GestureScore compare_hp1_projected_gesture(
    std::span<const Vec2> drawn_points,
    std::span<const Vec2> template_points,
    float accuracy_radius) {
    std::vector<Vec3> legacy_points;
    const auto submitted_count =
        std::min(drawn_points.size(), kHp1NativeGesturePointLimit);
    legacy_points.reserve(submitted_count);
    for (const Vec2 point : drawn_points.first(submitted_count)) {
        legacy_points.push_back({point.x, point.y, 0.0F});
    }
    return compare_hp1_gesture(
        legacy_points, template_points, accuracy_radius);
}

std::int64_t hp1_lesson_resampling_period_ns(float draw_time_seconds) noexcept {
    if (!std::isfinite(draw_time_seconds) || draw_time_seconds <= 0.0F) {
        return 0;
    }
    constexpr long double nanoseconds_per_second{1'000'000'000.0L};
    const long double period = static_cast<long double>(draw_time_seconds) *
                               nanoseconds_per_second /
                               static_cast<long double>(
                                   kAuthoredLessonPointCapacity);
    if (period < 1.0L ||
        period > static_cast<long double>(
                     std::numeric_limits<std::int64_t>::max())) {
        return 0;
    }
    return static_cast<std::int64_t>(std::llround(period));
}

}  // namespace hpvr::wand
