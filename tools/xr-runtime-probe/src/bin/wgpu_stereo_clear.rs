use std::error::Error;
use std::ffi::{CStr, CString};
use std::path::PathBuf;
use std::sync::{
    Arc,
    atomic::{AtomicUsize, Ordering},
};
use std::time::{Duration, Instant};

use ash::{
    Entry as VulkanEntry,
    vk::{self, Handle},
};
use glam::{Quat, Vec3};
use openxr as xr;

#[path = "support/calibration_scene.rs"]
mod calibration_scene;
#[cfg(feature = "gesture-projection")]
#[path = "support/gesture_projection.rs"]
mod gesture_projection;
#[cfg(not(feature = "gesture-projection"))]
#[path = "support/gesture_projection_disabled.rs"]
mod gesture_projection;
#[path = "support/hp1_bsp_collision.rs"]
mod hp1_bsp_collision;
#[path = "support/hp1_bsp_ffi.rs"]
mod hp1_bsp_ffi;
#[path = "support/hp1_character_population.rs"]
mod hp1_character_population;
#[cfg(feature = "gesture-projection")]
#[path = "support/hp1_gesture_ffi.rs"]
mod hp1_gesture_ffi;
#[path = "support/hp1_npc_preview.rs"]
mod hp1_npc_preview;
#[path = "support/hp1_npc_spell_interaction.rs"]
mod hp1_npc_spell_interaction;
#[path = "support/hp1_wand_model.rs"]
mod hp1_wand_model;
#[path = "support/wand_input.rs"]
mod wand_input;
#[cfg(feature = "gesture-projection")]
#[path = "support/wand_trajectory_ffi.rs"]
mod wand_trajectory_ffi;

const VIEW_TYPE: xr::ViewConfigurationType = xr::ViewConfigurationType::PRIMARY_STEREO;
const VIEW_COUNT: u32 = 2;
const DEFAULT_FRAME_COUNT: u32 = 300;
const SESSION_READY_TIMEOUT: Duration = Duration::from_secs(120);
const GPU_WAIT_TIMEOUT: Duration = Duration::from_secs(30);

fn abort_without_destructors(context: &str) -> ! {
    eprintln!(
        "FATAL cleanup state is unknown ({context}); aborting the probe without Vulkan/OpenXR destructors"
    );
    std::process::abort()
}

struct Options {
    loader: Option<PathBuf>,
    frame_count: u32,
    geometry: bool,
    wand: bool,
    gesture: bool,
    flipendo_data_root: Option<PathBuf>,
    flipendo_test_assist: bool,
    hp1_map_slice: Option<Hp1MapSliceOptions>,
    hp1_player_start_ordinal: Option<u32>,
    hp1_eye_height_meters: f32,
    hp1_bsp_collision: bool,
    hp1_npc_preview: Option<Hp1NpcPreviewOptions>,
    hp1_npc_actor_reference: Option<i32>,
    hp1_character_population: Option<Hp1CharacterPopulationOptions>,
    hp1_character_animation: bool,
    hp1_character_stage_near_player: bool,
    hp1_npc_spell_interaction: bool,
    validate_assets_only: bool,
}

struct Hp1MapSliceOptions {
    map_package: PathBuf,
    meters_per_unreal_unit: f32,
    maximum_triangle_count: u32,
}

struct Hp1NpcPreviewOptions {
    source: Hp1NpcPreviewSource,
    meters_per_unit: f32,
}

struct Hp1CharacterPopulationOptions {
    data_root: PathBuf,
    excluded_actor_reference: i32,
}

enum Hp1NpcPreviewSource {
    Psk {
        psk_path: PathBuf,
        texture_directory: PathBuf,
    },
    Package {
        package_path: PathBuf,
        mesh_reference: i32,
    },
}

enum FrameOutcome {
    Skipped,
    Rendered(xr::ViewStateFlags),
}

struct ImportedImage {
    left_view: wgpu::TextureView,
    right_view: wgpu::TextureView,
    _texture: wgpu::Texture,
}

struct ImportedImages {
    images: Option<Vec<ImportedImage>>,
    device: wgpu::Device,
    drop_callbacks: Arc<AtomicUsize>,
}

impl ImportedImages {
    fn new(device: wgpu::Device, drop_callbacks: Arc<AtomicUsize>) -> Self {
        Self {
            images: Some(Vec::new()),
            device,
            drop_callbacks,
        }
    }

    fn push(&mut self, image: ImportedImage) {
        self.images
            .as_mut()
            .expect("imported image storage is present until teardown")
            .push(image);
    }

    fn get(&self, index: usize) -> Option<&ImportedImage> {
        self.images.as_ref()?.get(index)
    }

    fn len(&self) -> usize {
        self.images.as_ref().map_or(0, Vec::len)
    }

    fn clear_and_verify(&mut self) -> Result<(), Box<dyn Error>> {
        self.device.poll(wgpu::PollType::Wait {
            submission_index: None,
            timeout: Some(GPU_WAIT_TIMEOUT),
        })?;
        let images = self.images.take().unwrap_or_default();
        let expected_callbacks = images.len();
        drop(images);
        if let Err(error) = self.device.poll(wgpu::PollType::Wait {
            submission_index: None,
            timeout: Some(GPU_WAIT_TIMEOUT),
        }) {
            eprintln!("final wgpu resource-retirement poll failed: {error}");
            abort_without_destructors("external image views may still be alive");
        }
        let observed_callbacks = self.drop_callbacks.load(Ordering::SeqCst);
        if observed_callbacks != expected_callbacks {
            eprintln!(
                "external texture drop callbacks: expected {expected_callbacks}, observed {observed_callbacks}"
            );
            abort_without_destructors("non-owning external texture teardown was not proven");
        }
        println!(
            "external texture wrappers dropped non-owningly: {observed_callbacks}/{expected_callbacks}"
        );
        Ok(())
    }
}

impl Drop for ImportedImages {
    fn drop(&mut self) {
        let Some(images) = self.images.take() else {
            return;
        };
        let gpu_idle = self
            .device
            .poll(wgpu::PollType::Wait {
                submission_index: None,
                timeout: Some(GPU_WAIT_TIMEOUT),
            })
            .is_ok();
        if !gpu_idle {
            abort_without_destructors("GPU idle was not proven during unwind");
        }
        drop(images);
        if let Err(error) = self.device.poll(wgpu::PollType::Wait {
            submission_index: None,
            timeout: Some(GPU_WAIT_TIMEOUT),
        }) {
            eprintln!("wgpu resource-retirement poll failed during unwind: {error}");
            abort_without_destructors("external image views may still be alive during unwind");
        }
    }
}

#[derive(Default)]
struct RuntimeCounters {
    frames_begun: u32,
    frames_ended: u32,
    acquired: u32,
    waited: u32,
    released: u32,
    submissions: u32,
    gpu_completions: u32,
    rendered: u32,
    skipped: u32,
    session_begins: u32,
    session_ends: u32,
    events_lost: u32,
    reference_space_changes: u32,
    outstanding_images: u32,
    max_outstanding_images: u32,
    per_image_acquisitions: Vec<u32>,
}

impl RuntimeCounters {
    fn new(image_count: usize) -> Self {
        Self {
            per_image_acquisitions: vec![0; image_count],
            ..Self::default()
        }
    }

    fn on_acquire(&mut self, image_index: usize) -> Result<(), Box<dyn Error>> {
        if image_index >= self.per_image_acquisitions.len() {
            return Err("runtime acquired an out-of-range swapchain image".into());
        }
        if self.outstanding_images != 0 {
            return Err("more than one swapchain image would be outstanding".into());
        }
        self.per_image_acquisitions[image_index] += 1;
        self.acquired += 1;
        self.outstanding_images += 1;
        self.max_outstanding_images = self.max_outstanding_images.max(self.outstanding_images);
        Ok(())
    }

    fn on_release(&mut self) -> Result<(), Box<dyn Error>> {
        if self.outstanding_images != 1 {
            return Err(format!(
                "release counter expected one outstanding image, observed {}",
                self.outstanding_images
            )
            .into());
        }
        self.released += 1;
        self.outstanding_images -= 1;
        Ok(())
    }

    fn verify(&self) -> Result<(), Box<dyn Error>> {
        if self.frames_begun != self.frames_ended {
            return Err(format!(
                "unbalanced frames: begin={} end={}",
                self.frames_begun, self.frames_ended
            )
            .into());
        }
        if self.acquired != self.waited || self.waited != self.released {
            return Err(format!(
                "unbalanced swapchain operations: acquire={} wait={} release={}",
                self.acquired, self.waited, self.released
            )
            .into());
        }
        if self.submissions != self.gpu_completions
            || self.gpu_completions != self.rendered
            || self.rendered != self.released
        {
            return Err(format!(
                "unbalanced rendering: submit={} gpu_complete={} rendered={} release={}",
                self.submissions, self.gpu_completions, self.rendered, self.released
            )
            .into());
        }
        if self.rendered + self.skipped != self.frames_ended {
            return Err(format!(
                "frame outcome mismatch: rendered={} skipped={} frame_end={}",
                self.rendered, self.skipped, self.frames_ended
            )
            .into());
        }
        if self.session_begins != self.session_ends {
            return Err(format!(
                "unbalanced session lifecycle: begin={} end={}",
                self.session_begins, self.session_ends
            )
            .into());
        }
        if self.events_lost != 0 {
            return Err(format!("OpenXR reported {} lost events", self.events_lost).into());
        }
        if self.outstanding_images != 0 || self.max_outstanding_images > 1 {
            return Err(format!(
                "invalid outstanding-image counters: current={} max={}",
                self.outstanding_images, self.max_outstanding_images
            )
            .into());
        }
        Ok(())
    }
}

fn parse_options() -> Result<Options, Box<dyn Error>> {
    let mut options = Options {
        loader: std::env::var_os("HPVR_OPENXR_LOADER").map(PathBuf::from),
        frame_count: DEFAULT_FRAME_COUNT,
        geometry: false,
        wand: false,
        gesture: false,
        flipendo_data_root: None,
        flipendo_test_assist: false,
        hp1_map_slice: None,
        hp1_player_start_ordinal: None,
        hp1_eye_height_meters: 0.0,
        hp1_bsp_collision: false,
        hp1_npc_preview: None,
        hp1_npc_actor_reference: None,
        hp1_character_population: None,
        hp1_character_animation: false,
        hp1_character_stage_near_player: false,
        hp1_npc_spell_interaction: false,
        validate_assets_only: false,
    };
    let mut arguments = std::env::args_os().skip(1);
    while let Some(argument) = arguments.next() {
        match argument.to_string_lossy().as_ref() {
            "--loader" => {
                options.loader = Some(
                    arguments
                        .next()
                        .ok_or("--loader requires a path to openxr_loader.dll")?
                        .into(),
                );
            }
            "--frames" => {
                let value = arguments
                    .next()
                    .ok_or("--frames requires a positive integer")?;
                options.frame_count = value.to_string_lossy().parse()?;
                if options.frame_count == 0 || options.frame_count > 1_000_000 {
                    return Err("--frames must be in the range 1..=1000000".into());
                }
            }
            "--geometry" => {
                options.geometry = true;
            }
            "--wand" => {
                options.geometry = true;
                options.wand = true;
            }
            "--gesture" => {
                if !cfg!(feature = "gesture-projection") {
                    return Err(
                        "--gesture requires a build with Cargo feature 'gesture-projection'".into(),
                    );
                }
                options.geometry = true;
                options.wand = true;
                options.gesture = true;
            }
            "--flipendo-data-root" => {
                if !cfg!(feature = "gesture-projection") {
                    return Err(
                        "--flipendo-data-root requires a build with Cargo feature 'gesture-projection'"
                            .into(),
                    );
                }
                if options.flipendo_data_root.is_some() {
                    return Err("--flipendo-data-root may be specified only once".into());
                }
                options.flipendo_data_root = Some(
                    arguments
                        .next()
                        .ok_or("--flipendo-data-root requires the HP installation root")?
                        .into(),
                );
                options.geometry = true;
                options.wand = true;
                options.gesture = true;
            }
            "--flipendo-test-assist" => {
                options.flipendo_test_assist = true;
            }
            "--hp1-map-slice" => {
                if !cfg!(feature = "gesture-projection") {
                    return Err(
                        "--hp1-map-slice requires a build with Cargo feature 'gesture-projection'"
                            .into(),
                    );
                }
                if options.hp1_map_slice.is_some() {
                    return Err("--hp1-map-slice may be specified only once".into());
                }
                let map_package = arguments
                    .next()
                    .ok_or("--hp1-map-slice requires <map> <meters-per-unit> <max-triangles>")?
                    .into();
                let scale = arguments
                    .next()
                    .ok_or("--hp1-map-slice requires <map> <meters-per-unit> <max-triangles>")?;
                let meters_per_unreal_unit: f32 = scale.to_string_lossy().parse()?;
                if !meters_per_unreal_unit.is_finite() || meters_per_unreal_unit <= 0.0 {
                    return Err(
                        "HP1 map meters-per-unit must be finite and greater than zero".into(),
                    );
                }
                let limit = arguments
                    .next()
                    .ok_or("--hp1-map-slice requires <map> <meters-per-unit> <max-triangles>")?;
                let maximum_triangle_count: u32 = limit.to_string_lossy().parse()?;
                if maximum_triangle_count == 0
                    || maximum_triangle_count > hp1_bsp_ffi::MAX_SLICE_TRIANGLES
                {
                    return Err(format!(
                        "HP1 map max-triangles must be in 1..={}",
                        hp1_bsp_ffi::MAX_SLICE_TRIANGLES
                    )
                    .into());
                }
                options.hp1_map_slice = Some(Hp1MapSliceOptions {
                    map_package,
                    meters_per_unreal_unit,
                    maximum_triangle_count,
                });
                options.geometry = true;
            }
            "--hp1-player-start" => {
                if !cfg!(feature = "gesture-projection") {
                    return Err(
                        "--hp1-player-start requires a build with Cargo feature 'gesture-projection'"
                            .into(),
                    );
                }
                if options.hp1_player_start_ordinal.is_some() {
                    return Err("--hp1-player-start may be specified only once".into());
                }
                let ordinal = arguments
                    .next()
                    .ok_or("--hp1-player-start requires a zero-based ordinal")?;
                options.hp1_player_start_ordinal = Some(ordinal.to_string_lossy().parse()?);
                options.geometry = true;
            }
            "--hp1-eye-height" => {
                let value = arguments.next().ok_or("--hp1-eye-height requires meters")?;
                options.hp1_eye_height_meters = value.to_string_lossy().parse()?;
                if !options.hp1_eye_height_meters.is_finite()
                    || !(0.0..=2.0).contains(&options.hp1_eye_height_meters)
                {
                    return Err("HP1 eye height must be finite and in 0..=2 meters".into());
                }
            }
            "--hp1-bsp-collision" => {
                options.hp1_bsp_collision = true;
            }
            "--hp1-npc-preview" => {
                if options.hp1_npc_preview.is_some() {
                    return Err("--hp1-npc-preview may be specified only once".into());
                }
                let psk_path = arguments
                    .next()
                    .ok_or(
                        "--hp1-npc-preview requires <psk> <texture-directory> <meters-per-unit>",
                    )?
                    .into();
                let texture_directory = arguments
                    .next()
                    .ok_or(
                        "--hp1-npc-preview requires <psk> <texture-directory> <meters-per-unit>",
                    )?
                    .into();
                let scale = arguments.next().ok_or(
                    "--hp1-npc-preview requires <psk> <texture-directory> <meters-per-unit>",
                )?;
                let meters_per_unit: f32 = scale.to_string_lossy().parse()?;
                if !meters_per_unit.is_finite() || meters_per_unit <= 0.0 {
                    return Err("HP1 NPC meters-per-unit must be finite and positive".into());
                }
                options.hp1_npc_preview = Some(Hp1NpcPreviewOptions {
                    source: Hp1NpcPreviewSource::Psk {
                        psk_path,
                        texture_directory,
                    },
                    meters_per_unit,
                });
                options.geometry = true;
            }
            "--hp1-npc-package-preview" => {
                if !cfg!(feature = "gesture-projection") {
                    return Err(
                        "--hp1-npc-package-preview requires a build with Cargo feature 'gesture-projection'"
                            .into(),
                    );
                }
                if options.hp1_npc_preview.is_some() {
                    return Err("an HP1 NPC preview may be specified only once".into());
                }
                let package_path = arguments
                    .next()
                    .ok_or(
                        "--hp1-npc-package-preview requires <package> <export-reference> <meters-per-unit>",
                    )?
                    .into();
                let reference = arguments.next().ok_or(
                    "--hp1-npc-package-preview requires <package> <export-reference> <meters-per-unit>",
                )?;
                let mesh_reference: i32 = reference.to_string_lossy().parse()?;
                if mesh_reference <= 0 {
                    return Err("HP1 NPC export-reference must be positive".into());
                }
                let scale = arguments.next().ok_or(
                    "--hp1-npc-package-preview requires <package> <export-reference> <meters-per-unit>",
                )?;
                let meters_per_unit: f32 = scale.to_string_lossy().parse()?;
                if !meters_per_unit.is_finite() || meters_per_unit <= 0.0 {
                    return Err("HP1 NPC meters-per-unit must be finite and positive".into());
                }
                options.hp1_npc_preview = Some(Hp1NpcPreviewOptions {
                    source: Hp1NpcPreviewSource::Package {
                        package_path,
                        mesh_reference,
                    },
                    meters_per_unit,
                });
                options.geometry = true;
            }
            "--hp1-npc-actor" => {
                if !cfg!(feature = "gesture-projection") {
                    return Err(
                        "--hp1-npc-actor requires a build with Cargo feature 'gesture-projection'"
                            .into(),
                    );
                }
                if options.hp1_npc_actor_reference.is_some() {
                    return Err("--hp1-npc-actor may be specified only once".into());
                }
                let reference = arguments
                    .next()
                    .ok_or("--hp1-npc-actor requires a positive actor reference")?;
                let actor_reference: i32 = reference.to_string_lossy().parse()?;
                if actor_reference <= 0 {
                    return Err("HP1 NPC actor reference must be positive".into());
                }
                options.hp1_npc_actor_reference = Some(actor_reference);
                options.geometry = true;
            }
            "--hp1-character-population" => {
                if !cfg!(feature = "gesture-projection") {
                    return Err(
                        "--hp1-character-population requires a build with Cargo feature 'gesture-projection'"
                            .into(),
                    );
                }
                if options.hp1_character_population.is_some() {
                    return Err("--hp1-character-population may be specified only once".into());
                }
                let data_root = arguments
                    .next()
                    .ok_or(
                        "--hp1-character-population requires <data-root> <excluded-actor-reference>",
                    )?
                    .into();
                let excluded = arguments.next().ok_or(
                    "--hp1-character-population requires <data-root> <excluded-actor-reference>",
                )?;
                let excluded_actor_reference: i32 = excluded.to_string_lossy().parse()?;
                if excluded_actor_reference < 0 {
                    return Err("excluded HP1 actor reference must be non-negative".into());
                }
                options.hp1_character_population = Some(Hp1CharacterPopulationOptions {
                    data_root,
                    excluded_actor_reference,
                });
                options.geometry = true;
            }
            "--hp1-character-animation" => {
                options.hp1_character_animation = true;
            }
            "--hp1-character-stage-near-player" => {
                options.hp1_character_stage_near_player = true;
            }
            "--hp1-npc-spell-interaction" => {
                options.hp1_npc_spell_interaction = true;
            }
            "--validate-assets-only" => {
                options.validate_assets_only = true;
            }
            unknown => {
                return Err(format!(
                    "unknown argument '{unknown}'; expected --loader <path>, --frames <count>, --geometry, --wand, --gesture, --flipendo-data-root <path>, --flipendo-test-assist, --hp1-map-slice <map> <meters-per-unit> <max-triangles>, --hp1-player-start <ordinal>, --hp1-eye-height <meters>, --hp1-bsp-collision, --hp1-npc-preview <psk> <texture-directory> <meters-per-unit>, --hp1-npc-package-preview <package> <export-reference> <meters-per-unit>, --hp1-npc-actor <actor-reference>, --hp1-character-population <data-root> <excluded-actor-reference>, --hp1-character-animation, --hp1-character-stage-near-player, --hp1-npc-spell-interaction, or --validate-assets-only"
                )
                .into());
            }
        }
    }
    if options.hp1_player_start_ordinal.is_some() && options.hp1_map_slice.is_none() {
        return Err("--hp1-player-start requires --hp1-map-slice".into());
    }
    if options.flipendo_test_assist && options.flipendo_data_root.is_none() {
        return Err("--flipendo-test-assist requires --flipendo-data-root".into());
    }
    if options.hp1_eye_height_meters != 0.0 && options.hp1_player_start_ordinal.is_none() {
        return Err("--hp1-eye-height requires --hp1-player-start".into());
    }
    if options.hp1_bsp_collision
        && (options.hp1_player_start_ordinal.is_none() || options.hp1_eye_height_meters <= 0.0)
    {
        return Err("--hp1-bsp-collision requires PlayerStart and a positive eye height".into());
    }
    if options.hp1_npc_actor_reference.is_some()
        && (options.hp1_map_slice.is_none() || options.hp1_npc_preview.is_none())
    {
        return Err("--hp1-npc-actor requires both --hp1-map-slice and an NPC preview".into());
    }
    if options.hp1_character_population.is_some() && options.hp1_map_slice.is_none() {
        return Err("--hp1-character-population requires --hp1-map-slice".into());
    }
    if options.hp1_character_population.is_some() && options.hp1_npc_preview.is_some() {
        return Err("character population and single NPC preview are mutually exclusive".into());
    }
    if options.hp1_character_animation && options.hp1_character_population.is_none() {
        return Err("--hp1-character-animation requires --hp1-character-population".into());
    }
    if options.hp1_character_stage_near_player
        && (options.hp1_character_population.is_none()
            || options.hp1_player_start_ordinal.is_none())
    {
        return Err(
            "--hp1-character-stage-near-player requires character population and PlayerStart"
                .into(),
        );
    }
    if options.hp1_npc_spell_interaction
        && (!options.hp1_character_animation
            || options.hp1_character_population.is_none()
            || !options.hp1_bsp_collision
            || !options.gesture)
    {
        return Err("--hp1-npc-spell-interaction requires gesture, character animation/population, and BSP collision".into());
    }
    if options.validate_assets_only
        && options.hp1_map_slice.is_none()
        && options.hp1_npc_preview.is_none()
        && options.hp1_character_population.is_none()
    {
        return Err("--validate-assets-only requires an HP1 map or NPC input".into());
    }
    Ok(options)
}

fn choose_color_format(formats: &[u32]) -> Option<(vk::Format, wgpu::TextureFormat)> {
    [
        (
            vk::Format::R8G8B8A8_SRGB,
            wgpu::TextureFormat::Rgba8UnormSrgb,
        ),
        (
            vk::Format::B8G8R8A8_SRGB,
            wgpu::TextureFormat::Bgra8UnormSrgb,
        ),
        (vk::Format::R8G8B8A8_UNORM, wgpu::TextureFormat::Rgba8Unorm),
        (vk::Format::B8G8R8A8_UNORM, wgpu::TextureFormat::Bgra8Unorm),
    ]
    .into_iter()
    .find(|(format, _)| formats.contains(&(format.as_raw() as u32)))
}

fn static_extension_names(raw: &str) -> Result<Vec<&'static CStr>, Box<dyn Error>> {
    raw.split_whitespace()
        .map(|name| {
            let name = CString::new(name)?.into_boxed_c_str();
            let name: &'static CStr = Box::leak(name);
            Ok(name)
        })
        .collect()
}

fn merge_extensions(
    destination: &mut Vec<&'static CStr>,
    additions: impl IntoIterator<Item = &'static CStr>,
) {
    for addition in additions {
        if !destination
            .iter()
            .any(|existing| existing.to_bytes() == addition.to_bytes())
        {
            destination.push(addition);
        }
    }
}

fn extension_list(extensions: &[&CStr]) -> String {
    extensions
        .iter()
        .map(|name| name.to_string_lossy())
        .collect::<Vec<_>>()
        .join(" ")
}

fn main() {
    if let Err(error) = run() {
        eprintln!("hpvr-xr-wgpu-stereo-clear: {error}");
        std::process::exit(1);
    }
}

fn run() -> Result<(), Box<dyn Error>> {
    let options = parse_options()?;
    let hp1_map_slice = options
        .hp1_map_slice
        .as_ref()
        .map(|map| {
            hp1_bsp_ffi::MapSlice::load(
                &map.map_package,
                map.meters_per_unreal_unit,
                map.maximum_triangle_count,
            )
        })
        .transpose()?;
    let hp1_player_start = match (
        options.hp1_map_slice.as_ref(),
        options.hp1_player_start_ordinal,
    ) {
        (Some(map), Some(ordinal)) => Some(hp1_bsp_ffi::PlayerStart::load(
            &map.map_package,
            map.meters_per_unreal_unit,
            ordinal,
        )?),
        _ => None,
    };
    let hp1_npc_preview = options
        .hp1_npc_preview
        .as_ref()
        .map(|npc| match &npc.source {
            Hp1NpcPreviewSource::Psk {
                psk_path,
                texture_directory,
            } => {
                hp1_npc_preview::NpcPreview::load(psk_path, texture_directory, npc.meters_per_unit)
            }
            Hp1NpcPreviewSource::Package {
                package_path,
                mesh_reference,
            } => hp1_npc_preview::NpcPreview::load_package(
                package_path,
                *mesh_reference,
                npc.meters_per_unit,
            ),
        })
        .transpose()?;
    let hp1_npc_actor = match (
        options.hp1_map_slice.as_ref(),
        options.hp1_npc_actor_reference,
    ) {
        (Some(map), Some(actor_reference)) => Some(hp1_bsp_ffi::ActorVisual::load(
            &map.map_package,
            map.meters_per_unreal_unit,
            actor_reference,
        )?),
        _ => None,
    };
    let character_stage = if options.hp1_character_stage_near_player {
        let start = hp1_player_start
            .as_ref()
            .ok_or("character staging requires a loaded PlayerStart")?;
        let map = options
            .hp1_map_slice
            .as_ref()
            .ok_or("character staging requires a loaded map")?;
        Some(hp1_character_population::CharacterStage {
            player_start_position_m: start.position_m,
            player_start_yaw_radians: start.rotation_units[1] as f32 * std::f32::consts::TAU
                / 65_536.0,
            floor_drop_meters: 42.0 * map.meters_per_unreal_unit,
        })
    } else {
        None
    };
    let hp1_character_population = match (
        options.hp1_map_slice.as_ref(),
        options.hp1_character_population.as_ref(),
    ) {
        (Some(map), Some(population)) => Some(hp1_character_population::CharacterPopulation::load(
            &population.data_root,
            &map.map_package,
            map.meters_per_unreal_unit,
            population.excluded_actor_reference,
            character_stage,
        )?),
        _ => None,
    };
    if let (Some(map), Some(slice)) = (options.hp1_map_slice.as_ref(), hp1_map_slice.as_ref()) {
        let report = slice.report;
        println!(
            "[hp1.map.load] path={} meters_per_unit={} selected_triangles={} available_triangles={} omitted_triangles={} vertices={} raw_flags_zero={} raw_flags_nonzero={} texture_refs=imported:{} local:{} none:{} actor_refs=imported:{} local:{} none:{} source_bounds_min={:?} source_bounds_max={:?} degenerate_skipped={} winding_reversed={}",
            map.map_package.display(),
            map.meters_per_unreal_unit,
            report.selected_triangle_count,
            report.available_triangle_count,
            report.omitted_triangle_count,
            slice.vertices.len(),
            report.zero_flag_triangle_count,
            report.nonzero_flag_triangle_count,
            report.imported_texture_triangle_count,
            report.local_texture_triangle_count,
            report.no_texture_triangle_count,
            report.imported_actor_triangle_count,
            report.local_actor_triangle_count,
            report.no_actor_triangle_count,
            report.bounds_min_m,
            report.bounds_max_m,
            report.degenerate_triangle_count,
            report.winding_reversal_count,
        );
    }
    if let Some(start) = hp1_player_start.as_ref() {
        println!(
            "[hp1.player_start] selected_ordinal={} available={} actor_ref={} actor_slot={} object={} position_m={:?} rotation_units={:?} location_serialized={} rotation_serialized={}",
            start.ordinal,
            start.available_count,
            start.actor_reference,
            start.actor_slot_index,
            start.object_name,
            start.position_m,
            start.rotation_units,
            start.location_serialized,
            start.rotation_serialized,
        );
    }
    if let Some(npc) = hp1_npc_preview.as_ref() {
        println!(
            "[hp1.npc.load] points={} wedges={} faces={} vertices={} materials={} texture_bytes={} names={}",
            npc.point_count,
            npc.wedge_count,
            npc.face_count,
            npc.vertices.len(),
            npc.texture_layer_count,
            npc.texture_rgba8.len(),
            npc.material_names.join(","),
        );
    }
    if let Some(actor) = hp1_npc_actor.as_ref() {
        println!(
            "[hp1.npc.actor] actor_ref={} actor_slot={} object={} position_m={:?} rotation_units={:?} draw_scale={} location_serialized={} rotation_serialized={} draw_scale_serialized={}",
            actor.actor_reference,
            actor.actor_slot_index,
            actor.object_name,
            actor.position_m,
            actor.rotation_units,
            actor.draw_scale,
            actor.location_serialized,
            actor.rotation_serialized,
            actor.draw_scale_serialized,
        );
    }
    if let Some(population) = hp1_character_population.as_ref() {
        println!(
            "[hp1.characters.load] actors={} distinct_meshes={} source_faces={} expanded_vertices={} texture_layers={} texture_bytes={} inspected={} excluded={} unresolved_class={}",
            population.actor_count,
            population.distinct_mesh_count,
            population.total_face_count,
            population.vertices.len(),
            population.texture_layer_count,
            population.texture_rgba8.len(),
            population.inspected_actor_count,
            population.excluded_actor_count,
            population.unresolved_class_count,
        );
    }
    if options.validate_assets_only {
        if let Some(data_root) = options.flipendo_data_root.as_deref() {
            let _gesture_preflight =
                gesture_projection::GestureProjectionCapture::new_with_test_assist(
                    Some(data_root),
                    options.flipendo_test_assist,
                )?;
        }
        if options.wand {
            if let Some(data_root) = options.flipendo_data_root.as_deref() {
                hp1_wand_model::HarryWandModel::load(data_root)?;
            }
        }
        if options.hp1_bsp_collision {
            let slice = hp1_map_slice
                .as_ref()
                .ok_or("collision preflight requires a loaded map")?;
            let start = hp1_player_start
                .as_ref()
                .ok_or("collision preflight requires a loaded PlayerStart")?;
            let yaw_radians = start.rotation_units[1] as f32 * std::f32::consts::TAU / 65_536.0;
            let rotation = Quat::from_rotation_y(yaw_radians);
            let collision = hp1_bsp_collision::BspCollision::new(
                slice,
                rotation,
                -(rotation * start.position_m),
                options.hp1_eye_height_meters * (15.0 / 40.75),
                options.hp1_eye_height_meters * (42.0 / 40.75),
            )?;
            let probe = collision.resolve_movement(Vec3::ZERO, Vec3::new(0.01, 0.0, 0.0));
            if !probe.displacement.is_finite() {
                return Err("collision preflight produced non-finite movement".into());
            }
            println!(
                "[hp1.collision.validate] displacement={:?} blocked_substeps={} grounded_substeps={} PASS",
                probe.displacement, probe.blocked_substeps, probe.grounded_substeps
            );
            if options.hp1_npc_spell_interaction {
                let population = hp1_character_population
                    .as_ref()
                    .ok_or("NPC spell preflight requires character population")?;
                let interaction = hp1_npc_spell_interaction::NpcSpellInteraction::new(
                    population,
                    rotation,
                    -(rotation * start.position_m),
                )?;
                let ground_adjustment = collision.ground_adjustment(Vec3::ZERO).unwrap_or(0.0);
                interaction.offline_validate(
                    &collision,
                    Vec3::Y * (options.hp1_eye_height_meters + ground_adjustment),
                )?;
            }
        }
        println!("[hp1.assets.validate] PASS openxr_started=0");
        return Ok(());
    }
    let probe_label = if options.hp1_map_slice.is_some() {
        "Harry Potter VR Gate B8 diagnostic map slice"
    } else if options.flipendo_data_root.is_some() {
        "Harry Potter VR Gate A6 Flipendo test target"
    } else if options.gesture {
        "Harry Potter VR Gate A5 trajectory projection"
    } else if options.wand {
        "Harry Potter VR Gate A4 tracked wand"
    } else if options.geometry {
        "Harry Potter VR Gate A3 tracked geometry"
    } else {
        "Harry Potter VR direct-wgpu probe"
    };
    println!(
        "HPVR OpenXR {} probe",
        if options.hp1_map_slice.is_some() {
            "Gate B8 diagnostic HP1 map slice"
        } else if options.flipendo_data_root.is_some() {
            "Gate A6 live Flipendo test target"
        } else if options.gesture {
            "Gate A5 live trajectory projection"
        } else if options.wand {
            "Gate A4 tracked-wand diagnostic"
        } else if options.geometry {
            "Gate A3 tracked-geometry"
        } else {
            "direct-wgpu stereo-clear"
        }
    );
    let entry = unsafe {
        match options.loader.as_deref() {
            Some(path) => {
                println!("OpenXR loader: {}", path.display());
                xr::Entry::load_from(path)?
            }
            None => {
                println!("OpenXR loader: platform default search");
                xr::Entry::load()?
            }
        }
    };

    let available_extensions = entry.enumerate_extensions()?;
    if !available_extensions.khr_vulkan_enable {
        return Err("active runtime does not expose XR_KHR_vulkan_enable".into());
    }

    let mut enabled_extensions = xr::ExtensionSet::default();
    enabled_extensions.khr_vulkan_enable = true;
    let meta_touch_plus_enabled = options.wand && available_extensions.meta_touch_controller_plus;
    enabled_extensions.meta_touch_controller_plus = meta_touch_plus_enabled;
    if options.wand {
        println!(
            "XR_META_touch_controller_plus: available={} enabled={} (OpenXR 1.0 extension profile)",
            available_extensions.meta_touch_controller_plus, meta_touch_plus_enabled
        );
    }
    let xr_instance = entry.create_instance(
        &xr::ApplicationInfo {
            application_name: probe_label,
            application_version: 1,
            engine_name: "HPVR",
            engine_version: 1,
            api_version: xr::Version::new(1, 0, 0),
        },
        &enabled_extensions,
        &[],
    )?;
    let runtime = xr_instance.properties()?;
    println!(
        "runtime: {} {}",
        runtime.runtime_name, runtime.runtime_version
    );

    let system = xr_instance
        .system(xr::FormFactor::HEAD_MOUNTED_DISPLAY)
        .map_err(|error| format!("no active HMD system: {error}"))?;
    let view_configuration = xr_instance.enumerate_view_configuration_views(system, VIEW_TYPE)?;
    if view_configuration.len() != VIEW_COUNT as usize {
        return Err(format!(
            "PRIMARY_STEREO reported {} views instead of {VIEW_COUNT}",
            view_configuration.len()
        )
        .into());
    }
    if view_configuration[0].recommended_image_rect_width
        != view_configuration[1].recommended_image_rect_width
        || view_configuration[0].recommended_image_rect_height
            != view_configuration[1].recommended_image_rect_height
    {
        return Err("the two recommended eye extents differ; array swapchain is unsafe".into());
    }
    let width = view_configuration[0].recommended_image_rect_width;
    let height = view_configuration[0].recommended_image_rect_height;

    let requirements = xr_instance.graphics_requirements::<xr::Vulkan>(system)?;
    let target_vulkan_xr = xr::Version::new(1, 1, 0);
    if target_vulkan_xr < requirements.min_api_version_supported
        || target_vulkan_xr > requirements.max_api_version_supported
    {
        return Err(format!(
            "Vulkan 1.1 is outside runtime range {}..={}",
            requirements.min_api_version_supported, requirements.max_api_version_supported
        )
        .into());
    }
    println!(
        "Vulkan API range: {}..={}",
        requirements.min_api_version_supported, requirements.max_api_version_supported
    );

    let environment_blend_mode = *xr_instance
        .enumerate_environment_blend_modes(system, VIEW_TYPE)?
        .first()
        .ok_or("runtime reported no environment blend mode")?;

    unsafe {
        let vk_entry = VulkanEntry::load()?;
        let validation_available =
            vk_entry
                .enumerate_instance_layer_properties()?
                .iter()
                .any(|layer| {
                    layer
                        .layer_name_as_c_str()
                        .is_ok_and(|name| name == c"VK_LAYER_KHRONOS_validation")
                });
        println!(
            "Vulkan validation layer: {} (not enabled; this run is functional evidence only)",
            if validation_available {
                "available"
            } else {
                "unavailable"
            }
        );

        let instance_flags = wgpu::InstanceFlags::empty();
        let mut instance_extensions = wgpu::hal::vulkan::Instance::desired_extensions(
            &vk_entry,
            vk::API_VERSION_1_1,
            instance_flags,
        )?;
        let xr_instance_extensions =
            static_extension_names(&xr_instance.vulkan_legacy_instance_extensions(system)?)?;
        println!(
            "runtime Vulkan instance extensions: {}",
            extension_list(&xr_instance_extensions)
        );
        merge_extensions(
            &mut instance_extensions,
            xr_instance_extensions.iter().copied(),
        );
        println!(
            "combined Vulkan instance extensions: {}",
            extension_list(&instance_extensions)
        );
        let instance_extension_pointers = instance_extensions
            .iter()
            .map(|name| name.as_ptr())
            .collect::<Vec<_>>();
        let vk_application_info = vk::ApplicationInfo::default()
            .application_name(c"HPVR direct wgpu")
            .engine_name(c"HPVR")
            .api_version(vk::API_VERSION_1_1);
        let vk_instance_create_info = vk::InstanceCreateInfo::default()
            .application_info(&vk_application_info)
            .enabled_extension_names(&instance_extension_pointers);
        let raw_instance = vk_entry.create_instance(&vk_instance_create_info, None)?;
        let cleanup_instance = raw_instance.clone();
        let hal_instance = match wgpu::hal::vulkan::Instance::from_raw(
            vk_entry,
            raw_instance,
            vk::API_VERSION_1_1,
            0,
            None,
            instance_extensions,
            instance_flags,
            wgpu::MemoryBudgetThresholds::default(),
            false,
            None,
        ) {
            Ok(instance) => instance,
            Err(error) => {
                cleanup_instance.destroy_instance(None);
                return Err(error.into());
            }
        };
        let raw_instance_handle = hal_instance.shared_instance().raw_instance().handle();

        let physical_device = vk::PhysicalDevice::from_raw(
            xr_instance.vulkan_graphics_device(system, raw_instance_handle.as_raw() as _)? as _,
        );
        let physical_device_properties = hal_instance
            .shared_instance()
            .raw_instance()
            .get_physical_device_properties(physical_device);
        let gpu_name =
            CStr::from_ptr(physical_device_properties.device_name.as_ptr()).to_string_lossy();
        println!(
            "OpenXR-selected GPU: {gpu_name}; Vulkan {}.{}.{}",
            vk::api_version_major(physical_device_properties.api_version),
            vk::api_version_minor(physical_device_properties.api_version),
            vk::api_version_patch(physical_device_properties.api_version)
        );

        let queue_families = hal_instance
            .shared_instance()
            .raw_instance()
            .get_physical_device_queue_family_properties(physical_device);
        let queue_family_zero = queue_families
            .first()
            .ok_or("OpenXR-selected GPU exposes no queue families")?;
        let required_queue_flags = vk::QueueFlags::GRAPHICS | vk::QueueFlags::COMPUTE;
        if queue_family_zero.queue_count == 0
            || !queue_family_zero.queue_flags.contains(required_queue_flags)
        {
            return Err(format!(
                "wgpu-hal 29 requires queue family 0; flags={:?}, count={}",
                queue_family_zero.queue_flags, queue_family_zero.queue_count
            )
            .into());
        }
        let queue_family_index = 0_u32;
        let queue_index = 0_u32;

        let exposed_adapter = hal_instance
            .expose_adapter(physical_device)
            .ok_or("wgpu-hal rejected the OpenXR-selected Vulkan adapter")?;
        println!(
            "wgpu-hal exposed adapter: {} ({:?})",
            exposed_adapter.info.name, exposed_adapter.info.device_type
        );
        let wgpu_instance = wgpu::Instance::from_hal::<wgpu::hal::api::Vulkan>(hal_instance);
        let wgpu_adapter =
            wgpu_instance.create_adapter_from_hal::<wgpu::hal::api::Vulkan>(exposed_adapter);

        let required_features = wgpu::Features::empty();
        let required_limits =
            wgpu::Limits::downlevel_defaults().using_resolution(wgpu_adapter.limits());
        let required_extent = width.max(height);
        if required_limits.max_texture_dimension_2d < required_extent {
            return Err(format!(
                "wgpu max_texture_dimension_2d={} is below required eye extent {required_extent}",
                required_limits.max_texture_dimension_2d
            )
            .into());
        }
        let memory_hints = wgpu::MemoryHints::MemoryUsage;
        let device_descriptor = wgpu::DeviceDescriptor {
            label: Some("HPVR OpenXR shared Vulkan device"),
            required_features,
            required_limits: required_limits.clone(),
            memory_hints: memory_hints.clone(),
            ..Default::default()
        };

        let xr_device_extensions =
            static_extension_names(&xr_instance.vulkan_legacy_device_extensions(system)?)?;
        println!(
            "runtime Vulkan device extensions: {}",
            extension_list(&xr_device_extensions)
        );
        let callback_extensions = xr_device_extensions.clone();
        let open_device = {
            let hal_adapter = wgpu_adapter
                .as_hal::<wgpu::hal::api::Vulkan>()
                .ok_or("wgpu adapter is not Vulkan")?;
            hal_adapter.open_with_callback(
                required_features,
                &required_limits,
                &memory_hints,
                Some(Box::new(move |arguments| {
                    merge_extensions(arguments.extensions, callback_extensions.iter().copied());
                })),
            )?
        };
        if open_device.device.queue_family_index() != queue_family_index
            || open_device.device.queue_index() != queue_index
        {
            return Err(format!(
                "wgpu queue mismatch: family={} index={} expected family=0 index=0",
                open_device.device.queue_family_index(),
                open_device.device.queue_index()
            )
            .into());
        }
        println!(
            "combined Vulkan device extensions: {}",
            extension_list(open_device.device.enabled_device_extensions())
        );
        let raw_device_handle = open_device.device.raw_device().handle();
        let raw_queue_handle = open_device.queue.as_raw();
        let (device, queue) = wgpu_adapter
            .create_device_from_hal::<wgpu::hal::api::Vulkan>(open_device, &device_descriptor)?;

        {
            let hal_device = device
                .as_hal::<wgpu::hal::api::Vulkan>()
                .ok_or("wgpu device is not Vulkan")?;
            if hal_device.raw_device().handle() != raw_device_handle
                || hal_device.raw_physical_device() != physical_device
                || hal_device.queue_family_index() != queue_family_index
                || hal_device.queue_index() != queue_index
            {
                return Err("wgpu device raw-handle identity check failed".into());
            }
        }
        {
            let hal_queue = queue
                .as_hal::<wgpu::hal::api::Vulkan>()
                .ok_or("wgpu queue is not Vulkan")?;
            if hal_queue.as_raw() != raw_queue_handle {
                return Err("wgpu queue raw-handle identity check failed".into());
            }
        }
        println!(
            "shared Vulkan handles verified: instance=0x{:x} physical=0x{:x} device=0x{:x} queue=0x{:x} family=0 index=0",
            raw_instance_handle.as_raw(),
            physical_device.as_raw(),
            raw_device_handle.as_raw(),
            raw_queue_handle.as_raw()
        );

        let wgpu_errors = Arc::new(AtomicUsize::new(0));
        let wgpu_errors_for_callback = Arc::clone(&wgpu_errors);
        device.on_uncaptured_error(Arc::new(move |error| {
            wgpu_errors_for_callback.fetch_add(1, Ordering::SeqCst);
            eprintln!("wgpu uncaptured error: {error}");
        }));
        let device_losses = Arc::new(AtomicUsize::new(0));
        let device_losses_for_callback = Arc::clone(&device_losses);
        device.set_device_lost_callback(move |reason, message| {
            device_losses_for_callback.fetch_add(1, Ordering::SeqCst);
            eprintln!("wgpu device lost ({reason:?}): {message}");
        });

        let (session, mut frame_waiter, mut frame_stream) = xr_instance
            .create_session::<xr::Vulkan>(
                system,
                &xr::vulkan::SessionCreateInfo {
                    instance: raw_instance_handle.as_raw() as _,
                    physical_device: physical_device.as_raw() as _,
                    device: raw_device_handle.as_raw() as _,
                    queue_family_index,
                    queue_index,
                },
            )?;
        let reference_space =
            session.create_reference_space(xr::ReferenceSpaceType::LOCAL, xr::Posef::IDENTITY)?;
        let view_space = if options.geometry {
            Some(
                session
                    .create_reference_space(xr::ReferenceSpaceType::VIEW, xr::Posef::IDENTITY)?,
            )
        } else {
            None
        };
        let mut wand_input = if options.wand {
            Some(wand_input::WandInput::new(
                &xr_instance,
                &session,
                meta_touch_plus_enabled,
            )?)
        } else {
            None
        };

        let swapchain_formats = session.enumerate_swapchain_formats()?;
        let (vk_color_format, wgpu_color_format) = choose_color_format(&swapchain_formats)
            .ok_or("runtime exposes none of the supported RGBA/BGRA color formats")?;
        println!(
            "stereo swapchain: {}x{}x2, Vulkan format {}, wgpu format {:?}, sample_count=1",
            width,
            height,
            vk_color_format.as_raw(),
            wgpu_color_format
        );
        let mut swapchain = session.create_swapchain(&xr::SwapchainCreateInfo {
            create_flags: xr::SwapchainCreateFlags::EMPTY,
            usage_flags: xr::SwapchainUsageFlags::COLOR_ATTACHMENT,
            format: vk_color_format.as_raw() as _,
            sample_count: 1,
            width,
            height,
            face_count: 1,
            array_size: VIEW_COUNT,
            mip_count: 1,
        })?;
        let swapchain_images = swapchain.enumerate_images()?;
        if swapchain_images.is_empty() {
            return Err("runtime returned no swapchain images".into());
        }

        let texture_size = wgpu::Extent3d {
            width,
            height,
            depth_or_array_layers: VIEW_COUNT,
        };
        let hal_texture_descriptor = wgpu::hal::TextureDescriptor {
            label: Some("HPVR OpenXR external swapchain image"),
            size: texture_size,
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu_color_format,
            usage: wgpu::TextureUses::COLOR_TARGET,
            memory_flags: wgpu::hal::MemoryFlags::empty(),
            view_formats: Vec::new(),
        };
        let texture_descriptor = wgpu::TextureDescriptor {
            label: Some("HPVR OpenXR external swapchain image"),
            size: texture_size,
            mip_level_count: 1,
            sample_count: 1,
            dimension: wgpu::TextureDimension::D2,
            format: wgpu_color_format,
            usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
            view_formats: &[],
        };
        let drop_callbacks = Arc::new(AtomicUsize::new(0));
        let mut imported_images = ImportedImages::new(device.clone(), Arc::clone(&drop_callbacks));
        for (index, raw_image) in swapchain_images.iter().copied().enumerate() {
            let vk_image = vk::Image::from_raw(raw_image);
            let drop_callbacks_for_image = Arc::clone(&drop_callbacks);
            let hal_texture = {
                let hal_device = device
                    .as_hal::<wgpu::hal::api::Vulkan>()
                    .ok_or("wgpu device is not Vulkan during image import")?;
                hal_device.texture_from_raw(
                    vk_image,
                    &hal_texture_descriptor,
                    Some(Box::new(move || {
                        drop_callbacks_for_image.fetch_add(1, Ordering::SeqCst);
                    })),
                    wgpu::hal::vulkan::TextureMemory::External,
                )
            };
            let texture = device.create_texture_from_hal::<wgpu::hal::api::Vulkan>(
                hal_texture,
                &texture_descriptor,
            );
            let wrapped_handle = {
                let hal_texture = texture
                    .as_hal::<wgpu::hal::api::Vulkan>()
                    .ok_or("imported wgpu texture is not Vulkan")?;
                hal_texture.raw_handle()
            };
            if wrapped_handle != vk_image {
                return Err(format!(
                    "image {index} handle mismatch: Xr=0x{:x} wgpu-hal=0x{:x}",
                    vk_image.as_raw(),
                    wrapped_handle.as_raw()
                )
                .into());
            }
            println!(
                "direct import image {index}: Xr VkImage 0x{:x} == wgpu-hal VkImage 0x{:x}",
                vk_image.as_raw(),
                wrapped_handle.as_raw()
            );
            let make_eye_view = |array_layer| {
                texture.create_view(&wgpu::TextureViewDescriptor {
                    label: Some(if array_layer == 0 {
                        "HPVR wgpu left-eye layer"
                    } else {
                        "HPVR wgpu right-eye layer"
                    }),
                    format: Some(wgpu_color_format),
                    dimension: Some(wgpu::TextureViewDimension::D2),
                    usage: Some(wgpu::TextureUsages::RENDER_ATTACHMENT),
                    aspect: wgpu::TextureAspect::All,
                    base_mip_level: 0,
                    mip_level_count: Some(1),
                    base_array_layer: array_layer,
                    array_layer_count: Some(1),
                })
            };
            let left_view = make_eye_view(0);
            let right_view = make_eye_view(1);
            imported_images.push(ImportedImage {
                left_view,
                right_view,
                _texture: texture,
            });
        }
        println!(
            "direct no-copy imports ready: {}/{} runtime images; no intermediate color texture",
            imported_images.len(),
            swapchain_images.len()
        );

        let mut calibration_scene = if options.geometry {
            Some(calibration_scene::CalibrationScene::new(
                &device,
                &queue,
                wgpu_color_format,
                width,
                height,
                swapchain_images.len(),
                options.wand,
                options.gesture,
                options.flipendo_data_root.as_deref(),
                hp1_map_slice.as_ref(),
                hp1_player_start.as_ref(),
                options.hp1_eye_height_meters,
                options.hp1_bsp_collision,
                hp1_npc_preview.as_ref(),
                hp1_npc_actor.as_ref(),
                hp1_character_population.as_ref(),
                options.hp1_character_animation,
                options.hp1_npc_spell_interaction,
                options.flipendo_test_assist,
            )?)
        } else {
            None
        };

        let left_clear = wgpu::Color {
            r: 0.02,
            g: 0.62,
            b: 0.08,
            a: 1.0,
        };
        let right_clear = wgpu::Color {
            r: 0.55,
            g: 0.02,
            b: 0.72,
            a: 1.0,
        };
        let mut counters = RuntimeCounters::new(swapchain_images.len());
        let mut event_buffer = xr::EventDataBuffer::new();
        let mut session_running = false;
        let mut current_session_state = xr::SessionState::IDLE;
        let mut exit_requested = false;
        let mut rendered_frames = 0_u32;
        let mut wait_started = Instant::now();
        let mut render_wait_started = Instant::now();

        'session_loop: loop {
            while let Some(event) = xr_instance.poll_event(&mut event_buffer)? {
                use xr::Event;
                match event {
                    Event::SessionStateChanged(event) => {
                        println!("session state: {:?}", event.state());
                        current_session_state = event.state();
                        match event.state() {
                            xr::SessionState::READY => {
                                session.begin(VIEW_TYPE)?;
                                counters.session_begins += 1;
                                if let Some(input) = wand_input.as_mut() {
                                    input.on_session_begin(counters.session_begins);
                                }
                                if let Some(scene) = calibration_scene.as_mut() {
                                    scene.on_session_begin(counters.session_begins);
                                }
                                session_running = true;
                                render_wait_started = Instant::now();
                            }
                            xr::SessionState::STOPPING => {
                                if let Some(input) = wand_input.as_mut() {
                                    input.on_session_end();
                                }
                                if let Some(scene) = calibration_scene.as_mut() {
                                    scene.on_session_end();
                                }
                                session.end()?;
                                counters.session_ends += 1;
                                session_running = false;
                                wait_started = Instant::now();
                                if exit_requested {
                                    break 'session_loop;
                                }
                            }
                            xr::SessionState::EXITING | xr::SessionState::LOSS_PENDING => {
                                return Err(format!(
                                    "session terminated before clean STOPPING: {:?}",
                                    event.state()
                                )
                                .into());
                            }
                            _ => {}
                        }
                    }
                    Event::InstanceLossPending(_) => {
                        return Err("OpenXR instance loss pending".into());
                    }
                    Event::EventsLost(event) => {
                        println!("OpenXR events lost: {}", event.lost_event_count());
                        counters.events_lost = counters
                            .events_lost
                            .saturating_add(event.lost_event_count());
                    }
                    Event::ReferenceSpaceChangePending(event) => {
                        counters.reference_space_changes =
                            counters.reference_space_changes.saturating_add(1);
                        println!(
                            "OpenXR reference-space change pending: type={:?} time_ns={} pose_valid={}",
                            event.reference_space_type(),
                            event.change_time().as_nanos(),
                            event.pose_valid()
                        );
                        if event.reference_space_type() == xr::ReferenceSpaceType::LOCAL {
                            if let Some(input) = wand_input.as_mut() {
                                input.schedule_reference_space_change(event.change_time());
                            }
                        }
                    }
                    Event::InteractionProfileChanged(_) => {
                        if let Some(input) = wand_input.as_mut() {
                            println!("[wand.profile] interaction-profile-changed event");
                            input.on_interaction_profile_changed(&session);
                            if let Some(scene) = calibration_scene.as_mut() {
                                scene.on_interaction_profile_changed();
                            }
                        }
                    }
                    _ => {}
                }
            }

            if !session_running {
                if wait_started.elapsed() > SESSION_READY_TIMEOUT {
                    return Err("session did not reach READY within 120 seconds".into());
                }
                std::thread::sleep(Duration::from_millis(10));
                continue;
            }

            let frame_state = frame_waiter.wait()?;
            frame_stream.begin()?;
            counters.frames_begun += 1;
            let mut frame_ended = false;
            let mut image_acquired = false;
            let mut image_waited = false;
            let mut acquire_counted = false;
            let mut gpu_may_be_in_flight = false;
            let frame_result = (|| -> Result<FrameOutcome, Box<dyn Error>> {
                let (wand_frame, locomotion_sample, local_reset_applied) =
                    if let Some(input) = wand_input.as_mut() {
                        let (frame, locomotion) = input.sync_and_sample(
                            &session,
                            &reference_space,
                            frame_state.predicted_display_time,
                            current_session_state == xr::SessionState::FOCUSED,
                        )?;
                        (frame, locomotion, input.local_reset_applied_this_sample())
                    } else {
                        (
                            wand_input::WandFrame::Disabled,
                            wand_input::LocomotionSample::default(),
                            false,
                        )
                    };
                if let Some(scene) = calibration_scene.as_mut() {
                    if local_reset_applied {
                        scene.on_local_reference_space_reset();
                    }
                    scene.tick(
                        counters.frames_begun,
                        frame_state.predicted_display_time,
                        wand_frame,
                        locomotion_sample,
                    )?;
                }
                if !frame_state.should_render {
                    frame_stream.end(
                        frame_state.predicted_display_time,
                        environment_blend_mode,
                        &[],
                    )?;
                    counters.frames_ended += 1;
                    counters.skipped += 1;
                    frame_ended = true;
                    return Ok(FrameOutcome::Skipped);
                }

                let (view_state, views) = session.locate_views(
                    VIEW_TYPE,
                    frame_state.predicted_display_time,
                    &reference_space,
                )?;
                if views.len() != VIEW_COUNT as usize {
                    return Err("locate_views did not return exactly two eyes".into());
                }
                let required_view_state = if options.geometry {
                    calibration_scene::required_view_flags()
                } else {
                    xr::ViewStateFlags::ORIENTATION_VALID | xr::ViewStateFlags::POSITION_VALID
                };
                if !view_state.contains(required_view_state) {
                    println!("skipping frame with invalid view state: {view_state:?}");
                    frame_stream.end(
                        frame_state.predicted_display_time,
                        environment_blend_mode,
                        &[],
                    )?;
                    counters.frames_ended += 1;
                    counters.skipped += 1;
                    frame_ended = true;
                    return Ok(FrameOutcome::Skipped);
                }

                let head_pose = if let Some(view_space) = view_space.as_ref() {
                    let head_location =
                        view_space.locate(&reference_space, frame_state.predicted_display_time)?;
                    if !head_location
                        .location_flags
                        .contains(calibration_scene::required_head_flags())
                    {
                        println!(
                            "skipping geometry frame with invalid/untracked head pose: {:?}",
                            head_location.location_flags
                        );
                        frame_stream.end(
                            frame_state.predicted_display_time,
                            environment_blend_mode,
                            &[],
                        )?;
                        counters.frames_ended += 1;
                        counters.skipped += 1;
                        frame_ended = true;
                        return Ok(FrameOutcome::Skipped);
                    }
                    Some(head_location.pose)
                } else {
                    None
                };
                let stereo_views = [views[0], views[1]];

                let image_index = swapchain.acquire_image()?;
                image_acquired = true;
                counters.on_acquire(image_index as usize)?;
                acquire_counted = true;
                if let Err(error) = swapchain.wait_image(xr::Duration::INFINITE) {
                    eprintln!("xrWaitSwapchainImage failed after acquire: {error}");
                    abort_without_destructors("an acquired OpenXR image cannot be safely released");
                }
                image_waited = true;
                counters.waited += 1;

                let targets = imported_images
                    .get(image_index as usize)
                    .ok_or("acquired image has no imported wgpu views")?;
                let mut encoder = device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
                    label: Some(if options.gesture {
                        "HPVR Gate A5 trajectory-projection encoder"
                    } else if options.wand {
                        "HPVR Gate A4 tracked-wand encoder"
                    } else if options.geometry {
                        "HPVR Gate A3 tracked-geometry encoder"
                    } else {
                        "HPVR OpenXR direct-wgpu clear encoder"
                    }),
                });
                if let Some(scene) = calibration_scene.as_mut() {
                    scene.encode_frame(
                        &queue,
                        &mut encoder,
                        calibration_scene::CalibrationFrame {
                            image_index: image_index as usize,
                            color_views: [&targets.left_view, &targets.right_view],
                            views: &stereo_views,
                            head_pose: head_pose.ok_or("geometry mode has no tracked head pose")?,
                            predicted_display_time: frame_state.predicted_display_time,
                        },
                    )?;
                } else {
                    for (view, clear, label) in [
                        (&targets.left_view, left_clear, "HPVR wgpu left clear"),
                        (&targets.right_view, right_clear, "HPVR wgpu right clear"),
                    ] {
                        let color_attachments = [Some(wgpu::RenderPassColorAttachment {
                            view,
                            depth_slice: None,
                            resolve_target: None,
                            ops: wgpu::Operations {
                                load: wgpu::LoadOp::Clear(clear),
                                store: wgpu::StoreOp::Store,
                            },
                        })];
                        let pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                            label: Some(label),
                            color_attachments: &color_attachments,
                            depth_stencil_attachment: None,
                            timestamp_writes: None,
                            occlusion_query_set: None,
                            multiview_mask: None,
                        });
                        drop(pass);
                    }
                }
                gpu_may_be_in_flight = true;
                let submission_index = queue.submit([encoder.finish()]);
                counters.submissions += 1;
                let completion_wait_started = Instant::now();
                device.poll(wgpu::PollType::Wait {
                    submission_index: Some(submission_index),
                    timeout: Some(GPU_WAIT_TIMEOUT),
                })?;
                if let Some(scene) = calibration_scene.as_mut() {
                    scene.record_completion_wait(completion_wait_started.elapsed());
                }
                gpu_may_be_in_flight = false;
                counters.gpu_completions += 1;
                if wgpu_errors.load(Ordering::SeqCst) != 0 {
                    return Err("wgpu reported an uncaptured error".into());
                }
                if device_losses.load(Ordering::SeqCst) != 0 {
                    return Err("wgpu reported device loss".into());
                }

                if let Err(error) = swapchain.release_image() {
                    eprintln!("xrReleaseSwapchainImage failed after GPU completion: {error}");
                    abort_without_destructors("OpenXR image release state is unknown");
                }
                image_acquired = false;
                image_waited = false;
                counters.on_release()?;
                acquire_counted = false;

                let image_rect = xr::Rect2Di {
                    offset: xr::Offset2Di { x: 0, y: 0 },
                    extent: xr::Extent2Di {
                        width: width as i32,
                        height: height as i32,
                    },
                };
                let projection_views = [
                    xr::CompositionLayerProjectionView::new()
                        .pose(views[0].pose)
                        .fov(views[0].fov)
                        .sub_image(
                            xr::SwapchainSubImage::new()
                                .swapchain(&swapchain)
                                .image_array_index(0)
                                .image_rect(image_rect),
                        ),
                    xr::CompositionLayerProjectionView::new()
                        .pose(views[1].pose)
                        .fov(views[1].fov)
                        .sub_image(
                            xr::SwapchainSubImage::new()
                                .swapchain(&swapchain)
                                .image_array_index(1)
                                .image_rect(image_rect),
                        ),
                ];
                let projection_layer = xr::CompositionLayerProjection::new()
                    .space(&reference_space)
                    .views(&projection_views);
                frame_stream.end(
                    frame_state.predicted_display_time,
                    environment_blend_mode,
                    &[&projection_layer],
                )?;
                counters.frames_ended += 1;
                counters.rendered += 1;
                frame_ended = true;
                Ok(FrameOutcome::Rendered(view_state))
            })();

            let view_state = match frame_result {
                Ok(FrameOutcome::Rendered(view_state)) => {
                    render_wait_started = Instant::now();
                    view_state
                }
                Ok(FrameOutcome::Skipped) => {
                    if render_wait_started.elapsed() > SESSION_READY_TIMEOUT {
                        return Err("session produced no renderable frame for 120 seconds".into());
                    }
                    continue;
                }
                Err(error) => {
                    if image_acquired && !image_waited {
                        if let Err(cleanup_error) = swapchain.wait_image(xr::Duration::INFINITE) {
                            eprintln!(
                                "cleanup xrWaitSwapchainImage failed after '{error}': {cleanup_error}"
                            );
                            abort_without_destructors(
                                "an acquired OpenXR image cannot be safely released during unwind",
                            );
                        }
                        image_waited = true;
                        if acquire_counted {
                            counters.waited += 1;
                        }
                    }
                    if gpu_may_be_in_flight
                        && device
                            .poll(wgpu::PollType::Wait {
                                submission_index: None,
                                timeout: Some(GPU_WAIT_TIMEOUT),
                            })
                            .is_err()
                    {
                        abort_without_destructors(
                            "GPU completion was not proven after a frame error",
                        );
                    }
                    if image_acquired && image_waited {
                        if let Err(cleanup_error) = swapchain.release_image() {
                            eprintln!(
                                "cleanup xrReleaseSwapchainImage failed after '{error}': {cleanup_error}"
                            );
                            abort_without_destructors(
                                "OpenXR image release state is unknown during unwind",
                            );
                        }
                        if acquire_counted {
                            if let Err(counter_error) = counters.on_release() {
                                eprintln!("cleanup counter mismatch: {counter_error}");
                            }
                        }
                    }
                    if !frame_ended
                        && frame_stream
                            .end(
                                frame_state.predicted_display_time,
                                environment_blend_mode,
                                &[],
                            )
                            .is_ok()
                    {
                        counters.frames_ended += 1;
                    }
                    return Err(error);
                }
            };

            rendered_frames += 1;
            if rendered_frames == 1 {
                if options.hp1_map_slice.is_some() {
                    if options.hp1_player_start_ordinal.is_some() {
                        println!(
                            "first Gate B9 frame submitted; bounded textured HP1 BSP preview uses selected PlayerStart in LOCAL space"
                        );
                    } else {
                        println!(
                            "first Gate B9 frame submitted; bounded textured HP1 BSP preview uses diagnostic centering in LOCAL space"
                        );
                    }
                } else if options.flipendo_data_root.is_some() {
                    println!(
                        "first Gate A6 frame submitted; accepted Flipendo events drive only the isolated fixed test target"
                    );
                } else if options.gesture {
                    println!(
                        "first Gate A5 frame submitted; timestamped raw capture is separate from the visual trail"
                    );
                } else if options.wand {
                    println!(
                        "first Gate A4 frame submitted; view_state={view_state:?}; room and provisional wand use LOCAL space"
                    );
                } else if options.geometry {
                    println!(
                        "first Gate A3 frame submitted; view_state={view_state:?}; metric room fixed in LOCAL space"
                    );
                } else {
                    println!(
                        "first direct-wgpu stereo frame submitted; view_state={view_state:?}; left=green right=purple"
                    );
                }
            }
            if rendered_frames >= options.frame_count && !exit_requested {
                println!(
                    "submitted {rendered_frames} {} frames; requesting clean exit",
                    if options.hp1_map_slice.is_some() {
                        "Gate B8 diagnostic-map-slice"
                    } else if options.flipendo_data_root.is_some() {
                        "Gate A6 Flipendo-test-target"
                    } else if options.gesture {
                        "Gate A5 trajectory-projection"
                    } else if options.wand {
                        "Gate A4 tracked-wand"
                    } else if options.geometry {
                        "Gate A3 tracked-geometry"
                    } else {
                        "direct-wgpu stereo"
                    }
                );
                session.request_exit()?;
                exit_requested = true;
            }
        }

        counters.verify()?;
        println!(
            "balanced counters: session_begin={} session_end={} frame_begin={} frame_end={} rendered={} skipped={} acquire={} wait={} submit={} gpu_complete={} release={} max_outstanding={} events_lost={} reference_space_changes={} per_image={:?}",
            counters.session_begins,
            counters.session_ends,
            counters.frames_begun,
            counters.frames_ended,
            counters.rendered,
            counters.skipped,
            counters.acquired,
            counters.waited,
            counters.submissions,
            counters.gpu_completions,
            counters.released,
            counters.max_outstanding_images,
            counters.events_lost,
            counters.reference_space_changes,
            counters.per_image_acquisitions
        );
        if let Some(scene) = calibration_scene.as_ref() {
            let motion_conclusive = scene.verify_and_report(
                counters.frames_begun,
                counters.rendered,
                counters.reference_space_changes,
            )?;
            println!(
                "[geo.result] runtime_invariants=PASS motion_coverage={} visual_acceptance=PENDING",
                if motion_conclusive {
                    "PASS"
                } else {
                    "INCONCLUSIVE"
                }
            );
            if options.hp1_map_slice.is_some() {
                println!(
                    "[hp1.map.result] runtime_invariants=PASS placement={} texture_sampling=NOT_IMPLEMENTED gameplay_transform=NOT_IMPLEMENTED visual_acceptance=PENDING",
                    if options.hp1_player_start_ordinal.is_some() {
                        "PLAYER_START"
                    } else {
                        "DIAGNOSTIC_CENTERED"
                    }
                );
            }
        }
        if let Some(input) = wand_input.as_ref() {
            let tracking_conclusive = input.verify_and_report(counters.frames_begun)?;
            println!(
                "[wand.result] runtime_invariants=PASS controller_tracking={} visual_acceptance=PENDING gesture_recognition=NOT_IMPLEMENTED",
                if tracking_conclusive {
                    "PASS"
                } else {
                    "INCONCLUSIVE"
                }
            );
        }
        drop(calibration_scene.take());
        imported_images.clear_and_verify()?;
        let final_wgpu_errors = wgpu_errors.load(Ordering::SeqCst);
        let final_device_losses = device_losses.load(Ordering::SeqCst);
        if final_wgpu_errors != 0 || final_device_losses != 0 {
            return Err(format!(
                "final wgpu callback counts are nonzero: errors={final_wgpu_errors} device_losses={final_device_losses}"
            )
            .into());
        }
        println!(
            "final wgpu runtime errors: {final_wgpu_errors}; device losses: {final_device_losses}"
        );
        drop(imported_images);
        drop(swapchain);
        drop(wand_input.take());
        drop(view_space);
        drop(reference_space);
        drop(frame_waiter);
        drop(frame_stream);
        drop(session);
        drop(queue);
        drop(device);
        drop(wgpu_adapter);
        drop(wgpu_instance);
    }

    println!(
        "{} probe exited cleanly",
        if options.hp1_map_slice.is_some() {
            "Gate B8 diagnostic-map-slice"
        } else if options.flipendo_data_root.is_some() {
            "Gate A6 Flipendo-test-target"
        } else if options.gesture {
            "Gate A5 trajectory-projection"
        } else if options.wand {
            "Gate A4 tracked-wand"
        } else if options.geometry {
            "Gate A3 tracked-geometry"
        } else {
            "direct-wgpu stereo-clear"
        }
    );
    Ok(())
}
