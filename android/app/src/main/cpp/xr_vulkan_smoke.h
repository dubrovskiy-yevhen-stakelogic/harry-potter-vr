#pragma once

#include <openxr/openxr.h>

#include <filesystem>
#include <memory>
struct ANativeActivity;

namespace hpvr::quest {

class XrVulkanSmoke final {
public:
    XrVulkanSmoke();
    ~XrVulkanSmoke();

    XrVulkanSmoke(const XrVulkanSmoke&) = delete;
    XrVulkanSmoke& operator=(const XrVulkanSmoke&) = delete;

    [[nodiscard]] bool InitializeGraphics(XrInstance instance,
                                          XrSystemId system_id,bool metrics_extension=false,bool performance_extension=false);
    [[nodiscard]] bool LoadHogwarts(
        const std::filesystem::path& data_root,const std::filesystem::path& save_root);
    [[nodiscard]] bool PumpHogwartsLoad();
    [[nodiscard]] bool CreateSession();
    void DestroySession();
    void Destroy();

    [[nodiscard]] bool PollEvents(bool* exit_requested);
    [[nodiscard]] bool RenderFrame();
    bool ConsumeCommunityRequest();
    void UpdateVoicePlatform(ANativeActivity* activity);
    [[nodiscard]] bool IsRunning() const;
    [[nodiscard]] bool HasSession() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace hpvr::quest
