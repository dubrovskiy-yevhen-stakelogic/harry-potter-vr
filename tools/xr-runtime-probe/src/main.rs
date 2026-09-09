use std::error::Error;
use std::path::PathBuf;

use openxr as xr;

fn yes_no(value: bool) -> &'static str {
    if value { "yes" } else { "no" }
}

fn loader_path() -> Result<Option<PathBuf>, Box<dyn Error>> {
    let mut arguments = std::env::args_os().skip(1);
    let Some(argument) = arguments.next() else {
        return Ok(std::env::var_os("HPVR_OPENXR_LOADER").map(PathBuf::from));
    };

    if argument != "--loader" {
        return Err(format!(
            "unknown argument '{}'; expected --loader <openxr_loader path>",
            argument.to_string_lossy()
        )
        .into());
    }

    let path = arguments
        .next()
        .ok_or("--loader requires a path to the OpenXR loader library")?;
    if arguments.next().is_some() {
        return Err("unexpected arguments after the OpenXR loader path".into());
    }
    Ok(Some(PathBuf::from(path)))
}

fn main() -> Result<(), Box<dyn Error>> {
    println!("HPVR OpenXR runtime probe");
    let selected_loader = loader_path()?;
    let entry = unsafe {
        match selected_loader.as_deref() {
            Some(path) => {
                println!("loader: {}", path.display());
                xr::Entry::load_from(path)?
            }
            None => {
                println!("loader: platform default search");
                xr::Entry::load()?
            }
        }
    };
    let available = entry.enumerate_extensions()?;

    println!(
        "extensions: XR_KHR_vulkan_enable={} XR_KHR_vulkan_enable2={}",
        yes_no(available.khr_vulkan_enable),
        yes_no(available.khr_vulkan_enable2)
    );
    println!(
        "input: XR_EXT_hand_tracking={} XR_META_simultaneous_hands_and_controllers={} \
XR_META_touch_controller_plus={}",
        yes_no(available.ext_hand_tracking),
        yes_no(available.meta_simultaneous_hands_and_controllers),
        yes_no(available.meta_touch_controller_plus)
    );
    println!(
        "quest features: XR_FB_display_refresh_rate={} XR_FB_foveation={} \
XR_FB_foveation_vulkan={}",
        yes_no(available.fb_display_refresh_rate),
        yes_no(available.fb_foveation),
        yes_no(available.fb_foveation_vulkan)
    );

    if !available.khr_vulkan_enable && !available.khr_vulkan_enable2 {
        return Err("active OpenXR runtime exposes no Vulkan graphics binding".into());
    }

    let mut enabled = xr::ExtensionSet::default();
    enabled.khr_vulkan_enable = available.khr_vulkan_enable;
    enabled.khr_vulkan_enable2 = available.khr_vulkan_enable2;

    let instance = entry.create_instance(
        &xr::ApplicationInfo {
            application_name: "Harry Potter VR runtime probe",
            application_version: 1,
            engine_name: "HPVR",
            engine_version: 1,
            api_version: xr::Version::new(1, 0, 0),
        },
        &enabled,
        &[],
    )?;

    let properties = instance.properties()?;
    println!(
        "runtime: {} {}",
        properties.runtime_name, properties.runtime_version
    );

    let system = instance
        .system(xr::FormFactor::HEAD_MOUNTED_DISPLAY)
        .map_err(|error| format!("runtime loaded, but no HMD system is available: {error}"))?;
    let requirements = instance.graphics_requirements::<xr::Vulkan>(system)?;
    println!(
        "Vulkan API range: min={} max={}",
        requirements.min_api_version_supported, requirements.max_api_version_supported
    );

    if available.khr_vulkan_enable {
        println!(
            "legacy Vulkan instance extensions: {}",
            instance.vulkan_legacy_instance_extensions(system)?
        );
        println!(
            "legacy Vulkan device extensions: {}",
            instance.vulkan_legacy_device_extensions(system)?
        );
    }

    let views = instance
        .enumerate_view_configuration_views(system, xr::ViewConfigurationType::PRIMARY_STEREO)?;
    println!("PRIMARY_STEREO view count: {}", views.len());
    for (index, view) in views.iter().enumerate() {
        println!(
            "view {index}: recommended={}x{} max={}x{} samples={} max_samples={}",
            view.recommended_image_rect_width,
            view.recommended_image_rect_height,
            view.max_image_rect_width,
            view.max_image_rect_height,
            view.recommended_swapchain_sample_count,
            view.max_swapchain_sample_count
        );
    }

    let blend_modes = instance
        .enumerate_environment_blend_modes(system, xr::ViewConfigurationType::PRIMARY_STEREO)?;
    println!("environment blend modes: {blend_modes:?}");
    println!("probe result: runtime capability query passed; renderer interop gate remains open");
    Ok(())
}
