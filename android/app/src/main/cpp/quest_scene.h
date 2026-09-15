#pragma once

#include "hpvr/quest_gesture.h"
#include "hpvr/quest_spell_targets.h"
#include "hpvr/quest_view.h"
#include "hpvr/quest_vr_settings.h"
#include "hpvr/quest_performance.h"
#include "hpvr/quest_frontend.h"

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
        const std::filesystem::path& data_root,const std::filesystem::path& save_root,unsigned map_id=0);
    bool ConsumeMapTransition(unsigned* map_id,ProgressSave* progress,unsigned* slot);
    void RestoreTransferredProgress(const ProgressSave& progress,unsigned slot);
    void AbortMapTransition(const char* message);
    void Swap(QuestScene& other) noexcept;
    [[nodiscard]] bool CreateGpu(VkPhysicalDevice physical_device,
                                 VkDevice device,
                                 VkQueue queue,
                                 std::uint32_t queue_family,
                                 VkRenderPass render_pass,unsigned width,unsigned height,VkFormat color,VkFormat depth,unsigned frame_count);
    void PrepareReflections(const Matrix4& matrix,unsigned width,unsigned height,bool active);
    bool ReflectionCaptureEnabled() const;
    void CaptureReflections(VkCommandBuffer command,VkImage color,VkImage depth,const Matrix4& matrix,unsigned width,unsigned height);
    VrSettings GetVrSettings() const;
    void SetSupportedRefreshRates(const std::vector<int>& rates);
    bool WantsGesture() const;
    bool GestureTargetLocked() const;
    bool VoiceCaptureAllowed() const;
    std::int32_t VoiceTarget(std::array<float,3>* point=nullptr) const;
    void SetVoiceStatus(unsigned status);
    bool DispatchVoiceCast(std::int32_t target,const std::array<float,3>& point,const ViewPose& wand);
    bool IsGestureLesson() const;
    unsigned LessonRound() const;
    bool ConsumeCommunityRequest();
    void UpdateExitTracking(const ViewPose& local_head,const ViewPose& reference,bool valid);
    void ToggleVrMenu();
    void SetPerformance(const PerformanceSnapshot& performance);
    void RecordFrontDraw(VkCommandBuffer command_buffer,const Matrix4& view_projection) const;
    void DestroyGpu();
    void RecordMirrorCapture(VkCommandBuffer command,const Matrix4& projection);
    void RecordMirrorComposite(VkCommandBuffer command,const Matrix4& projection,unsigned width,unsigned height) const;

    void RecordDraw(VkCommandBuffer command_buffer,
                    std::uint32_t width,
                    std::uint32_t height,
                    const std::array<float, 16>& view_projection,unsigned frame_slot) const;
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
    void ResetMovementContinuity();
    bool NeedsPhysicsTick() const;
    void UpdateHudPose(const ViewPose& head);
    void UpdatePlayerPose(const ViewPose& head,float yaw,const std::array<float,3>& capsule_center);
    void RecordHudDraw(VkCommandBuffer command_buffer,const Matrix4& view_projection) const;
    void RecordDeathFade(VkCommandBuffer command_buffer) const;
    void UpdateFrontEnd(const LocomotionInput& input,bool confirm,bool back,const ViewPose& head,float yaw);
    void UpdateFrontPresentation(const ViewPose& rendered_head,const ViewPose* cinematic_rig,bool first_person,bool recapture);
    bool ConsumePlayerPlacement(std::array<float,3>* position,float* yaw);
    bool ConsumePlayerTransport(std::array<float,3>* displacement);
    bool IsFrontEndVisible() const;
    bool IsWorldPaused() const;
    bool CanCast() const;
    GestureSpell ActiveGestureSpell() const;
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
    void StartCharmsLesson(std::int32_t reference);
    void SubmitCharmsLesson(float score);
    void AdvanceCharms(float seconds);
    void UpdateCharmsWand(const std::array<float,3>& tip,const std::array<float,3>& direction,bool tracked,bool held);
    void StartBroomScene(const std::string& tag,unsigned phase);
    void BeginBroomTrial();
    void AdvanceBroom(float seconds);
    void DispatchBroomEvent(const std::string& event);
    void RestoreBroomProgress();
    void SaveBroomProgress();
    void RequestBroomTravel();
    bool ResolveBroomMovement(const std::array<float,3>& center,LocomotionMove* output) const;
    void BeginChallengeDeath(const char* reason) const;
    void AdvanceChallengeDeath(float step);
    void StartPickupFlight(std::int32_t actor_reference);
    std::int32_t FindChallengeSpellTarget(const std::array<float,3>& origin,const std::array<float,3>& direction,float* distance,
        std::array<float,3>* bounds_min=nullptr,std::array<float,3>* bounds_max=nullptr) const;
    void LaunchChallengeSpell(const std::array<float,3>& origin,const std::array<float,3>& direction,const std::array<float,3>& tip,std::uint64_t serial);
    void AdvanceChallenge(float seconds);
    void StartChallengeScene(std::int32_t reference);
    void RequestChallengeTravel();
    void RebuildChallengeCollision();
    void RestoreCurrentProgress();
    void AdvanceIntroCutscene(float delta_seconds);
    void StartOpening();
    void StartRonEncounter();
    void StartTwinsEncounter();
    void StartJumpLesson(bool finish);
    void StartTwinsTransition(bool after_peeves);
    void StartStoryEncounter(unsigned index);
    void StartTutorialScene(bool reward);
    void MoveTwinsToNextRoom(bool finish=false);
    void SaveCheckpoint(bool authored=false);
    void SkipOpening();
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace hpvr::quest
