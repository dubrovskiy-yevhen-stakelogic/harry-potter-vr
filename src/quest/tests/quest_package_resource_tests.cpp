#include "hpvr/quest_package_resource.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace hpvr;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
wand::Hp1PackageExport Export(int ref, const char* name, const char* type) {
    wand::Hp1PackageExport value;
    value.reference = ref; value.object_path = {name}; value.qualified_class_name = type;
    return value;
}
}
int main() {
    try {
        wand::Hp1PackageLinkTable table;
        table.status = wand::Hp1ProfileStatus::ok;
        const auto resolve = [&] { return quest::FindUniqueRootExport(table, "WandMesh", "Engine.SkeletalMesh"); };
        Check(resolve() == 0, "missing mesh rejected");
        table.exports = {Export(1092, "WandMesh", "Engine.SkeletalMesh")};
        Check(resolve() == 1092, "original table");
        table.exports = {Export(1092, "HarryAnimType", "Core.ByteProperty"),
                         Export(1213, "WandMesh", "Engine.SkeletalMesh")};
        Check(resolve() == 1213, "reordered table ignores historical index");
        std::reverse(table.exports.begin(), table.exports.end());
        Check(resolve() == 1213, "enumeration order independent");
        table.exports.front().object_path = {"wAnDmEsH"};
        Check(resolve() == 1213, "Unreal names are case insensitive");
        table.exports.push_back(Export(2000, "WandMesh", "Engine.SkeletalMesh"));
        Check(resolve() == 0, "ambiguous mesh rejected");
        table.exports.pop_back();
        table.exports.front().qualified_class_name = "Engine.Texture";
        Check(resolve() == 0, "wrong type rejected");
        table.exports.front() = Export(1213, "WandMesh", "Engine.SkeletalMesh");
        table.exports.front().outer_reference = 3;
        table.exports.front().object_path = {"OtherGroup", "WandMesh"};
        Check(resolve() == 0, "nested same leaf is not root mesh");
        table.exports.front() = Export(-1, "WandMesh", "Engine.SkeletalMesh");
        Check(resolve() == 0, "import is not an export");
        table.exports.front().reference = 1213;
        table.status = wand::Hp1ProfileStatus::invalid_package;
        Check(resolve() == 0, "invalid package rejected");
        std::cout << "PACKAGE_RESOURCE_TESTS=PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
