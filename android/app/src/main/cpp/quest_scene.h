#pragma once

#include "hpvr/quest_gesture.h"
#include "hpvr/quest_spell_targets.h"
#include "hpvr/quest_view.h"
#include "hpvr/quest_vr_settings.h"
#include "hpvr/quest_performance.h"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace hpvr::quest {

class QuestScene final {
public:
    QuestScene();
    ~QuestScene();

    QuestScene(const QuestScene&) = delete;
    QuestScene& operator=(const QuestScene&) = delete;

    [[nodiscard]] bool LoadFromOwnedData(
        const std::filesystem::path& data_root,const std::filesystem::path& save_root);
    [[nodiscard]] bool CreateGpu(VkPhysicalDevice physical_device,
                                 VkDevice device,
                                 VkQueue queue,
                                 std::uint32_t queue_family,
                                 VkRenderPass render_pass,unsigned width,unsigned height,VkFormat color,VkFormat depth);
    void PrepareReflections(const Matrix4& matrix,unsigned width,unsigned height,bool active);
    bool ReflectionCaptureEnabled() const;
    void CaptureReflections(VkCommandBuffer command,VkImage color,VkImage depth,const Matrix4& matrix,unsigned width,unsigned height);
    VrSettings GetVrSettings() const;
    unsigned LessonRound() const;
    bool ConsumeCommunityRequest();
    void UpdateExitTracking(const ViewPose& local_head,const ViewPose& reference,bool valid);
    void ToggleVrMenu();
    void SetPerformance(const PerformanceSnapshot& performance);
    void RecordFrontDraw(VkCommandBuffer command_buffer,const Matrix4& view_projection) const;
    void DestroyGpu();

    void RecordDraw(VkCommandBuffer command_buffer,
                    std::uint32_t width,
                    std::uint32_t height,
                    const std::array<float, 16>& view_projection) const;
    void RecordWandDraw(VkCommandBuffer command_buffer,
                        std::uint32_t width,
                        std::uint32_t height,
                        const std::array<float, 16>& wand_mvp,
                        const std::array<float, 4>& color_multiplier) const;
    void RecordGestureGuideDraw(
        VkCommandBuffer command_buffer,
        std::uint32_t width,
        std::uint32_t height,
        const std::array<float, 16>& view_projection,
        const GestureGuide& guide) const;
    void RecordSpellDraw(
        VkCommandBuffer command_buffer,
        std::uint32_t width,
        std::uint32_t height,
        const std::array<float, 16>& view_projection) const;
    void SetWandDrawing(bool drawing);
    void UpdateBasicCast(const ViewPose& wand, bool tracked, bool held, float seconds);
    void UpdateJumpInput(bool held,float seconds);
    bool NeedsPhysicsTick() const;
    void UpdateHudPose(const ViewPose& head);
    void RecordHudDraw(VkCommandBuffer command_buffer,const Matrix4& view_projection) const;
    void UpdateFrontEnd(const LocomotionInput& input,bool confirm,bool back,const ViewPose& head,float yaw);
    void UpdateFrontPresentation(const ViewPose& rendered_head,const ViewPose* cinematic_rig,bool first_person,bool recapture);
    bool ConsumePlayerPlacement(std::array<float,3>* position,float* yaw);
    bool IsFrontEndVisible() const;
    bool IsWorldPaused() const;
    bool CanCast() const;
    void Advance(float delta_seconds);
    void SetTrackingActive(bool active);
    void RejectLessonGesture();
    [[nodiscard]] bool ResolvePlayerMovement(
        const std::array<float, 3>& capsule_center,
        const std::array<float, 3>& requested_displacement,
        LocomotionMove* output) const;
    [[nodiscard]] bool DispatchFlipendo(const FlipendoEvent& event);
    [[nodiscard]] bool IsCutscenePlaying() const;
    [[nodiscard]] bool GetCinematicCameraPosition(
        std::array<float, 3>* output) const;
    bool GetCinematicCameraPose(ViewPose* output,bool* first_person=nullptr) const;

    [[nodiscard]] bool IsLoaded() const;
    [[nodiscard]] bool IsGpuReady() const;
    [[nodiscard]] std::uint32_t VertexCount() const;
    [[nodiscard]] std::uint32_t TextureLayerCount() const;
    [[nodiscard]] std::uint32_t WandVertexCount() const;
    [[nodiscard]] std::uint32_t CharacterCount() const;
    [[nodiscard]] std::uint32_t AnimationFrameCount() const;
    [[nodiscard]] std::uint32_t CurrentAnimationFrame() const;

private:
    void AdvanceIntroCutscene(float delta_seconds);
    void StartOpening();
    void StartRonEncounter();
    void StartTwinsEncounter();
    void StartJumpLesson(bool finish);
    void StartTwinsTransition(bool after_peeves);
    void StartStoryEncounter(unsigned index);
    void StartTutorialScene(bool reward);
    void MoveTwinsToNextRoom(bool finish=false);
    void SaveCheckpoint();
    void SkipOpening();
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace hpvr::quest
