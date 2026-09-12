// Include after quest_broom_scene.inl. The owned mesh supplies emission points,
// not visible triangles; a single additive batch draws the next three hoops.
constexpr std::size_t kBroomHoopParticlesPerRing = 96;
constexpr std::size_t kBroomHoopVertexCapacity = 3 * kBroomHoopParticlesPerRing * 6;

struct BroomRingStyle {
    std::array<float, 2> lifetime{}, width{}, length{}, speed{}, end_scale{};
    std::array<float, 3> color_start{}, color_end{};
    float rate = 0, gravity = 0;
};

struct BroomVisuals {
    bool valid = false;
    std::string error;
    std::uint32_t texture_layer = 0;
    std::array<BroomRingStyle, 5> styles;
    std::vector<std::array<float, 3>> standby_points, target_points;
};

namespace broom_visual_detail {
std::array<float, 2> Range(const broom::LessonProperties& properties, std::string_view name) {
    const auto* value = broom::lesson_detail::Property(properties, name);
    if (!value || value->value.size() != 8 ||
        broom::lesson_detail::Fold(value->structure_name) != "floatparams")
        throw std::runtime_error("Missing broom particle range: " + std::string(name));
    std::array<float, 2> result{};
    for (unsigned component = 0; component < 2; ++component) {
        std::uint32_t bits = 0;
        for (unsigned byte = 0; byte < 4; ++byte)
            bits |= std::uint32_t(value->value[component * 4 + byte]) << (byte * 8);
        result[component] = std::bit_cast<float>(bits);
        if (!std::isfinite(result[component])) throw std::runtime_error("Non-finite broom particle range");
    }
    if (result[1] < 0) throw std::runtime_error("Negative broom particle variation");
    return result;
}

std::array<float, 3> Color(const broom::LessonProperties& properties, std::string_view name) {
    const auto* value = broom::lesson_detail::Property(properties, name);
    if (!value || value->value.size() != 8 ||
        broom::lesson_detail::Fold(value->structure_name) != "colorparams")
        throw std::runtime_error("Missing broom particle color");
    return {float(value->value[0]) / 255, float(value->value[1]) / 255, float(value->value[2]) / 255};
}

std::uint32_t Hash(std::uint32_t value) {
    value ^= value >> 16; value *= 0x7feb352dU;
    value ^= value >> 15; value *= 0x846ca68bU;
    return value ^ (value >> 16);
}
float Unit(std::uint32_t seed) { return float(Hash(seed) >> 8) * (1.0F / 16777216.0F); }
} // namespace broom_visual_detail

bool LoadBroomVisuals(const std::filesystem::path& root, std::uint32_t layer_width,
    std::uint32_t layer_height, std::vector<std::uint8_t>& texture_rgba8,
    std::uint32_t& texture_layers, BroomVisuals& output) {
    BroomVisuals next;
    try {
        using namespace broom_visual_detail;
        if (!layer_width || !layer_height || layer_width > 4096 || layer_height > 4096 ||
            texture_layers >= kMaximumCombinedTextureLayers)
            throw std::runtime_error("Invalid broom texture array");
        const auto layer_bytes = std::size_t(layer_width) * layer_height * 4;
        if (texture_rgba8.size() != layer_bytes * texture_layers)
            throw std::runtime_error("Incomplete broom texture array");
        const auto particle_package = root / "system/HPParticle.u";
        const auto table = wand::inspect_hp1_package_link_table(particle_package);
        if (table.status != wand::Hp1ProfileStatus::ok) throw std::runtime_error(table.error);
        std::int32_t texture_reference = 0;
        for (unsigned stage = 0; stage < 5; ++stage) {
            const auto properties = broom::lesson_detail::Class(
                particle_package, table, "Ring" + std::to_string(stage + 1));
            auto& style = next.styles[stage];
            style.lifetime = Range(properties, "Lifetime");
            style.width = Range(properties, "SizeWidth");
            style.length = Range(properties, "SizeLength");
            style.speed = Range(properties, "Speed");
            style.end_scale = Range(properties, "SizeEndScale");
            style.rate = Range(properties, "ParticlesPerSec")[0];
            style.color_start = Color(properties, "ColorStart");
            style.color_end = Color(properties, "ColorEnd");
            style.gravity = broom::lesson_detail::Number(properties, "GravityModifier", 0);
            if (style.lifetime[0] <= 0 || style.lifetime[0] + style.lifetime[1] > 10 ||
                style.width[0] <= 0 || style.length[0] <= 0 || style.rate <= 0 || style.rate > 1000)
                throw std::runtime_error("Invalid broom particle settings");
            const auto* texture = broom::lesson_detail::Property(properties, "Textures");
            const auto* distribution = broom::lesson_detail::Property(properties, "Distribution");
            if (!texture || !texture->object_reference_serialized || texture->object_reference <= 0 ||
                !distribution || distribution->value != std::vector<std::uint8_t>{2} ||
                (texture_reference && texture_reference != texture->object_reference))
                throw std::runtime_error("Unsupported broom particle source");
            texture_reference = texture->object_reference;
        }
        const auto texture = wand::load_hp1_p8_texture(particle_package, texture_reference);
        if (texture.status != wand::Hp1ProfileStatus::ok || texture.object_name != "flare4" ||
            texture.mips.empty() || !texture.mips.front().width || !texture.mips.front().height)
            throw std::runtime_error("Broom flare texture unavailable");
        const auto& mip = texture.mips.front();
        if (texture.rgba8.size() < std::size_t(mip.width) * mip.height * 4)
            throw std::runtime_error("Incomplete broom flare texture");
        std::vector<std::uint8_t> pixels(layer_bytes);
        for (std::uint32_t y = 0; y < layer_height; ++y)
            for (std::uint32_t x = 0; x < layer_width; ++x) {
                const auto source = (std::size_t(y * mip.height / layer_height) * mip.width +
                    x * mip.width / layer_width) * 4;
                const auto destination = (std::size_t(y) * layer_width + x) * 4;
                std::copy_n(texture.rgba8.data() + source, 4, pixels.data() + destination);
            }
        const auto props_package = root / "system/HProps.u";
        const auto props_table = wand::inspect_hp1_package_link_table(props_package);
        if (props_table.status != wand::Hp1ProfileStatus::ok) throw std::runtime_error(props_table.error);
        const auto defaults = broom::lesson_detail::Class(props_package, props_table, "BroomHoop");
        const auto* mesh = broom::lesson_detail::Property(defaults, "Mesh");
        if (!mesh || !mesh->object_reference_serialized || mesh->object_reference <= 0)
            throw std::runtime_error("Broom emission mesh unavailable");
        const auto skin = wand::load_hp1_skeletal_skin(props_package, mesh->object_reference);
        if (skin.status != wand::Hp1ProfileStatus::ok || skin.points.empty() || skin.points.size() > 4096)
            throw std::runtime_error("Invalid broom emission mesh");
        const auto animation = wand::load_hp1_animation(props_package, skin.census.animation_reference);
        if (animation.status != wand::Hp1ProfileStatus::ok) throw std::runtime_error(animation.error);
        const auto sample = [&](std::string_view name, auto& points) {
            const auto sequence = std::ranges::find_if(animation.sequences, [&](const auto& candidate) {
                return broom::lesson_detail::Fold(candidate.name) == name;
            });
            if (sequence == animation.sequences.end()) throw std::runtime_error("Missing broom emission pose");
            const auto pose = wand::sample_hp1_skeletal_animation(skin, animation,
                static_cast<std::size_t>(sequence - animation.sequences.begin()), 0);
            if (pose.status != wand::Hp1ProfileStatus::ok || pose.points.size() != skin.points.size())
                throw std::runtime_error("Invalid broom emission pose");
            for (const auto& point : pose.points) {
                const std::array<float, 3> p{point.x, point.y, point.z};
                if (!std::all_of(p.begin(), p.end(), [](float v) { return std::isfinite(v); }))
                    throw std::runtime_error("Non-finite broom emission point");
                points.push_back(p);
            }
        };
        sample("hold1", next.standby_points);
        sample("hold3", next.target_points);
        next.texture_layer = texture_layers;
        texture_rgba8.insert(texture_rgba8.end(), pixels.begin(), pixels.end());
        ++texture_layers;
        next.valid = true;
        output = std::move(next);
        return true;
    } catch (const std::exception& error) {
        next.error = error.what();
        output = std::move(next);
        return false;
    }
}

// Shared head axes keep mapped vertices identical for both eye submissions.
// The caller owns a host-visible buffer with kBroomHoopVertexCapacity entries.
std::size_t BuildBroomHoopVertices(const BroomRuntime& state, const BroomVisuals& visuals,
    float elapsed, const std::array<float, 16>& shared_view_transform,
    ParticleGpuVertex* output, std::size_t capacity) {
    if (!state.active || !visuals.valid || !output || capacity < 6 || !std::isfinite(elapsed) ||
        elapsed < 0 || state.path >= 2 || state.stage >= 5 ||
        visuals.standby_points.empty() || visuals.target_points.empty()) return 0;
    if (!std::all_of(shared_view_transform.begin(), shared_view_transform.end(),
        [](float value) { return std::isfinite(value); })) return 0;
    using namespace broom_visual_detail;
    const std::array<float, 3> camera_right{
        shared_view_transform[0], shared_view_transform[1], shared_view_transform[2]};
    const std::array<float, 3> camera_up{
        shared_view_transform[4], shared_view_transform[5], shared_view_transform[6]};
    const auto& route = state.routes[state.path][state.stage];
    std::size_t count = 0;
    capacity = std::min(capacity, kBroomHoopVertexCapacity);
    for (std::size_t i = state.route.next_index; i < route.size() && i - state.route.next_index < 3; ++i) {
        const auto& hoop = route[i];
        const auto metadata = state.hoop_indices.find(hoop.id);
        if (metadata == state.hoop_indices.end() || metadata->second >= state.lesson.hoops.size()) continue;
        const auto& authored = state.lesson.hoops[metadata->second];
        if (authored.stage < 1 || authored.stage > 5) continue;
        const auto& style = visuals.styles[authored.stage - 1];
        const bool target = i == state.route.next_index;
        const auto& points = target ? visuals.target_points : visuals.standby_points;
        std::array<float, 3> right{}, up{}, normal{};
        if (!NormalizeVector(hoop.normal, &normal)) continue;
        if (!NormalizeVector(CrossVector({0, 1, 0}, normal), &right)) right = {1, 0, 0};
        up = CrossVector(normal, right);
        const auto center = hoop.center;
        const float scale = authored.play_scale * kMetersPerUnrealUnit;
        if (!std::isfinite(scale) || scale <= 0) continue;
        const float nominal_life = style.lifetime[0] + style.lifetime[1] * .5F;
        const auto slots = static_cast<unsigned>(std::clamp(std::ceil(
            (target ? style.rate : std::min(style.rate, 50.0F)) * nominal_life), 1.0F,
            target ? float(kBroomHoopParticlesPerRing) : 28.0F));
        const float pulse_time = std::fmod(elapsed, .39F);
        const float glow = target ? (pulse_time < .14F ? .2F + 20 * pulse_time : 5.2F - 20 * (pulse_time - .14F)) : 1.0F;
        for (unsigned slot = 0; slot < slots && count + 6 <= capacity; ++slot) {
            const auto seed = Hash(static_cast<std::uint32_t>(hoop.id) ^ (slot * 65537U));
            const float life = std::clamp(style.lifetime[0] + style.lifetime[1] * Unit(seed + 1), .05F, 10.0F);
            const float time = std::min(elapsed, 1000000.0F) + Unit(seed + 2) * life;
            const float cycle = std::floor(time / life), age = time - cycle * life, t = age / life;
            const auto birth = Hash(seed ^ static_cast<std::uint32_t>(cycle));
            const auto& point = points[Hash(birth + 3) % points.size()];
            // Skin points are in original mesh coordinates, with X along the hoop normal.
            auto position = AddVector(center, AddVector(ScaleVector(normal, -point[0] * scale),
                AddVector(ScaleVector(right, point[1] * scale), ScaleVector(up, point[2] * scale))));
            const float velocity = (style.speed[0] + style.speed[1] * Unit(birth + 4)) * kMetersPerUnrealUnit;
            position[1] += velocity * age - .5F * 15.0F * style.gravity * age * age;
            const float end_scale = std::max(0.0F, style.end_scale[0] + style.end_scale[1] * Unit(birth + 5));
            const float shrink = (1 - t) + end_scale * t;
            const float width = (style.width[0] + style.width[1] * Unit(birth + 6)) * scale * shrink;
            const float height = (style.length[0] + style.length[1] * Unit(birth + 7)) * scale * shrink;
            const float alpha = std::min(1.0F, t * 12.5F) * (1 - t) * glow;
            if (width <= 0 || height <= 0 || !std::isfinite(width) || !std::isfinite(height)) continue;
            for (const auto uv : std::array<std::array<float, 2>, 6>{{{0,0},{0,1},{1,1},{0,0},{1,1},{1,0}}}) {
                auto& vertex = output[count++];
                const auto p = AddVector(position, AddVector(ScaleVector(camera_right, (uv[0] - .5F) * width),
                    ScaleVector(camera_up, (.5F - uv[1]) * height)));
                for (unsigned axis = 0; axis < 3; ++axis) {
                    vertex.position[axis] = p[axis];
                    vertex.color[axis] = style.color_start[axis] * (1 - t) + style.color_end[axis] * t;
                }
                vertex.texture_uv[0] = uv[0]; vertex.texture_uv[1] = uv[1];
                vertex.color[3] = alpha;
            }
        }
    }
    return count;
}
