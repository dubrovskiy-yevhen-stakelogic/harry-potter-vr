#include "xr_vulkan_smoke.h"
#include "hpvr/quest_reflection_math.h"

#include "quest_scene.h"
#include "quest_load_trace.h"
#include "quest_voice_cast_android.h"
#include <android/native_activity.h>

#include "hpvr/quest_view.h"
#include "hpvr/quest_recenter.h"
#include "hpvr/quest_gesture.h"
#include "hpvr/quest_frontend.h"
#include "hpvr/quest_startup.h"
#include "hpvr/quest_loading_indicator.h"

#include <android/log.h>
#include <jni.h>
#include <vulkan/vulkan.h>

#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <future>
#include <iomanip>
#include <sstream>
#include <string>
#include <ctime>
#include <vector>

namespace hpvr::quest {
namespace {

constexpr char kLogTag[] = "HPVR.Quest";
constexpr std::uint32_t kViewCount = 2;
double WallMs(){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
double ThreadMs(){timespec t{};clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return double(t.tv_sec)*1000+double(t.tv_nsec)*1e-6;}
void Smooth(float& value,double sample){if(!std::isfinite(sample)||sample<0)return;value=value<0?float(sample):value*.9F+float(sample)*.1F;}

#define HPVR_LOGI(...) __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)
#define HPVR_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kLogTag, __VA_ARGS__)

bool CheckXr(const XrResult result, const char* operation) {
    if (XR_SUCCEEDED(result)) {
        return true;
    }
    HPVR_LOGE("[hpvr.quest.xr.error] operation=%s result=%d", operation,
              static_cast<int>(result));
    return false;
}

bool CheckVk(const VkResult result, const char* operation) {
    if (result == VK_SUCCESS) {
        return true;
    }
    HPVR_LOGE("[hpvr.quest.vk.error] operation=%s result=%d", operation,
              static_cast<int>(result));
    return false;
}

VkFormat SelectColorFormat(const std::vector<std::int64_t>& formats) {
    constexpr std::array preferred{
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
    };
    for (const VkFormat candidate : preferred) {
        if (std::find(formats.begin(), formats.end(),
                      static_cast<std::int64_t>(candidate)) != formats.end()) {
            return candidate;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

bool FindDepthFormat(const VkPhysicalDevice physical_device,
                     VkFormat* output_format) {
    constexpr std::array candidates{VK_FORMAT_D32_SFLOAT,VK_FORMAT_D16_UNORM};
    for (const VkFormat candidate : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physical_device, candidate,
                                            &properties);
        constexpr auto required=VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|
            VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if ((properties.optimalTilingFeatures & required) == required) {
            *output_format = candidate;
            return true;
        }
    }
    return false;
}

bool FindDeviceMemoryType(const VkPhysicalDevice physical_device,
                          const std::uint32_t type_bits,
                          const VkMemoryPropertyFlags required,
                          std::uint32_t* output_index) {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        if ((type_bits & (1U << index)) != 0 &&
            (properties.memoryTypes[index].propertyFlags & required) ==
                required) {
            *output_index = index;
            return true;
        }
    }
    return false;
}

}  // namespace

struct XrVulkanSmoke::State {
    QuestVoiceCast voice;
    bool voice_permission=false,voice_configured=false,voice_was_enabled=false;
    double voice_platform_poll=0;
    double voice_diagnostic_poll=0;
    std::uint64_t voice_generation=0;
    std::int32_t voice_target=0;
    VoiceSpell voice_spell=VoiceSpell::Flipendo;
    int voice_last_error=0;
    std::array<float,3> voice_target_point{};
    std::vector<std::uint8_t> startup_pixels;
    XrSwapchain startup_swapchain=XR_NULL_HANDLE;
    XrSwapchain loading_icon_swapchain=XR_NULL_HANDLE;
    XrSpace startup_space=XR_NULL_HANDLE;
    ViewPose startup_pose{};
    bool startup_anchor_valid=false;
    XrInstance xr_instance = XR_NULL_HANDLE;
    XrSystemId system_id = XR_NULL_SYSTEM_ID;

    VkInstance vk_instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;

    XrSession session = XR_NULL_HANDLE;
    XrSessionState session_state = XR_SESSION_STATE_UNKNOWN;
    XrSpace local_space = XR_NULL_HANDLE;
    XrActionSet action_set = XR_NULL_HANDLE;
    XrAction grip_action = XR_NULL_HANDLE;
    XrAction aim_action = XR_NULL_HANDLE;
    XrAction trigger_action = XR_NULL_HANDLE;
    XrAction cast_action = XR_NULL_HANDLE;
    XrAction back_action = XR_NULL_HANDLE;
    XrAction jump_action = XR_NULL_HANDLE, menu_action=XR_NULL_HANDLE, sprint_action=XR_NULL_HANDLE;
    XrAction right_stick_click_action = XR_NULL_HANDLE;
    RecenterChord recenter_chord;
    bool recenter_requested=false;
    bool sprint_enabled=false,sprint_button_held=false;
    XrAction left_squeeze=XR_NULL_HANDLE,right_squeeze=XR_NULL_HANDLE;
    VrMenuChord vr_chord;
    bool vr_menu_requested=false;
    bool ui_trigger_consumed=false;
    bool input_release_pending=false;
    XrAction move_action = XR_NULL_HANDLE;
    XrAction turn_action = XR_NULL_HANDLE;
    XrPath left_hand = XR_NULL_PATH;
    XrPath right_hand = XR_NULL_PATH;
    XrSpace grip_space = XR_NULL_HANDLE;
    XrSpace aim_space = XR_NULL_HANDLE;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    bool running = false;

    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    unsigned recommended_width=0,recommended_height=0;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkRenderPass overlay_pass = VK_NULL_HANDLE;
    VkCommandPool command_pool = VK_NULL_HANDLE;
    std::vector<XrSwapchainImageVulkanKHR> swapchain_images;
    std::vector<std::array<VkImageView, kViewCount>> image_views;
    std::vector<std::array<VkFramebuffer, kViewCount>> framebuffers;
    std::vector<VkImage> depth_images;
    std::vector<VkDeviceMemory> depth_memories;
    std::vector<std::array<VkImageView, kViewCount>> depth_views;
    std::vector<VkCommandBuffer> command_buffers;
    std::vector<VkFence> fences;
    VkQueryPool timing_pool=VK_NULL_HANDLE;
    unsigned timestamp_bits=0;float timestamp_period=0;
    PerformanceSnapshot performance;
    double perf_start=0;unsigned perf_frames=0;
    PFN_xrQueryPerformanceMetricsCounterMETA query_metric=nullptr;
    PFN_xrSetPerformanceMetricsStateMETA set_metrics=nullptr;
    std::array<XrPath,4> metric_paths{};
    bool metrics_enabled=false;
    bool metrics_extension=false;
    bool performance_extension=false;
    PFN_xrPerfSettingsSetPerformanceLevelEXT set_performance=nullptr;
    int requested_gpu_boost=-1;
    PFN_xrRequestDisplayRefreshRateFB request_refresh=nullptr;
    std::vector<int> refresh_rates;
    int requested_refresh=-1;

    std::uint64_t submitted_frames = 0;
    std::uint64_t input_syncs = 0;
    std::uint64_t tracked_wand_frames = 0;
    XrTime last_predicted_time = 0;
    XrTime local_space_change_time = 0;
    float trigger_value = 0.0F;
    bool cast_held = false;
    bool back_held = false;
    bool jump_held=false;
    bool cinematic_reference_valid=false;
    bool cinematic_first_person=false;
    ViewPose cinematic_reference{},last_world_head{};
    std::array<float,3> last_world_capsule{};
    bool have_world_head=false,rebase_head=false;
    LocomotionState locomotion{};
    std::unique_ptr<QuestGesture> gesture=std::make_unique<QuestGesture>();
    GestureGuide gesture_guide{};
    std::uint64_t gesture_diagnostic_serial=0;
    QuestScene scene{};
    std::filesystem::path data_root,save_root;
    std::unique_ptr<QuestScene> loading_scene;
    std::unique_ptr<QuestGesture> loading_gesture;
    std::future<bool> scene_load_future{};
    ProgressSave transferred_progress{};
    unsigned transferred_slot=0,loading_map_id=0;
    bool map_transfer=false;
    bool scene_load_failed = false;
    bool scene_load_adopted = false;
};

namespace {

void ResetSceneInput(auto& state,bool reset_placement){
    state.voice.SetListening({});state.voice_target=0;++state.voice_generation;
    state.last_predicted_time=0;
    state.gesture->Reset();state.gesture_guide={};
    state.gesture_diagnostic_serial=0;
    state.trigger_value=0;state.cast_held=state.back_held=state.jump_held=false;
    state.sprint_enabled=state.sprint_button_held=false;
    state.vr_menu_requested=false;state.vr_chord={};
    state.recenter_requested=false;state.recenter_chord.Reset();
    state.ui_trigger_consumed=true;
    state.input_release_pending=true;
    state.cinematic_reference_valid=false;state.cinematic_first_person=false;
    state.startup_anchor_valid=false;
    if(reset_placement){
        state.locomotion.Reset();state.have_world_head=false;state.rebase_head=false;
        state.cinematic_reference={};state.last_world_head={};state.last_world_capsule={};
        state.performance={};state.perf_start=0;state.perf_frames=0;
    }
}

bool QueueSceneLoad(auto& state,unsigned map_id,bool transfer){
    if(state.scene_load_future.valid())return false;
    try{
        state.loading_scene=std::make_unique<QuestScene>();
        state.loading_gesture=std::make_unique<QuestGesture>();
        QuestScene* const pending=state.loading_scene.get();
        QuestGesture* const profile=state.loading_gesture.get();
        // Capture no live scene or input state: only the pending objects belong
        // to the worker until future::get establishes the ownership hand-off.
        state.scene_load_future=std::async(std::launch::async,
            [pending,profile,root=state.data_root,saves=state.save_root,map_id](){
                HPVR_LOGI("[hpvr.quest.scene.async] status=STARTED map=%u mode=CPU_ONLY",map_id);
                return pending->LoadFromOwnedData(root,saves,map_id)&&
                    (!profile||profile->LoadProfiles(root,map_id==kCharmsTrainingMapId));
            });
        state.loading_map_id=map_id;state.map_transfer=transfer;
        state.scene_load_failed=false;
        state.scene.SetTrackingActive(false);state.scene.SetWandDrawing(false);
        state.scene.PrepareReflections(Matrix4{},state.width,state.height,false);
        ResetSceneInput(state,false);
        HPVR_LOGI("[hpvr.quest.scene.async] status=QUEUED map=%u transfer=%u startup=NON_BLOCKING",map_id,transfer?1U:0U);
        return true;
    }catch(const std::exception& error){
        HPVR_LOGE("[hpvr.quest.scene.async] status=QUEUE_FAILED map=%u reason=%s",map_id,error.what());
        state.loading_scene.reset();state.loading_gesture.reset();
        return false;
    }
}

bool UploadStaticLayer(auto& s,const std::vector<std::uint8_t>& pixels,
                       unsigned width,unsigned height,XrSwapchain* swapchain){
    if(!swapchain||pixels.size()!=static_cast<std::size_t>(width)*height*4)return false;
    XrSwapchainCreateInfo info{};info.type=XR_TYPE_SWAPCHAIN_CREATE_INFO;
    info.createFlags=XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
    info.usageFlags=XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT|XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    info.format=s.color_format;info.sampleCount=1;info.width=width;info.height=height;
    info.faceCount=1;info.arraySize=1;info.mipCount=1;
    if(!CheckXr(xrCreateSwapchain(s.session,&info,swapchain),"startup swapchain"))return false;
    std::uint32_t count=0;
    if(!CheckXr(xrEnumerateSwapchainImages(*swapchain,0,&count,nullptr),"startup images")||!count)return false;
    std::vector<XrSwapchainImageVulkanKHR> images(count);
    for(auto& image:images)image.type=XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
    if(!CheckXr(xrEnumerateSwapchainImages(*swapchain,count,&count,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"startup images"))return false;
    VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;VkCommandBuffer command=VK_NULL_HANDLE;
    bool acquired=false;
    const bool ok=[&](){
        VkBufferCreateInfo b{};b.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;b.size=pixels.size();
        b.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;b.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        if(!CheckVk(vkCreateBuffer(s.device,&b,nullptr,&buffer),"startup staging"))return false;
        VkMemoryRequirements req;vkGetBufferMemoryRequirements(s.device,buffer,&req);
        std::uint32_t type=0;
        if(!FindDeviceMemoryType(s.physical_device,req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&type))return false;
        VkMemoryAllocateInfo a{};a.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;a.allocationSize=req.size;a.memoryTypeIndex=type;
        if(!CheckVk(vkAllocateMemory(s.device,&a,nullptr,&memory),"startup memory")||
           !CheckVk(vkBindBufferMemory(s.device,buffer,memory,0),"startup binding"))return false;
        void* mapped=nullptr;
        if(!CheckVk(vkMapMemory(s.device,memory,0,b.size,0,&mapped),"startup map"))return false;
        std::memcpy(mapped,pixels.data(),pixels.size());
        if(s.color_format==VK_FORMAT_B8G8R8A8_SRGB||s.color_format==VK_FORMAT_B8G8R8A8_UNORM){
            auto* bytes=static_cast<std::uint8_t*>(mapped);
            for(std::size_t i=0;i<pixels.size();i+=4)std::swap(bytes[i],bytes[i+2]);
        }
        vkUnmapMemory(s.device,memory);
        std::uint32_t index=0;XrSwapchainImageAcquireInfo acquire{};acquire.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
        if(!CheckXr(xrAcquireSwapchainImage(*swapchain,&acquire,&index),"startup acquire"))return false;
        acquired=true;
        XrSwapchainImageWaitInfo wait{};wait.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;wait.timeout=XR_INFINITE_DURATION;
        if(!CheckXr(xrWaitSwapchainImage(*swapchain,&wait),"startup wait"))return false;
        VkCommandBufferAllocateInfo alloc{};alloc.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool=s.command_pool;alloc.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;alloc.commandBufferCount=1;
        if(!CheckVk(vkAllocateCommandBuffers(s.device,&alloc,&command),"startup command"))return false;
        VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if(!CheckVk(vkBeginCommandBuffer(command,&begin),"startup begin"))return false;
        VkImageMemoryBarrier barrier{};barrier.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        barrier.image=images[index].image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={width,height,1};
        vkCmdCopyBufferToImage(command,buffer,images[index].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        if(!CheckVk(vkEndCommandBuffer(command),"startup end"))return false;
        VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        return CheckVk(vkQueueSubmit(s.queue,1,&submit,VK_NULL_HANDLE),"startup upload")&&CheckVk(vkQueueWaitIdle(s.queue),"startup ready");
    }();
    if(acquired){XrSwapchainImageReleaseInfo release{};release.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;xrReleaseSwapchainImage(*swapchain,&release);}
    if(command)vkFreeCommandBuffers(s.device,s.command_pool,1,&command);
    if(buffer)vkDestroyBuffer(s.device,buffer,nullptr);
    if(memory)vkFreeMemory(s.device,memory,nullptr);
    return ok;
}

bool CreateStartupLayer(auto& s){
    XrReferenceSpaceCreateInfo space{};space.type=XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    space.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;space.poseInReferenceSpace.orientation.w=1;
    if(!CheckXr(xrCreateReferenceSpace(s.session,&space,&s.startup_space),"startup space"))return false;
    if(!UploadStaticLayer(s,s.startup_pixels,640,480,&s.startup_swapchain))return false;
    const auto icon=BuildLoadingIconAtlas();
    if(!UploadStaticLayer(s,icon,kLoadingAtlasWidth,kLoadingIconSize,&s.loading_icon_swapchain))return false;
    HPVR_LOGI("[hpvr.quest.startup] status=READY content=WARNER_OWNED size=640x480 indicator=HOURGLASS_ATLAS frames=%u",kLoadingIconFrames);
    return true;
}

bool ResolveSceneLocomotion(
    void* const context,
    const std::array<float, 3>& capsule_center,
    const std::array<float, 3>& requested_displacement,
    LocomotionMove* const output) {
    if (context == nullptr) return false;
    return static_cast<QuestScene*>(context)->ResolvePlayerMovement(
        capsule_center, requested_displacement, output);
}

bool StringToPath(const XrInstance instance, const char* value,
                  XrPath* const output) {
    return CheckXr(xrStringToPath(instance, value, output), value);
}

bool CreateAction(const XrActionSet action_set, const XrActionType type,
                  const char* name, const char* localized_name,
                  const XrPath subaction, XrAction* const output) {
    XrActionCreateInfo info{};
    info.type = XR_TYPE_ACTION_CREATE_INFO;
    info.actionType = type;
    std::strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    std::strncpy(info.localizedActionName, localized_name,
                 XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    info.countSubactionPaths = 1;
    info.subactionPaths = &subaction;
    return CheckXr(xrCreateAction(action_set, &info, output), name);
}

bool CreateInput(auto& state) {
    if (!StringToPath(state.xr_instance, "/user/hand/left",
                      &state.left_hand) ||
        !StringToPath(state.xr_instance, "/user/hand/right",
                      &state.right_hand)) {
        return false;
    }
    XrActionSetCreateInfo set_info{};
    set_info.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    std::strncpy(set_info.actionSetName, "hpvr_gameplay",
                 XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::strncpy(set_info.localizedActionSetName, "HPVR Gameplay",
                 XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    if (!CheckXr(xrCreateActionSet(state.xr_instance, &set_info,
                                   &state.action_set),
                 "xrCreateActionSet(gameplay)") ||
        !CreateAction(state.action_set, XR_ACTION_TYPE_POSE_INPUT,
                      "right_grip", "Right Wand Grip", state.right_hand,
                      &state.grip_action) ||
        !CreateAction(state.action_set, XR_ACTION_TYPE_POSE_INPUT,
                      "right_aim", "Right Wand Aim", state.right_hand,
                      &state.aim_action) ||
        !CreateAction(state.action_set, XR_ACTION_TYPE_FLOAT_INPUT,
                      "right_trigger", "Right Wand Trigger", state.right_hand,
                      &state.trigger_action) ||
        !CreateAction(state.action_set, XR_ACTION_TYPE_BOOLEAN_INPUT,
                      "right_cast", "Cast Spell", state.right_hand,
                      &state.cast_action) ||
        !CreateAction(state.action_set, XR_ACTION_TYPE_BOOLEAN_INPUT,
                      "menu_back", "Menu / Back", state.right_hand,
                      &state.back_action) ||
        !CreateAction(state.action_set,XR_ACTION_TYPE_BOOLEAN_INPUT,"jump","Jump",state.right_hand,&state.jump_action) ||
        !CreateAction(state.action_set,XR_ACTION_TYPE_BOOLEAN_INPUT,"game_menu","Game Menu",state.left_hand,&state.menu_action) ||
        !CreateAction(state.action_set,XR_ACTION_TYPE_BOOLEAN_INPUT,"sprint","Sprint Toggle",state.left_hand,&state.sprint_action) ||
        !CreateAction(state.action_set,XR_ACTION_TYPE_BOOLEAN_INPUT,"right_stick_click","Recenter With L3",state.right_hand,&state.right_stick_click_action) ||
        !CreateAction(state.action_set,XR_ACTION_TYPE_FLOAT_INPUT,"left_squeeze","Left Grip",state.left_hand,&state.left_squeeze) ||
        !CreateAction(state.action_set,XR_ACTION_TYPE_FLOAT_INPUT,"right_squeeze","Right Grip",state.right_hand,&state.right_squeeze) ||
        !CreateAction(state.action_set, XR_ACTION_TYPE_VECTOR2F_INPUT,
                      "left_move", "Move", state.left_hand,
                      &state.move_action) ||
        !CreateAction(state.action_set, XR_ACTION_TYPE_VECTOR2F_INPUT,
                      "right_turn", "Turn", state.right_hand,
                      &state.turn_action)) {
        return false;
    }

    XrPath profile = XR_NULL_PATH;
    std::array<XrPath, 12> source_paths{};
    if (!StringToPath(state.xr_instance,
                      "/interaction_profiles/oculus/touch_controller",
                      &profile) ||
        !StringToPath(state.xr_instance,
                      "/user/hand/right/input/grip/pose", &source_paths[0]) ||
        !StringToPath(state.xr_instance,
                      "/user/hand/right/input/aim/pose", &source_paths[1]) ||
        !StringToPath(state.xr_instance,
                      "/user/hand/right/input/trigger/value",
                      &source_paths[2]) ||
        !StringToPath(state.xr_instance,
                      "/user/hand/left/input/thumbstick", &source_paths[3]) ||
        !StringToPath(state.xr_instance,
                      "/user/hand/right/input/thumbstick", &source_paths[4]) ||
        !StringToPath(state.xr_instance,
                      "/user/hand/right/input/b/click", &source_paths[5]) ||
        !StringToPath(state.xr_instance,"/user/hand/right/input/a/click",&source_paths[6]) ||
        !StringToPath(state.xr_instance,"/user/hand/left/input/menu/click",&source_paths[7]) ||
        !StringToPath(state.xr_instance,"/user/hand/left/input/thumbstick/click",&source_paths[8]) ||
        !StringToPath(state.xr_instance,"/user/hand/left/input/squeeze/value",&source_paths[9]) ||
        !StringToPath(state.xr_instance,"/user/hand/right/input/squeeze/value",&source_paths[10]) ||
        !StringToPath(state.xr_instance,"/user/hand/right/input/thumbstick/click",&source_paths[11])) {
        return false;
    }
    const std::array<XrActionSuggestedBinding, 13> bindings{{
        {state.grip_action, source_paths[0]},
        {state.aim_action, source_paths[1]},
        {state.trigger_action, source_paths[2]},
        {state.cast_action, source_paths[2]},
        {state.move_action, source_paths[3]},
        {state.turn_action, source_paths[4]},
        {state.back_action, source_paths[5]},
        {state.jump_action,source_paths[6]}, {state.menu_action,source_paths[7]},
        {state.sprint_action,source_paths[8]},
        {state.left_squeeze,source_paths[9]},{state.right_squeeze,source_paths[10]},
        {state.right_stick_click_action,source_paths[11]},
    }};
    XrInteractionProfileSuggestedBinding suggested{};
    suggested.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
    suggested.interactionProfile = profile;
    suggested.countSuggestedBindings =
        static_cast<std::uint32_t>(bindings.size());
    suggested.suggestedBindings = bindings.data();
    if (!CheckXr(xrSuggestInteractionProfileBindings(state.xr_instance,
                                                      &suggested),
                 "xrSuggestInteractionProfileBindings(Touch)")) {
        return false;
    }
    XrSessionActionSetsAttachInfo attach{};
    attach.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
    attach.countActionSets = 1;
    attach.actionSets = &state.action_set;
    if (!CheckXr(xrAttachSessionActionSets(state.session, &attach),
                 "xrAttachSessionActionSets")) {
        return false;
    }
    XrActionSpaceCreateInfo space_info{};
    space_info.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
    space_info.subactionPath = state.right_hand;
    space_info.poseInActionSpace.orientation.w = 1.0F;
    space_info.action = state.grip_action;
    if (!CheckXr(xrCreateActionSpace(state.session, &space_info,
                                     &state.grip_space),
                 "xrCreateActionSpace(grip)")) {
        return false;
    }
    space_info.action = state.aim_action;
    if (!CheckXr(xrCreateActionSpace(state.session, &space_info,
                                     &state.aim_space),
                 "xrCreateActionSpace(aim)")) {
        return false;
    }
    HPVR_LOGI(
        "[hpvr.quest.input] status=READY profile=oculus_touch "
        "right_grip=1 right_aim=1 trigger=1 cast=1 left_move=1 right_turn=1");
    return true;
}

void DestroyInputSpaces(auto& state) {
    if (state.aim_space != XR_NULL_HANDLE) {
        xrDestroySpace(state.aim_space);
        state.aim_space = XR_NULL_HANDLE;
    }
    if (state.grip_space != XR_NULL_HANDLE) {
        xrDestroySpace(state.grip_space);
        state.grip_space = XR_NULL_HANDLE;
    }
}

void DestroyInputActions(auto& state) {
    if (state.action_set != XR_NULL_HANDLE) {
        xrDestroyActionSet(state.action_set);
    }
    state.action_set = XR_NULL_HANDLE;
    state.grip_action = XR_NULL_HANDLE;
    state.aim_action = XR_NULL_HANDLE;
    state.trigger_action = XR_NULL_HANDLE;
    state.cast_action = XR_NULL_HANDLE;
    state.back_action = XR_NULL_HANDLE;
    state.jump_action=state.menu_action=state.sprint_action=state.right_stick_click_action=XR_NULL_HANDLE;
    state.left_squeeze=state.right_squeeze=XR_NULL_HANDLE;
    state.recenter_chord.Reset();state.recenter_requested=false;
    state.move_action = XR_NULL_HANDLE;
    state.turn_action = XR_NULL_HANDLE;
    state.left_hand = XR_NULL_PATH;
    state.right_hand = XR_NULL_PATH;
}

bool GetVectorAction(const XrSession session, const XrAction action,
                     const XrPath subaction, XrActionStateVector2f* output) {
    XrActionStateGetInfo info{};
    info.type = XR_TYPE_ACTION_STATE_GET_INFO;
    info.action = action;
    info.subactionPath = subaction;
    output->type = XR_TYPE_ACTION_STATE_VECTOR2F;
    return CheckXr(xrGetActionStateVector2f(session, &info, output),
                   "xrGetActionStateVector2f");
}

bool SyncInput(auto& state, const XrTime predicted_time, const bool tracking_active,
               LocomotionInput* const locomotion_input,
               ViewPose* const wand_local, bool* const wand_tracked,
               bool* const cast_held) {
    *locomotion_input = {};
    *wand_tracked = false;
    *cast_held = false;
    state.recenter_requested=false;
    XrActiveActionSet active{};
    active.actionSet = state.action_set;
    XrActionsSyncInfo sync{};
    sync.type = XR_TYPE_ACTIONS_SYNC_INFO;
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (!CheckXr(xrSyncActions(state.session, &sync), "xrSyncActions")) {
        return false;
    }
    ++state.input_syncs;

    XrActionStateVector2f move{};
    XrActionStateVector2f turn{};
    if (!GetVectorAction(state.session, state.move_action, state.left_hand,
                         &move) ||
        !GetVectorAction(state.session, state.turn_action, state.right_hand,
                         &turn)) {
        return false;
    }
    const bool focused = state.session_state == XR_SESSION_STATE_FOCUSED;
    locomotion_input->move_active = focused && move.isActive == XR_TRUE;
    locomotion_input->turn_active = focused && turn.isActive == XR_TRUE;
    if (locomotion_input->move_active) {
        locomotion_input->move_x = move.currentState.x;
        locomotion_input->move_y = move.currentState.y;
    }
    if (locomotion_input->turn_active) {
        locomotion_input->turn_x = turn.currentState.x;
        locomotion_input->turn_y = turn.currentState.y;
    }

    XrActionStateGetInfo get_info{};
    get_info.type = XR_TYPE_ACTION_STATE_GET_INFO;
    get_info.action = state.trigger_action;
    get_info.subactionPath = state.right_hand;
    XrActionStateFloat trigger{};
    trigger.type = XR_TYPE_ACTION_STATE_FLOAT;
    if (!CheckXr(xrGetActionStateFloat(state.session, &get_info, &trigger),
                 "xrGetActionStateFloat(trigger)")) {
        return false;
    }
    state.trigger_value =
        focused && trigger.isActive == XR_TRUE ? trigger.currentState : 0.0F;
    get_info.action = state.cast_action;
    XrActionStateBoolean cast{};
    cast.type = XR_TYPE_ACTION_STATE_BOOLEAN;
    if (!CheckXr(xrGetActionStateBoolean(state.session, &get_info, &cast),
                 "xrGetActionStateBoolean(cast)")) {
        return false;
    }
    *cast_held =
        focused && cast.isActive == XR_TRUE && cast.currentState == XR_TRUE;
    state.cast_held = *cast_held;
    get_info.action=state.back_action;
    XrActionStateBoolean back{};
    back.type=XR_TYPE_ACTION_STATE_BOOLEAN;
    if(!CheckXr(xrGetActionStateBoolean(state.session,&get_info,&back),"xrGetActionStateBoolean(menu)"))return false;
    state.back_held=focused&&back.isActive==XR_TRUE&&back.currentState==XR_TRUE;
    get_info.action=state.jump_action;XrActionStateBoolean jump{};jump.type=XR_TYPE_ACTION_STATE_BOOLEAN;
    if(!CheckXr(xrGetActionStateBoolean(state.session,&get_info,&jump),"xrGetActionStateBoolean(jump)"))return false;
    state.jump_held=focused&&jump.isActive&&jump.currentState;
    get_info.action=state.menu_action;get_info.subactionPath=state.left_hand;
    XrActionStateBoolean menu{};menu.type=XR_TYPE_ACTION_STATE_BOOLEAN;
    if(!CheckXr(xrGetActionStateBoolean(state.session,&get_info,&menu),"xrGetActionStateBoolean(left_menu)"))return false;
    std::array<float,2> grips{};
    for(unsigned i=0;i<2;++i){
        get_info.action=i?state.right_squeeze:state.left_squeeze;get_info.subactionPath=i?state.right_hand:state.left_hand;
        XrActionStateFloat grip{};grip.type=XR_TYPE_ACTION_STATE_FLOAT;
        if(!CheckXr(xrGetActionStateFloat(state.session,&get_info,&grip),"squeeze"))return false;
        grips[i]=grip.isActive?grip.currentState:0;
    }
    const bool menu_pressed=focused&&menu.isActive&&menu.currentState;
    state.vr_menu_requested=state.vr_chord.Update(focused,menu_pressed,grips[0],grips[1]);
    state.back_held=state.back_held||(menu_pressed&&!state.vr_chord.consumed);
    get_info.subactionPath=state.left_hand;
    get_info.action=state.sprint_action;
    XrActionStateBoolean sprint{};sprint.type=XR_TYPE_ACTION_STATE_BOOLEAN;
    if(!CheckXr(xrGetActionStateBoolean(state.session,&get_info,&sprint),"xrGetActionStateBoolean(sprint)"))return false;
    const bool sprint_held=focused&&sprint.isActive&&sprint.currentState;
    get_info.subactionPath=state.right_hand;get_info.action=state.right_stick_click_action;
    XrActionStateBoolean right_click{};right_click.type=XR_TYPE_ACTION_STATE_BOOLEAN;
    if(!CheckXr(xrGetActionStateBoolean(state.session,&get_info,&right_click),"xrGetActionStateBoolean(recenter)"))return false;
    const bool right_held=focused&&right_click.isActive&&right_click.currentState;
    state.recenter_requested=state.recenter_chord.Update(focused,tracking_active,sprint_held,right_held);
    // Recovery must remain accessible even if the ordinary neutral-input latch
    // is waiting for a held stick. The chord never fires sprint or a spell.
    if(state.recenter_requested||state.recenter_chord.consumed){
        state.input_release_pending=true;
        *locomotion_input={};*cast_held=false;
        state.cast_held=state.back_held=state.jump_held=state.vr_menu_requested=false;
        state.sprint_enabled=false;state.sprint_button_held=sprint_held;state.trigger_value=0;
        return true;
    }
    if(sprint_held&&!state.sprint_button_held)state.sprint_enabled=!state.sprint_enabled;
    if(!focused||!locomotion_input->move_active||std::hypot(locomotion_input->move_x,locomotion_input->move_y)<0.18F||
       state.scene.IsFrontEndVisible()||state.scene.IsCutscenePlaying())state.sprint_enabled=false;
    state.sprint_button_held=sprint_held;locomotion_input->sprint=state.sprint_enabled;
    if(state.input_release_pending){
        const bool neutral=!state.cast_held&&!state.back_held&&!state.jump_held&&!menu_pressed&&!sprint_held&&!right_held&&
            state.trigger_value<.15F&&std::hypot(locomotion_input->move_x,locomotion_input->move_y)<.18F&&
            std::abs(locomotion_input->turn_x)<.18F;
        state.input_release_pending=!neutral;
        *locomotion_input={};*cast_held=false;
        state.cast_held=state.back_held=state.jump_held=state.vr_menu_requested=false;
        state.sprint_enabled=false;state.trigger_value=0;
        return true;
    }
    get_info.subactionPath=state.right_hand;

    get_info.action = state.grip_action;
    XrActionStatePose grip_state{};
    grip_state.type = XR_TYPE_ACTION_STATE_POSE;
    if (!CheckXr(xrGetActionStatePose(state.session, &get_info, &grip_state),
                 "xrGetActionStatePose(grip)")) {
        return false;
    }
    get_info.action = state.aim_action;
    XrActionStatePose aim_state{};
    aim_state.type = XR_TYPE_ACTION_STATE_POSE;
    if (!CheckXr(xrGetActionStatePose(state.session, &get_info, &aim_state),
                 "xrGetActionStatePose(aim)")) {
        return false;
    }
    if (!focused || grip_state.isActive != XR_TRUE ||
        aim_state.isActive != XR_TRUE) {
        return true;
    }
    XrSpaceLocation grip_location{};
    grip_location.type = XR_TYPE_SPACE_LOCATION;
    XrSpaceLocation aim_location{};
    aim_location.type = XR_TYPE_SPACE_LOCATION;
    if (!CheckXr(xrLocateSpace(state.grip_space, state.local_space,
                               predicted_time, &grip_location),
                 "xrLocateSpace(grip)") ||
        !CheckXr(xrLocateSpace(state.aim_space, state.local_space,
                               predicted_time, &aim_location),
                 "xrLocateSpace(aim)")) {
        return false;
    }
    constexpr XrSpaceLocationFlags required =
        XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT |
        XR_SPACE_LOCATION_POSITION_TRACKED_BIT |
        XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
    if ((grip_location.locationFlags & required) != required ||
        (aim_location.locationFlags & required) != required) {
        return true;
    }
    wand_local->position = {
        grip_location.pose.position.x,
        grip_location.pose.position.y,
        grip_location.pose.position.z,
    };
    wand_local->orientation = {
        aim_location.pose.orientation.x,
        aim_location.pose.orientation.y,
        aim_location.pose.orientation.z,
        aim_location.pose.orientation.w,
    };
    *wand_tracked = true;
    ++state.tracked_wand_frames;
    return true;
}

}  // namespace

XrVulkanSmoke::XrVulkanSmoke() : state_(std::make_unique<State>()) {}

XrVulkanSmoke::~XrVulkanSmoke() {
    Destroy();
}

bool XrVulkanSmoke::LoadHogwarts(const std::filesystem::path& data_root,const std::filesystem::path& save_root) {
    State& state = *state_;
    if (state.scene_load_future.valid() || state.scene_load_adopted) {
        return false;
    }
    state.startup_pixels=LoadWarnerStartup(data_root);
    if(state.startup_pixels.empty()){HPVR_LOGE("[hpvr.quest.startup] status=ART_MISSING");return false;}
    state.data_root=data_root;state.save_root=save_root;
    return QueueSceneLoad(state,0,false);
}

bool XrVulkanSmoke::PumpHogwartsLoad() {
    State& state = *state_;
    if(!state.scene_load_future.valid()){
        if(state.scene_load_adopted){
            unsigned map_id=0;
            if(state.scene.ConsumeMapTransition(&map_id,&state.transferred_progress,&state.transferred_slot)&&
               !QueueSceneLoad(state,map_id,true))
                state.scene.AbortMapTransition("Could not start loading this level.");
        }
        return true;
    }
    if (state.scene_load_future.wait_for(std::chrono::seconds(0)) !=
        std::future_status::ready) return true;
    // Adoption only happens between XR frames, with a valid render pass. If
    // Android suspended the session, keep the finished CPU scene pending.
    if(state.device==VK_NULL_HANDLE||state.render_pass==VK_NULL_HANDLE)return true;
    bool loaded=false;
    try{
        loaded=state.scene_load_future.get();
        if(loaded&&state.map_transfer)
            state.loading_scene->RestoreTransferredProgress(state.transferred_progress,state.transferred_slot);
    }catch(const std::exception& error){
        HPVR_LOGE("[hpvr.quest.scene.async] status=LOAD_EXCEPTION map=%u reason=%s",state.loading_map_id,error.what());
        loaded=false;
    }catch(...){
        HPVR_LOGE("[hpvr.quest.scene.async] status=LOAD_EXCEPTION map=%u",state.loading_map_id);
        loaded=false;
    }
    if (!loaded) {
        SceneLoadTrace::Append(state.save_root,state.loading_map_id,"TRANSFER_CPU_FAILED");
        state.scene_load_failed = true;
        state.loading_scene.reset();state.loading_gesture.reset();
        HPVR_LOGE("[hpvr.quest.scene.async] status=LOAD_FAILED map=%u retained=%u",state.loading_map_id,state.map_transfer?1U:0U);
        if(state.map_transfer){
            state.scene.AbortMapTransition("Could not load this level. Check your installed game data.");
            ResetSceneInput(state,false);state.map_transfer=false;
            return true;
        }
        return false;
    }
    if(!CheckVk(vkDeviceWaitIdle(state.device),"map transfer idle"))return false;
    // Retain the old CPU scene through GPU upload so a failed map can roll back
    // without touching saves. Old GPU allocations are freed first for Quest RAM.
    state.scene.DestroyGpu();
    state.scene.Swap(*state.loading_scene);
    SceneLoadTrace::Append(state.save_root,state.loading_map_id,"GPU_UPLOAD_BEGIN");
    const auto upload=[&](){return state.scene.CreateGpu(state.physical_device,state.device,state.queue,
        state.queue_family,state.render_pass,state.width,state.height,state.color_format,state.depth_format,
        static_cast<unsigned>(state.swapchain_images.size()));};
    bool uploaded=false;
    try{uploaded=upload();}catch(const std::exception& error){
        HPVR_LOGE("[hpvr.quest.scene.async] status=GPU_EXCEPTION reason=%s",error.what());
    }catch(...){HPVR_LOGE("[hpvr.quest.scene.async] status=GPU_EXCEPTION");}
    if(!uploaded){
        SceneLoadTrace::Append(state.save_root,state.loading_map_id,"GPU_UPLOAD_FAILED");
        state.scene_load_failed = true;
        if(!CheckVk(vkDeviceWaitIdle(state.device),"map rollback idle"))return false;
        state.scene.DestroyGpu();state.scene.Swap(*state.loading_scene);
        state.loading_scene.reset();state.loading_gesture.reset();
        bool recovered=false;
        if(state.map_transfer){try{recovered=upload();}catch(...){recovered=false;}}
        HPVR_LOGE("[hpvr.quest.scene.async] status=GPU_UPLOAD_FAILED map=%u rollback=%u",state.loading_map_id,recovered?1U:0U);
        if(recovered){
            state.scene.AbortMapTransition("Could not prepare this level. Your previous level is still available.");
            ResetSceneInput(state,false);state.map_transfer=false;
        }
        return recovered;
    }
    if(state.loading_gesture)state.gesture.swap(state.loading_gesture);
    state.loading_scene.reset();state.loading_gesture.reset();
    ResetSceneInput(state,true);state.map_transfer=false;
    state.scene_load_adopted = true;
    SceneLoadTrace::Append(state.save_root,state.loading_map_id,"ADOPTED");
    HPVR_LOGI(
        "[hpvr.quest.scene.async] status=ADOPTED map=%u content=HOGWARTS "
        "vertices=%u layers=%u characters=%u animation_frames=%u",
        state.loading_map_id,
        state.scene.VertexCount(), state.scene.TextureLayerCount(),
        state.scene.CharacterCount(), state.scene.AnimationFrameCount());
    return true;
}

bool XrVulkanSmoke::InitializeGraphics(const XrInstance instance,
                                       const XrSystemId system_id,bool metrics_extension,bool performance_extension) {
    State& state = *state_;
    if (state.device != VK_NULL_HANDLE) {
        return state.xr_instance == instance && state.system_id == system_id;
    }
    state.xr_instance = instance;
    state.system_id = system_id;
    state.metrics_extension=metrics_extension;
    state.performance_extension=performance_extension;

    PFN_xrGetVulkanGraphicsRequirements2KHR get_requirements = nullptr;
    PFN_xrCreateVulkanInstanceKHR create_vulkan_instance = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR get_graphics_device = nullptr;
    PFN_xrCreateVulkanDeviceKHR create_vulkan_device = nullptr;
    if (!CheckXr(xrGetInstanceProcAddr(
                     instance, "xrGetVulkanGraphicsRequirements2KHR",
                     reinterpret_cast<PFN_xrVoidFunction*>(&get_requirements)),
                 "xrGetVulkanGraphicsRequirements2KHR(proc)") ||
        !CheckXr(xrGetInstanceProcAddr(
                     instance, "xrCreateVulkanInstanceKHR",
                     reinterpret_cast<PFN_xrVoidFunction*>(
                         &create_vulkan_instance)),
                 "xrCreateVulkanInstanceKHR(proc)") ||
        !CheckXr(xrGetInstanceProcAddr(
                     instance, "xrGetVulkanGraphicsDevice2KHR",
                     reinterpret_cast<PFN_xrVoidFunction*>(&get_graphics_device)),
                 "xrGetVulkanGraphicsDevice2KHR(proc)") ||
        !CheckXr(xrGetInstanceProcAddr(
                     instance, "xrCreateVulkanDeviceKHR",
                     reinterpret_cast<PFN_xrVoidFunction*>(&create_vulkan_device)),
                 "xrCreateVulkanDeviceKHR(proc)") ||
        get_requirements == nullptr || create_vulkan_instance == nullptr ||
        get_graphics_device == nullptr || create_vulkan_device == nullptr) {
        return false;
    }

    XrGraphicsRequirementsVulkan2KHR requirements{};
    requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR;
    if (!CheckXr(get_requirements(instance, system_id, &requirements),
                 "xrGetVulkanGraphicsRequirements2KHR")) {
        return false;
    }
    constexpr XrVersion requested_api = XR_MAKE_VERSION(1, 1, 0);
    if (requested_api < requirements.minApiVersionSupported ||
        requested_api > requirements.maxApiVersionSupported) {
        HPVR_LOGE(
            "[hpvr.quest.vk] status=API_VERSION_REJECTED requested=%llu "
            "minimum=%llu maximum=%llu",
            static_cast<unsigned long long>(requested_api),
            static_cast<unsigned long long>(requirements.minApiVersionSupported),
            static_cast<unsigned long long>(requirements.maxApiVersionSupported));
        return false;
    }

    VkApplicationInfo application_info{};
    application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application_info.pApplicationName = "Harry Potter VR";
    application_info.applicationVersion = 1;
    application_info.pEngineName = "HPVR clean-room";
    application_info.engineVersion = 1;
    application_info.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo vk_instance_info{};
    vk_instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    vk_instance_info.pApplicationInfo = &application_info;

    XrVulkanInstanceCreateInfoKHR xr_instance_info{};
    xr_instance_info.type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR;
    xr_instance_info.systemId = system_id;
    xr_instance_info.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xr_instance_info.vulkanCreateInfo = &vk_instance_info;

    VkResult vk_result = VK_SUCCESS;
    if (!CheckXr(create_vulkan_instance(instance, &xr_instance_info,
                                        &state.vk_instance, &vk_result),
                 "xrCreateVulkanInstanceKHR") ||
        !CheckVk(vk_result, "vkCreateInstance(via OpenXR)")) {
        Destroy();
        return false;
    }

    XrVulkanGraphicsDeviceGetInfoKHR device_get_info{};
    device_get_info.type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR;
    device_get_info.systemId = system_id;
    device_get_info.vulkanInstance = state.vk_instance;
    if (!CheckXr(get_graphics_device(instance, &device_get_info,
                                     &state.physical_device),
                 "xrGetVulkanGraphicsDevice2KHR")) {
        Destroy();
        return false;
    }

    std::uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(state.physical_device,
                                              &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(state.physical_device,
                                              &family_count, families.data());
    bool found_family = false;
    for (std::uint32_t index = 0; index < family_count; ++index) {
        if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
            families[index].queueCount > 0) {
            state.queue_family = index;
            found_family = true;
            break;
        }
    }
    if (!found_family) {
        HPVR_LOGE("[hpvr.quest.vk] status=NO_GRAPHICS_QUEUE");
        Destroy();
        return false;
    }

    constexpr float queue_priority = 1.0F;
    VkDeviceQueueCreateInfo queue_info{};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = state.queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo device_info{};
    device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;

    XrVulkanDeviceCreateInfoKHR xr_device_info{};
    xr_device_info.type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
    xr_device_info.systemId = system_id;
    xr_device_info.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xr_device_info.vulkanPhysicalDevice = state.physical_device;
    xr_device_info.vulkanCreateInfo = &device_info;

    vk_result = VK_SUCCESS;
    if (!CheckXr(create_vulkan_device(instance, &xr_device_info, &state.device,
                                      &vk_result),
                 "xrCreateVulkanDeviceKHR") ||
        !CheckVk(vk_result, "vkCreateDevice(via OpenXR)")) {
        Destroy();
        return false;
    }
    vkGetDeviceQueue(state.device, state.queue_family, 0, &state.queue);

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(state.physical_device, &properties);
    state.timestamp_bits=families[state.queue_family].timestampValidBits;
    state.timestamp_period=properties.limits.timestampPeriod;
    HPVR_LOGI(
        "[hpvr.quest.vk] status=DEVICE_READY name=%s api=%u.%u.%u "
        "queue_family=%u",
        properties.deviceName, VK_VERSION_MAJOR(properties.apiVersion),
        VK_VERSION_MINOR(properties.apiVersion),
        VK_VERSION_PATCH(properties.apiVersion), state.queue_family);
    return true;
}

bool XrVulkanSmoke::CreateSession() {
    State& state = *state_;
    if (state.session != XR_NULL_HANDLE) {
        return true;
    }
    if (state.device == VK_NULL_HANDLE || state.xr_instance == XR_NULL_HANDLE) {
        HPVR_LOGE("[hpvr.quest.session] status=GRAPHICS_NOT_READY");
        return false;
    }

    XrGraphicsBindingVulkan2KHR binding{};
    binding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR;
    binding.instance = state.vk_instance;
    binding.physicalDevice = state.physical_device;
    binding.device = state.device;
    binding.queueFamilyIndex = state.queue_family;
    binding.queueIndex = 0;

    XrSessionCreateInfo session_info{};
    session_info.type = XR_TYPE_SESSION_CREATE_INFO;
    session_info.next = &binding;
    session_info.systemId = state.system_id;
    if (!CheckXr(xrCreateSession(state.xr_instance, &session_info,
                                 &state.session),
                 "xrCreateSession")) {
        state.session = XR_NULL_HANDLE;
        return false;
    }

    PFN_xrEnumerateDisplayRefreshRatesFB enumerate_refresh=nullptr;
    xrGetInstanceProcAddr(state.xr_instance,"xrEnumerateDisplayRefreshRatesFB",reinterpret_cast<PFN_xrVoidFunction*>(&enumerate_refresh));
    xrGetInstanceProcAddr(state.xr_instance,"xrRequestDisplayRefreshRateFB",reinterpret_cast<PFN_xrVoidFunction*>(&state.request_refresh));
    state.refresh_rates.clear();state.requested_refresh=-1;
    state.set_performance=nullptr;state.requested_gpu_boost=-1;
    if(state.performance_extension)
        xrGetInstanceProcAddr(state.xr_instance,"xrPerfSettingsSetPerformanceLevelEXT",reinterpret_cast<PFN_xrVoidFunction*>(&state.set_performance));
    if(enumerate_refresh&&state.request_refresh){
        unsigned count=0;
        if(XR_SUCCEEDED(enumerate_refresh(state.session,0,&count,nullptr))&&count>0&&count<32){
            std::vector<float> rates(count);
            if(XR_SUCCEEDED(enumerate_refresh(state.session,count,&count,rates.data()))){
                state.refresh_rates.push_back(0);
                for(float hz:rates)if(std::isfinite(hz)&&hz>=60&&hz<=144){
                    const int value=static_cast<int>(std::lround(hz));
                    if(ValidRefreshRate(value))state.refresh_rates.push_back(value);
                }
                std::sort(state.refresh_rates.begin(),state.refresh_rates.end());
                state.refresh_rates.erase(std::unique(state.refresh_rates.begin(),state.refresh_rates.end()),state.refresh_rates.end());
            }
        }
    }
    XrReferenceSpaceCreateInfo space_info{};
    PFN_xrEnumeratePerformanceMetricsCounterPathsMETA enumerate_metrics=nullptr;
    state.query_metric=nullptr;state.set_metrics=nullptr;
    if(state.metrics_extension){
    xrGetInstanceProcAddr(state.xr_instance,"xrEnumeratePerformanceMetricsCounterPathsMETA",reinterpret_cast<PFN_xrVoidFunction*>(&enumerate_metrics));
    xrGetInstanceProcAddr(state.xr_instance,"xrQueryPerformanceMetricsCounterMETA",reinterpret_cast<PFN_xrVoidFunction*>(&state.query_metric));
    xrGetInstanceProcAddr(state.xr_instance,"xrSetPerformanceMetricsStateMETA",reinterpret_cast<PFN_xrVoidFunction*>(&state.set_metrics));
    }
    state.metric_paths={};state.metrics_enabled=false;
    if(enumerate_metrics&&state.query_metric&&state.set_metrics){
        unsigned count=0;
        if(XR_SUCCEEDED(enumerate_metrics(state.xr_instance,0,&count,nullptr))&&count<1024){
            std::vector<XrPath> paths(count);
            if(XR_SUCCEEDED(enumerate_metrics(state.xr_instance,count,&count,paths.data())))for(auto path:paths){
                char name[XR_MAX_PATH_LENGTH]{};unsigned length=0;
                if(XR_FAILED(xrPathToString(state.xr_instance,path,sizeof(name),&length,name)))continue;
                const std::string n(name);
                if(n=="/perfmetrics_meta/device/cpu_utilization_average")state.metric_paths[0]=path;
                if(n=="/perfmetrics_meta/device/gpu_utilization")state.metric_paths[1]=path;
                if(n=="/perfmetrics_meta/app/cpu_frametime")state.metric_paths[2]=path;
                if(n=="/perfmetrics_meta/app/gpu_frametime")state.metric_paths[3]=path;
                HPVR_LOGI("[hpvr.quest.metrics] available=%s",name);
            }
        }
    }
    if(state.timestamp_bits){
        VkQueryPoolCreateInfo q{};q.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;q.queryType=VK_QUERY_TYPE_TIMESTAMP;q.queryCount=8;
        if(vkCreateQueryPool(state.device,&q,nullptr,&state.timing_pool)!=VK_SUCCESS)state.timing_pool=VK_NULL_HANDLE;
    }
    space_info.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    space_info.poseInReferenceSpace.orientation.w = 1.0F;
    if (!CheckXr(xrCreateReferenceSpace(state.session, &space_info,
                                        &state.local_space),
                 "xrCreateReferenceSpace(LOCAL)")) {
        DestroySession();
        return false;
    }

    std::uint32_t view_count = 0;
    if (!CheckXr(xrEnumerateViewConfigurationViews(
                     state.xr_instance, state.system_id,
                     XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count,
                     nullptr),
                 "xrEnumerateViewConfigurationViews(count)") ||
        view_count != kViewCount) {
        HPVR_LOGE("[hpvr.quest.session] status=INVALID_VIEW_COUNT count=%u",
                  view_count);
        DestroySession();
        return false;
    }
    std::array<XrViewConfigurationView, kViewCount> views{};
    for (auto& view : views) {
        view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    }
    if (!CheckXr(xrEnumerateViewConfigurationViews(
                     state.xr_instance, state.system_id,
                     XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view_count,
                     &view_count, views.data()),
                 "xrEnumerateViewConfigurationViews(values)")) {
        DestroySession();
        return false;
    }
    state.width = std::max(views[0].recommendedImageRectWidth,
                           views[1].recommendedImageRectWidth);
    state.height = std::max(views[0].recommendedImageRectHeight,
                            views[1].recommendedImageRectHeight);
    state.recommended_width=state.width;state.recommended_height=state.height;
    // Bounded allocation once, active rendering rectangle changes live.
    state.width=VrRenderExtent(state.width,175,std::min(views[0].maxImageRectWidth,views[1].maxImageRectWidth));
    state.height=VrRenderExtent(state.height,175,std::min(views[0].maxImageRectHeight,views[1].maxImageRectHeight));
    if (state.width == 0 || state.height == 0) {
        HPVR_LOGE(
            "[hpvr.quest.session] status=INVALID_RECOMMENDED_EXTENT width=%u "
            "height=%u",
            state.width, state.height);
        DestroySession();
        return false;
    }

    std::uint32_t format_count = 0;
    if (!CheckXr(xrEnumerateSwapchainFormats(state.session, 0, &format_count,
                                             nullptr),
                 "xrEnumerateSwapchainFormats(count)")) {
        DestroySession();
        return false;
    }
    if (format_count == 0) {
        HPVR_LOGE("[hpvr.quest.session] status=NO_SWAPCHAIN_FORMATS");
        DestroySession();
        return false;
    }
    std::vector<std::int64_t> formats(format_count);
    if (!CheckXr(xrEnumerateSwapchainFormats(
                     state.session, format_count, &format_count, formats.data()),
                 "xrEnumerateSwapchainFormats(values)")) {
        DestroySession();
        return false;
    }
    state.color_format = SelectColorFormat(formats);
    if (state.color_format == VK_FORMAT_UNDEFINED) {
        HPVR_LOGE("[hpvr.quest.session] status=NO_SUPPORTED_COLOR_FORMAT");
        DestroySession();
        return false;
    }

    XrSwapchainCreateInfo swapchain_info{};
    swapchain_info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    swapchain_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
    swapchain_info.format = state.color_format;
    swapchain_info.sampleCount = 1;
    swapchain_info.width = state.width;
    swapchain_info.height = state.height;
    swapchain_info.faceCount = 1;
    swapchain_info.arraySize = kViewCount;
    swapchain_info.mipCount = 1;
    if (!CheckXr(xrCreateSwapchain(state.session, &swapchain_info,
                                   &state.swapchain),
                 "xrCreateSwapchain")) {
        DestroySession();
        return false;
    }

    std::uint32_t image_count = 0;
    if (!CheckXr(xrEnumerateSwapchainImages(state.swapchain, 0, &image_count,
                                            nullptr),
                 "xrEnumerateSwapchainImages(count)")) {
        DestroySession();
        return false;
    }
    if (image_count == 0) {
        HPVR_LOGE("[hpvr.quest.session] status=NO_SWAPCHAIN_IMAGES");
        DestroySession();
        return false;
    }
    state.swapchain_images.resize(image_count);
    for (auto& image : state.swapchain_images) {
        image.type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
        image.next = nullptr;
    }
    if (!CheckXr(xrEnumerateSwapchainImages(
                     state.swapchain, image_count, &image_count,
                     reinterpret_cast<XrSwapchainImageBaseHeader*>(
                         state.swapchain_images.data())),
                 "xrEnumerateSwapchainImages(values)")) {
        DestroySession();
        return false;
    }

    if (!FindDepthFormat(state.physical_device, &state.depth_format)) {
        HPVR_LOGE("[hpvr.quest.session] status=NO_DEPTH_FORMAT");
        DestroySession();
        return false;
    }
    std::array<VkAttachmentDescription, 2> attachments{};
    attachments[0].format = state.color_format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].format = state.depth_format;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_reference{};
    color_reference.attachment = 0;
    color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depth_reference{};
    depth_reference.attachment = 1;
    depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_reference;
    subpass.pDepthStencilAttachment = &depth_reference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = dependency.srcStageMask;
    dependency.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = attachments.size();
    render_pass_info.pAttachments = attachments.data();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;
    if (!CheckVk(vkCreateRenderPass(state.device, &render_pass_info, nullptr,
                                    &state.render_pass),
                 "vkCreateRenderPass")) {
        DestroySession();
        return false;
    }

    // Compatible with world pipelines/framebuffers. LOAD only resumes the left
    // eye for HUD/wand after clean history capture; never redraws world geometry.
    attachments[0].loadOp=attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].initialLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].initialLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if(!CheckVk(vkCreateRenderPass(state.device,&render_pass_info,nullptr,&state.overlay_pass),
                "vkCreateRenderPass(left_overlay_load)")){DestroySession();return false;}

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = state.queue_family;
    if (!CheckVk(vkCreateCommandPool(state.device, &pool_info, nullptr,
                                     &state.command_pool),
                 "vkCreateCommandPool")) {
        DestroySession();
        return false;
    }

    state.command_buffers.resize(image_count);
    VkCommandBufferAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool = state.command_pool;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = image_count;
    if (!CheckVk(vkAllocateCommandBuffers(state.device, &allocate_info,
                                          state.command_buffers.data()),
                 "vkAllocateCommandBuffers")) {
        DestroySession();
        return false;
    }

    state.image_views.resize(image_count);
    state.framebuffers.resize(image_count);
    state.depth_images.resize(image_count, VK_NULL_HANDLE);
    state.depth_memories.resize(image_count, VK_NULL_HANDLE);
    state.depth_views.resize(image_count);
    state.fences.resize(image_count, VK_NULL_HANDLE);
    for (std::uint32_t image_index = 0; image_index < image_count;
         ++image_index) {
        VkImageCreateInfo depth_image_info{};
        depth_image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depth_image_info.imageType = VK_IMAGE_TYPE_2D;
        depth_image_info.format = state.depth_format;
        depth_image_info.extent = {state.width, state.height, 1};
        depth_image_info.mipLevels = 1;
        depth_image_info.arrayLayers = kViewCount;
        depth_image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        depth_image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        depth_image_info.usage =
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        depth_image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        depth_image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (!CheckVk(vkCreateImage(state.device, &depth_image_info, nullptr,
                                   &state.depth_images[image_index]),
                     "vkCreateImage(depth)")) {
            DestroySession();
            return false;
        }
        VkMemoryRequirements depth_requirements{};
        vkGetImageMemoryRequirements(state.device,
                                     state.depth_images[image_index],
                                     &depth_requirements);
        std::uint32_t depth_memory_type = 0;
        if (!FindDeviceMemoryType(
                state.physical_device, depth_requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &depth_memory_type)) {
            HPVR_LOGE(
                "[hpvr.quest.session] status=NO_DEPTH_MEMORY_TYPE index=%u",
                image_index);
            DestroySession();
            return false;
        }
        VkMemoryAllocateInfo depth_allocate{};
        depth_allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        depth_allocate.allocationSize = depth_requirements.size;
        depth_allocate.memoryTypeIndex = depth_memory_type;
        if (!CheckVk(vkAllocateMemory(
                         state.device, &depth_allocate, nullptr,
                         &state.depth_memories[image_index]),
                     "vkAllocateMemory(depth)") ||
            !CheckVk(vkBindImageMemory(
                         state.device, state.depth_images[image_index],
                         state.depth_memories[image_index], 0),
                     "vkBindImageMemory(depth)")) {
            DestroySession();
            return false;
        }
        for (std::uint32_t eye = 0; eye < kViewCount; ++eye) {
            VkImageViewCreateInfo view_info{};
            view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image = state.swapchain_images[image_index].image;
            view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format = state.color_format;
            view_info.components = {
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY,
            };
            view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_info.subresourceRange.baseMipLevel = 0;
            view_info.subresourceRange.levelCount = 1;
            view_info.subresourceRange.baseArrayLayer = eye;
            view_info.subresourceRange.layerCount = 1;
            if (!CheckVk(vkCreateImageView(
                             state.device, &view_info, nullptr,
                             &state.image_views[image_index][eye]),
                         "vkCreateImageView")) {
                DestroySession();
                return false;
            }

            VkImageViewCreateInfo depth_view_info{};
            depth_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            depth_view_info.image = state.depth_images[image_index];
            depth_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            depth_view_info.format = state.depth_format;
            depth_view_info.subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_DEPTH_BIT;
            depth_view_info.subresourceRange.levelCount = 1;
            depth_view_info.subresourceRange.baseArrayLayer = eye;
            depth_view_info.subresourceRange.layerCount = 1;
            if (!CheckVk(vkCreateImageView(
                             state.device, &depth_view_info, nullptr,
                             &state.depth_views[image_index][eye]),
                         "vkCreateImageView(depth)")) {
                DestroySession();
                return false;
            }

            const std::array framebuffer_attachments{
                state.image_views[image_index][eye],
                state.depth_views[image_index][eye],
            };
            VkFramebufferCreateInfo framebuffer_info{};
            framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_info.renderPass = state.render_pass;
            framebuffer_info.attachmentCount = framebuffer_attachments.size();
            framebuffer_info.pAttachments = framebuffer_attachments.data();
            framebuffer_info.width = state.width;
            framebuffer_info.height = state.height;
            framebuffer_info.layers = 1;
            if (!CheckVk(vkCreateFramebuffer(
                             state.device, &framebuffer_info, nullptr,
                             &state.framebuffers[image_index][eye]),
                         "vkCreateFramebuffer")) {
                DestroySession();
                return false;
            }
        }

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (!CheckVk(vkCreateFence(state.device, &fence_info, nullptr,
                                   &state.fences[image_index]),
                     "vkCreateFence")) {
            DestroySession();
            return false;
        }
    }

    if (!state.scene_load_future.valid() &&
        !state.scene.CreateGpu(state.physical_device, state.device, state.queue,
                               state.queue_family, state.render_pass,state.width,state.height,state.color_format,state.depth_format,
                               static_cast<unsigned>(state.swapchain_images.size()))) {
        HPVR_LOGE("[hpvr.quest.session] status=SCENE_GPU_FAILED");
        DestroySession();
        return false;
    }
    if(!CreateStartupLayer(state)){DestroySession();return false;}
    if (!CreateInput(state)) {
        HPVR_LOGE("[hpvr.quest.session] status=INPUT_FAILED");
        DestroySession();
        return false;
    }

    state.session_state = XR_SESSION_STATE_UNKNOWN;
    state.running = false;
    state.last_predicted_time = 0;
    state.locomotion.Reset();
    state.rebase_head=state.have_world_head;
    state.recenter_chord.Reset();state.input_release_pending=true;
    state.gesture->Reset();
    state.scene.SetWandDrawing(false);
    HPVR_LOGI(
        "[hpvr.quest.session] status=CREATED width=%u height=%u images=%u "
        "array_layers=%u format=%d depth_format=%d scene=%s characters=%u "
        "animation_frames=%u",
        state.width, state.height, image_count, kViewCount,
        static_cast<int>(state.color_format),
        static_cast<int>(state.depth_format),
        state.scene.IsGpuReady() ? "HOGWARTS" : "WARNER_LOADING",
        state.scene_load_future.valid()
            ? 0U
            : state.scene.CharacterCount(),
        state.scene_load_future.valid()
            ? 0U
            : state.scene.AnimationFrameCount());
    return true;
}

void XrVulkanSmoke::DestroySession() {
    State& state = *state_;
    state.voice.SetListening({});state.voice_target=0;++state.voice_generation;
    state.running = false;
    state.session_state = XR_SESSION_STATE_UNKNOWN;
    if (state.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(state.device);
        state.scene.DestroyGpu();
        if(state.timing_pool)vkDestroyQueryPool(state.device,state.timing_pool,nullptr);
        state.timing_pool=VK_NULL_HANDLE;state.metrics_enabled=false;state.perf_start=0;state.perf_frames=0;state.performance={};
        for (const VkFence fence : state.fences) {
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(state.device, fence, nullptr);
            }
        }
        for (const auto& eye_framebuffers : state.framebuffers) {
            for (const VkFramebuffer framebuffer : eye_framebuffers) {
                if (framebuffer != VK_NULL_HANDLE) {
                    vkDestroyFramebuffer(state.device, framebuffer, nullptr);
                }
            }
        }
        for (const auto& eye_views : state.image_views) {
            for (const VkImageView view : eye_views) {
                if (view != VK_NULL_HANDLE) {
                    vkDestroyImageView(state.device, view, nullptr);
                }
            }
        }
        for (const auto& eye_views : state.depth_views) {
            for (const VkImageView view : eye_views) {
                if (view != VK_NULL_HANDLE) {
                    vkDestroyImageView(state.device, view, nullptr);
                }
            }
        }
        for (const VkImage image : state.depth_images) {
            if (image != VK_NULL_HANDLE) {
                vkDestroyImage(state.device, image, nullptr);
            }
        }
        for (const VkDeviceMemory memory : state.depth_memories) {
            if (memory != VK_NULL_HANDLE) {
                vkFreeMemory(state.device, memory, nullptr);
            }
        }
        if (state.command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(state.device, state.command_pool, nullptr);
        }
        if (state.render_pass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(state.device, state.render_pass, nullptr);
        }
        if(state.overlay_pass)vkDestroyRenderPass(state.device,state.overlay_pass,nullptr);
    }
    DestroyInputSpaces(state);
    state.fences.clear();
    state.framebuffers.clear();
    state.image_views.clear();
    state.depth_views.clear();
    state.depth_images.clear();
    state.depth_memories.clear();
    state.command_buffers.clear();
    state.swapchain_images.clear();
    state.command_pool = VK_NULL_HANDLE;
    state.render_pass = VK_NULL_HANDLE;
    state.overlay_pass = VK_NULL_HANDLE;

    if(state.startup_swapchain){xrDestroySwapchain(state.startup_swapchain);state.startup_swapchain=XR_NULL_HANDLE;}
    if(state.loading_icon_swapchain){xrDestroySwapchain(state.loading_icon_swapchain);state.loading_icon_swapchain=XR_NULL_HANDLE;}
    if(state.startup_space){xrDestroySpace(state.startup_space);state.startup_space=XR_NULL_HANDLE;}
    if (state.swapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(state.swapchain);
        state.swapchain = XR_NULL_HANDLE;
    }
    if (state.local_space != XR_NULL_HANDLE) {
        xrDestroySpace(state.local_space);
        state.local_space = XR_NULL_HANDLE;
    }
    if (state.session != XR_NULL_HANDLE) {
        xrDestroySession(state.session);
        state.session = XR_NULL_HANDLE;
        HPVR_LOGI("[hpvr.quest.session] status=DESTROYED");
    }
    DestroyInputActions(state);
    state.color_format = VK_FORMAT_UNDEFINED;
    state.depth_format = VK_FORMAT_UNDEFINED;
    state.width = 0;
    state.height = 0;
}

void XrVulkanSmoke::Destroy() {
    State& state = *state_;
    // A CPU loader never owns Vulkan handles; join it before pending objects
    // are released, including Activity destruction during a map transition.
    if(state.scene_load_future.valid()){
        try{(void)state.scene_load_future.get();}catch(...){}
    }
    state.loading_scene.reset();state.loading_gesture.reset();
    DestroySession();
    if (state.device != VK_NULL_HANDLE) {
        vkDestroyDevice(state.device, nullptr);
        state.device = VK_NULL_HANDLE;
    }
    state.queue = VK_NULL_HANDLE;
    state.physical_device = VK_NULL_HANDLE;
    if (state.vk_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(state.vk_instance, nullptr);
        state.vk_instance = VK_NULL_HANDLE;
    }
    state.xr_instance = XR_NULL_HANDLE;
    state.system_id = XR_NULL_SYSTEM_ID;
}

bool XrVulkanSmoke::PollEvents(bool* exit_requested) {
    State& state = *state_;
    if (exit_requested != nullptr) {
        *exit_requested = false;
    }
    if (state.xr_instance == XR_NULL_HANDLE) {
        return true;
    }

    for (;;) {
        XrEventDataBuffer event{};
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
        const XrResult result = xrPollEvent(state.xr_instance, &event);
        if (result == XR_EVENT_UNAVAILABLE) {
            return true;
        }
        if (!CheckXr(result, "xrPollEvent")) {
            return false;
        }

        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto& changed =
                *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
            if (changed.session != state.session) {
                continue;
            }
            if(changed.state!=XR_SESSION_STATE_FOCUSED){
                state.last_predicted_time=0;state.rebase_head=state.have_world_head;
                state.recenter_chord.Reset();
                state.gesture->Reset();state.scene.SetWandDrawing(false);
            }
            state.session_state = changed.state;
            HPVR_LOGI("[hpvr.quest.session] state=%d",
                      static_cast<int>(changed.state));
            if (changed.state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo begin_info{};
                begin_info.type = XR_TYPE_SESSION_BEGIN_INFO;
                begin_info.primaryViewConfigurationType =
                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (!CheckXr(xrBeginSession(state.session, &begin_info),
                             "xrBeginSession")) {
                    return false;
                }
                state.running = true;
                state.requested_gpu_boost=-1;
                HPVR_LOGI("[hpvr.quest.session] status=RUNNING");
            } else if (changed.state == XR_SESSION_STATE_STOPPING) {
                state.running = false;
                if (!CheckXr(xrEndSession(state.session), "xrEndSession")) {
                    return false;
                }
            } else if (changed.state == XR_SESSION_STATE_EXITING ||
                       changed.state == XR_SESSION_STATE_LOSS_PENDING) {
                state.running = false;
                if (exit_requested != nullptr) {
                    *exit_requested = true;
                }
            }
        } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            if (exit_requested != nullptr) {
                *exit_requested = true;
            }
        } else if (event.type ==
                   XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            const auto& changed =
                *reinterpret_cast<
                    const XrEventDataReferenceSpaceChangePending*>(&event);
            if (changed.session == state.session &&
                changed.referenceSpaceType == XR_REFERENCE_SPACE_TYPE_LOCAL) {
                // The event announces a future coordinate change. Rebase on
                // its first display frame, not while locations are still old.
                state.local_space_change_time=changed.changeTime;
            }
        } else if (event.type ==
                   XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED) {
            state.gesture->Reset();
            state.scene.SetWandDrawing(false);
            HPVR_LOGI("[hpvr.quest.input] status=PROFILE_CHANGED");
        }
    }
}

bool XrVulkanSmoke::RenderFrame() {
    State& state = *state_;
    if (!state.running || state.session == XR_NULL_HANDLE) {
        return true;
    }
    const bool scene_visible=!state.scene_load_future.valid()&&state.scene.IsGpuReady();

    XrFrameWaitInfo wait_info{};
    wait_info.type = XR_TYPE_FRAME_WAIT_INFO;
    XrFrameState frame_state{};
    frame_state.type = XR_TYPE_FRAME_STATE;
    if (!CheckXr(xrWaitFrame(state.session, &wait_info, &frame_state),
                 "xrWaitFrame")) {
        return false;
    }
    XrFrameBeginInfo begin_info{};
    const double cpu_start=ThreadMs();
    begin_info.type = XR_TYPE_FRAME_BEGIN_INFO;
    if (!CheckXr(xrBeginFrame(state.session, &begin_info), "xrBeginFrame")) {
        return false;
    }

    std::array<XrView, kViewCount> views{};
    for (auto& view : views) {
        view.type = XR_TYPE_VIEW;
    }
    XrViewLocateInfo locate_info{};
    locate_info.type = XR_TYPE_VIEW_LOCATE_INFO;
    locate_info.viewConfigurationType =
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate_info.displayTime = frame_state.predictedDisplayTime;
    locate_info.space = state.local_space;
    XrViewState view_state{};
    view_state.type = XR_TYPE_VIEW_STATE;
    std::uint32_t located_count = 0;
    const XrResult locate_result = xrLocateViews(
        state.session, &locate_info, &view_state, views.size(), &located_count,
        views.data());

    bool submit_projection = frame_state.shouldRender == XR_TRUE &&
                             XR_SUCCEEDED(locate_result) &&
                             located_count == kViewCount &&
                             (view_state.viewStateFlags &
                              XR_VIEW_STATE_POSITION_VALID_BIT) != 0 &&
                             (view_state.viewStateFlags &
                              XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
    const bool tracking_active=submit_projection&&state.session_state==XR_SESSION_STATE_FOCUSED&&
        (view_state.viewStateFlags&XR_VIEW_STATE_POSITION_TRACKED_BIT)&&
        (view_state.viewStateFlags&XR_VIEW_STATE_ORIENTATION_TRACKED_BIT);
    if(state.local_space_change_time!=0&&frame_state.predictedDisplayTime>=state.local_space_change_time){
        state.local_space_change_time=0;state.rebase_head=state.have_world_head;
        state.cinematic_reference_valid=false;state.startup_anchor_valid=false;
        state.sprint_enabled=false;state.gesture->Reset();state.scene.SetWandDrawing(false);
        state.last_predicted_time=0;
        HPVR_LOGI("[hpvr.quest.locomotion] status=PRESERVE_WORLD reason=LOCAL_CHANGE");
    }
    if(!tracking_active){state.last_predicted_time=0;state.rebase_head=state.have_world_head;state.recenter_chord.Reset();}
    state.scene.SetTrackingActive(tracking_active&&scene_visible);
    bool frame_error = false;
    // Loading art needs its anchor before the scene becomes GPU-ready.
    if(submit_projection&&!state.startup_anchor_valid){
        ViewPose loading_head{{(views[0].pose.position.x+views[1].pose.position.x)*.5F,
            (views[0].pose.position.y+views[1].pose.position.y)*.5F,
            (views[0].pose.position.z+views[1].pose.position.z)*.5F},
            {views[0].pose.orientation.x,views[0].pose.orientation.y,views[0].pose.orientation.z,views[0].pose.orientation.w}};
        state.startup_anchor_valid=BuildTheaterPose(loading_head,&state.startup_pose);
    }
    std::array<Matrix4, kViewCount> view_projections{};
    std::array<Matrix4, kViewCount> wand_mvps{};
    std::array<float, 4> wand_color{1.0F, 1.0F, 1.0F, 1.0F};
    bool wand_tracked = false;
    if (submit_projection && scene_visible) {
        ViewPose head_pose{
            {(views[0].pose.position.x + views[1].pose.position.x) * 0.5F,
             (views[0].pose.position.y + views[1].pose.position.y) * 0.5F,
             (views[0].pose.position.z + views[1].pose.position.z) * 0.5F},
            {views[0].pose.orientation.x, views[0].pose.orientation.y,
             views[0].pose.orientation.z, views[0].pose.orientation.w}};
        LocomotionInput locomotion_input{};
        ViewPose wand_local{};
        bool cast_held = false;
        float delta_seconds =
            !tracking_active || state.last_predicted_time == 0
                ? 0.0F
                : std::clamp(
                      static_cast<float>(frame_state.predictedDisplayTime -
                                         state.last_predicted_time) *
                          1.0e-9F,
                      0.0F, 0.05F);
        state.last_predicted_time = tracking_active?frame_state.predictedDisplayTime:0;
        // Recovery uses the separately retained body, never a pose observed
        // while tracking was invalid or the headset was off.
        bool input_ok = (!tracking_active||state.locomotion.ObserveHead(head_pose)) &&
            SyncInput(state, frame_state.predictedDisplayTime, tracking_active,
                      &locomotion_input, &wand_local, &wand_tracked,
                      &cast_held);
        bool recentered=false;
        if(tracking_active&&input_ok&&(state.recenter_requested||(state.rebase_head&&state.have_world_head))){
            const bool recovering=state.rebase_head&&state.have_world_head;
            const auto capsule=recovering?state.last_world_capsule:state.locomotion.CapsuleCenter();
            float yaw=state.locomotion.yaw_radians();
            if(recovering){
                Matrix4 local{},world{};
                input_ok=BuildRigidTransform(head_pose,&local)&&BuildRigidTransform(state.last_world_head,&world);
                if(input_ok)yaw=std::atan2(world[8],world[10])-std::atan2(local[8],local[10]);
            }
            if(input_ok&&state.locomotion.RecenterToCapsule(capsule,yaw)){
                state.rebase_head=false;recentered=true;
                state.scene.ResetMovementContinuity();
                state.cinematic_reference_valid=false;state.startup_anchor_valid=false;
                state.gesture->Reset();state.scene.SetWandDrawing(false);
                state.sprint_enabled=false;state.input_release_pending=true;
                locomotion_input={};cast_held=false;wand_tracked=false;state.jump_held=false;
                delta_seconds=0;state.last_predicted_time=0;
                HPVR_LOGI("[hpvr.quest.recenter] reason=%s body=(%.3f,%.3f,%.3f) height=CALIBRATED progress=UNCHANGED",
                    state.recenter_requested?"L3_R3":"TRACKING_RETURN",capsule[0],capsule[1],capsule[2]);
            }else{input_ok=false;}
        }
        if(!tracking_active){locomotion_input={};cast_held=false;wand_tracked=false;}
        ViewPose menu_head{};
        if(tracking_active&&input_ok && state.locomotion.MapPose(head_pose,&menu_head)){
            if(state.vr_menu_requested)state.scene.ToggleVrMenu();
            const bool was_menu=state.scene.IsFrontEndVisible();
            if(!cast_held)state.ui_trigger_consumed=false;
            state.scene.UpdateFrontEnd(locomotion_input,cast_held&&!state.ui_trigger_consumed,state.back_held,menu_head,
                state.locomotion.yaw_radians());
            if(cast_held&&was_menu&&!state.scene.IsFrontEndVisible())state.ui_trigger_consumed=true;
            if(state.ui_trigger_consumed)cast_held=false;
            std::array<float,3> restored{};float restored_yaw=0;
            if(state.scene.ConsumePlayerPlacement(&restored,&restored_yaw))
                input_ok=state.locomotion.RestoreHead(restored,restored_yaw);
        }
        if (input_ok && (state.scene.IsCutscenePlaying() || state.scene.IsFrontEndVisible())) {
            locomotion_input = {};
            wand_tracked = false;
            cast_held = false;
        }
        const bool basic_held=cast_held;
        state.scene.UpdateJumpInput(tracking_active&&state.jump_held,delta_seconds);
        locomotion_input.physics_active=tracking_active&&input_ok&&!recentered&&state.scene.NeedsPhysicsTick()&&
            !state.scene.IsCutscenePlaying()&&!state.scene.IsWorldPaused();
        if(!state.scene.WantsGesture())state.gesture->Reset();
        state.gesture->SetGameplayMode(!state.scene.IsGestureLesson());
        state.gesture->SetLessonDifficulty(state.scene.GetVrSettings().relaxed_lesson);
        const auto turning = state.scene.GetVrSettings();
        locomotion_input.smooth_turn = turning.turning_mode == TurningMode::Smooth;
        locomotion_input.smooth_turn_degrees = static_cast<float>(turning.smooth_turn_speed);
        if (!input_ok || !state.locomotion.Tick(
                locomotion_input, delta_seconds,
                ResolveSceneLocomotion, &state.scene)) {
            HPVR_LOGE("[hpvr.quest.input] status=FRAME_REJECTED");
            submit_projection = false;
            frame_error = true;
        }
        ViewPose hud_head{};
        if(tracking_active&&input_ok&&state.locomotion.MapPose(head_pose,&hud_head)){
            state.scene.UpdatePlayerPose(hud_head,state.locomotion.yaw_radians(),state.locomotion.CapsuleCenter());
            state.scene.UpdateHudPose(hud_head);state.last_world_head=hud_head;state.have_world_head=true;
            state.last_world_capsule=state.locomotion.CapsuleCenter();
        }
        ViewPose wand_world{};
        Matrix4 wand_model{};
        if (submit_projection && wand_tracked &&
            (!state.locomotion.MapPose(wand_local, &wand_world) ||
             !BuildRigidTransform(wand_world, &wand_model))) {
            wand_tracked = false;
        }
        state.scene.UpdateBasicCast(wand_world,submit_projection&&wand_tracked,basic_held,delta_seconds);
        if(!state.gesture->SelectSpell(state.scene.ActiveGestureSpell()))state.gesture->Reset();
        state.gesture->SetLessonRound(state.scene.LessonRound());
        std::array<float,3> voice_point{};
        const bool voice_allowed=submit_projection&&tracking_active&&wand_tracked&&
            state.scene.VoiceCaptureAllowed();
        const auto target=voice_allowed?state.scene.VoiceTarget(&voice_point):0;
        const auto voice_spell=state.scene.ActiveGestureSpell()==GestureSpell::Alohomora?
            VoiceSpell::Alohomora:state.scene.ActiveGestureSpell()==GestureSpell::Wingardium?
            VoiceSpell::Wingardium:VoiceSpell::Flipendo;
        if(target!=state.voice_target||voice_spell!=state.voice_spell){
            state.voice_spell=voice_spell;
            state.voice_target=target;state.voice_target_point=voice_point;
            if(++state.voice_generation>kVoiceMaxGeneration)state.voice_generation=1;
        }
        const VoiceCastArm voice_arm{state.voice_generation,state.scene.GetVrSettings().voice_cast,
            state.voice_permission,tracking_active,voice_allowed,target>0,voice_spell};
        state.voice.SetListening(voice_arm);
        VoiceCastEvent voice_event{};
        if(state.voice.PollEvent(voice_arm,&voice_event)){
            if(state.scene.DispatchVoiceCast(target,state.voice_target_point,wand_world)){
                state.gesture->Reset();
            }else{
                // The worker has consumed this generation. A transient game
                // rejection must not leave the same held target unusable.
                if(++state.voice_generation>kVoiceMaxGeneration)state.voice_generation=1;
                HPVR_LOGI("[hpvr.quest.voice] status=GAMEPLAY_RETRY target=%d",target);
            }
            // Disarm this result without closing the warm microphone between
            // consecutive casts. The next frame chooses the fresh target.
            auto disarmed=voice_arm;disarmed.generation=state.voice_generation;disarmed.target_locked=false;
            state.voice.SetListening(disarmed);
        }
        GestureSample gesture_sample{};
        gesture_sample.predicted_display_time_ns =
            frame_state.predictedDisplayTime;
        gesture_sample.tracked = wand_tracked && state.scene.WantsGesture();
        gesture_sample.cast_held = cast_held && state.scene.GestureTargetLocked();
        if (wand_tracked) {
            gesture_sample.aim_direction = {
                -wand_model[8], -wand_model[9], -wand_model[10]};
            gesture_sample.tip = {
                wand_model[12] + gesture_sample.aim_direction[0] * 0.34F,
                wand_model[13] + gesture_sample.aim_direction[1] * 0.34F,
                wand_model[14] + gesture_sample.aim_direction[2] * 0.34F};
        }
        state.gesture->Advance(delta_seconds);
        ViewPose before_camera;bool before_first_person=false;
        (void)state.scene.GetCinematicCameraPose(&before_camera,&before_first_person);
        if(before_first_person!=state.cinematic_first_person)state.cinematic_reference_valid=false;
        state.scene.UpdateExitTracking(head_pose,state.cinematic_reference,tracking_active&&state.cinematic_reference_valid);
        state.scene.Advance(delta_seconds);
        // Apply the actor's final position on this same frame, avoiding a flash
        // back to the old seated VR body when the exit camera is released.
        std::array<float,3> final_position{},transport{};float final_yaw=0;
        const bool placed=tracking_active&&state.scene.ConsumePlayerPlacement(&final_position,&final_yaw);
        const bool carried=state.scene.ConsumePlayerTransport(&transport);
        if(tracking_active&&(placed||carried)){
            if(placed?!state.locomotion.RestoreHead(final_position,final_yaw):
                !state.locomotion.TranslateWorld(transport))frame_error=true;
            if(state.locomotion.MapPose(head_pose,&hud_head)){
                state.scene.UpdatePlayerPose(hud_head,state.locomotion.yaw_radians(),state.locomotion.CapsuleCenter());
                state.scene.UpdateHudPose(hud_head);state.last_world_head=hud_head;
                state.last_world_capsule=state.locomotion.CapsuleCenter();
            }
            // Update rendering only; this frame's input/voice events have
            // already been consumed. The held wand travels with the platform.
            if(wand_tracked&&(!state.locomotion.MapPose(wand_local,&wand_world)||!BuildRigidTransform(wand_world,&wand_model)))
                wand_tracked=false;
        }
        ViewPose cinematic_camera{};
        bool cinematic_first_person=false;
        const bool cinematic_camera_active =
            state.scene.GetCinematicCameraPose(&cinematic_camera,&cinematic_first_person);
        const bool reanchor=cinematic_camera_active&&(!state.cinematic_reference_valid||
            cinematic_first_person!=state.cinematic_first_person);
        if(reanchor){
            state.cinematic_reference=head_pose;state.cinematic_reference_valid=true;
        }else if(!cinematic_camera_active)state.cinematic_reference_valid=false;
        if(cinematic_first_person!=state.cinematic_first_person)
            HPVR_LOGI("[hpvr.quest.camera] first_person=%u menu=LIVE rig=SCRIPTED_6DOF",cinematic_first_person?1U:0U);
        state.cinematic_first_person=cinematic_first_person;
        ViewPose presentation_head;
        const bool presentation_ok=cinematic_camera_active?
            MapCinematicEye(head_pose,head_pose,cinematic_camera,state.cinematic_reference,&presentation_head):
            state.locomotion.MapPose(head_pose,&presentation_head);
        if(tracking_active&&presentation_ok)
            state.scene.UpdateFrontPresentation(presentation_head,cinematic_camera_active?&cinematic_camera:nullptr,
                cinematic_first_person,reanchor||recentered);
        const std::uint32_t accepted_before = state.gesture->accepted_count();
        const std::uint32_t rejected_before = state.gesture->rejected_count();
        if (submit_projection && !state.gesture->Observe(gesture_sample)) {
            HPVR_LOGE("[hpvr.quest.gesture] status=FRAME_REJECTED");
            submit_projection = false;
            frame_error = true;
        }
        if (submit_projection &&
            !state.gesture->BuildGuide(&state.gesture_guide)) {
            HPVR_LOGE("[hpvr.quest.gesture.guide] status=FRAME_REJECTED");
            submit_projection = false;
            frame_error = true;
        }
        state.scene.SetWandDrawing(
            submit_projection &&
            state.gesture->visual_state() == GestureVisualState::Recording);
        if(!state.scene.WantsGesture())state.gesture_guide.visible=false;
        if(!state.scene.IsGestureLesson()){
            state.gesture_guide.template_points.clear();
            if(!ShowsGestureTrace(state.scene.GetVrSettings().casting_mode))state.gesture_guide.visible=false;
        }
        if (state.gesture->accepted_count() != accepted_before ||
            state.gesture->rejected_count() != rejected_before) {
            HPVR_LOGI(
                "[hpvr.quest.gesture.result] attempt=%u outcome=%s "
                "score=%.6f threshold=%.6f plane=AIM_FACING "
                "policy=SELECTED_VR_DIFFICULTY",
                state.gesture->attempt_count(),
                state.gesture->accepted_count() != accepted_before
                    ? "ACCEPTED"
                    : "REJECTED",
                state.gesture->last_score(), state.gesture->threshold());
            if(state.gesture->rejected_count()!=rejected_before)state.scene.RejectLessonGesture();
        }
        FlipendoEvent spell_event{};
        const auto diagnostic=state.gesture->diagnostics();
        if(diagnostic.serial&&diagnostic.serial!=state.gesture_diagnostic_serial){
            state.gesture_diagnostic_serial=diagnostic.serial;
            std::ostringstream points;
            points<<std::fixed<<std::setprecision(3);
            for(std::size_t i=0;i<diagnostic.projected_point_count;++i)
                points<<diagnostic.projected_points[i][0]<<','<<diagnostic.projected_points[i][1]<<';';
            HPVR_LOGI("[hpvr.quest.gesture.shape] spell=%u attempt=%u reason=%s samples=%u seconds=%.3f "
                "extent=(%.3f,%.3f) depth=%.3f length=%.3f score=%.3f threshold=%.3f points=%s",
                static_cast<unsigned>(state.gesture->selected_spell()),diagnostic.attempt,diagnostic.reason,diagnostic.sample_count,diagnostic.duration_seconds,
                diagnostic.projected_extent[0],diagnostic.projected_extent[1],diagnostic.depth_span_meters,
                diagnostic.path_length,diagnostic.score,diagnostic.threshold,points.str().c_str());
        }
        if (state.gesture->ConsumeEvent(&spell_event)) {
            HPVR_LOGI(
                "[hpvr.quest.spell] status=FLIPENDO_ACCEPTED serial=%llu "
                "score=%.6f threshold=%.6f target_lock=PRESS "
                "origin=(%.3f,%.3f,%.3f) direction=(%.3f,%.3f,%.3f)",
                static_cast<unsigned long long>(spell_event.serial),
                spell_event.score, spell_event.threshold,
                spell_event.locked_origin[0],
                spell_event.locked_origin[1],
                spell_event.locked_origin[2],
                spell_event.locked_direction[0],
                spell_event.locked_direction[1],
                spell_event.locked_direction[2]);
            if (!state.scene.DispatchFlipendo(spell_event)) {
                submit_projection = false;
                frame_error = true;
            }
        }
        switch (state.gesture->visual_state()) {
            case GestureVisualState::Recording:
                wand_color = {2.6F, 1.2F, 0.25F, 1.0F};
                break;
            case GestureVisualState::Accepted:
                wand_color = {0.35F, 3.2F, 0.55F, 1.0F};
                break;
            case GestureVisualState::Rejected:
                wand_color = {3.2F, 0.35F, 0.35F, 1.0F};
                break;
            case GestureVisualState::Canceled:
                wand_color = {2.4F, 1.1F, 0.2F, 1.0F};
                break;
            case GestureVisualState::Idle:
                break;
        }
        for (std::uint32_t eye = 0; eye < kViewCount; ++eye) {
            const ViewPose local_eye{
                {views[eye].pose.position.x, views[eye].pose.position.y,
                 views[eye].pose.position.z},
                {views[eye].pose.orientation.x, views[eye].pose.orientation.y,
                 views[eye].pose.orientation.z,
                 views[eye].pose.orientation.w}};
            ViewPose world_eye{};
            const ViewFov fov{views[eye].fov.angleLeft,
                              views[eye].fov.angleRight,
                              views[eye].fov.angleDown,
                              views[eye].fov.angleUp};
            if (!state.locomotion.MapPose(local_eye, &world_eye)) {
                HPVR_LOGE(
                    "[hpvr.quest.frame] status=INVALID_WORLD_EYE eye=%u",
                    eye);
                submit_projection = false;
                frame_error = true;
                break;
            }
            if (cinematic_camera_active) {
                if(!MapCinematicEye(local_eye,head_pose,cinematic_camera,state.cinematic_reference,&world_eye)){
                    submit_projection=false;frame_error=true;break;
                }
            }
            if (!BuildViewProjection(world_eye, fov, 0.0F, 0.05F, 250.0F,
                                     &view_projections[eye])) {
                HPVR_LOGE(
                    "[hpvr.quest.frame] status=INVALID_VIEW_PROJECTION eye=%u",
                    eye);
                submit_projection = false;
                frame_error = true;
                break;
            }
            if (wand_tracked) {
                wand_mvps[eye] =
                    MultiplyMatrices(view_projections[eye], wand_model);
            }
        }
    }

    std::uint32_t image_index = 0;
    bool image_acquired = false;
    bool image_waited = false;
    if (submit_projection) {
        XrSwapchainImageAcquireInfo acquire_info{};
        acquire_info.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
        submit_projection = CheckXr(
            xrAcquireSwapchainImage(state.swapchain, &acquire_info, &image_index),
            "xrAcquireSwapchainImage");
        image_acquired = submit_projection;
        frame_error = !submit_projection;
    }
    if (submit_projection) {
        XrSwapchainImageWaitInfo image_wait_info{};
        image_wait_info.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
        image_wait_info.timeout = XR_INFINITE_DURATION;
        submit_projection = CheckXr(
            xrWaitSwapchainImage(state.swapchain, &image_wait_info),
            "xrWaitSwapchainImage");
        image_waited = submit_projection;
        frame_error = frame_error || !submit_projection;
    }

    const auto vr=state.scene.IsGpuReady()?state.scene.GetVrSettings():VrSettings{};
    if(state.requested_gpu_boost!=int(vr.gpu_boost)&&
       (state.session_state==XR_SESSION_STATE_FOCUSED||state.session_state==XR_SESSION_STATE_VISIBLE)){
        state.requested_gpu_boost=int(vr.gpu_boost);
        const auto level=vr.gpu_boost?XR_PERF_SETTINGS_LEVEL_BOOST_EXT:XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT;
        const auto result=state.set_performance?state.set_performance(state.session,XR_PERF_SETTINGS_DOMAIN_GPU_EXT,level):XR_ERROR_FUNCTION_UNSUPPORTED;
        HPVR_LOGI("[hpvr.quest.gpu_boost] enabled=%d level=%d result=%d thermal_policy=RUNTIME",int(vr.gpu_boost),int(level),int(result));
    }
    if(state.scene.IsGpuReady())state.scene.SetSupportedRefreshRates(state.refresh_rates);
    if(state.scene.IsGpuReady()&&state.request_refresh&&vr.refresh_rate!=state.requested_refresh&&
       (state.session_state==XR_SESSION_STATE_FOCUSED||state.session_state==XR_SESSION_STATE_VISIBLE)){
        state.requested_refresh=vr.refresh_rate;
        if(std::ranges::find(state.refresh_rates,vr.refresh_rate)!=state.refresh_rates.end()){
            const auto result=state.request_refresh(state.session,static_cast<float>(vr.refresh_rate));
            HPVR_LOGI("[hpvr.quest.refresh] requested=%d result=%d",vr.refresh_rate,static_cast<int>(result));
        }else HPVR_LOGI("[hpvr.quest.refresh] requested=%d status=UNSUPPORTED_KEEP_RUNTIME",vr.refresh_rate);
    }
    const unsigned render_width=VrRenderExtent(state.recommended_width,vr.render_scale,state.width);
    const unsigned render_height=VrRenderExtent(state.recommended_height,vr.render_scale,state.height);
    if (submit_projection && image_index >= state.command_buffers.size()) {
        HPVR_LOGE("[hpvr.quest.frame] status=INVALID_IMAGE_INDEX index=%u",
                  image_index);
        submit_projection = false;
        frame_error = true;
    }
    if (submit_projection) {
        VkFence fence = state.fences[image_index];
        submit_projection =
            CheckVk(vkWaitForFences(state.device, 1, &fence, VK_TRUE, UINT64_MAX),
                    "vkWaitForFences") &&
            CheckVk(vkResetCommandBuffer(state.command_buffers[image_index], 0),
                    "vkResetCommandBuffer");
        frame_error = frame_error || !submit_projection;
    }
    if (submit_projection) {
        VkCommandBuffer command_buffer = state.command_buffers[image_index];
        if(scene_visible)state.scene.PrepareReflections(view_projections[0],render_width,render_height,tracking_active);
        VkCommandBufferBeginInfo command_begin{};
        command_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        command_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        submit_projection = CheckVk(
            vkBeginCommandBuffer(command_buffer, &command_begin),
            "vkBeginCommandBuffer");
        frame_error = frame_error || !submit_projection;

        VkClearColorValue clear_color{};
        if(submit_projection&&state.timing_pool)vkCmdResetQueryPool(command_buffer,state.timing_pool,0,8);
        const bool clean_history=scene_visible&&state.scene.ReflectionCaptureEnabled();
        std::array<double,2> eye_cpu_ms{};
        const auto draw_overlay=[&](unsigned eye){
            if(!scene_visible)return;
            state.scene.RecordGestureGuideDraw(command_buffer,render_width,render_height,view_projections[eye],state.gesture_guide);
            if(wand_tracked)state.scene.RecordWandDraw(command_buffer,render_width,render_height,wand_mvps[eye],wand_color);
            state.scene.RecordHudDraw(command_buffer,view_projections[eye]);
            state.scene.RecordDeathFade(command_buffer);
            state.scene.RecordFrontDraw(command_buffer,view_projections[eye]);
        };
        clear_color.float32[3] = 1.0F;
        for (std::uint32_t eye = 0; submit_projection && eye < kViewCount;
             ++eye) {
            const double eye_cpu_start=ThreadMs();
            if(state.timing_pool)vkCmdWriteTimestamp(command_buffer,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,state.timing_pool,eye*2);
            if(scene_visible)state.scene.RecordMirrorCapture(command_buffer,view_projections[eye]);
            std::array<VkClearValue, 2> clear_values{};
            clear_values[0].color = clear_color;
            if (scene_visible) {
                clear_values[0].color.float32[0] = 0.008F;
                clear_values[0].color.float32[1] = 0.012F;
                clear_values[0].color.float32[2] = 0.025F;
                clear_values[0].color.float32[3] = 1.0F;
            }
            clear_values[1].depthStencil = {1.0F, 0};
            VkRenderPassBeginInfo render_begin{};
            render_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            render_begin.renderPass = state.render_pass;
            render_begin.framebuffer = state.framebuffers[image_index][eye];
            render_begin.renderArea.extent = {render_width, render_height};
            render_begin.clearValueCount = clear_values.size();
            render_begin.pClearValues = clear_values.data();
            vkCmdBeginRenderPass(command_buffer, &render_begin,
                                 VK_SUBPASS_CONTENTS_INLINE);
            if(scene_visible){
                state.scene.RecordDraw(command_buffer,render_width,render_height,view_projections[eye],image_index*2U+eye);
                state.scene.RecordSpellDraw(command_buffer,render_width,render_height,view_projections[eye]);
            }
            if(!DelayReflectionOverlay(clean_history,eye))draw_overlay(eye);
            vkCmdEndRenderPass(command_buffer);
            if(state.timing_pool)vkCmdWriteTimestamp(command_buffer,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state.timing_pool,eye*2+1);
            eye_cpu_ms[eye]=ThreadMs()-eye_cpu_start;
        }
        if (submit_projection) {
            if(state.timing_pool)vkCmdWriteTimestamp(command_buffer,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,state.timing_pool,4);
            if(scene_visible)state.scene.CaptureReflections(command_buffer,state.swapchain_images[image_index].image,state.depth_images[image_index],view_projections[0],render_width,render_height);
            if(state.timing_pool)vkCmdWriteTimestamp(command_buffer,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state.timing_pool,5);
            // Both eyes finished sampling OLD history. Capture before drawing
            // left-eye UI; no feedback hole and no same-frame matrix mismatch.
            if(state.timing_pool)vkCmdWriteTimestamp(command_buffer,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,state.timing_pool,6);
            if(DelayReflectionOverlay(clean_history,0)){
                const double overlay_cpu_start=ThreadMs();
                VkRenderPassBeginInfo overlay{};overlay.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                overlay.renderPass=state.overlay_pass;overlay.framebuffer=state.framebuffers[image_index][0];
                overlay.renderArea.extent={render_width,render_height};
                vkCmdBeginRenderPass(command_buffer,&overlay,VK_SUBPASS_CONTENTS_INLINE);
                draw_overlay(0);vkCmdEndRenderPass(command_buffer);
                eye_cpu_ms[0]+=ThreadMs()-overlay_cpu_start;
            }
            if(state.timing_pool)vkCmdWriteTimestamp(command_buffer,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,state.timing_pool,7);
            for(unsigned eye=0;eye<2;++eye)Smooth(state.performance.cpu_eye[eye],eye_cpu_ms[eye]);
            submit_projection = CheckVk(vkEndCommandBuffer(command_buffer),
                                        "vkEndCommandBuffer");
            frame_error = frame_error || !submit_projection;
        }
        if (submit_projection) {
            VkSubmitInfo submit_info{};
            submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit_info.commandBufferCount = 1;
            submit_info.pCommandBuffers = &command_buffer;
            Smooth(state.performance.cpu_ms,ThreadMs()-cpu_start);
            submit_projection =
                CheckVk(vkResetFences(state.device, 1,
                                      &state.fences[image_index]),
                        "vkResetFences") &&
                CheckVk(vkQueueSubmit(state.queue, 1, &submit_info,
                                      state.fences[image_index]),
                        "vkQueueSubmit") &&
                CheckVk(vkWaitForFences(state.device, 1,
                                        &state.fences[image_index], VK_TRUE,
                                        UINT64_MAX),
                        "vkWaitForFences(submit)");
            if(submit_projection&&state.timing_pool){
                std::array<std::uint64_t,8> stamps{};
                if(vkGetQueryPoolResults(state.device,state.timing_pool,0,8,sizeof(stamps),stamps.data(),sizeof(std::uint64_t),VK_QUERY_RESULT_64_BIT)==VK_SUCCESS){
                    const auto ms=[&](unsigned a,unsigned b){return TimestampMilliseconds(stamps[a],stamps[b],state.timestamp_bits,state.timestamp_period);};
                    Smooth(state.performance.gpu_ms,ms(0,7));Smooth(state.performance.copy_ms,ms(4,5));
                    Smooth(state.performance.gpu_eye[0],ms(0,1)+(clean_history?ms(6,7):0));
                    Smooth(state.performance.gpu_eye[1],ms(2,3));
                }
            }
            frame_error = frame_error || !submit_projection;
        }
    }

    if (image_waited) {
        XrSwapchainImageReleaseInfo release_info{};
        release_info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        if (!CheckXr(xrReleaseSwapchainImage(state.swapchain, &release_info),
                     "xrReleaseSwapchainImage")) {
            submit_projection = false;
            frame_error = true;
        }
    } else if (image_acquired) {
        frame_error = true;
    }

    std::array<XrCompositionLayerProjectionView, kViewCount> projection_views{};
    for (std::uint32_t eye = 0; eye < kViewCount; ++eye) {
        projection_views[eye].type =
            XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        projection_views[eye].pose = views[eye].pose;
        projection_views[eye].fov = views[eye].fov;
        projection_views[eye].subImage.swapchain = state.swapchain;
        projection_views[eye].subImage.imageRect.offset = {0, 0};
        projection_views[eye].subImage.imageRect.extent = {
            static_cast<std::int32_t>(render_width),
            static_cast<std::int32_t>(render_height),
        };
        projection_views[eye].subImage.imageArrayIndex = eye;
    }
    XrCompositionLayerProjection projection{};
    projection.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    projection.space = state.local_space;
    projection.viewCount = kViewCount;
    projection.views = projection_views.data();
    XrCompositionLayerQuad startup{};startup.type=XR_TYPE_COMPOSITION_LAYER_QUAD;
    startup.space=state.startup_space;startup.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    startup.subImage.swapchain=state.startup_swapchain;startup.subImage.imageRect.extent={640,480};
    const auto& anchor=state.startup_pose;
    startup.pose.orientation={anchor.orientation[0],anchor.orientation[1],anchor.orientation[2],anchor.orientation[3]};
    startup.pose.position={anchor.position[0],anchor.position[1],anchor.position[2]};startup.size={2.8F,2.1F};
    const bool show_startup=!scene_visible&&state.startup_anchor_valid&&state.startup_swapchain!=XR_NULL_HANDLE;
    ViewPose icon_pose{};
    const bool show_loading_icon=show_startup&&state.loading_icon_swapchain!=XR_NULL_HANDLE&&
        BuildLoadingIconPose(anchor,&icon_pose);
    XrCompositionLayerQuad loading_icon{};loading_icon.type=XR_TYPE_COMPOSITION_LAYER_QUAD;
    loading_icon.layerFlags=XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT|XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT;
    loading_icon.space=state.startup_space;loading_icon.eyeVisibility=XR_EYE_VISIBILITY_BOTH;
    loading_icon.subImage.swapchain=state.loading_icon_swapchain;
    loading_icon.subImage.imageRect.offset={static_cast<std::int32_t>(LoadingIconFrame(frame_state.predictedDisplayTime)*kLoadingIconSize),0};
    loading_icon.subImage.imageRect.extent={kLoadingIconSize,kLoadingIconSize};
    loading_icon.pose.orientation={icon_pose.orientation[0],icon_pose.orientation[1],icon_pose.orientation[2],icon_pose.orientation[3]};
    loading_icon.pose.position={icon_pose.position[0],icon_pose.position[1],icon_pose.position[2]};loading_icon.size={.24F,.24F};
    const XrCompositionLayerBaseHeader* layers[] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection),
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&startup),
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&loading_icon)};

    XrFrameEndInfo end_info{};
    end_info.type = XR_TYPE_FRAME_END_INFO;
    end_info.displayTime = frame_state.predictedDisplayTime;
    end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end_info.layerCount = submit_projection ? (show_loading_icon?3U:show_startup?2U:1U) : 0U;
    end_info.layers = submit_projection ? layers : nullptr;
    if (!CheckXr(xrEndFrame(state.session, &end_info), "xrEndFrame")) {
        return false;
    }
    if (submit_projection) {
        ++state.submitted_frames;
        const double now=WallMs();if(state.perf_start==0)state.perf_start=now;
        ++state.perf_frames;
        if(now-state.perf_start>=1000){
            auto& p=state.performance;p.fps=float(state.perf_frames*1000/(now-state.perf_start));p.frame_ms=1000/p.fps;
            p.width=render_width;p.height=render_height;p.scale=state.scene.IsGpuReady()?state.scene.GetVrSettings().render_scale:100;
            p.cpu_util=p.gpu_util=p.app_cpu_ms=p.app_gpu_ms=-1;
            if(state.set_metrics&&!state.metrics_enabled){
                XrPerformanceMetricsStateMETA m{};m.type=XR_TYPE_PERFORMANCE_METRICS_STATE_META;m.enabled=XR_TRUE;
                state.metrics_enabled=XR_SUCCEEDED(state.set_metrics(state.session,&m));
            }
            if(state.metrics_enabled&&state.query_metric){
                const std::array<float*,4> values{&p.cpu_util,&p.gpu_util,&p.app_cpu_ms,&p.app_gpu_ms};
                for(unsigned i=0;i<4;++i)if(state.metric_paths[i]){
                    XrPerformanceMetricsCounterMETA m{};m.type=XR_TYPE_PERFORMANCE_METRICS_COUNTER_META;
                    if(XR_FAILED(state.query_metric(state.session,state.metric_paths[i],&m)))continue;
                    if(m.counterUnit!=(i<2?XR_PERFORMANCE_METRICS_COUNTER_UNIT_PERCENTAGE_META:XR_PERFORMANCE_METRICS_COUNTER_UNIT_MILLISECONDS_META))continue;
                    float value=-1;
                    if(m.counterFlags&XR_PERFORMANCE_METRICS_COUNTER_FLOAT_VALUE_VALID_BIT_META)value=m.floatValue;
                    else if(m.counterFlags&XR_PERFORMANCE_METRICS_COUNTER_UINT_VALUE_VALID_BIT_META)value=float(m.uintValue);
                    if(std::isfinite(value)&&value>=0&&(i>=2||value<=100))*values[i]=value;
                }
            }
            state.scene.SetPerformance(p);
            HPVR_LOGI("[hpvr.quest.perf] fps=%.1f cpu_ms=%.2f gpu_ms=%.2f left_gpu_ms=%.2f right_gpu_ms=%.2f copy_gpu_ms=%.2f cpu_util=%.1f gpu_util=%.1f scale=%d extent=%ux%u",p.fps,p.cpu_ms,p.gpu_ms,p.gpu_eye[0],p.gpu_eye[1],p.copy_ms,p.cpu_util,p.gpu_util,p.scale,p.width,p.height);
            state.perf_start=now;state.perf_frames=0;
        }
        if (state.submitted_frames == 1 ||
            state.submitted_frames % 300 == 0) {
            HPVR_LOGI(
                "[hpvr.quest.frame] status=SUBMITTED count=%llu views=2 "
                "content=%s vertices=%u texture_layers=%u "
                "wand=%s wand_vertices=%u input_syncs=%llu "
                "move_frames=%llu snap_turns=%llu "
                "collision_blocked=%llu grounded=%llu "
                "vertical_adjustment_m=%.3f animation_frame=%u/%u "
                "trigger=%.3f "
                "gesture_attempts=%u accepted=%u rejected=%u "
                "guide=%s template_points=%zu trail_points=%zu "
                "predicted_time_ns=%lld",
                static_cast<unsigned long long>(state.submitted_frames),
                state.scene.IsGpuReady() ? "HOGWARTS" : "WARNER_LOADING",
                state.scene.VertexCount(), state.scene.TextureLayerCount(),
                wand_tracked ? "TRACKED" : "UNAVAILABLE",
                state.scene.WandVertexCount(),
                static_cast<unsigned long long>(state.input_syncs),
                state.locomotion.move_frames(),
                state.locomotion.snap_turns(),
                state.locomotion.blocked_substeps(),
                state.locomotion.grounded_substeps(),
                state.locomotion.vertical_adjustment_m(),
                state.scene.CurrentAnimationFrame(),
                state.scene.AnimationFrameCount(), state.trigger_value,
                state.gesture->attempt_count(),
                state.gesture->accepted_count(),
                state.gesture->rejected_count(),
                state.gesture_guide.visible ? "VISIBLE" : "HIDDEN",
                state.gesture_guide.template_points.size(),
                state.gesture_guide.trail_points.size(),
                static_cast<long long>(frame_state.predictedDisplayTime));
        }
    }
    return !frame_error;
}

bool XrVulkanSmoke::IsRunning() const {
    return state_->running;
}
void XrVulkanSmoke::UpdateVoicePlatform(ANativeActivity* activity){
    auto& s=*state_;
    if(s.session_state!=XR_SESSION_STATE_FOCUSED||!IsRunning())s.voice.SetListening({});
    const bool enabled=s.scene.GetVrSettings().voice_cast;
    const bool request=enabled&&!s.voice_was_enabled;
    s.voice_was_enabled=enabled;
    const double now=WallMs();
    if(!request&&now-s.voice_platform_poll<500)return;
    s.voice_platform_poll=now;
    if(!activity||!activity->vm||!activity->clazz)return;
    JNIEnv* env=nullptr;bool attached=false;
    jint status=activity->vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6);
    if(status==JNI_EDETACHED){status=activity->vm->AttachCurrentThread(&env,nullptr);attached=status==JNI_OK;}
    if(status!=JNI_OK||!env)return;
    if(env->PushLocalFrame(8)!=JNI_OK){env->ExceptionClear();if(attached)activity->vm->DetachCurrentThread();return;}
    const auto cls=env->GetObjectClass(activity->clazz);
    const auto permission=cls?env->GetMethodID(cls,"isVoicePermissionGranted","()Z"):nullptr;
    if(permission&&!env->ExceptionCheck())s.voice_permission=env->CallBooleanMethod(activity->clazz,permission)==JNI_TRUE;
    if(env->ExceptionCheck()){env->ExceptionClear();s.voice_permission=false;}
    if(cls&&request&&!s.voice_permission){
        const auto ask=env->GetMethodID(cls,"requestVoicePermission","()V");
        if(ask&&!env->ExceptionCheck())env->CallVoidMethod(activity->clazz,ask);
        if(env->ExceptionCheck())env->ExceptionClear();
    }
    if(cls&&!s.voice_configured&&enabled){
        const auto path_method=env->GetMethodID(cls,"getVoiceModelPath","()Ljava/lang/String;");
        auto path=path_method&&!env->ExceptionCheck()?static_cast<jstring>(env->CallObjectMethod(activity->clazz,path_method)):nullptr;
        if(path&&!env->ExceptionCheck()){
            const char* utf=env->GetStringUTFChars(path,nullptr);
            if(utf){if(*utf)s.voice_configured=s.voice.Configure(std::filesystem::path(utf));env->ReleaseStringUTFChars(path,utf);}
        }
        if(env->ExceptionCheck())env->ExceptionClear();
    }
    env->PopLocalFrame(nullptr);if(attached)activity->vm->DetachCurrentThread();
    s.scene.SetVoiceStatus(enabled&&!s.voice_permission?1U:static_cast<unsigned>(s.voice.status()));
    const auto microphone_error=s.voice.microphone_error();
    if(microphone_error!=s.voice_last_error){
        s.voice_last_error=microphone_error;
        HPVR_LOGI("[hpvr.quest.voice] status=MICROPHONE_STATE error=%d",microphone_error);
    }
    if(enabled&&s.voice_configured&&(s.voice_target>0||microphone_error!=0)&&now-s.voice_diagnostic_poll>=1000){
        s.voice_diagnostic_poll=now;
        const auto d=s.voice.Stats();
        HPVR_LOGI("[hpvr.quest.voice.input] target=%d generation=%llu status=%u attempts=%llu samples=%llu decoder_steps=%llu stream_renewals=%llu window=%u nonzero=%u peak=%u rms=%.1f clipped=%u keywords=%llu duration_rejects=%llu keyword_s=%.3f accepted=%llu stale=%llu retries=%llu read_zeroes=%llu errors=%llu decoder_errors=%llu error=%d waveform_log=NONE",
            s.voice_target,static_cast<unsigned long long>(d.generation),static_cast<unsigned>(s.voice.status()),
            static_cast<unsigned long long>(d.capture_attempts),static_cast<unsigned long long>(d.total_samples),
            static_cast<unsigned long long>(d.decoder_steps),static_cast<unsigned long long>(d.stream_renewals),d.input_window_samples,d.input_nonzero,d.input_peak,
            static_cast<double>(d.input_rms),d.input_clipped,static_cast<unsigned long long>(d.keyword_hits),
            static_cast<unsigned long long>(d.duration_rejects),static_cast<double>(d.last_keyword_seconds),
            static_cast<unsigned long long>(d.accepted_events),static_cast<unsigned long long>(d.stale_discards),
            static_cast<unsigned long long>(d.retries),static_cast<unsigned long long>(d.read_zeroes),
            static_cast<unsigned long long>(d.errors),static_cast<unsigned long long>(d.decoder_errors),d.last_error);
    }
}
bool XrVulkanSmoke::ConsumeCommunityRequest(){return state_->scene.ConsumeCommunityRequest();}

bool XrVulkanSmoke::HasSession() const {
    return state_->session != XR_NULL_HANDLE;
}

}  // namespace hpvr::quest
