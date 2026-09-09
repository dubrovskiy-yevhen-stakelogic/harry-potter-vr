#include <android/log.h>
#include <android_native_app_glue.h>
#include <jni.h>

#include <vulkan/vulkan.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "hpvr/hp1_gesture_c.h"
#include "hpvr/quest_demo.h"
#include "hpvr/quest_lifecycle.h"
#include "hpvr/wand_trajectory_c.h"
#include "xr_vulkan_smoke.h"

namespace {

constexpr char kLogTag[] = "HPVR.Quest";

#define HPVR_LOGI(...) __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)
#define HPVR_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kLogTag, __VA_ARGS__)

struct QuestHost {
    android_app* app = nullptr;
    hpvr::quest::HostLifecycle lifecycle{};
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system_id = XR_NULL_SYSTEM_ID;
    bool metrics_extension=false;
    hpvr::quest::XrVulkanSmoke stereo_smoke{};
};

bool CheckCommunityJni(JNIEnv* env,const char* operation,bool value_ok=true) {
    if (env->ExceptionCheck()) {
        // Includes ActivityNotFoundException when no browser can handle VIEW.
        // A dismissed/failed external intent must not poison the game thread.
        env->ExceptionClear();
        HPVR_LOGE("[hpvr.quest.community] status=FAILED operation=%s exception=YES",operation);
        return false;
    }
    if (!value_ok) {
        HPVR_LOGE("[hpvr.quest.community] status=FAILED operation=%s result=NULL",operation);
        return false;
    }
    return true;
}

// Called only after ConsumeCommunityRequest(), which represents an explicit
// OPEN DISCORD button click. Showing either demo notice never opens a browser.
bool OpenDemoCommunity(ANativeActivity* activity) {
    if (activity==nullptr || activity->vm==nullptr || activity->clazz==nullptr) {
        HPVR_LOGE("[hpvr.quest.community] status=FAILED operation=activity result=MISSING");
        return false;
    }
    struct JniScope {
        JavaVM* vm=nullptr;
        JNIEnv* env=nullptr;
        bool attached=false,frame=false;
        ~JniScope() {
            if (frame) env->PopLocalFrame(nullptr);
            if (attached && vm->DetachCurrentThread()!=JNI_OK)
                HPVR_LOGE("[hpvr.quest.community] status=FAILED operation=detach");
        }
    } jni;
    jni.vm=activity->vm;
    jint status=jni.vm->GetEnv(reinterpret_cast<void**>(&jni.env),JNI_VERSION_1_6);
    if (status==JNI_EDETACHED) {
        status=jni.vm->AttachCurrentThread(&jni.env,nullptr);
        jni.attached=status==JNI_OK;
    }
    if (status!=JNI_OK || jni.env==nullptr) {
        HPVR_LOGE("[hpvr.quest.community] status=FAILED operation=attach result=%d",status);
        return false;
    }
    JNIEnv* env=jni.env;
    if (!CheckCommunityJni(env,"entry")) return false;
    const jint frame_status=env->PushLocalFrame(16);
    jni.frame=frame_status==JNI_OK;
    if (!CheckCommunityJni(env,"local_frame",jni.frame)) return false;

    jclass uri_class=env->FindClass("android/net/Uri");
    if (!CheckCommunityJni(env,"uri_class",uri_class!=nullptr)) return false;
    jmethodID parse=env->GetStaticMethodID(uri_class,"parse","(Ljava/lang/String;)Landroid/net/Uri;");
    if (!CheckCommunityJni(env,"uri_parse",parse!=nullptr)) return false;
    jstring url=env->NewStringUTF(hpvr::quest::kDemoCommunityUrl);
    if (!CheckCommunityJni(env,"url",url!=nullptr)) return false;
    jobject uri=env->CallStaticObjectMethod(uri_class,parse,url);
    if (!CheckCommunityJni(env,"parse_url",uri!=nullptr)) return false;

    jclass intent_class=env->FindClass("android/content/Intent");
    if (!CheckCommunityJni(env,"intent_class",intent_class!=nullptr)) return false;
    jmethodID constructor=env->GetMethodID(intent_class,"<init>","(Ljava/lang/String;Landroid/net/Uri;)V");
    if (!CheckCommunityJni(env,"intent_constructor",constructor!=nullptr)) return false;
    jstring action=env->NewStringUTF("android.intent.action.VIEW");
    if (!CheckCommunityJni(env,"action_view",action!=nullptr)) return false;
    jobject intent=env->NewObject(intent_class,constructor,action,uri);
    if (!CheckCommunityJni(env,"intent",intent!=nullptr)) return false;

    jclass activity_class=env->GetObjectClass(activity->clazz);
    if (!CheckCommunityJni(env,"activity_class",activity_class!=nullptr)) return false;
    jmethodID start_activity=env->GetMethodID(activity_class,"startActivity","(Landroid/content/Intent;)V");
    if (!CheckCommunityJni(env,"start_activity_method",start_activity!=nullptr)) return false;
    env->CallVoidMethod(activity->clazz,start_activity,intent);
    if (!CheckCommunityJni(env,"start_activity")) return false;
    HPVR_LOGI("[hpvr.quest.community] status=REQUESTED source=EXPLICIT_BUTTON action=VIEW");
    return true;
}

bool ValidatePortableCore() {
    const std::uint32_t wand_abi = hpvr_wand_abi_version();
    const std::uint32_t gesture_abi = hpvr_hp1_gesture_abi_version();
    const bool valid = wand_abi == HPVR_WAND_ABI_VERSION &&
                       gesture_abi == HPVR_HP1_GESTURE_ABI_VERSION;
    HPVR_LOGI(
        "[hpvr.quest.portable_core] wand_abi=%u gesture_abi=%u status=%s",
        wand_abi, gesture_abi, valid ? "READY" : "ABI_MISMATCH");
    return valid;
}

[[nodiscard]] bool PrepareOwnedScene(QuestHost& host) {
    const char* external_path = host.app->activity->externalDataPath;
    if (external_path == nullptr || external_path[0] == '\0') {
        HPVR_LOGI("[hpvr.quest.data] status=NO_EXTERNAL_DATA_PATH bundled_assets=0");
        return false;
    }

    const std::filesystem::path data_root =
        std::filesystem::path(external_path) / "HP";
    constexpr std::array<std::string_view, 3> required_files{
        "system/HPBase.u",
        "system/HarryPotter.u",
        "Maps/Lev_Tut1.unr",
    };
    std::uint32_t present = 0;
    for (const std::string_view relative : required_files) {
        std::error_code error;
        if (std::filesystem::is_regular_file(data_root / relative, error) &&
            !error) {
            ++present;
        }
    }
    HPVR_LOGI(
        "[hpvr.quest.data] root=%s required_present=%u required_total=%zu "
        "bundled_assets=0 status=%s",
        data_root.string().c_str(), present, required_files.size(),
        present == required_files.size() ? "READY" : "IMPORT_REQUIRED");
    if (present != required_files.size()) {
        return false;
    }

    constexpr float kAcceptedMetersPerUnrealUnit = 0.02F;
    const std::filesystem::path map_package = data_root / "Maps/Lev_Tut1.unr";
    hpvr_hp1_player_start_report report{};
    const std::string map_utf8 = map_package.string();
    const std::uint32_t status = hpvr_hp1_load_player_start_utf8(
        map_utf8.c_str(), kAcceptedMetersPerUnrealUnit, 0, &report);
    if (status != HPVR_HP1_PROFILE_OK ||
        report.status != HPVR_HP1_PROFILE_OK ||
        report.abi_version != HPVR_HP1_PLAYER_START_ABI_VERSION ||
        report.available_player_start_count == 0 ||
        report.location_serialized == 0) {
        HPVR_LOGE(
            "[hpvr.quest.data.probe] status=REJECTED result=%u report=%u "
            "abi=%u starts=%u error=%s",
            status, report.status, report.abi_version,
            report.available_player_start_count, report.error);
        return false;
    }
    HPVR_LOGI(
        "[hpvr.quest.data.probe] status=READY map=Lev_Tut1.unr "
        "player_start=%s ordinal=%u available=%u position_m=(%.3f,%.3f,%.3f) "
        "yaw_units=%d scale=%.5f",
        report.object_name, report.selected_ordinal,
        report.available_player_start_count, report.position_m[0],
        report.position_m[1], report.position_m[2], report.rotation_units[1],
        kAcceptedMetersPerUnrealUnit);
    const char* private_path=host.app->activity->internalDataPath;
    if(private_path==nullptr)return false;
    const auto save_root=std::filesystem::path(private_path)/"SaveGames";
    if (!host.stereo_smoke.LoadHogwarts(data_root,save_root)) {
        HPVR_LOGE("[hpvr.quest.data.probe] scene=REJECTED c3=ABORT");
        return false;
    }
    HPVR_LOGI(
        "[hpvr.quest.data.probe] scene=QUEUED startup=NON_BLOCKING c8=CONTINUE");
    return true;
}

bool XrSucceeded(const XrResult result, const char* operation) {
    if (XR_SUCCEEDED(result)) {
        return true;
    }
    HPVR_LOGE("[hpvr.quest.xr.error] operation=%s result=%d", operation,
              static_cast<int>(result));
    return false;
}

bool HasExtension(const std::vector<XrExtensionProperties>& extensions,
                  const std::string_view requested) {
    return std::any_of(extensions.begin(), extensions.end(),
                       [requested](const XrExtensionProperties& extension) {
                           return requested == extension.extensionName;
                       });
}

void DestroyOpenXr(QuestHost& host) {
    host.stereo_smoke.Destroy();
    host.system_id = XR_NULL_SYSTEM_ID;
    if (host.instance != XR_NULL_HANDLE) {
        const XrResult result = xrDestroyInstance(host.instance);
        HPVR_LOGI("[hpvr.quest.openxr] destroy_instance_result=%d",
                  static_cast<int>(result));
        host.instance = XR_NULL_HANDLE;
    }
}

bool InitializeOpenXr(QuestHost& host) {
    if (host.instance != XR_NULL_HANDLE) {
        return true;
    }
    if (host.app == nullptr || host.app->activity == nullptr) {
        HPVR_LOGE("[hpvr.quest.openxr] status=NO_ANDROID_ACTIVITY");
        return false;
    }

    PFN_xrInitializeLoaderKHR initialize_loader = nullptr;
    if (!XrSucceeded(
            xrGetInstanceProcAddr(
                XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&initialize_loader)),
            "xrGetInstanceProcAddr(xrInitializeLoaderKHR)") ||
        initialize_loader == nullptr) {
        return false;
    }

    XrLoaderInitInfoAndroidKHR loader_info{};
    loader_info.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
    loader_info.applicationVM = host.app->activity->vm;
    loader_info.applicationContext = host.app->activity->clazz;
    if (!XrSucceeded(initialize_loader(
                         reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(
                             &loader_info)),
                     "xrInitializeLoaderKHR")) {
        return false;
    }

    std::uint32_t extension_count = 0;
    if (!XrSucceeded(xrEnumerateInstanceExtensionProperties(
                         nullptr, 0, &extension_count, nullptr),
                     "xrEnumerateInstanceExtensionProperties(count)")) {
        return false;
    }
    std::vector<XrExtensionProperties> extensions(extension_count);
    for (auto& extension : extensions) {
        extension.type = XR_TYPE_EXTENSION_PROPERTIES;
        extension.next = nullptr;
    }
    if (!XrSucceeded(xrEnumerateInstanceExtensionProperties(
                         nullptr, extension_count, &extension_count,
                         extensions.data()),
                     "xrEnumerateInstanceExtensionProperties(values)")) {
        return false;
    }

    constexpr std::array required_extensions{
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
    };
    for (const char* extension : required_extensions) {
        if (!HasExtension(extensions, extension)) {
            HPVR_LOGE("[hpvr.quest.openxr] missing_extension=%s", extension);
            return false;
        }
    }

    XrInstanceCreateInfoAndroidKHR android_info{};
    android_info.type = XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR;
    android_info.applicationVM = host.app->activity->vm;
    android_info.applicationActivity = host.app->activity->clazz;

    XrInstanceCreateInfo create_info{};
    create_info.type = XR_TYPE_INSTANCE_CREATE_INFO;
    create_info.next = &android_info;
    std::strncpy(create_info.applicationInfo.applicationName,
                 "Harry Potter VR", XR_MAX_APPLICATION_NAME_SIZE - 1);
    std::strncpy(create_info.applicationInfo.engineName, "HPVR clean-room",
                 XR_MAX_ENGINE_NAME_SIZE - 1);
    create_info.applicationInfo.applicationVersion = 1;
    create_info.applicationInfo.engineVersion = 1;
    create_info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    std::vector<const char*> enabled(required_extensions.begin(),required_extensions.end());
    host.metrics_extension=HasExtension(extensions,XR_META_PERFORMANCE_METRICS_EXTENSION_NAME);
    if(host.metrics_extension)
        enabled.push_back(XR_META_PERFORMANCE_METRICS_EXTENSION_NAME);
    create_info.enabledExtensionCount = enabled.size();
    create_info.enabledExtensionNames = enabled.data();

    if (!XrSucceeded(xrCreateInstance(&create_info, &host.instance),
                     "xrCreateInstance")) {
        host.instance = XR_NULL_HANDLE;
        return false;
    }

    XrSystemGetInfo system_info{};
    system_info.type = XR_TYPE_SYSTEM_GET_INFO;
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!XrSucceeded(xrGetSystem(host.instance, &system_info, &host.system_id),
                     "xrGetSystem")) {
        DestroyOpenXr(host);
        return false;
    }

    std::uint32_t view_count = 0;
    if (!XrSucceeded(xrEnumerateViewConfigurationViews(
                         host.instance, host.system_id,
                         XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0,
                         &view_count, nullptr),
                     "xrEnumerateViewConfigurationViews(count)")) {
        DestroyOpenXr(host);
        return false;
    }
    if (view_count != 2) {
        HPVR_LOGE("[hpvr.quest.openxr] status=INVALID_STEREO_VIEW_COUNT count=%u",
                  view_count);
        DestroyOpenXr(host);
        return false;
    }

    HPVR_LOGI(
        "[hpvr.quest.openxr] status=INSTANCE_READY system=%llu views=%u "
        "vulkan_enable2=1",
        static_cast<unsigned long long>(host.system_id), view_count);
    return true;
}

const char* EventName(const hpvr::quest::HostEvent event) {
    using hpvr::quest::HostEvent;
    switch (event) {
        case HostEvent::Start: return "START";
        case HostEvent::Resume: return "RESUME";
        case HostEvent::Pause: return "PAUSE";
        case HostEvent::Stop: return "STOP";
        case HostEvent::WindowReady: return "WINDOW_READY";
        case HostEvent::WindowLost: return "WINDOW_LOST";
        case HostEvent::FocusGained: return "FOCUS_GAINED";
        case HostEvent::FocusLost: return "FOCUS_LOST";
        case HostEvent::Destroy: return "DESTROY";
    }
    return "UNKNOWN";
}

void ApplyEvent(QuestHost& host, const hpvr::quest::HostEvent event) {
    const auto transition = host.lifecycle.Apply(event);
    const auto& snapshot = host.lifecycle.snapshot();
    HPVR_LOGI(
        "[hpvr.quest.lifecycle] event=%s started=%u resumed=%u window=%u "
        "focused=%u generation=%llu start_session=%u stop_session=%u",
        EventName(event), snapshot.started, snapshot.resumed,
        snapshot.window_ready, snapshot.focused,
        static_cast<unsigned long long>(snapshot.session_generation),
        transition.should_start_session, transition.should_stop_session);

    if (transition.should_start_session) {
        if (InitializeOpenXr(host) &&
            host.stereo_smoke.InitializeGraphics(host.instance,
                                                 host.system_id,host.metrics_extension) &&
            host.stereo_smoke.CreateSession()) {
            HPVR_LOGI(
                "[hpvr.quest.lifecycle] generation=%llu session=CREATED",
                static_cast<unsigned long long>(
                    snapshot.session_generation));
        } else {
            HPVR_LOGE(
                "[hpvr.quest.lifecycle] generation=%llu session=FAILED",
                static_cast<unsigned long long>(
                    snapshot.session_generation));
        }
    }
    if (transition.should_stop_session) {
        host.stereo_smoke.DestroySession();
    }
    if (transition.should_destroy_runtime) {
        DestroyOpenXr(host);
    }
}

void OnAppCommand(android_app* app, const std::int32_t command) {
    auto& host = *static_cast<QuestHost*>(app->userData);
    using hpvr::quest::HostEvent;
    switch (command) {
        case APP_CMD_START: ApplyEvent(host, HostEvent::Start); break;
        case APP_CMD_RESUME: ApplyEvent(host, HostEvent::Resume); break;
        case APP_CMD_PAUSE: ApplyEvent(host, HostEvent::Pause); break;
        case APP_CMD_STOP: ApplyEvent(host, HostEvent::Stop); break;
        case APP_CMD_INIT_WINDOW: ApplyEvent(host, HostEvent::WindowReady); break;
        case APP_CMD_TERM_WINDOW: ApplyEvent(host, HostEvent::WindowLost); break;
        case APP_CMD_GAINED_FOCUS: ApplyEvent(host, HostEvent::FocusGained); break;
        case APP_CMD_LOST_FOCUS: ApplyEvent(host, HostEvent::FocusLost); break;
        case APP_CMD_DESTROY: ApplyEvent(host, HostEvent::Destroy); break;
        default: break;
    }
}

}  // namespace

extern "C" void android_main(android_app* app) {
    QuestHost host{};
    host.app = app;
    app->userData = &host;
    app->onAppCmd = OnAppCommand;
    HPVR_LOGI("[hpvr.quest.host] status=ENTER abi=arm64-v8a gate=C35 loading=WARNER_THEATER sprint=L3_TOGGLE running=MATCHED_TRANSLATION knights=ONESHOT_CLAMPED story=DRACO_THEN_OPTIONAL_FILCH peeves=MANDATORY_CONTACT ui=BOOK_HUD stairs=INVISIBLE_RAMPS_HIDDEN cutscene=6DOF_PCM_RELEASE basic_cast=SPELLNONE lesson=CLASSROOM_CAST_GHOST_BOARDS reward=APPROACH_FRED objective=OWNED_TEXT frog=ANIMATED_GROUNDED twins=STAGED_SWAP demo=FIRST_STEP_AND_LESSON_END lesson_difficulty=ORIGINAL_OPTIONAL_RELAXED");
    if (!ValidatePortableCore()) {
        HPVR_LOGE("[hpvr.quest.host] status=PORTABLE_CORE_REJECTED");
        return;
    }
    if (!PrepareOwnedScene(host)) {
        HPVR_LOGE("[hpvr.quest.host] status=C3_SCENE_REQUIRED");
        return;
    }

    while (!host.lifecycle.snapshot().destroy_requested &&
           app->destroyRequested == 0) {
        android_poll_source* source = nullptr;
        const int timeout_ms = host.stereo_smoke.IsRunning()
                                   ? 0
                                   : host.lifecycle.CanOwnSession() ? 50 : -1;
        const int result = ALooper_pollOnce(
            timeout_ms, nullptr, nullptr,
            reinterpret_cast<void**>(&source));
        if (result >= 0 && source != nullptr) {
            source->process(app, source);
        }
        if (!host.stereo_smoke.PumpHogwartsLoad()) {
            HPVR_LOGE("[hpvr.quest.host] status=SCENE_ASYNC_FAILURE");
            break;
        }
        bool xr_exit_requested = false;
        if (!host.stereo_smoke.PollEvents(&xr_exit_requested)) {
            HPVR_LOGE("[hpvr.quest.host] status=XR_EVENT_FAILURE");
            break;
        }
        if (xr_exit_requested) {
            HPVR_LOGI("[hpvr.quest.host] status=XR_EXIT_REQUESTED");
            ANativeActivity_finish(app->activity);
            break;
        }
        if (host.stereo_smoke.IsRunning() &&
            !host.stereo_smoke.RenderFrame()) {
            HPVR_LOGE("[hpvr.quest.host] status=FRAME_FAILURE");
            break;
        }
        if (host.stereo_smoke.ConsumeCommunityRequest()) {
            OpenDemoCommunity(app->activity);
        }
    }

    DestroyOpenXr(host);
    HPVR_LOGI("[hpvr.quest.host] status=EXIT");
}
