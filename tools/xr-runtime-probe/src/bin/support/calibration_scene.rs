use std::{
    error::Error,
    num::NonZeroU64,
    path::Path,
    time::{Duration, Instant},
};

use bytemuck::{Pod, Zeroable};
use glam::{Mat4, Quat, Vec3, Vec3Swizzles, Vec4};
use openxr as xr;
use wgpu::util::DeviceExt;

use super::gesture_projection::{
    GestureProjectionCapture, ProjectionVisualState, SpellCastEvent, SpellKind,
};
use super::hp1_bsp_collision::BspCollision;
#[cfg(test)]
use super::hp1_bsp_ffi::MapSliceVertex;
use super::hp1_bsp_ffi::{ActorVisual, MapSlice, PlayerStart};
use super::hp1_character_population::CharacterPopulation;
use super::hp1_npc_preview::NpcPreview;
use super::hp1_npc_spell_interaction::NpcSpellInteraction;
use super::hp1_wand_model::HarryWandModel;
use super::wand_input::{LocomotionSample, WandFrame, WandSample};

const DEPTH_FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Depth32Float;
const NEAR_Z: f32 = 0.05;
// Lev_Tut1 at the provisional 0.02 scale reaches about 153.3 m from its
// PlayerStart AABB corner. A 30 m plane created a head-relative polygonal hole
// in otherwise valid distant BSP, so retain bounded margin for the full map.
const FAR_Z: f32 = 200.0;
const MIN_IPD_METERS: f32 = 0.04;
const MAX_IPD_METERS: f32 = 0.09;
const REQUIRED_TRANSLATION_METERS: f32 = 0.04;
const REQUIRED_ROTATION_RADIANS: f32 = 8.0_f32.to_radians();
const MAX_DYNAMIC_VERTICES: usize = 49_152;
const MAX_TRAIL_POINTS: usize = 256;
const MAX_TEMPLATE_OVERLAY_POINTS: usize = 1024;
const TRAIL_MIN_STEP_METERS: f32 = 0.004;
const TRAIL_MAX_STEP_METERS: f32 = 0.25;
const TRAIL_MAX_GAP_NS: i64 = 100_000_000;
const SPELL_TARGET_BASE: Vec3 = Vec3::new(0.0, -0.08, -1.65);
const SPELL_TARGET_RADIUS_METERS: f32 = 0.115;
const SPELL_TARGET_RING_SEGMENTS: usize = 20;
const SPELL_REACTION_SECONDS: f32 = 1.35;
const SPELL_BEAM_SECONDS: f32 = 0.32;
const LOCOMOTION_SPEED_METERS_PER_SECOND: f32 = 1.8;
const LOCOMOTION_DEADZONE: f32 = 0.18;
const SNAP_TURN_ENTER: f32 = 0.72;
const SNAP_TURN_RELEASE: f32 = 0.35;
const SNAP_TURN_RADIANS: f32 = 30.0_f32.to_radians();

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct Vertex {
    position: [f32; 3],
    color: [f32; 3],
    animated: f32,
    texture_uv: [f32; 2],
    texture_layer: f32,
}

impl Vertex {
    const ATTRIBUTES: [wgpu::VertexAttribute; 5] = wgpu::vertex_attr_array![
        0 => Float32x3,
        1 => Float32x3,
        2 => Float32,
        3 => Float32x2,
        4 => Float32
    ];

    fn layout() -> wgpu::VertexBufferLayout<'static> {
        wgpu::VertexBufferLayout {
            array_stride: std::mem::size_of::<Self>() as wgpu::BufferAddress,
            step_mode: wgpu::VertexStepMode::Vertex,
            attributes: &Self::ATTRIBUTES,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Pod, Zeroable)]
struct EyeUniform {
    view_projection: [[f32; 4]; 4],
    eye_position: [f32; 4],
    frame_data: [f32; 4],
}

struct DepthTarget {
    left_view: wgpu::TextureView,
    right_view: wgpu::TextureView,
    _texture: wgpu::Texture,
}

#[derive(Clone, Copy)]
struct FrameSnapshot {
    tick_id: u64,
    predicted_display_ns: i64,
    phase: f32,
    model_hash: u64,
    dynamic_vertex_count: u32,
    dynamic_hash: u64,
    dynamic_geometry_present: bool,
    trail_point_count: u32,
}

struct CharacterAnimation {
    vertex_offset_bytes: wgpu::BufferAddress,
    frames: Vec<Vec<Vertex>>,
    last_uploaded_frame: Option<usize>,
    uploads: u32,
    scratch_frame: Vec<Vertex>,
    last_uploaded_tick: Option<u64>,
    reactions_were_active: bool,
}

#[derive(Default)]
struct WandVisualState {
    trail_points: Vec<Vec3>,
    stroke_active: bool,
    last_tip: Option<Vec3>,
    last_sample_ns: Option<i64>,
}

#[derive(Default)]
struct WandRenderTelemetry {
    visible_tick_frames: u32,
    hidden_tick_frames: u32,
    dynamic_rendered_frames: u32,
    dynamic_uploads: u32,
    max_uploads_per_frame: u32,
    duplicate_uploads: u32,
    last_dynamic_upload_tick: Option<u64>,
    dynamic_left_draws: u32,
    dynamic_right_draws: u32,
    snapshot_hash_mismatches: u32,
    capacity_overflows: u32,
    nonfinite_vertices: u32,
    strokes_started: u32,
    strokes_completed: u32,
    strokes_canceled: u32,
    points_recorded: u32,
    points_trimmed: u32,
    gap_breaks: u32,
    teleports_rejected: u32,
    first_sample_logged: bool,
}

#[derive(Default)]
struct SpellTargetTelemetry {
    events_received: u32,
    impulses_applied: u32,
    duplicate_or_reordered_events: u32,
    reactions_completed: u32,
}

struct SpellTargetState {
    enabled: bool,
    position: Vec3,
    reaction_direction: Vec3,
    reaction_elapsed_seconds: f32,
    reaction_active: bool,
    beam_start: Vec3,
    beam_end: Vec3,
    beam_remaining_seconds: f32,
    last_event_serial: Option<u64>,
    telemetry: SpellTargetTelemetry,
}

impl SpellTargetState {
    fn new(enabled: bool) -> Self {
        Self {
            enabled,
            position: SPELL_TARGET_BASE,
            reaction_direction: Vec3::NEG_Z,
            reaction_elapsed_seconds: 0.0,
            reaction_active: false,
            beam_start: SPELL_TARGET_BASE,
            beam_end: SPELL_TARGET_BASE,
            beam_remaining_seconds: 0.0,
            last_event_serial: None,
            telemetry: SpellTargetTelemetry::default(),
        }
    }

    fn reset_visuals(&mut self) {
        self.position = SPELL_TARGET_BASE;
        self.reaction_elapsed_seconds = 0.0;
        self.reaction_active = false;
        self.beam_remaining_seconds = 0.0;
    }

    fn consume(&mut self, event: SpellCastEvent) -> Result<(), Box<dyn Error>> {
        if !self.enabled {
            return Err("spell target received an event while disabled".into());
        }
        if self
            .last_event_serial
            .is_some_and(|last_serial| event.serial <= last_serial)
        {
            self.telemetry.duplicate_or_reordered_events += 1;
            return Err(format!(
                "spell target received duplicate/reordered serial {} after {:?}",
                event.serial, self.last_event_serial
            )
            .into());
        }
        if event.spell != SpellKind::Flipendo
            || !event.tip.is_finite()
            || !event.aim_origin.is_finite()
            || !event.aim_direction.is_finite()
            || (event.aim_direction.length() - 1.0).abs() > 1.0e-3
            || !event.score.is_finite()
            || !event.threshold.is_finite()
            || event.score < event.threshold
        {
            return Err("spell target rejected an invalid accepted-spell event".into());
        }

        let to_target = SPELL_TARGET_BASE - event.aim_origin;
        let ray_distance = to_target.dot(event.aim_direction).max(0.0);
        let aim_miss_m =
            (event.aim_origin + event.aim_direction * ray_distance).distance(SPELL_TARGET_BASE);
        let horizontal_push = Vec3::new(to_target.x, 0.0, to_target.z)
            .try_normalize()
            .unwrap_or(Vec3::NEG_Z);

        self.telemetry.events_received += 1;
        self.telemetry.impulses_applied += 1;
        self.last_event_serial = Some(event.serial);
        self.reaction_direction = horizontal_push;
        self.reaction_elapsed_seconds = 0.0;
        self.reaction_active = true;
        self.beam_start = event.tip;
        self.beam_end = self.position;
        self.beam_remaining_seconds = SPELL_BEAM_SECONDS;
        println!(
            "[spell.target] serial={} spell={} predicted_ns={} selection=FIXED_TEST_TARGET outcome=IMPULSE_APPLIED score={:.6} threshold={:.6} aim_miss_m={:.4}",
            event.serial,
            event.spell.label(),
            event.predicted_display_time_ns,
            event.score,
            event.threshold,
            aim_miss_m
        );
        Ok(())
    }

    fn advance(&mut self, delta_seconds: f32) {
        if !self.enabled {
            return;
        }
        self.beam_remaining_seconds =
            (self.beam_remaining_seconds - delta_seconds.min(0.05)).max(0.0);
        if !self.reaction_active {
            self.position = SPELL_TARGET_BASE;
            return;
        }
        self.reaction_elapsed_seconds += delta_seconds.min(0.05);
        let progress = (self.reaction_elapsed_seconds / SPELL_REACTION_SECONDS).clamp(0.0, 1.0);
        let arc = (std::f32::consts::PI * progress).sin();
        self.position =
            SPELL_TARGET_BASE + self.reaction_direction * (0.62 * arc) + Vec3::Y * (0.20 * arc);
        self.beam_end = self.position;
        if progress >= 1.0 {
            self.position = SPELL_TARGET_BASE;
            self.reaction_active = false;
            self.telemetry.reactions_completed += 1;
        }
    }
}

#[derive(Default)]
struct LocomotionTelemetry {
    active_move_frames: u32,
    distance_meters: f32,
    snap_turns: u32,
    resets: u32,
    nonfinite_rejections: u32,
    collision_blocked_substeps: u32,
    grounded_substeps: u32,
    vertical_adjustment_meters: f32,
}

struct LocomotionState {
    enabled: bool,
    eye_height_meters: f32,
    initial_world_translation: Vec3,
    world_translation: Vec3,
    yaw_radians: f32,
    snap_armed: bool,
    last_head_position_local: Option<Vec3>,
    last_head_forward_local: Option<Vec3>,
    telemetry: LocomotionTelemetry,
}

impl LocomotionState {
    fn new(enabled: bool, eye_height_meters: f32, initial_ground_adjustment: f32) -> Self {
        let initial_world_translation =
            Vec3::new(0.0, eye_height_meters + initial_ground_adjustment, 0.0);
        Self {
            enabled,
            eye_height_meters,
            initial_world_translation,
            world_translation: initial_world_translation,
            yaw_radians: 0.0,
            snap_armed: true,
            last_head_position_local: None,
            last_head_forward_local: None,
            telemetry: LocomotionTelemetry::default(),
        }
    }

    fn reset(&mut self) {
        self.world_translation = self.initial_world_translation;
        self.yaw_radians = 0.0;
        self.snap_armed = true;
        self.last_head_position_local = None;
        self.last_head_forward_local = None;
        self.telemetry.resets = self.telemetry.resets.saturating_add(1);
    }

    fn observe_head(&mut self, pose: xr::Posef) -> Result<(), Box<dyn Error>> {
        let position = vector_from_xr(pose.position)?;
        let (orientation, _) = quaternion_from_xr(pose.orientation)?;
        let forward = orientation * Vec3::NEG_Z;
        let horizontal = Vec3::new(forward.x, 0.0, forward.z)
            .try_normalize()
            .unwrap_or(Vec3::NEG_Z);
        self.last_head_position_local = Some(position);
        self.last_head_forward_local = Some(horizontal);
        Ok(())
    }

    fn tick(
        &mut self,
        sample: LocomotionSample,
        delta_seconds: f32,
        bsp_collision: Option<&BspCollision>,
    ) {
        if !self.enabled {
            return;
        }
        if !sample.move_axis.is_finite() || !sample.turn_axis.is_finite() {
            self.telemetry.nonfinite_rejections =
                self.telemetry.nonfinite_rejections.saturating_add(1);
            return;
        }

        if sample.move_active {
            let axis = radial_deadzone(sample.move_axis, LOCOMOTION_DEADZONE);
            if axis.length_squared() > 0.0 {
                let local_forward = self.last_head_forward_local.unwrap_or(Vec3::NEG_Z);
                let local_right = local_forward.cross(Vec3::Y).normalize_or_zero();
                let yaw = Quat::from_rotation_y(self.yaw_radians);
                let direction = yaw * (local_right * axis.x + local_forward * axis.y);
                let displacement =
                    direction * LOCOMOTION_SPEED_METERS_PER_SECOND * delta_seconds.min(0.05);
                let movement = bsp_collision.map_or_else(
                    || super::hp1_bsp_collision::CollisionMove {
                        displacement,
                        ..Default::default()
                    },
                    |collision| collision.resolve_movement(self.capsule_center(), displacement),
                );
                self.world_translation += movement.displacement;
                self.telemetry.active_move_frames =
                    self.telemetry.active_move_frames.saturating_add(1);
                self.telemetry.distance_meters += movement.displacement.xz().length();
                self.telemetry.collision_blocked_substeps = self
                    .telemetry
                    .collision_blocked_substeps
                    .saturating_add(movement.blocked_substeps);
                self.telemetry.grounded_substeps = self
                    .telemetry
                    .grounded_substeps
                    .saturating_add(movement.grounded_substeps);
                self.telemetry.vertical_adjustment_meters += movement.displacement.y.abs();
            }
        }

        if !sample.turn_active || sample.turn_axis.abs() <= SNAP_TURN_RELEASE {
            self.snap_armed = true;
        } else if self.snap_armed && sample.turn_axis.abs() >= SNAP_TURN_ENTER {
            let delta_yaw = -sample.turn_axis.signum() * SNAP_TURN_RADIANS;
            let old_rotation = Quat::from_rotation_y(self.yaw_radians);
            let head_local = self.last_head_position_local.unwrap_or(Vec3::ZERO);
            let head_world = old_rotation * head_local + self.world_translation;
            self.yaw_radians = (self.yaw_radians + delta_yaw).rem_euclid(std::f32::consts::TAU);
            let new_rotation = Quat::from_rotation_y(self.yaw_radians);
            self.world_translation = head_world - new_rotation * head_local;
            self.snap_armed = false;
            self.telemetry.snap_turns = self.telemetry.snap_turns.saturating_add(1);
        }
    }

    fn rotation(&self) -> Quat {
        Quat::from_rotation_y(self.yaw_radians)
    }

    fn capsule_center(&self) -> Vec3 {
        let head = self.last_head_position_local.unwrap_or(Vec3::ZERO);
        let body_local = Vec3::new(head.x, 0.0, head.z);
        self.rotation() * body_local + self.world_translation - Vec3::Y * self.eye_height_meters
    }

    fn world_from_local(&self) -> Mat4 {
        Mat4::from_rotation_translation(self.rotation(), self.world_translation)
    }

    fn map_wand(&self, frame: WandFrame) -> WandFrame {
        let WandFrame::Tracked(mut sample) = frame else {
            return frame;
        };
        let rotation = self.rotation();
        let map_position = |position: Vec3| rotation * position + self.world_translation;
        sample.prop_root = map_position(sample.prop_root);
        sample.prop_orientation = (rotation * sample.prop_orientation).normalize();
        sample.tip = map_position(sample.tip);
        sample.aim_origin = map_position(sample.aim_origin);
        sample.aim_direction = (rotation * sample.aim_direction).normalize();
        WandFrame::Tracked(sample)
    }
}

#[derive(Default)]
struct MotionTelemetry {
    first_head_position: Option<Vec3>,
    first_head_orientation: Option<Quat>,
    position_min: Vec3,
    position_max: Vec3,
    rotation_min: Vec3,
    rotation_max: Vec3,
    ipd_min: f32,
    ipd_max: f32,
    ipd_sum: f64,
    samples: u32,
    max_projection_boundary_error: f32,
    max_quaternion_norm_error: f32,
    head_local_x_min: f32,
    head_local_yz_max: f32,
}

pub(super) struct CalibrationFrame<'a> {
    pub image_index: usize,
    pub color_views: [&'a wgpu::TextureView; 2],
    pub views: &'a [xr::View; 2],
    pub head_pose: xr::Posef,
    pub predicted_display_time: xr::Time,
}

pub(super) struct CalibrationScene {
    pipeline: wgpu::RenderPipeline,
    vertex_buffer: wgpu::Buffer,
    vertex_count: u32,
    character_animation: Option<CharacterAnimation>,
    dynamic_vertex_buffer: Option<wgpu::Buffer>,
    dynamic_vertices: Vec<Vertex>,
    uniform_buffers: [wgpu::Buffer; 2],
    bind_groups: [wgpu::BindGroup; 2],
    _map_texture: wgpu::Texture,
    depth_targets: Vec<DepthTarget>,
    model_hash: u64,
    current_snapshot: Option<FrameSnapshot>,
    last_tick_frame: Option<u32>,
    last_predicted_display_ns: Option<i64>,
    phase: f32,
    simulation_ticks: u32,
    max_ticks_per_frame: u32,
    session_generation: u32,
    left_draws: u32,
    right_draws: u32,
    snapshot_mismatches: u32,
    encode_cpu_total: Duration,
    gpu_completion_wait_total: Duration,
    first_frame_logged: bool,
    motion: MotionTelemetry,
    wand_enabled: bool,
    harry_wand_model: Option<HarryWandModel>,
    wand_state: WandVisualState,
    wand_telemetry: WandRenderTelemetry,
    gesture_projection: Option<GestureProjectionCapture>,
    gesture_visual_state: ProjectionVisualState,
    gesture_feedback_marker: Option<(Vec3, ProjectionVisualState)>,
    spell_target: SpellTargetState,
    locomotion: LocomotionState,
    bsp_collision: Option<BspCollision>,
    npc_spell_interaction: Option<NpcSpellInteraction>,
}

impl CalibrationScene {
    pub(super) fn new(
        device: &wgpu::Device,
        queue: &wgpu::Queue,
        color_format: wgpu::TextureFormat,
        width: u32,
        height: u32,
        image_count: usize,
        wand_enabled: bool,
        gesture_enabled: bool,
        flipendo_data_root: Option<&Path>,
        hp1_map_slice: Option<&MapSlice>,
        hp1_player_start: Option<&PlayerStart>,
        hp1_eye_height_meters: f32,
        hp1_bsp_collision_enabled: bool,
        hp1_npc_preview: Option<&NpcPreview>,
        hp1_npc_actor: Option<&ActorVisual>,
        hp1_character_population: Option<&CharacterPopulation>,
        hp1_character_animation_enabled: bool,
        hp1_npc_spell_interaction_enabled: bool,
        flipendo_test_assist: bool,
    ) -> Result<Self, Box<dyn Error>> {
        if gesture_enabled && !wand_enabled {
            return Err("gesture projection requires the tracked-wand mode".into());
        }
        let harry_wand_model = if wand_enabled {
            flipendo_data_root.map(HarryWandModel::load).transpose()?
        } else {
            None
        };
        let spell_target_enabled =
            flipendo_data_root.is_some() && !hp1_npc_spell_interaction_enabled;
        if hp1_player_start.is_some() {
            println!(
                "[hp1.player.eye] offset_m={hp1_eye_height_meters:.3} source=EXPLICIT_CLASS_DEFAULT"
            );
        }
        let gesture_projection = if gesture_enabled {
            Some(GestureProjectionCapture::new_with_test_assist(
                flipendo_data_root,
                flipendo_test_assist,
            )?)
        } else {
            None
        };
        let mut map_placement_transform = None;
        let mut character_animation = None;
        let mut npc_spell_interaction = None;
        let (mut vertices, vertex_buffer_label) = if let Some(slice) = hp1_map_slice {
            let (vertices, translation, yaw_radians, placement) =
                build_hp1_map_slice_geometry(slice, hp1_player_start)?;
            map_placement_transform = Some((Quat::from_rotation_y(yaw_radians), translation));
            println!(
                "[hp1.map.preview] transform={placement} translation_m={translation:?} yaw_radians={yaw_radians} texture_sampling=P8_ARRAY decoded_textures={} fallback_materials={} fallback_triangles={} layers={} layer_size={}x{}",
                slice.decoded_texture_count,
                slice.fallback_material_count,
                slice.fallback_triangle_count,
                slice.texture_layer_count,
                slice.texture_layer_width,
                slice.texture_layer_height,
            );
            (vertices, "HPVR Gate B8 diagnostic HP1 BSP geometry")
        } else {
            (
                build_calibration_geometry(),
                "HPVR Gate A3 procedural calibration geometry",
            )
        };
        let bsp_collision = if hp1_bsp_collision_enabled {
            let slice = hp1_map_slice.ok_or("HP1 BSP collision requires a loaded map")?;
            let (map_rotation, map_translation) = map_placement_transform
                .ok_or("HP1 BSP collision requires a PlayerStart placement transform")?;
            Some(BspCollision::new(
                slice,
                map_rotation,
                map_translation,
                hp1_eye_height_meters * (15.0 / 40.75),
                hp1_eye_height_meters * (42.0 / 40.75),
            )?)
        } else {
            None
        };
        let initial_ground_adjustment = bsp_collision
            .as_ref()
            .and_then(|collision| collision.ground_adjustment(Vec3::ZERO))
            .unwrap_or(0.0);
        if bsp_collision.is_some() {
            println!(
                "[hp1.player.spawn] ground_adjustment_m={initial_ground_adjustment:.3} collision=BSP_CAPSULE"
            );
        }
        if let Some(npc) = hp1_npc_preview {
            let texture_layer_base = hp1_map_slice.map_or(0, |slice| slice.texture_layer_count);
            if let Some(actor) = hp1_npc_actor {
                let (map_rotation, map_translation) = map_placement_transform
                    .ok_or("HP1 NPC actor placement requires an HP1 map placement transform")?;
                if !actor.location_serialized || !actor.position_m.is_finite() {
                    return Err("HP1 NPC actor has no finite serialized Location".into());
                }
                if actor.rotation_units[0] != 0 || actor.rotation_units[2] != 0 {
                    return Err(
                        "HP1 NPC actor placement currently requires zero pitch and roll".into(),
                    );
                }
                let actor_yaw = actor.rotation_units[1] as f32 * std::f32::consts::TAU / 65_536.0;
                let actor_rotation = Quat::from_rotation_y(actor_yaw);
                vertices.extend(npc.vertices.iter().map(|source| {
                    let bind_local =
                        (source.position_m - npc.diagnostic_translation) * actor.draw_scale;
                    let position = map_rotation * (actor.position_m + actor_rotation * bind_local)
                        + map_translation;
                    Vertex {
                        position: position.to_array(),
                        color: Vec3::ONE.to_array(),
                        animated: 0.0,
                        texture_uv: source.texture_uv,
                        texture_layer: (texture_layer_base + source.material_index) as f32,
                    }
                }));
                println!(
                    "[hp1.npc.preview] placement=LEVEL_ACTOR actor_ref={} actor_slot={} object={} position_m={:?} rotation_units={:?} actor_yaw_radians={} draw_scale={} points={} wedges={} faces={} vertices={} materials={} names={} scale_source=EXPLICIT",
                    actor.actor_reference,
                    actor.actor_slot_index,
                    actor.object_name,
                    actor.position_m,
                    actor.rotation_units,
                    actor_yaw,
                    actor.draw_scale,
                    npc.point_count,
                    npc.wedge_count,
                    npc.face_count,
                    npc.vertices.len(),
                    npc.texture_layer_count,
                    npc.material_names.join(","),
                );
            } else {
                vertices.extend(npc.vertices.iter().map(|source| Vertex {
                    position: source.position_m.to_array(),
                    color: Vec3::ONE.to_array(),
                    animated: 0.0,
                    texture_uv: source.texture_uv,
                    texture_layer: (texture_layer_base + source.material_index) as f32,
                }));
                println!(
                    "[hp1.npc.preview] placement=DIAGNOSTIC_FRONT points={} wedges={} faces={} vertices={} materials={} names={} scale_source=EXPLICIT",
                    npc.point_count,
                    npc.wedge_count,
                    npc.face_count,
                    npc.vertices.len(),
                    npc.texture_layer_count,
                    npc.material_names.join(","),
                );
            }
        }
        if let Some(population) = hp1_character_population {
            let (map_rotation, map_translation) = map_placement_transform
                .ok_or("HP1 character population requires an HP1 map placement transform")?;
            let texture_layer_base = hp1_map_slice.map_or(0, |slice| slice.texture_layer_count);
            let character_vertex_offset = vertices.len();
            vertices.extend(population.vertices.iter().map(|source| Vertex {
                position: (map_rotation * source.position_m + map_translation).to_array(),
                color: Vec3::ONE.to_array(),
                animated: 0.0,
                texture_uv: source.texture_uv,
                texture_layer: (texture_layer_base + source.texture_layer) as f32,
            }));
            if hp1_character_animation_enabled {
                let animation_frames = population
                    .animation_frames
                    .iter()
                    .map(|positions| {
                        positions
                            .iter()
                            .zip(&population.vertices)
                            .map(|(position, source)| Vertex {
                                position: (map_rotation * *position + map_translation).to_array(),
                                color: Vec3::ONE.to_array(),
                                animated: 0.0,
                                texture_uv: source.texture_uv,
                                texture_layer: (texture_layer_base + source.texture_layer) as f32,
                            })
                            .collect::<Vec<_>>()
                    })
                    .collect::<Vec<_>>();
                if animation_frames.is_empty()
                    || animation_frames
                        .iter()
                        .any(|frame| frame.len() != population.vertices.len())
                {
                    return Err("HP1 character animation frame layout is invalid".into());
                }
                character_animation = Some(CharacterAnimation {
                    vertex_offset_bytes: (character_vertex_offset * std::mem::size_of::<Vertex>())
                        as wgpu::BufferAddress,
                    frames: animation_frames,
                    last_uploaded_frame: None,
                    uploads: 0,
                    scratch_frame: Vec::with_capacity(population.vertices.len()),
                    last_uploaded_tick: None,
                    reactions_were_active: false,
                });
            }
            if hp1_npc_spell_interaction_enabled {
                npc_spell_interaction = Some(NpcSpellInteraction::new(
                    population,
                    map_rotation,
                    map_translation,
                )?);
            }
            println!(
                "[hp1.characters.preview] placement=LEVEL_ACTORS actors={} staged_near_player={} distinct_meshes={} source_faces={} expanded_vertices={} texture_layers={} animation_frames={} animation_upload={} inspected={} excluded={} unresolved_class={}",
                population.actor_count,
                population.staged_actor_count,
                population.distinct_mesh_count,
                population.total_face_count,
                population.vertices.len(),
                population.texture_layer_count,
                population.animation_frames.len(),
                if hp1_character_animation_enabled {
                    "ENABLED"
                } else {
                    "DISABLED_SAFE_STATIC"
                },
                population.inspected_actor_count,
                population.excluded_actor_count,
                population.unresolved_class_count,
            );
        }
        if vertices.is_empty()
            || vertices.iter().any(|vertex| {
                vertex
                    .position
                    .iter()
                    .any(|component| !component.is_finite())
                    || vertex.color.iter().any(|component| !component.is_finite())
            })
        {
            return Err("static calibration geometry is empty or non-finite".into());
        }
        let model_hash = fnv1a64(bytemuck::cast_slice(&vertices));
        let vertex_buffer = device.create_buffer_init(&wgpu::util::BufferInitDescriptor {
            label: Some(vertex_buffer_label),
            contents: bytemuck::cast_slice(&vertices),
            usage: wgpu::BufferUsages::VERTEX
                | if character_animation.is_some() {
                    wgpu::BufferUsages::COPY_DST
                } else {
                    wgpu::BufferUsages::empty()
                },
        });
        let dynamic_vertex_buffer = wand_enabled.then(|| {
            device.create_buffer(&wgpu::BufferDescriptor {
                label: Some("HPVR Gate A4 shared stereo wand geometry"),
                size: (MAX_DYNAMIC_VERTICES * std::mem::size_of::<Vertex>()) as wgpu::BufferAddress,
                usage: wgpu::BufferUsages::VERTEX | wgpu::BufferUsages::COPY_DST,
                mapped_at_creation: false,
            })
        });

        let fallback_pixels = [255_u8, 255, 255, 255];
        let mut combined_texture_pixels = Vec::new();
        let extra_texture = if let Some(population) = hp1_character_population {
            Some((
                population.texture_rgba8.as_slice(),
                population.texture_layer_count,
            ))
        } else {
            hp1_npc_preview.map(|npc| (npc.texture_rgba8.as_slice(), npc.texture_layer_count))
        };
        let (texture_width, texture_height, texture_layers, texture_pixels) =
            match (hp1_map_slice, extra_texture) {
                (Some(slice), Some((extra_pixels, extra_layers))) => {
                    if slice.texture_layer_width != 256 || slice.texture_layer_height != 256 {
                        return Err(
                            "NPC preview requires the observed 256x256 BSP texture array".into(),
                        );
                    }
                    combined_texture_pixels.reserve(slice.texture_rgba8.len() + extra_pixels.len());
                    combined_texture_pixels.extend_from_slice(&slice.texture_rgba8);
                    combined_texture_pixels.extend_from_slice(extra_pixels);
                    (
                        256,
                        256,
                        slice.texture_layer_count + extra_layers,
                        combined_texture_pixels.as_slice(),
                    )
                }
                (Some(slice), None) => (
                    slice.texture_layer_width,
                    slice.texture_layer_height,
                    slice.texture_layer_count,
                    slice.texture_rgba8.as_slice(),
                ),
                (None, Some((extra_pixels, extra_layers))) => {
                    (256, 256, extra_layers, extra_pixels)
                }
                (None, None) => (1, 1, 1, fallback_pixels.as_slice()),
            };
        let map_texture = device.create_texture_with_data(
            queue,
            &wgpu::TextureDescriptor {
                label: Some("HPVR Gate B9 HP1 P8 texture array"),
                size: wgpu::Extent3d {
                    width: texture_width,
                    height: texture_height,
                    depth_or_array_layers: texture_layers,
                },
                mip_level_count: 1,
                sample_count: 1,
                dimension: wgpu::TextureDimension::D2,
                format: wgpu::TextureFormat::Rgba8UnormSrgb,
                usage: wgpu::TextureUsages::TEXTURE_BINDING,
                view_formats: &[],
            },
            wgpu::wgt::TextureDataOrder::LayerMajor,
            texture_pixels,
        );
        let map_texture_view = map_texture.create_view(&wgpu::TextureViewDescriptor {
            label: Some("HPVR Gate B9 HP1 P8 array view"),
            dimension: Some(wgpu::TextureViewDimension::D2Array),
            ..Default::default()
        });
        let map_sampler = device.create_sampler(&wgpu::SamplerDescriptor {
            label: Some("HPVR Gate B9 repeating P8 sampler"),
            address_mode_u: wgpu::AddressMode::Repeat,
            address_mode_v: wgpu::AddressMode::Repeat,
            address_mode_w: wgpu::AddressMode::ClampToEdge,
            mag_filter: wgpu::FilterMode::Linear,
            min_filter: wgpu::FilterMode::Linear,
            mipmap_filter: wgpu::MipmapFilterMode::Nearest,
            ..Default::default()
        });

        let uniform_layout = device.create_bind_group_layout(&wgpu::BindGroupLayoutDescriptor {
            label: Some("HPVR Gate A3 eye-uniform layout"),
            entries: &[
                wgpu::BindGroupLayoutEntry {
                    binding: 0,
                    visibility: wgpu::ShaderStages::VERTEX_FRAGMENT,
                    ty: wgpu::BindingType::Buffer {
                        ty: wgpu::BufferBindingType::Uniform,
                        has_dynamic_offset: false,
                        min_binding_size: NonZeroU64::new(std::mem::size_of::<EyeUniform>() as u64),
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 1,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Texture {
                        sample_type: wgpu::TextureSampleType::Float { filterable: true },
                        view_dimension: wgpu::TextureViewDimension::D2Array,
                        multisampled: false,
                    },
                    count: None,
                },
                wgpu::BindGroupLayoutEntry {
                    binding: 2,
                    visibility: wgpu::ShaderStages::FRAGMENT,
                    ty: wgpu::BindingType::Sampler(wgpu::SamplerBindingType::Filtering),
                    count: None,
                },
            ],
        });
        let uniform_buffers = std::array::from_fn(|eye| {
            device.create_buffer(&wgpu::BufferDescriptor {
                label: Some(if eye == 0 {
                    "HPVR Gate A3 left-eye uniform"
                } else {
                    "HPVR Gate A3 right-eye uniform"
                }),
                size: std::mem::size_of::<EyeUniform>() as u64,
                usage: wgpu::BufferUsages::UNIFORM | wgpu::BufferUsages::COPY_DST,
                mapped_at_creation: false,
            })
        });
        let bind_groups = std::array::from_fn(|eye| {
            device.create_bind_group(&wgpu::BindGroupDescriptor {
                label: Some(if eye == 0 {
                    "HPVR Gate A3 left-eye bind group"
                } else {
                    "HPVR Gate A3 right-eye bind group"
                }),
                layout: &uniform_layout,
                entries: &[
                    wgpu::BindGroupEntry {
                        binding: 0,
                        resource: uniform_buffers[eye].as_entire_binding(),
                    },
                    wgpu::BindGroupEntry {
                        binding: 1,
                        resource: wgpu::BindingResource::TextureView(&map_texture_view),
                    },
                    wgpu::BindGroupEntry {
                        binding: 2,
                        resource: wgpu::BindingResource::Sampler(&map_sampler),
                    },
                ],
            })
        });
        let shader = device.create_shader_module(wgpu::ShaderModuleDescriptor {
            label: Some("HPVR Gate A3 calibration shader"),
            source: wgpu::ShaderSource::Wgsl(include_str!("calibration_scene.wgsl").into()),
        });
        let pipeline_layout = device.create_pipeline_layout(&wgpu::PipelineLayoutDescriptor {
            label: Some("HPVR Gate A3 pipeline layout"),
            bind_group_layouts: &[Some(&uniform_layout)],
            immediate_size: 0,
        });
        let pipeline = device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
            label: Some("HPVR Gate A3 stereo calibration pipeline"),
            layout: Some(&pipeline_layout),
            vertex: wgpu::VertexState {
                module: &shader,
                entry_point: Some("vs_main"),
                compilation_options: wgpu::PipelineCompilationOptions::default(),
                buffers: &[Vertex::layout()],
            },
            primitive: wgpu::PrimitiveState {
                topology: wgpu::PrimitiveTopology::TriangleList,
                front_face: wgpu::FrontFace::Ccw,
                cull_mode: None,
                ..Default::default()
            },
            depth_stencil: Some(wgpu::DepthStencilState {
                format: DEPTH_FORMAT,
                depth_write_enabled: Some(true),
                depth_compare: Some(wgpu::CompareFunction::Less),
                stencil: wgpu::StencilState::default(),
                bias: wgpu::DepthBiasState::default(),
            }),
            multisample: wgpu::MultisampleState::default(),
            fragment: Some(wgpu::FragmentState {
                module: &shader,
                entry_point: Some("fs_main"),
                compilation_options: wgpu::PipelineCompilationOptions::default(),
                targets: &[Some(wgpu::ColorTargetState {
                    format: color_format,
                    blend: None,
                    write_mask: wgpu::ColorWrites::ALL,
                })],
            }),
            multiview_mask: None,
            cache: None,
        });

        let depth_targets = (0..image_count)
            .map(|index| create_depth_target(device, width, height, index))
            .collect();
        println!(
            "[geo.model] vertices={} triangles={} fnv1a64={model_hash:016x} world=LOCAL meters near={NEAR_Z:.3} far={FAR_Z:.1}",
            vertices.len(),
            vertices.len() / 3
        );
        Ok(Self {
            pipeline,
            vertex_buffer,
            vertex_count: vertices.len() as u32,
            character_animation,
            dynamic_vertex_buffer,
            dynamic_vertices: Vec::with_capacity(MAX_DYNAMIC_VERTICES),
            uniform_buffers,
            bind_groups,
            _map_texture: map_texture,
            depth_targets,
            model_hash,
            current_snapshot: None,
            last_tick_frame: None,
            last_predicted_display_ns: None,
            phase: 0.0,
            simulation_ticks: 0,
            max_ticks_per_frame: 0,
            session_generation: 0,
            left_draws: 0,
            right_draws: 0,
            snapshot_mismatches: 0,
            encode_cpu_total: Duration::ZERO,
            gpu_completion_wait_total: Duration::ZERO,
            first_frame_logged: false,
            motion: MotionTelemetry::new(),
            wand_enabled,
            harry_wand_model,
            wand_state: WandVisualState::default(),
            wand_telemetry: WandRenderTelemetry::default(),
            gesture_projection,
            gesture_visual_state: ProjectionVisualState::Idle,
            gesture_feedback_marker: None,
            spell_target: SpellTargetState::new(spell_target_enabled),
            locomotion: LocomotionState::new(
                hp1_map_slice.is_some() && wand_enabled,
                hp1_eye_height_meters,
                initial_ground_adjustment,
            ),
            bsp_collision,
            npc_spell_interaction,
        })
    }

    pub(super) fn on_session_begin(&mut self, generation: u32) {
        self.session_generation = generation;
        self.current_snapshot = None;
        self.last_tick_frame = None;
        self.last_predicted_display_ns = None;
        self.wand_telemetry.last_dynamic_upload_tick = None;
        self.reset_wand_visual();
        self.gesture_feedback_marker = None;
        self.spell_target.reset_visuals();
        if let Some(interaction) = self.npc_spell_interaction.as_mut() {
            interaction.reset_visuals();
        }
        if let Some(animation) = self.character_animation.as_mut() {
            animation.last_uploaded_frame = None;
            animation.last_uploaded_tick = None;
            animation.reactions_were_active = false;
        }
        self.locomotion.reset();
        if let Some(capture) = self.gesture_projection.as_mut() {
            capture.on_session_begin();
        }
        println!("[geo.session] begin generation={generation}; animation timebase reset");
    }

    pub(super) fn on_session_end(&mut self) {
        self.current_snapshot = None;
        self.last_tick_frame = None;
        self.last_predicted_display_ns = None;
        self.reset_wand_visual();
        self.gesture_feedback_marker = None;
        self.spell_target.reset_visuals();
        if let Some(interaction) = self.npc_spell_interaction.as_mut() {
            interaction.reset_visuals();
        }
        if let Some(capture) = self.gesture_projection.as_mut() {
            capture.on_session_end();
        }
        println!("[geo.session] end generation={}", self.session_generation);
    }

    pub(super) fn tick(
        &mut self,
        frame_id: u32,
        predicted_display_time: xr::Time,
        wand_frame: WandFrame,
        locomotion_sample: LocomotionSample,
    ) -> Result<(), Box<dyn Error>> {
        if self.last_tick_frame == Some(frame_id) {
            return Err(format!("simulation tick repeated for OpenXR frame {frame_id}").into());
        }
        let predicted_display_ns = predicted_display_time.as_nanos();
        let delta_seconds = self
            .last_predicted_display_ns
            .map(|last| (predicted_display_ns - last) as f32 * 1.0e-9)
            .unwrap_or(0.0);
        if delta_seconds < 0.0 {
            return Err("predicted display time moved backwards inside a running session".into());
        }
        self.phase = (self.phase + delta_seconds.min(0.05)).rem_euclid(1000.0);
        self.locomotion.tick(
            locomotion_sample,
            delta_seconds,
            self.bsp_collision.as_ref(),
        );
        let wand_frame = self.locomotion.map_wand(wand_frame);
        let prior_tip = self
            .wand_state
            .last_tip
            .or_else(|| self.wand_state.trail_points.last().copied());
        let (next_state, spell_event) = if let Some(capture) = self.gesture_projection.as_mut() {
            let next_state = capture.observe(wand_frame, predicted_display_ns)?;
            (Some(next_state), capture.take_spell_event())
        } else {
            (None, None)
        };
        if let Some(next_state) = next_state {
            self.update_gesture_feedback(next_state, wand_frame, prior_tip);
        }
        if let Some(event) = spell_event {
            if let Some(interaction) = self.npc_spell_interaction.as_mut() {
                interaction.consume(event, self.bsp_collision.as_ref())?;
            } else {
                self.spell_target.consume(event)?;
            }
        }
        self.spell_target.advance(delta_seconds);
        if let Some(interaction) = self.npc_spell_interaction.as_mut() {
            interaction.advance(delta_seconds);
        }
        self.update_wand_visual(wand_frame, predicted_display_ns)?;
        self.build_spell_target();
        self.build_npc_spell_feedback();
        if self.dynamic_vertices.len() > MAX_DYNAMIC_VERTICES {
            self.wand_telemetry.capacity_overflows += 1;
            return Err(format!(
                "wand geometry exceeded fixed capacity: {} > {MAX_DYNAMIC_VERTICES}",
                self.dynamic_vertices.len()
            )
            .into());
        }
        if self.dynamic_vertices.iter().any(|vertex| {
            vertex
                .position
                .iter()
                .chain(vertex.color.iter())
                .any(|component| !component.is_finite())
                || !vertex.animated.is_finite()
        }) {
            self.wand_telemetry.nonfinite_vertices += 1;
            return Err("wand geometry contains a non-finite vertex".into());
        }
        let dynamic_hash = fnv1a64(bytemuck::cast_slice(&self.dynamic_vertices));
        self.simulation_ticks += 1;
        self.max_ticks_per_frame = self.max_ticks_per_frame.max(1);
        self.last_tick_frame = Some(frame_id);
        self.last_predicted_display_ns = Some(predicted_display_ns);
        self.current_snapshot = Some(FrameSnapshot {
            tick_id: self.simulation_ticks as u64,
            predicted_display_ns,
            phase: self.phase,
            model_hash: self.model_hash,
            dynamic_vertex_count: self.dynamic_vertices.len() as u32,
            dynamic_hash,
            dynamic_geometry_present: !self.dynamic_vertices.is_empty(),
            trail_point_count: self.wand_state.trail_points.len() as u32,
        });
        Ok(())
    }

    fn reset_wand_visual(&mut self) {
        if self.wand_state.stroke_active {
            self.wand_telemetry.strokes_canceled += 1;
        }
        self.wand_state = WandVisualState::default();
        self.dynamic_vertices.clear();
    }

    pub(super) fn on_interaction_profile_changed(&mut self) {
        let prior_tip = self
            .wand_state
            .last_tip
            .or_else(|| self.wand_state.trail_points.last().copied());
        self.reset_wand_visual();
        if let Some(next_state) = self
            .gesture_projection
            .as_mut()
            .map(GestureProjectionCapture::on_interaction_profile_changed)
        {
            self.update_gesture_feedback(next_state, WandFrame::Unavailable, prior_tip);
        } else {
            self.gesture_visual_state = ProjectionVisualState::Idle;
            self.gesture_feedback_marker = None;
        }
        println!("[geo.profile] dynamic wand/gesture state reset");
    }

    pub(super) fn on_local_reference_space_reset(&mut self) {
        let prior_tip = self
            .wand_state
            .last_tip
            .or_else(|| self.wand_state.trail_points.last().copied());
        self.reset_wand_visual();
        if let Some(next_state) = self
            .gesture_projection
            .as_mut()
            .map(GestureProjectionCapture::on_local_reference_space_reset)
        {
            self.update_gesture_feedback(next_state, WandFrame::Unavailable, prior_tip);
        } else {
            self.gesture_visual_state = ProjectionVisualState::Idle;
            self.gesture_feedback_marker = None;
        }
        self.locomotion.reset();
        println!("[geo.reference] LOCAL dynamic wand/gesture state reset");
    }

    fn update_wand_visual(
        &mut self,
        wand_frame: WandFrame,
        predicted_display_ns: i64,
    ) -> Result<(), Box<dyn Error>> {
        self.dynamic_vertices.clear();
        match wand_frame {
            WandFrame::Disabled => {
                if self.wand_enabled {
                    return Err("Gate A4 scene received a disabled wand frame".into());
                }
                self.wand_state = WandVisualState::default();
                return Ok(());
            }
            WandFrame::Unavailable => {
                if !self.wand_enabled {
                    return Err("Gate A3 scene received a live wand frame".into());
                }
                self.wand_telemetry.hidden_tick_frames += 1;
                self.reset_wand_visual();
                self.build_gesture_feedback_marker();
                return Ok(());
            }
            WandFrame::Tracked(sample) => {
                if !self.wand_enabled {
                    return Err("Gate A3 scene received a tracked wand sample".into());
                }
                self.wand_telemetry.visible_tick_frames += 1;
                self.update_trace(sample, predicted_display_ns);
                self.build_wand_geometry(sample)?;
                self.build_gesture_template();
                self.build_gesture_feedback_marker();
            }
        }
        Ok(())
    }

    fn update_gesture_feedback(
        &mut self,
        next_state: ProjectionVisualState,
        wand_frame: WandFrame,
        prior_tip: Option<Vec3>,
    ) {
        match next_state {
            ProjectionVisualState::FlipendoRejected
            | ProjectionVisualState::ProjectionRejected
            | ProjectionVisualState::Canceled => {
                if self.gesture_visual_state != next_state || self.gesture_feedback_marker.is_none()
                {
                    self.gesture_feedback_marker = prior_tip
                        .or_else(|| match wand_frame {
                            WandFrame::Tracked(sample) => Some(sample.tip),
                            WandFrame::Disabled | WandFrame::Unavailable => None,
                        })
                        .map(|position| (position, next_state));
                }
            }
            // A cancellation may happen on a non-rendered tracking-loss frame.
            // Keep its marker latched across the neutral recovery sample and
            // clear it only when the player deliberately starts a new attempt.
            ProjectionVisualState::Idle => {}
            ProjectionVisualState::Recording
            | ProjectionVisualState::TrajectoryProjectedNotSpell
            | ProjectionVisualState::FlipendoAccepted => {
                self.gesture_feedback_marker = None;
            }
        }
        self.gesture_visual_state = next_state;
    }

    fn build_gesture_feedback_marker(&mut self) {
        let Some((position, feedback_state)) = self.gesture_feedback_marker else {
            return;
        };
        let color = match feedback_state {
            ProjectionVisualState::FlipendoRejected | ProjectionVisualState::ProjectionRejected => {
                Vec3::new(1.0, 0.04, 0.04)
            }
            ProjectionVisualState::Canceled => Vec3::new(1.0, 0.58, 0.02),
            ProjectionVisualState::Idle
            | ProjectionVisualState::Recording
            | ProjectionVisualState::TrajectoryProjectedNotSpell
            | ProjectionVisualState::FlipendoAccepted => return,
        };
        add_box(
            &mut self.dynamic_vertices,
            position - Vec3::splat(0.014),
            position + Vec3::splat(0.014),
            color,
            false,
        );
    }

    fn build_gesture_template(&mut self) {
        let Some(capture) = self.gesture_projection.as_ref() else {
            return;
        };
        let points = capture.template_world_points();
        if points.len() > MAX_TEMPLATE_OVERLAY_POINTS {
            return;
        }
        let color = Vec3::new(0.04, 0.78, 1.0);
        if let [point] = points.as_slice() {
            add_box(
                &mut self.dynamic_vertices,
                *point - Vec3::splat(0.004),
                *point + Vec3::splat(0.004),
                color,
                false,
            );
            return;
        }
        for pair in points.windows(2) {
            add_tapered_segment(
                &mut self.dynamic_vertices,
                pair[0],
                pair[1],
                0.0025,
                0.0025,
                color,
            );
        }
    }

    fn build_spell_target(&mut self) {
        if !self.spell_target.enabled {
            return;
        }
        let center = self.spell_target.position;
        let target_color = if self.spell_target.reaction_active {
            Vec3::new(0.12, 1.0, 0.35)
        } else {
            Vec3::new(0.92, 0.32, 1.0)
        };
        add_octahedron(
            &mut self.dynamic_vertices,
            center,
            SPELL_TARGET_RADIUS_METERS,
            target_color,
        );
        let ring_color = if self.spell_target.reaction_active {
            Vec3::new(0.30, 1.0, 0.55)
        } else {
            Vec3::new(1.0, 0.72, 0.10)
        };
        for index in 0..SPELL_TARGET_RING_SEGMENTS {
            let angle0 = std::f32::consts::TAU * index as f32 / SPELL_TARGET_RING_SEGMENTS as f32;
            let angle1 =
                std::f32::consts::TAU * (index + 1) as f32 / SPELL_TARGET_RING_SEGMENTS as f32;
            let point0 = center
                + Vec3::new(angle0.cos(), angle0.sin(), 0.0) * (SPELL_TARGET_RADIUS_METERS * 1.55);
            let point1 = center
                + Vec3::new(angle1.cos(), angle1.sin(), 0.0) * (SPELL_TARGET_RADIUS_METERS * 1.55);
            add_tapered_segment(
                &mut self.dynamic_vertices,
                point0,
                point1,
                0.005,
                0.005,
                ring_color,
            );
        }
        if self.spell_target.beam_remaining_seconds > 0.0 {
            add_tapered_segment(
                &mut self.dynamic_vertices,
                self.spell_target.beam_start,
                self.spell_target.beam_end,
                0.010,
                0.004,
                Vec3::new(0.08, 1.0, 0.32),
            );
        }
    }

    fn build_npc_spell_feedback(&mut self) {
        let Some((beam_start, beam_end, hit)) = self
            .npc_spell_interaction
            .as_ref()
            .and_then(NpcSpellInteraction::beam)
        else {
            return;
        };
        add_tapered_segment(
            &mut self.dynamic_vertices,
            beam_start,
            beam_end,
            0.010,
            0.004,
            if hit {
                Vec3::new(0.08, 1.0, 0.32)
            } else {
                Vec3::new(1.0, 0.28, 0.08)
            },
        );
    }

    fn update_trace(&mut self, sample: WandSample, predicted_display_ns: i64) {
        update_trace_state(
            &mut self.wand_state,
            &mut self.wand_telemetry,
            sample,
            predicted_display_ns,
        );
    }

    fn build_wand_geometry(&mut self, sample: WandSample) -> Result<(), Box<dyn Error>> {
        let prop_forward = (sample.prop_orientation * Vec3::NEG_Z).normalize();
        let derived_forward = (sample.tip - sample.prop_root).normalize();
        if !prop_forward.is_finite()
            || !derived_forward.is_finite()
            || prop_forward.dot(derived_forward) < 0.999
        {
            return Err("derived wand tip does not follow the explicit prop -Z axis".into());
        }
        if sample.aim_origin.distance(sample.tip) > 1.0e-5
            || sample.aim_direction.dot(derived_forward) < 0.999
        {
            return Err("visible wand and gameplay aim ray do not share one axis".into());
        }

        if let Some(model) = self.harry_wand_model.as_ref() {
            self.dynamic_vertices
                .extend(model.vertices.iter().map(|source| {
                    let position =
                        sample.prop_root + sample.prop_orientation * source.local_position;
                    Vertex {
                        position: position.to_array(),
                        color: source.color.to_array(),
                        animated: 0.0,
                        texture_uv: [0.0, 0.0],
                        texture_layer: -1.0,
                    }
                }));
        } else {
            let handle_back = sample.prop_root - derived_forward * 0.055;
            let shaft_join = sample.prop_root.lerp(sample.tip, 0.22);
            add_tapered_segment(
                &mut self.dynamic_vertices,
                handle_back,
                shaft_join,
                0.018,
                0.011,
                Vec3::new(0.22, 0.055, 0.018),
            );
            add_tapered_segment(
                &mut self.dynamic_vertices,
                shaft_join,
                sample.tip,
                0.011,
                0.003,
                Vec3::new(0.48, 0.16, 0.045),
            );
        }

        let trigger_glow = sample.trigger_value.clamp(0.0, 1.0);
        if trigger_glow > 0.05 {
            let tip_color = Vec3::new(0.25 + 0.75 * trigger_glow, 0.72, 1.0);
            let glow_radius = 0.0025 + trigger_glow * 0.0025;
            add_box(
                &mut self.dynamic_vertices,
                sample.tip - Vec3::splat(glow_radius),
                sample.tip + Vec3::splat(glow_radius),
                tip_color,
                false,
            );
        }
        if aim_guide_visible(sample) {
            add_tapered_segment(
                &mut self.dynamic_vertices,
                sample.tip,
                sample.tip + derived_forward * 0.72,
                0.0015,
                0.0015,
                Vec3::new(0.10, 0.82, 0.92),
            );
        }

        let trace_color = match self.gesture_visual_state {
            ProjectionVisualState::TrajectoryProjectedNotSpell
            | ProjectionVisualState::FlipendoAccepted => Vec3::new(0.08, 1.0, 0.42),
            ProjectionVisualState::FlipendoRejected | ProjectionVisualState::ProjectionRejected => {
                Vec3::new(1.0, 0.04, 0.04)
            }
            ProjectionVisualState::Canceled => Vec3::new(1.0, 0.58, 0.02),
            ProjectionVisualState::Idle | ProjectionVisualState::Recording => {
                Vec3::new(1.0, 0.28, 0.055)
            }
        };
        if self.wand_state.trail_points.len() == 1 {
            let point = self.wand_state.trail_points[0];
            add_box(
                &mut self.dynamic_vertices,
                point - Vec3::splat(0.007),
                point + Vec3::splat(0.007),
                trace_color,
                false,
            );
        } else {
            for pair in self.wand_state.trail_points.windows(2) {
                add_tapered_segment(
                    &mut self.dynamic_vertices,
                    pair[0],
                    pair[1],
                    0.006,
                    0.006,
                    trace_color,
                );
            }
        }

        if !self.wand_telemetry.first_sample_logged {
            println!(
                "[wand.first] grip=({:.3},{:.3},{:.3}) aim=({:.3},{:.3},{:.3}) root=({:.3},{:.3},{:.3}) tip=({:.3},{:.3},{:.3}) trigger={:.3} dynamic_vertices={}",
                sample.raw_grip_pose.position.x,
                sample.raw_grip_pose.position.y,
                sample.raw_grip_pose.position.z,
                sample.raw_aim_pose.position.x,
                sample.raw_aim_pose.position.y,
                sample.raw_aim_pose.position.z,
                sample.prop_root.x,
                sample.prop_root.y,
                sample.prop_root.z,
                sample.tip.x,
                sample.tip.y,
                sample.tip.z,
                sample.trigger_value,
                self.dynamic_vertices.len()
            );
            self.wand_telemetry.first_sample_logged = true;
        }
        Ok(())
    }

    pub(super) fn encode_frame(
        &mut self,
        queue: &wgpu::Queue,
        encoder: &mut wgpu::CommandEncoder,
        frame: CalibrationFrame<'_>,
    ) -> Result<(), Box<dyn Error>> {
        let encode_started = Instant::now();
        let predicted_display_ns = frame.predicted_display_time.as_nanos();
        let Some(snapshot) = self.current_snapshot else {
            self.snapshot_mismatches += 1;
            return Err("calibration frame has no immutable simulation snapshot".into());
        };
        if snapshot.predicted_display_ns != predicted_display_ns
            || snapshot.model_hash != self.model_hash
            || self.last_tick_frame.is_none()
        {
            self.snapshot_mismatches += 1;
            return Err("calibration frame and simulation snapshot do not match".into());
        }
        let actual_dynamic_hash = fnv1a64(bytemuck::cast_slice(&self.dynamic_vertices));
        if snapshot.dynamic_vertex_count != self.dynamic_vertices.len() as u32
            || snapshot.dynamic_hash != actual_dynamic_hash
            || snapshot.trail_point_count != self.wand_state.trail_points.len() as u32
            || snapshot.dynamic_geometry_present != !self.dynamic_vertices.is_empty()
        {
            self.wand_telemetry.snapshot_hash_mismatches += 1;
            return Err("wand dynamic geometry changed after the per-frame snapshot".into());
        }
        if snapshot.dynamic_vertex_count > 0 {
            if self.wand_telemetry.last_dynamic_upload_tick == Some(snapshot.tick_id) {
                self.wand_telemetry.duplicate_uploads += 1;
                self.wand_telemetry.max_uploads_per_frame =
                    self.wand_telemetry.max_uploads_per_frame.max(2);
                return Err(format!(
                    "wand dynamic snapshot {} would be uploaded twice",
                    snapshot.tick_id
                )
                .into());
            }
            self.wand_telemetry.last_dynamic_upload_tick = Some(snapshot.tick_id);
            let dynamic_buffer = self
                .dynamic_vertex_buffer
                .as_ref()
                .ok_or("wand snapshot has vertices but no dynamic GPU buffer")?;
            queue.write_buffer(
                dynamic_buffer,
                0,
                bytemuck::cast_slice(&self.dynamic_vertices),
            );
            self.wand_telemetry.dynamic_uploads += 1;
            self.wand_telemetry.max_uploads_per_frame =
                self.wand_telemetry.max_uploads_per_frame.max(1);
            self.wand_telemetry.dynamic_rendered_frames += 1;
        }
        if let Some(animation) = self.character_animation.as_mut() {
            // Sixteen normalized idle samples at ten updates per second give
            // a calm 1.6-second loop while keeping one shared population
            // upload, independent of eye count.
            let animation_frame = ((snapshot.phase * 10.0) as usize) % animation.frames.len();
            let reaction_offsets = self
                .npc_spell_interaction
                .as_ref()
                .map(|interaction| interaction.active_offsets().collect::<Vec<_>>())
                .unwrap_or_default();
            let reactions_active = !reaction_offsets.is_empty();
            if animation.last_uploaded_frame != Some(animation_frame)
                || (reactions_active && animation.last_uploaded_tick != Some(snapshot.tick_id))
                || animation.reactions_were_active != reactions_active
            {
                let upload_vertices = if reactions_active {
                    animation.scratch_frame.clear();
                    animation
                        .scratch_frame
                        .extend_from_slice(&animation.frames[animation_frame]);
                    for (range, offset) in reaction_offsets {
                        if range.start >= range.end || range.end > animation.scratch_frame.len() {
                            return Err("NPC reaction vertex range exceeds animation frame".into());
                        }
                        for vertex in &mut animation.scratch_frame[range] {
                            let position = Vec3::from_array(vertex.position) + offset;
                            if !position.is_finite() {
                                return Err("NPC reaction produced a non-finite vertex".into());
                            }
                            vertex.position = position.to_array();
                        }
                    }
                    &animation.scratch_frame
                } else {
                    &animation.frames[animation_frame]
                };
                queue.write_buffer(
                    &self.vertex_buffer,
                    animation.vertex_offset_bytes,
                    bytemuck::cast_slice(upload_vertices),
                );
                animation.last_uploaded_frame = Some(animation_frame);
                animation.last_uploaded_tick = Some(snapshot.tick_id);
                animation.reactions_were_active = reactions_active;
                animation.uploads += 1;
            }
        }
        let depth_target = self
            .depth_targets
            .get(frame.image_index)
            .ok_or("acquired image has no matching depth target")?;

        self.motion.observe(frame.head_pose, frame.views)?;
        self.locomotion.observe_head(frame.head_pose)?;
        let world_from_local = self.locomotion.world_from_local();
        let mut matrices = [Mat4::IDENTITY; 2];
        for eye in 0..2 {
            let projection = projection_from_fov(frame.views[eye].fov, NEAR_Z, FAR_Z)?;
            let eye_to_local = pose_matrix(frame.views[eye].pose)?;
            let eye_to_world = world_from_local * eye_to_local;
            matrices[eye] = projection * eye_to_world.inverse();
            let position = eye_to_world.transform_point3(Vec3::ZERO);
            let uniform = EyeUniform {
                view_projection: matrices[eye].to_cols_array_2d(),
                eye_position: [position.x, position.y, position.z, 1.0],
                frame_data: [
                    snapshot.phase,
                    snapshot.tick_id as f32,
                    eye as f32,
                    self.session_generation as f32,
                ],
            };
            queue.write_buffer(&self.uniform_buffers[eye], 0, bytemuck::bytes_of(&uniform));
        }

        for eye in 0..2 {
            let color_attachments = [Some(wgpu::RenderPassColorAttachment {
                view: frame.color_views[eye],
                depth_slice: None,
                resolve_target: None,
                ops: wgpu::Operations {
                    load: wgpu::LoadOp::Clear(wgpu::Color {
                        r: 0.006,
                        g: 0.009,
                        b: 0.020,
                        a: 1.0,
                    }),
                    store: wgpu::StoreOp::Store,
                },
            })];
            let depth_view = if eye == 0 {
                &depth_target.left_view
            } else {
                &depth_target.right_view
            };
            let depth_stencil_attachment = wgpu::RenderPassDepthStencilAttachment {
                view: depth_view,
                depth_ops: Some(wgpu::Operations {
                    load: wgpu::LoadOp::Clear(1.0),
                    store: wgpu::StoreOp::Discard,
                }),
                stencil_ops: None,
            };
            let mut pass = encoder.begin_render_pass(&wgpu::RenderPassDescriptor {
                label: Some(if eye == 0 {
                    "HPVR tracked-scene left-eye geometry"
                } else {
                    "HPVR tracked-scene right-eye geometry"
                }),
                color_attachments: &color_attachments,
                depth_stencil_attachment: Some(depth_stencil_attachment),
                timestamp_writes: None,
                occlusion_query_set: None,
                multiview_mask: None,
            });
            pass.set_pipeline(&self.pipeline);
            pass.set_bind_group(0, &self.bind_groups[eye], &[]);
            pass.set_vertex_buffer(0, self.vertex_buffer.slice(..));
            pass.draw(0..self.vertex_count, 0..1);
            if snapshot.dynamic_vertex_count > 0 {
                let dynamic_buffer = self
                    .dynamic_vertex_buffer
                    .as_ref()
                    .ok_or("wand draw has no dynamic GPU buffer")?;
                pass.set_vertex_buffer(0, dynamic_buffer.slice(..));
                pass.draw(0..snapshot.dynamic_vertex_count, 0..1);
                if eye == 0 {
                    self.wand_telemetry.dynamic_left_draws += 1;
                } else {
                    self.wand_telemetry.dynamic_right_draws += 1;
                }
            }
            drop(pass);
            if eye == 0 {
                self.left_draws += 1;
            } else {
                self.right_draws += 1;
            }
        }

        if !self.first_frame_logged {
            println!(
                "[geo.first] tick={} predicted_ns={} left_fov={:?} right_fov={:?} left_vp_hash={:016x} right_vp_hash={:016x} dynamic_vertices={} dynamic_hash={:016x} trail_points={}",
                snapshot.tick_id,
                predicted_display_ns,
                frame.views[0].fov,
                frame.views[1].fov,
                matrix_hash(matrices[0]),
                matrix_hash(matrices[1]),
                snapshot.dynamic_vertex_count,
                snapshot.dynamic_hash,
                snapshot.trail_point_count
            );
            self.first_frame_logged = true;
        }
        self.encode_cpu_total += encode_started.elapsed();
        Ok(())
    }

    pub(super) fn record_completion_wait(&mut self, elapsed: Duration) {
        self.gpu_completion_wait_total += elapsed;
    }

    pub(super) fn verify_and_report(
        &self,
        frames_begun: u32,
        rendered_frames: u32,
        reference_space_changes: u32,
    ) -> Result<bool, Box<dyn Error>> {
        if self.simulation_ticks != frames_begun || self.max_ticks_per_frame != 1 {
            return Err(format!(
                "simulation scheduler mismatch: ticks={} frames_begun={} max_ticks_per_frame={}",
                self.simulation_ticks, frames_begun, self.max_ticks_per_frame
            )
            .into());
        }
        if self.left_draws != rendered_frames || self.right_draws != rendered_frames {
            return Err(format!(
                "stereo draw mismatch: left={} right={} rendered={rendered_frames}",
                self.left_draws, self.right_draws
            )
            .into());
        }
        if self.snapshot_mismatches != 0 {
            return Err(format!(
                "immutable frame snapshot mismatches: {}",
                self.snapshot_mismatches
            )
            .into());
        }
        if self.wand_enabled {
            if self.wand_telemetry.visible_tick_frames + self.wand_telemetry.hidden_tick_frames
                != frames_begun
            {
                return Err(format!(
                    "wand tick categories do not cover begun frames: visible={} hidden={} begun={frames_begun}",
                    self.wand_telemetry.visible_tick_frames,
                    self.wand_telemetry.hidden_tick_frames
                )
                .into());
            }
            if self.wand_telemetry.dynamic_uploads != self.wand_telemetry.dynamic_rendered_frames
                || self.wand_telemetry.dynamic_left_draws
                    != self.wand_telemetry.dynamic_rendered_frames
                || self.wand_telemetry.dynamic_right_draws
                    != self.wand_telemetry.dynamic_rendered_frames
                || self.wand_telemetry.duplicate_uploads != 0
                || self.wand_telemetry.max_uploads_per_frame > 1
            {
                return Err(format!(
                    "wand stereo upload/draw mismatch: rendered_dynamic={} uploads={} duplicate_uploads={} max_uploads_per_frame={} left={} right={}",
                    self.wand_telemetry.dynamic_rendered_frames,
                    self.wand_telemetry.dynamic_uploads,
                    self.wand_telemetry.duplicate_uploads,
                    self.wand_telemetry.max_uploads_per_frame,
                    self.wand_telemetry.dynamic_left_draws,
                    self.wand_telemetry.dynamic_right_draws
                )
                .into());
            }
            if self.wand_telemetry.snapshot_hash_mismatches != 0
                || self.wand_telemetry.capacity_overflows != 0
                || self.wand_telemetry.nonfinite_vertices != 0
            {
                return Err(format!(
                    "wand renderer invariant failure: snapshot_hash_mismatches={} capacity_overflows={} nonfinite_vertices={}",
                    self.wand_telemetry.snapshot_hash_mismatches,
                    self.wand_telemetry.capacity_overflows,
                    self.wand_telemetry.nonfinite_vertices
                )
                .into());
            }
            if self.wand_telemetry.strokes_started
                != self.wand_telemetry.strokes_completed + self.wand_telemetry.strokes_canceled
            {
                return Err(format!(
                    "wand stroke lifecycle mismatch: started={} completed={} canceled={}",
                    self.wand_telemetry.strokes_started,
                    self.wand_telemetry.strokes_completed,
                    self.wand_telemetry.strokes_canceled
                )
                .into());
            }
        }
        if self.motion.samples != rendered_frames {
            return Err(format!(
                "pose sample mismatch: samples={} rendered={rendered_frames}",
                self.motion.samples
            )
            .into());
        }
        if self.motion.max_projection_boundary_error > 1.0e-4 {
            return Err(format!(
                "projection boundary error is too large: {:.8}",
                self.motion.max_projection_boundary_error
            )
            .into());
        }
        if self.motion.max_quaternion_norm_error > 1.0e-3 {
            return Err(format!(
                "OpenXR quaternion norm error is too large: {:.8}",
                self.motion.max_quaternion_norm_error
            )
            .into());
        }
        if self.motion.ipd_min < MIN_IPD_METERS || self.motion.ipd_max > MAX_IPD_METERS {
            return Err(format!(
                "eye separation outside human-scale bounds: min={:.5}m max={:.5}m",
                self.motion.ipd_min, self.motion.ipd_max
            )
            .into());
        }
        if self.motion.head_local_x_min <= 0.0 || self.motion.head_local_yz_max > 0.01 {
            return Err(format!(
                "right-eye position is not consistently +X in head VIEW coordinates: min_x={:.5}m max_yz={:.5}m",
                self.motion.head_local_x_min, self.motion.head_local_yz_max
            )
            .into());
        }

        let translation_span = self.motion.position_max - self.motion.position_min;
        let rotation_span = self.motion.rotation_max - self.motion.rotation_min;
        let translation_pass = translation_span.cmpge(Vec3::splat(REQUIRED_TRANSLATION_METERS));
        let rotation_pass = rotation_span.cmpge(Vec3::splat(REQUIRED_ROTATION_RADIANS));
        let motion_conclusive =
            translation_pass.all() && rotation_pass.all() && reference_space_changes == 0;
        let average_ipd = self.motion.ipd_sum / f64::from(self.motion.samples.max(1));
        let rendered_divisor = f64::from(rendered_frames.max(1));
        println!(
            "[geo.scheduler] ticks={} frames_begun={} max_ticks_per_frame={} snapshot_mismatches={} draws_left={} draws_right={} PASS",
            self.simulation_ticks,
            frames_begun,
            self.max_ticks_per_frame,
            self.snapshot_mismatches,
            self.left_draws,
            self.right_draws
        );
        println!(
            "[geo.projection] samples={} boundary_max_error={:.8} quaternion_norm_max_error={:.8} ipd_min={:.5}m ipd_avg={average_ipd:.5}m ipd_max={:.5}m head_local_min_x={:.5}m head_local_max_yz={:.5}m PASS",
            self.motion.samples,
            self.motion.max_projection_boundary_error,
            self.motion.max_quaternion_norm_error,
            self.motion.ipd_min,
            self.motion.ipd_max,
            self.motion.head_local_x_min,
            self.motion.head_local_yz_max
        );
        println!(
            "[geo.motion] translation_span=({:.4},{:.4},{:.4})m required_each={REQUIRED_TRANSLATION_METERS:.3}m rotation_span=({:.2},{:.2},{:.2})deg required_each={:.1}deg reference_space_changes={} {}",
            translation_span.x,
            translation_span.y,
            translation_span.z,
            rotation_span.x.to_degrees(),
            rotation_span.y.to_degrees(),
            rotation_span.z.to_degrees(),
            REQUIRED_ROTATION_RADIANS.to_degrees(),
            reference_space_changes,
            if motion_conclusive {
                "PASS"
            } else {
                "INCONCLUSIVE"
            }
        );
        println!(
            "[geo.timing] encode_cpu_avg_ms={:.4} exact_gpu_completion_wait_avg_ms={:.4} gpu_timestamps_collected=false",
            self.encode_cpu_total.as_secs_f64() * 1000.0 / rendered_divisor,
            self.gpu_completion_wait_total.as_secs_f64() * 1000.0 / rendered_divisor
        );
        if self.locomotion.enabled {
            if self.locomotion.telemetry.nonfinite_rejections != 0
                || !self.locomotion.world_translation.is_finite()
                || !self.locomotion.yaw_radians.is_finite()
            {
                return Err(format!(
                    "locomotion invariant failure: nonfinite_rejections={} translation={:?} yaw={}",
                    self.locomotion.telemetry.nonfinite_rejections,
                    self.locomotion.world_translation,
                    self.locomotion.yaw_radians
                )
                .into());
            }
            println!(
                "[locomotion] scheme=HEAD_RELATIVE move_speed_mps={LOCOMOTION_SPEED_METERS_PER_SECOND:.2} deadzone={LOCOMOTION_DEADZONE:.2} snap_degrees={:.1} active_move_frames={} distance_m={:.3} snap_turns={} resets={} collision={} blocked_substeps={} grounded_substeps={} vertical_adjustment_m={:.3} world_translation={:?} yaw_degrees={:.1} PASS",
                SNAP_TURN_RADIANS.to_degrees(),
                self.locomotion.telemetry.active_move_frames,
                self.locomotion.telemetry.distance_meters,
                self.locomotion.telemetry.snap_turns,
                self.locomotion.telemetry.resets,
                if self.bsp_collision.is_some() {
                    "BSP_CAPSULE"
                } else {
                    "OFF"
                },
                self.locomotion.telemetry.collision_blocked_substeps,
                self.locomotion.telemetry.grounded_substeps,
                self.locomotion.telemetry.vertical_adjustment_meters,
                self.locomotion.world_translation,
                self.locomotion.yaw_radians.to_degrees(),
            );
        }
        if self.wand_enabled {
            println!(
                "[wand.render] snapshots={} visible_ticks={} hidden_ticks={} dynamic_rendered={} uploads={} duplicate_uploads={} max_uploads_per_frame={} draws_left={} draws_right={} snapshot_hash_mismatches={} capacity_overflows={} nonfinite_vertices={} PASS",
                frames_begun,
                self.wand_telemetry.visible_tick_frames,
                self.wand_telemetry.hidden_tick_frames,
                self.wand_telemetry.dynamic_rendered_frames,
                self.wand_telemetry.dynamic_uploads,
                self.wand_telemetry.duplicate_uploads,
                self.wand_telemetry.max_uploads_per_frame,
                self.wand_telemetry.dynamic_left_draws,
                self.wand_telemetry.dynamic_right_draws,
                self.wand_telemetry.snapshot_hash_mismatches,
                self.wand_telemetry.capacity_overflows,
                self.wand_telemetry.nonfinite_vertices
            );
            println!(
                "[wand.trace] started={} completed={} canceled={} points_recorded={} points_trimmed={} gap_breaks={} teleports_rejected={} gap_bridges=0 open_strokes={} PASS",
                self.wand_telemetry.strokes_started,
                self.wand_telemetry.strokes_completed,
                self.wand_telemetry.strokes_canceled,
                self.wand_telemetry.points_recorded,
                self.wand_telemetry.points_trimmed,
                self.wand_telemetry.gap_breaks,
                self.wand_telemetry.teleports_rejected,
                u32::from(self.wand_state.stroke_active)
            );
        }
        if let Some(capture) = self.gesture_projection.as_ref() {
            capture.verify_and_report()?;
        }
        if let Some(interaction) = self.npc_spell_interaction.as_ref() {
            interaction.verify_and_report()?;
        }
        if self.spell_target.enabled {
            if self.spell_target.telemetry.events_received
                != self.spell_target.telemetry.impulses_applied
                || self.spell_target.telemetry.duplicate_or_reordered_events != 0
                || self.spell_target.last_event_serial.unwrap_or(0)
                    != u64::from(self.spell_target.telemetry.events_received)
                || !self.spell_target.position.is_finite()
            {
                return Err(format!(
                    "spell target accounting mismatch: received={} applied={} duplicate_or_reordered={} last_serial={:?} position={:?}",
                    self.spell_target.telemetry.events_received,
                    self.spell_target.telemetry.impulses_applied,
                    self.spell_target.telemetry.duplicate_or_reordered_events,
                    self.spell_target.last_event_serial,
                    self.spell_target.position
                )
                .into());
            }
            println!(
                "[spell.target.verify] selection=FIXED_TEST_TARGET events_received={} impulses_applied={} duplicate_or_reordered={} reactions_completed={} last_serial={} PASS",
                self.spell_target.telemetry.events_received,
                self.spell_target.telemetry.impulses_applied,
                self.spell_target.telemetry.duplicate_or_reordered_events,
                self.spell_target.telemetry.reactions_completed,
                self.spell_target.last_event_serial.unwrap_or(0)
            );
        }
        Ok(motion_conclusive)
    }
}

fn aim_guide_visible(sample: WandSample) -> bool {
    !sample.cast_held && !sample.pressed && !sample.released
}

fn radial_deadzone(axis: glam::Vec2, deadzone: f32) -> glam::Vec2 {
    let magnitude = axis.length().min(1.0);
    if magnitude <= deadzone || magnitude == 0.0 {
        return glam::Vec2::ZERO;
    }
    axis.normalize() * ((magnitude - deadzone) / (1.0 - deadzone))
}

fn update_trace_state(
    state: &mut WandVisualState,
    telemetry: &mut WandRenderTelemetry,
    sample: WandSample,
    predicted_display_ns: i64,
) {
    if sample.pressed {
        if state.stroke_active {
            telemetry.strokes_canceled += 1;
        }
        state.trail_points.clear();
        state.stroke_active = true;
        state.last_tip = None;
        state.last_sample_ns = None;
        telemetry.strokes_started += 1;
    }

    if !state.stroke_active {
        return;
    }
    if sample.released {
        state.stroke_active = false;
        state.last_tip = None;
        state.last_sample_ns = None;
        telemetry.strokes_completed += 1;
        return;
    }
    if !sample.cast_held {
        *state = WandVisualState::default();
        telemetry.strokes_canceled += 1;
        return;
    }

    if state
        .last_sample_ns
        .is_some_and(|last| predicted_display_ns - last > TRAIL_MAX_GAP_NS)
    {
        *state = WandVisualState::default();
        telemetry.strokes_canceled += 1;
        telemetry.gap_breaks += 1;
        return;
    }
    if state
        .last_tip
        .is_some_and(|last| last.distance(sample.tip) > TRAIL_MAX_STEP_METERS)
    {
        *state = WandVisualState::default();
        telemetry.strokes_canceled += 1;
        telemetry.teleports_rejected += 1;
        return;
    }

    state.last_sample_ns = Some(predicted_display_ns);
    let should_append = state
        .last_tip
        .is_none_or(|last| last.distance(sample.tip) >= TRAIL_MIN_STEP_METERS);
    if should_append {
        if state.trail_points.len() == MAX_TRAIL_POINTS {
            state.trail_points.remove(0);
            telemetry.points_trimmed += 1;
        }
        state.trail_points.push(sample.tip);
        state.last_tip = Some(sample.tip);
        telemetry.points_recorded += 1;
    }
}

impl MotionTelemetry {
    fn new() -> Self {
        Self {
            ipd_min: f32::INFINITY,
            head_local_x_min: f32::INFINITY,
            ..Self::default()
        }
    }

    fn observe(
        &mut self,
        head_pose: xr::Posef,
        views: &[xr::View; 2],
    ) -> Result<(), Box<dyn Error>> {
        let head_position = vector_from_xr(head_pose.position)?;
        let (head_orientation, head_norm_error) = quaternion_from_xr(head_pose.orientation)?;
        let left_position = vector_from_xr(views[0].pose.position)?;
        let right_position = vector_from_xr(views[1].pose.position)?;
        let (_, left_norm_error) = quaternion_from_xr(views[0].pose.orientation)?;
        let (_, right_norm_error) = quaternion_from_xr(views[1].pose.orientation)?;
        self.max_quaternion_norm_error = self
            .max_quaternion_norm_error
            .max(head_norm_error.max(left_norm_error).max(right_norm_error));

        let ipd = left_position.distance(right_position);
        self.ipd_min = self.ipd_min.min(ipd);
        self.ipd_max = self.ipd_max.max(ipd);
        self.ipd_sum += f64::from(ipd);
        let head_local = head_orientation.conjugate() * (right_position - left_position);
        self.head_local_x_min = self.head_local_x_min.min(head_local.x);
        self.head_local_yz_max = self
            .head_local_yz_max
            .max(head_local.y.abs().max(head_local.z.abs()));

        for view in views {
            self.max_projection_boundary_error = self
                .max_projection_boundary_error
                .max(projection_boundary_error(view.fov, NEAR_Z, FAR_Z)?);
        }

        if let Some(first_orientation) = self.first_head_orientation {
            let relative = first_orientation.conjugate() * head_orientation;
            let rotation_vector = shortest_scaled_axis(relative);
            self.position_min = self.position_min.min(head_position);
            self.position_max = self.position_max.max(head_position);
            self.rotation_min = self.rotation_min.min(rotation_vector);
            self.rotation_max = self.rotation_max.max(rotation_vector);
        } else {
            self.first_head_position = Some(head_position);
            self.first_head_orientation = Some(head_orientation);
            self.position_min = head_position;
            self.position_max = head_position;
            self.rotation_min = Vec3::ZERO;
            self.rotation_max = Vec3::ZERO;
        }
        self.samples += 1;
        Ok(())
    }
}

pub(super) fn required_view_flags() -> xr::ViewStateFlags {
    xr::ViewStateFlags::ORIENTATION_VALID
        | xr::ViewStateFlags::POSITION_VALID
        | xr::ViewStateFlags::ORIENTATION_TRACKED
        | xr::ViewStateFlags::POSITION_TRACKED
}

pub(super) fn required_head_flags() -> xr::SpaceLocationFlags {
    xr::SpaceLocationFlags::ORIENTATION_VALID
        | xr::SpaceLocationFlags::POSITION_VALID
        | xr::SpaceLocationFlags::ORIENTATION_TRACKED
        | xr::SpaceLocationFlags::POSITION_TRACKED
}

fn create_depth_target(
    device: &wgpu::Device,
    width: u32,
    height: u32,
    index: usize,
) -> DepthTarget {
    let texture = device.create_texture(&wgpu::TextureDescriptor {
        label: Some("HPVR Gate A3 two-layer depth target"),
        size: wgpu::Extent3d {
            width,
            height,
            depth_or_array_layers: 2,
        },
        mip_level_count: 1,
        sample_count: 1,
        dimension: wgpu::TextureDimension::D2,
        format: DEPTH_FORMAT,
        usage: wgpu::TextureUsages::RENDER_ATTACHMENT,
        view_formats: &[],
    });
    let make_view = |layer, label| {
        texture.create_view(&wgpu::TextureViewDescriptor {
            label: Some(label),
            format: Some(DEPTH_FORMAT),
            dimension: Some(wgpu::TextureViewDimension::D2),
            usage: Some(wgpu::TextureUsages::RENDER_ATTACHMENT),
            aspect: wgpu::TextureAspect::DepthOnly,
            base_mip_level: 0,
            mip_level_count: Some(1),
            base_array_layer: layer,
            array_layer_count: Some(1),
        })
    };
    let left_view = make_view(0, "HPVR Gate A3 left-eye depth");
    let right_view = make_view(1, "HPVR Gate A3 right-eye depth");
    println!("[geo.depth] image={index} layers=2 format={DEPTH_FORMAT:?}");
    DepthTarget {
        left_view,
        right_view,
        _texture: texture,
    }
}

fn projection_from_fov(fov: xr::Fovf, near_z: f32, far_z: f32) -> Result<Mat4, Box<dyn Error>> {
    if !(near_z.is_finite() && far_z.is_finite() && near_z > 0.0 && far_z > near_z) {
        return Err("invalid projection near/far planes".into());
    }
    let left = fov.angle_left.tan();
    let right = fov.angle_right.tan();
    let down = fov.angle_down.tan();
    let up = fov.angle_up.tan();
    if ![left, right, down, up]
        .iter()
        .all(|value| value.is_finite())
        || right <= left
        || up <= down
    {
        return Err("invalid OpenXR field of view".into());
    }
    Ok(Mat4::from_cols_array(&[
        2.0 / (right - left),
        0.0,
        0.0,
        0.0,
        0.0,
        2.0 / (up - down),
        0.0,
        0.0,
        (right + left) / (right - left),
        (up + down) / (up - down),
        far_z / (near_z - far_z),
        -1.0,
        0.0,
        0.0,
        far_z * near_z / (near_z - far_z),
        0.0,
    ]))
}

fn pose_matrix(pose: xr::Posef) -> Result<Mat4, Box<dyn Error>> {
    let position = vector_from_xr(pose.position)?;
    let (orientation, _) = quaternion_from_xr(pose.orientation)?;
    Ok(Mat4::from_rotation_translation(orientation, position))
}

fn vector_from_xr(vector: xr::Vector3f) -> Result<Vec3, Box<dyn Error>> {
    let vector = Vec3::new(vector.x, vector.y, vector.z);
    if !vector.is_finite() {
        return Err("OpenXR returned a non-finite position".into());
    }
    Ok(vector)
}

fn quaternion_from_xr(quaternion: xr::Quaternionf) -> Result<(Quat, f32), Box<dyn Error>> {
    let raw = Quat::from_xyzw(quaternion.x, quaternion.y, quaternion.z, quaternion.w);
    if !raw.is_finite() || raw.length_squared() < 1.0e-12 {
        return Err("OpenXR returned an invalid orientation quaternion".into());
    }
    let norm_error = (raw.length() - 1.0).abs();
    Ok((raw.normalize(), norm_error))
}

fn shortest_scaled_axis(mut rotation: Quat) -> Vec3 {
    rotation = rotation.normalize();
    if rotation.w < 0.0 {
        rotation = -rotation;
    }
    rotation.to_scaled_axis()
}

fn projection_boundary_error(
    fov: xr::Fovf,
    near_z: f32,
    far_z: f32,
) -> Result<f32, Box<dyn Error>> {
    let projection = projection_from_fov(fov, near_z, far_z)?;
    let rays = [
        (Vec4::new(fov.angle_left.tan(), 0.0, -1.0, 1.0), 0, -1.0),
        (Vec4::new(fov.angle_right.tan(), 0.0, -1.0, 1.0), 0, 1.0),
        (Vec4::new(0.0, fov.angle_down.tan(), -1.0, 1.0), 1, -1.0),
        (Vec4::new(0.0, fov.angle_up.tan(), -1.0, 1.0), 1, 1.0),
        (Vec4::new(0.0, 0.0, -near_z, 1.0), 2, 0.0),
        (Vec4::new(0.0, 0.0, -far_z, 1.0), 2, 1.0),
    ];
    let mut max_error = 0.0_f32;
    for (point, component, expected) in rays {
        let clip = projection * point;
        if !clip.is_finite() || clip.w.abs() < f32::EPSILON {
            return Err("projection produced a non-finite clip-space point".into());
        }
        let actual = [clip.x, clip.y, clip.z][component] / clip.w;
        max_error = max_error.max((actual - expected).abs());
    }
    Ok(max_error)
}

fn matrix_hash(matrix: Mat4) -> u64 {
    fnv1a64(bytemuck::cast_slice(&matrix.to_cols_array()))
}

fn fnv1a64(bytes: &[u8]) -> u64 {
    let mut hash = 0xcbf29ce484222325_u64;
    for byte in bytes {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x100000001b3);
    }
    hash
}

fn vertex(position: Vec3, color: Vec3, animated: bool) -> Vertex {
    Vertex {
        position: position.to_array(),
        color: color.to_array(),
        animated: if animated { 1.0 } else { 0.0 },
        texture_uv: [0.0; 2],
        texture_layer: -1.0,
    }
}

fn add_quad(vertices: &mut Vec<Vertex>, corners: [Vec3; 4], color: Vec3, animated: bool) {
    for index in [0, 1, 2, 0, 2, 3] {
        vertices.push(vertex(corners[index], color, animated));
    }
}

fn add_box(vertices: &mut Vec<Vertex>, min: Vec3, max: Vec3, color: Vec3, animated: bool) {
    let p000 = Vec3::new(min.x, min.y, min.z);
    let p001 = Vec3::new(min.x, min.y, max.z);
    let p010 = Vec3::new(min.x, max.y, min.z);
    let p011 = Vec3::new(min.x, max.y, max.z);
    let p100 = Vec3::new(max.x, min.y, min.z);
    let p101 = Vec3::new(max.x, min.y, max.z);
    let p110 = Vec3::new(max.x, max.y, min.z);
    let p111 = Vec3::new(max.x, max.y, max.z);
    for face in [
        [p001, p101, p111, p011],
        [p100, p000, p010, p110],
        [p000, p001, p011, p010],
        [p101, p100, p110, p111],
        [p010, p011, p111, p110],
        [p000, p100, p101, p001],
    ] {
        add_quad(vertices, face, color, animated);
    }
}

fn add_octahedron(vertices: &mut Vec<Vertex>, center: Vec3, radius: f32, color: Vec3) {
    let top = center + Vec3::Y * radius;
    let bottom = center - Vec3::Y * radius;
    let ring = [
        center + Vec3::X * radius,
        center + Vec3::Z * radius,
        center - Vec3::X * radius,
        center - Vec3::Z * radius,
    ];
    for index in 0..ring.len() {
        let next = (index + 1) % ring.len();
        for point in [
            top,
            ring[index],
            ring[next],
            bottom,
            ring[next],
            ring[index],
        ] {
            vertices.push(vertex(point, color, false));
        }
    }
}

fn add_tapered_segment(
    vertices: &mut Vec<Vertex>,
    start: Vec3,
    end: Vec3,
    start_radius: f32,
    end_radius: f32,
    color: Vec3,
) {
    let axis = end - start;
    if !axis.is_finite()
        || axis.length_squared() < 1.0e-12
        || !start_radius.is_finite()
        || !end_radius.is_finite()
        || start_radius <= 0.0
        || end_radius <= 0.0
    {
        return;
    }
    let forward = axis.normalize();
    let reference = if forward.dot(Vec3::Y).abs() < 0.95 {
        Vec3::Y
    } else {
        Vec3::X
    };
    let side = forward.cross(reference).normalize();
    let up = side.cross(forward).normalize();
    let start_corners = [
        start + side * start_radius + up * start_radius,
        start - side * start_radius + up * start_radius,
        start - side * start_radius - up * start_radius,
        start + side * start_radius - up * start_radius,
    ];
    let end_corners = [
        end + side * end_radius + up * end_radius,
        end - side * end_radius + up * end_radius,
        end - side * end_radius - up * end_radius,
        end + side * end_radius - up * end_radius,
    ];
    for corners in [
        [
            start_corners[0],
            start_corners[1],
            end_corners[1],
            end_corners[0],
        ],
        [
            start_corners[1],
            start_corners[2],
            end_corners[2],
            end_corners[1],
        ],
        [
            start_corners[2],
            start_corners[3],
            end_corners[3],
            end_corners[2],
        ],
        [
            start_corners[3],
            start_corners[0],
            end_corners[0],
            end_corners[3],
        ],
        [
            start_corners[3],
            start_corners[2],
            start_corners[1],
            start_corners[0],
        ],
        [
            end_corners[0],
            end_corners[1],
            end_corners[2],
            end_corners[3],
        ],
    ] {
        add_quad(vertices, corners, color, false);
    }
}

fn build_calibration_geometry() -> Vec<Vertex> {
    let mut vertices = Vec::new();
    let floor_y = -1.50;

    for x in -3..3 {
        for row in 0..9 {
            let x0 = x as f32;
            let x1 = x0 + 1.0;
            let z0 = -0.65 - row as f32;
            let z1 = z0 - 1.0;
            let light = (x + row) & 1 == 0;
            let color = if light {
                Vec3::new(0.18, 0.21, 0.27)
            } else {
                Vec3::new(0.055, 0.065, 0.090)
            };
            add_quad(
                &mut vertices,
                [
                    Vec3::new(x0, floor_y, z0),
                    Vec3::new(x0, floor_y, z1),
                    Vec3::new(x1, floor_y, z1),
                    Vec3::new(x1, floor_y, z0),
                ],
                color,
                false,
            );
        }
    }

    for column in -3..3 {
        for row in 0..4 {
            let x0 = column as f32;
            let x1 = x0 + 1.0;
            let y0 = floor_y + row as f32;
            let y1 = y0 + 1.0;
            let color = if (column + row) & 1 == 0 {
                Vec3::new(0.075, 0.105, 0.16)
            } else {
                Vec3::new(0.025, 0.035, 0.065)
            };
            add_quad(
                &mut vertices,
                [
                    Vec3::new(x0, y0, -9.65),
                    Vec3::new(x1, y0, -9.65),
                    Vec3::new(x1, y1, -9.65),
                    Vec3::new(x0, y1, -9.65),
                ],
                color,
                false,
            );
        }
    }

    let red = Vec3::new(0.92, 0.045, 0.035);
    let green = Vec3::new(0.03, 0.84, 0.16);
    let blue = Vec3::new(0.035, 0.20, 0.98);
    let purple = Vec3::new(0.53, 0.07, 0.82);
    let gold = Vec3::new(1.0, 0.55, 0.045);
    add_box(
        &mut vertices,
        Vec3::new(-1.58, floor_y, -2.45),
        Vec3::new(-1.17, 0.75, -2.04),
        red,
        false,
    );
    add_box(
        &mut vertices,
        Vec3::new(1.12, floor_y, -5.80),
        Vec3::new(1.72, -0.30, -5.20),
        blue,
        false,
    );
    add_box(
        &mut vertices,
        Vec3::new(-2.15, floor_y, -8.40),
        Vec3::new(-1.25, -0.55, -7.50),
        purple,
        false,
    );

    add_box(
        &mut vertices,
        Vec3::new(-1.42, floor_y, -4.93),
        Vec3::new(-1.13, 0.95, -4.65),
        green,
        false,
    );
    add_box(
        &mut vertices,
        Vec3::new(1.13, floor_y, -4.93),
        Vec3::new(1.42, 0.95, -4.65),
        green,
        false,
    );
    add_box(
        &mut vertices,
        Vec3::new(-1.42, 0.68, -4.93),
        Vec3::new(1.42, 0.95, -4.65),
        green,
        false,
    );

    add_box(
        &mut vertices,
        Vec3::new(-0.25, -0.76, -4.07),
        Vec3::new(0.25, -0.26, -3.57),
        gold,
        true,
    );

    let axis_origin = Vec3::new(-0.10, -1.02, -1.55);
    add_box(
        &mut vertices,
        axis_origin,
        axis_origin + Vec3::new(0.90, 0.055, 0.055),
        red,
        false,
    );
    add_box(
        &mut vertices,
        axis_origin,
        axis_origin + Vec3::new(0.055, 0.90, 0.055),
        green,
        false,
    );
    add_box(
        &mut vertices,
        axis_origin - Vec3::new(0.0, 0.0, 0.90),
        axis_origin + Vec3::new(0.055, 0.055, 0.0),
        blue,
        false,
    );

    for depth in [2.8_f32, 6.7] {
        add_box(
            &mut vertices,
            Vec3::new(-0.012, floor_y + 0.003, -depth - 0.50),
            Vec3::new(0.012, floor_y + 0.028, -depth + 0.50),
            Vec3::splat(0.72),
            false,
        );
    }
    vertices
}

fn build_hp1_map_slice_geometry(
    slice: &MapSlice,
    player_start: Option<&PlayerStart>,
) -> Result<(Vec<Vertex>, Vec3, f32, &'static str), Box<dyn Error>> {
    if slice.vertices.is_empty() || slice.vertices.len() % 3 != 0 {
        return Err("HP1 BSP preview requires a non-empty triangle-aligned slice".into());
    }
    let mut minimum = Vec3::splat(f32::INFINITY);
    let mut maximum = Vec3::splat(f32::NEG_INFINITY);
    for source in &slice.vertices {
        if !source.position_m.is_finite() || !source.normal.is_finite() {
            return Err("HP1 BSP preview contains a non-finite source vertex".into());
        }
        minimum = minimum.min(source.position_m);
        maximum = maximum.max(source.position_m);
    }
    let reported_minimum = slice.report.bounds_min_m;
    let reported_maximum = slice.report.bounds_max_m;
    let tolerance = (maximum - minimum).max_element().abs() * 1.0e-5 + 1.0e-5;
    if (minimum - reported_minimum).abs().max_element() > tolerance
        || (maximum - reported_maximum).abs().max_element() > tolerance
    {
        return Err("HP1 BSP preview bounds disagree with the validated native report".into());
    }

    let (rotation, translation, yaw_radians, placement) = if let Some(start) = player_start {
        if !start.location_serialized || !start.position_m.is_finite() {
            return Err("selected PlayerStart has no finite serialized Location".into());
        }
        if start.rotation_units[0] != 0 || start.rotation_units[2] != 0 {
            return Err(
                "PlayerStart preview currently requires zero serialized pitch and roll".into(),
            );
        }
        let yaw_radians = start.rotation_units[1] as f32 * std::f32::consts::TAU / 65_536.0;
        let rotation = Quat::from_rotation_y(yaw_radians);
        (
            rotation,
            -(rotation * start.position_m),
            yaw_radians,
            "PLAYER_START",
        )
    } else {
        // This remains a diagnostic fallback, not a recovered gameplay
        // transform: preserve scale/winding, put the selected minimum on
        // the existing floor, and center its X/Z bounds at Z=-4 m.
        let center = (minimum + maximum) * 0.5;
        (
            Quat::IDENTITY,
            Vec3::new(-center.x, -1.5 - minimum.y, -4.0 - center.z),
            0.0,
            "DIAGNOSTIC_CENTERED",
        )
    };
    let vertices = slice
        .vertices
        .iter()
        .map(|source| Vertex {
            position: (rotation * source.position_m + translation).to_array(),
            color: Vec3::ONE.to_array(),
            animated: 0.0,
            texture_uv: source.texture_uv,
            texture_layer: source.texture_layer as f32,
        })
        .collect::<Vec<_>>();
    Ok((vertices, translation, yaw_radians, placement))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn trace_sample(tip: Vec3, held: bool, pressed: bool, released: bool) -> WandSample {
        WandSample {
            raw_grip_pose: xr::Posef::IDENTITY,
            raw_aim_pose: xr::Posef::IDENTITY,
            prop_root: tip + Vec3::Z * 0.34,
            prop_orientation: Quat::IDENTITY,
            tip,
            aim_origin: Vec3::ZERO,
            aim_direction: Vec3::NEG_Z,
            trigger_value: if held { 1.0 } else { 0.0 },
            cast_held: held,
            pressed,
            released,
        }
    }

    fn asymmetric_fov() -> xr::Fovf {
        xr::Fovf {
            angle_left: -0.83,
            angle_right: 0.91,
            angle_up: 0.88,
            angle_down: -0.79,
        }
    }

    #[test]
    fn aim_guide_hides_from_press_through_release() {
        assert!(aim_guide_visible(trace_sample(
            Vec3::NEG_Z,
            false,
            false,
            false
        )));
        assert!(!aim_guide_visible(trace_sample(
            Vec3::NEG_Z,
            true,
            true,
            false
        )));
        assert!(!aim_guide_visible(trace_sample(
            Vec3::NEG_Z,
            true,
            false,
            false
        )));
        assert!(!aim_guide_visible(trace_sample(
            Vec3::NEG_Z,
            false,
            false,
            true
        )));
    }

    #[test]
    fn radial_deadzone_is_zero_inside_and_reaches_unit_edge() {
        assert_eq!(
            radial_deadzone(glam::Vec2::new(0.1, 0.0), 0.18),
            glam::Vec2::ZERO
        );
        let edge = radial_deadzone(glam::Vec2::new(0.6, 0.8), 0.18);
        assert!((edge.length() - 1.0).abs() < 1.0e-6);
        assert!((edge.normalize() - glam::Vec2::new(0.6, 0.8)).length() < 1.0e-6);
    }

    #[test]
    fn smooth_locomotion_follows_horizontal_head_forward() {
        let mut locomotion = LocomotionState::new(true, 0.0, 0.0);
        locomotion.last_head_forward_local = Some(Vec3::NEG_Z);
        locomotion.tick(
            LocomotionSample {
                move_axis: glam::Vec2::Y,
                move_active: true,
                ..LocomotionSample::default()
            },
            0.05,
            None,
        );
        assert!((locomotion.world_translation - Vec3::new(0.0, 0.0, -0.09)).length() < 1.0e-6);
        assert_eq!(locomotion.telemetry.active_move_frames, 1);
    }

    #[test]
    fn snap_turn_preserves_virtual_head_position_and_requires_release() {
        let mut locomotion = LocomotionState::new(true, 0.0, 0.0);
        locomotion.world_translation = Vec3::new(2.0, 0.0, -3.0);
        locomotion.last_head_position_local = Some(Vec3::new(0.31, 1.62, -0.18));
        let head = locomotion.last_head_position_local.unwrap();
        let before = locomotion.world_from_local().transform_point3(head);
        let right = LocomotionSample {
            turn_axis: 1.0,
            turn_active: true,
            ..LocomotionSample::default()
        };
        locomotion.tick(right, 0.01, None);
        let after = locomotion.world_from_local().transform_point3(head);
        assert!(before.distance(after) < 1.0e-5);
        assert_eq!(locomotion.telemetry.snap_turns, 1);
        locomotion.tick(right, 0.01, None);
        assert_eq!(locomotion.telemetry.snap_turns, 1);
        locomotion.tick(LocomotionSample::default(), 0.01, None);
        locomotion.tick(right, 0.01, None);
        assert_eq!(locomotion.telemetry.snap_turns, 2);
    }

    #[test]
    fn reset_preserves_authored_eye_height_offset() {
        let mut locomotion = LocomotionState::new(true, 0.815, 0.0);
        locomotion.world_translation += Vec3::new(2.0, -0.2, -3.0);
        locomotion.reset();
        assert!((locomotion.world_translation - Vec3::new(0.0, 0.815, 0.0)).length() < 1.0e-6);
    }

    #[test]
    fn authored_eye_height_and_ground_adjustment_have_distinct_roles() {
        let locomotion = LocomotionState::new(true, 0.815, 0.115);
        assert!((locomotion.world_translation.y - 0.930).abs() < 1.0e-6);
        assert!((locomotion.capsule_center().y - 0.115).abs() < 1.0e-6);
    }

    #[test]
    fn asymmetric_projection_maps_all_six_boundaries() {
        let error = projection_boundary_error(asymmetric_fov(), NEAR_Z, FAR_Z).unwrap();
        assert!(error < 1.0e-5, "boundary error={error}");
    }

    #[test]
    fn projection_keeps_positive_y_up_for_wgpu() {
        let projection = projection_from_fov(asymmetric_fov(), NEAR_Z, FAR_Z).unwrap();
        let clip = projection * Vec4::new(0.0, 0.5, -2.0, 1.0);
        assert!(clip.y / clip.w > 0.0);
    }

    #[test]
    fn pose_matrix_has_a_rigid_inverse() {
        let orientation = Quat::from_euler(glam::EulerRot::YXZ, 0.35, -0.22, 0.11);
        let [x, y, z, w] = orientation.to_array();
        let pose = xr::Posef {
            orientation: xr::Quaternionf { x, y, z, w },
            position: xr::Vector3f {
                x: 0.31,
                y: 1.62,
                z: -0.47,
            },
        };
        let transform = pose_matrix(pose).unwrap();
        let identity = transform.inverse() * transform;
        assert!(identity.abs_diff_eq(Mat4::IDENTITY, 1.0e-5));
    }

    #[test]
    fn ipd_axis_is_measured_in_head_space_not_canted_eye_space() {
        let canted = Quat::from_rotation_y(10.0_f32.to_radians());
        let [x, y, z, w] = canted.to_array();
        let make_view = |position_x, orientation| xr::View {
            pose: xr::Posef {
                orientation,
                position: xr::Vector3f {
                    x: position_x,
                    y: 0.0,
                    z: 0.0,
                },
            },
            fov: asymmetric_fov(),
        };
        let views = [
            make_view(-0.032, xr::Quaternionf { x, y, z, w }),
            make_view(
                0.032,
                xr::Quaternionf {
                    x: -x,
                    y: -y,
                    z: -z,
                    w,
                },
            ),
        ];
        let mut telemetry = MotionTelemetry::new();
        telemetry.observe(xr::Posef::IDENTITY, &views).unwrap();
        assert!((telemetry.head_local_x_min - 0.064).abs() < 1.0e-6);
        assert!(telemetry.head_local_yz_max < 1.0e-6);
    }

    #[test]
    fn procedural_geometry_is_finite_and_in_front() {
        let vertices = build_calibration_geometry();
        assert!(vertices.len() > 500);
        assert_eq!(vertices.len() % 3, 0);
        assert!(vertices.iter().all(|vertex| {
            vertex
                .position
                .iter()
                .all(|component| component.is_finite())
                && vertex.position[2] < 0.0
        }));
    }

    #[test]
    fn calibration_shader_parses_and_validates_with_naga() {
        let module = wgpu::naga::front::wgsl::parse_str(include_str!("calibration_scene.wgsl"))
            .expect("calibration WGSL must parse");
        wgpu::naga::valid::Validator::new(
            wgpu::naga::valid::ValidationFlags::all(),
            wgpu::naga::valid::Capabilities::all(),
        )
        .validate(&module)
        .expect("calibration WGSL must validate");
    }

    #[test]
    fn hp1_map_preview_preserves_triangle_count_and_centers_reported_bounds() {
        let source_vertices = [
            MapSliceVertex {
                position_m: Vec3::new(-2.0, 1.0, -1.0),
                normal: Vec3::Y,
                polygon_flags: 0,
                node_index: 2,
                surface_index: 3,
                texture_uv: [0.0, 0.0],
                texture_layer: 1,
            },
            MapSliceVertex {
                position_m: Vec3::new(2.0, 1.0, -1.0),
                normal: Vec3::Y,
                polygon_flags: 4,
                node_index: 2,
                surface_index: 3,
                texture_uv: [1.0, 0.0],
                texture_layer: 1,
            },
            MapSliceVertex {
                position_m: Vec3::new(0.0, 3.0, 3.0),
                normal: Vec3::Y,
                polygon_flags: 4,
                node_index: 2,
                surface_index: 3,
                texture_uv: [0.5, 1.0],
                texture_layer: 1,
            },
        ];
        let slice = MapSlice {
            vertices: source_vertices.to_vec(),
            report: super::super::hp1_bsp_ffi::MapSliceReport {
                available_triangle_count: 1,
                selected_triangle_count: 1,
                omitted_triangle_count: 0,
                degenerate_triangle_count: 0,
                winding_reversal_count: 0,
                zero_flag_triangle_count: 0,
                nonzero_flag_triangle_count: 1,
                imported_texture_triangle_count: 1,
                local_texture_triangle_count: 0,
                no_texture_triangle_count: 0,
                imported_actor_triangle_count: 0,
                local_actor_triangle_count: 1,
                no_actor_triangle_count: 0,
                bounds_min_m: Vec3::new(-2.0, 1.0, -1.0),
                bounds_max_m: Vec3::new(2.0, 3.0, 3.0),
            },
            texture_layer_width: 1,
            texture_layer_height: 1,
            texture_layer_count: 2,
            texture_rgba8: vec![255; 8],
            decoded_texture_count: 1,
            fallback_material_count: 0,
            fallback_triangle_count: 0,
        };
        let (vertices, translation, yaw_radians, placement) =
            build_hp1_map_slice_geometry(&slice, None).unwrap();
        assert_eq!(vertices.len(), source_vertices.len());
        assert_eq!(translation, Vec3::new(0.0, -2.5, -5.0));
        assert_eq!(yaw_radians, 0.0);
        assert_eq!(placement, "DIAGNOSTIC_CENTERED");
        assert_eq!(vertices[1].texture_uv, [1.0, 0.0]);
        assert_eq!(vertices[1].texture_layer, 1.0);
        let positions = vertices
            .iter()
            .map(|vertex| Vec3::from_array(vertex.position))
            .collect::<Vec<_>>();
        let minimum = positions
            .iter()
            .copied()
            .fold(Vec3::splat(f32::INFINITY), Vec3::min);
        let maximum = positions
            .iter()
            .copied()
            .fold(Vec3::splat(f32::NEG_INFINITY), Vec3::max);
        assert_eq!(minimum.y, -1.5);
        assert_eq!((minimum.x + maximum.x) * 0.5, 0.0);
        assert_eq!((minimum.z + maximum.z) * 0.5, -4.0);
        assert!(vertices.iter().all(|vertex| {
            vertex
                .position
                .iter()
                .chain(vertex.color.iter())
                .all(|component| component.is_finite())
        }));

        let start = PlayerStart {
            available_count: 1,
            ordinal: 0,
            actor_slot_index: 3,
            actor_reference: 6,
            position_m: source_vertices[0].position_m,
            rotation_units: [0, 16_384, 0],
            location_serialized: true,
            rotation_serialized: true,
            object_name: "PlayerStart0".to_owned(),
        };
        let (placed, _, placed_yaw, placed_mode) =
            build_hp1_map_slice_geometry(&slice, Some(&start)).unwrap();
        assert_eq!(placed_mode, "PLAYER_START");
        assert!((placed_yaw - std::f32::consts::FRAC_PI_2).abs() < 1.0e-6);
        assert!(Vec3::from_array(placed[0].position).abs_diff_eq(Vec3::ZERO, 1.0e-5));
    }

    #[test]
    fn tapered_segment_is_finite_and_triangle_aligned() {
        let mut vertices = Vec::new();
        add_tapered_segment(
            &mut vertices,
            Vec3::new(0.2, 1.1, -0.4),
            Vec3::new(-0.3, 1.5, -1.2),
            0.018,
            0.003,
            Vec3::new(0.5, 0.2, 0.05),
        );
        assert_eq!(vertices.len(), 36);
        assert_eq!(vertices.len() % 3, 0);
        assert!(vertices.iter().all(|vertex| {
            vertex
                .position
                .iter()
                .chain(vertex.color.iter())
                .all(|component| component.is_finite())
        }));
    }

    #[test]
    fn maximum_visual_trace_fits_the_fixed_dynamic_buffer() {
        let wand_vertices = 4 * 36;
        let maximum_trace_vertices = (MAX_TRAIL_POINTS - 1) * 36;
        let maximum_template_vertices = (MAX_TEMPLATE_OVERLAY_POINTS - 1) * 36;
        let feedback_marker_vertices = 36;
        let spell_target_vertices = 24 + SPELL_TARGET_RING_SEGMENTS * 36 + 36;
        assert!(
            wand_vertices
                + maximum_trace_vertices
                + maximum_template_vertices
                + feedback_marker_vertices
                + spell_target_vertices
                <= MAX_DYNAMIC_VERTICES
        );
    }

    #[test]
    fn spell_target_applies_each_accepted_serial_once() {
        let mut target = SpellTargetState::new(true);
        let event = SpellCastEvent {
            serial: 1,
            spell: SpellKind::Flipendo,
            predicted_display_time_ns: 42,
            tip: Vec3::new(0.2, -0.1, -0.4),
            aim_origin: Vec3::ZERO,
            aim_direction: Vec3::NEG_Z,
            score: 0.8,
            threshold: 0.5,
        };
        target.consume(event).unwrap();
        assert!(target.reaction_active);
        assert_eq!(target.telemetry.events_received, 1);
        assert_eq!(target.telemetry.impulses_applied, 1);
        assert!(target.consume(event).is_err());
        assert_eq!(target.telemetry.duplicate_or_reordered_events, 1);
    }

    #[test]
    fn spell_target_reaction_is_finite_and_returns_home() {
        let mut target = SpellTargetState::new(true);
        target
            .consume(SpellCastEvent {
                serial: 1,
                spell: SpellKind::Flipendo,
                predicted_display_time_ns: 42,
                tip: Vec3::new(0.2, -0.1, -0.4),
                aim_origin: Vec3::ZERO,
                aim_direction: Vec3::NEG_Z,
                score: 0.8,
                threshold: 0.5,
            })
            .unwrap();
        for _ in 0..200 {
            target.advance(0.01);
            assert!(target.position.is_finite());
        }
        assert!(!target.reaction_active);
        assert_eq!(target.position, SPELL_TARGET_BASE);
        assert_eq!(target.telemetry.reactions_completed, 1);
    }

    #[test]
    fn trace_stops_on_release_and_next_press_does_not_bridge() {
        let mut state = WandVisualState::default();
        let mut telemetry = WandRenderTelemetry::default();
        update_trace_state(
            &mut state,
            &mut telemetry,
            trace_sample(Vec3::ZERO, true, true, false),
            1_000,
        );
        update_trace_state(
            &mut state,
            &mut telemetry,
            trace_sample(Vec3::new(0.01, 0.0, 0.0), true, false, false),
            11_000_000,
        );
        update_trace_state(
            &mut state,
            &mut telemetry,
            trace_sample(Vec3::new(0.02, 0.0, 0.0), false, false, true),
            22_000_000,
        );
        assert!(!state.stroke_active);
        assert_eq!(state.trail_points.len(), 2);
        assert_eq!(telemetry.strokes_completed, 1);

        update_trace_state(
            &mut state,
            &mut telemetry,
            trace_sample(Vec3::new(0.8, 0.0, 0.0), true, true, false),
            33_000_000,
        );
        assert!(state.stroke_active);
        assert_eq!(state.trail_points, [Vec3::new(0.8, 0.0, 0.0)]);
        assert_eq!(telemetry.strokes_started, 2);
    }

    #[test]
    fn trace_gap_cancels_without_restarting_while_trigger_remains_held() {
        let mut state = WandVisualState::default();
        let mut telemetry = WandRenderTelemetry::default();
        update_trace_state(
            &mut state,
            &mut telemetry,
            trace_sample(Vec3::ZERO, true, true, false),
            1_000,
        );
        update_trace_state(
            &mut state,
            &mut telemetry,
            trace_sample(Vec3::new(0.01, 0.0, 0.0), true, false, false),
            TRAIL_MAX_GAP_NS + 1_001,
        );
        assert!(!state.stroke_active);
        assert!(state.trail_points.is_empty());
        assert_eq!(telemetry.gap_breaks, 1);
        assert_eq!(telemetry.strokes_canceled, 1);

        update_trace_state(
            &mut state,
            &mut telemetry,
            trace_sample(Vec3::new(0.02, 0.0, 0.0), true, false, false),
            TRAIL_MAX_GAP_NS + 2_001,
        );
        assert!(!state.stroke_active);
        assert!(state.trail_points.is_empty());
        assert_eq!(telemetry.strokes_started, 1);
    }

    #[test]
    fn trace_teleport_is_rejected_without_a_connecting_segment() {
        let mut state = WandVisualState::default();
        let mut telemetry = WandRenderTelemetry::default();
        update_trace_state(
            &mut state,
            &mut telemetry,
            trace_sample(Vec3::ZERO, true, true, false),
            1_000,
        );
        update_trace_state(
            &mut state,
            &mut telemetry,
            trace_sample(
                Vec3::new(TRAIL_MAX_STEP_METERS + 0.001, 0.0, 0.0),
                true,
                false,
                false,
            ),
            11_000_000,
        );
        assert!(!state.stroke_active);
        assert!(state.trail_points.is_empty());
        assert_eq!(telemetry.teleports_rejected, 1);
        assert_eq!(telemetry.strokes_canceled, 1);
    }
}
