use std::error::Error;

use glam::{Quat, Vec2, Vec3};
use openxr as xr;

const META_TOUCH_PLUS_EXTENSION_PROFILE: &str = "/interaction_profiles/meta/touch_controller_plus";
const OCULUS_TOUCH_PROFILE: &str = "/interaction_profiles/oculus/touch_controller";
const RIGHT_HAND_PATH: &str = "/user/hand/right";
const RIGHT_GRIP_PATH: &str = "/user/hand/right/input/grip/pose";
const RIGHT_AIM_PATH: &str = "/user/hand/right/input/aim/pose";
const RIGHT_TRIGGER_PATH: &str = "/user/hand/right/input/trigger/value";
const LEFT_HAND_PATH: &str = "/user/hand/left";
const LEFT_THUMBSTICK_PATH: &str = "/user/hand/left/input/thumbstick";
const RIGHT_THUMBSTICK_PATH: &str = "/user/hand/right/input/thumbstick";
const MAX_QUATERNION_NORM_ERROR: f32 = 1.0e-3;

#[derive(Clone, Copy, Debug)]
pub(super) struct WandCalibration {
    pub grip_to_prop_translation: Vec3,
    pub grip_to_prop_rotation: Quat,
    pub length_meters: f32,
}

impl Default for WandCalibration {
    fn default() -> Self {
        Self {
            // Gate A4 deliberately starts with an explicit identity mounting transform. The
            // headset check calibrates this; it is not hidden inside the renderer.
            grip_to_prop_translation: Vec3::ZERO,
            grip_to_prop_rotation: Quat::IDENTITY,
            length_meters: 0.34,
        }
    }
}

fn canonical_wand_pose(
    grip_position: Vec3,
    grip_orientation: Quat,
    aim_orientation: Quat,
    calibration: WandCalibration,
) -> (Vec3, Quat, Vec3, Vec3) {
    let prop_root = grip_position + grip_orientation * calibration.grip_to_prop_translation;
    let prop_orientation = (aim_orientation * calibration.grip_to_prop_rotation).normalize();
    let aim_direction = (prop_orientation * Vec3::NEG_Z).normalize();
    let tip = prop_root + aim_direction * calibration.length_meters;
    (prop_root, prop_orientation, tip, aim_direction)
}

#[derive(Clone, Copy, Debug)]
pub(super) struct WandSample {
    pub raw_grip_pose: xr::Posef,
    pub raw_aim_pose: xr::Posef,
    pub prop_root: Vec3,
    pub prop_orientation: Quat,
    pub tip: Vec3,
    pub aim_origin: Vec3,
    pub aim_direction: Vec3,
    pub trigger_value: f32,
    pub cast_held: bool,
    pub pressed: bool,
    pub released: bool,
}

#[derive(Clone, Copy, Debug)]
pub(super) enum WandFrame {
    Disabled,
    Unavailable,
    Tracked(WandSample),
}

#[derive(Clone, Copy, Debug, Default)]
pub(super) struct LocomotionSample {
    pub move_axis: Vec2,
    pub turn_axis: f32,
    pub move_active: bool,
    pub turn_active: bool,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
struct CastEdges {
    held: bool,
    pressed: bool,
    released: bool,
    suppressed_held: bool,
    canceled: bool,
}

#[derive(Default)]
struct CastLatch {
    armed: bool,
    held: bool,
}

impl CastLatch {
    fn reset_unknown(&mut self) -> bool {
        let canceled = self.held;
        self.armed = false;
        self.held = false;
        canceled
    }

    fn update(&mut self, active: bool, down: bool) -> CastEdges {
        if !active {
            return CastEdges {
                canceled: self.reset_unknown(),
                ..CastEdges::default()
            };
        }
        if !self.armed {
            self.held = false;
            if down {
                return CastEdges {
                    suppressed_held: true,
                    ..CastEdges::default()
                };
            }
            self.armed = true;
            return CastEdges::default();
        }

        let pressed = down && !self.held;
        let released = !down && self.held;
        self.held = down;
        CastEdges {
            held: self.held,
            pressed,
            released,
            suppressed_held: false,
            canceled: false,
        }
    }
}

#[derive(Default)]
struct InputTelemetry {
    action_syncs: u32,
    nonfocused_frames: u32,
    grip_active_frames: u32,
    aim_active_frames: u32,
    trigger_active_frames: u32,
    cast_active_frames: u32,
    move_active_frames: u32,
    turn_active_frames: u32,
    locomotion_nonzero_frames: u32,
    tracked_frames: u32,
    inactive_frames: u32,
    invalid_flag_frames: u32,
    nonfinite_frames: u32,
    invalid_value_frames: u32,
    nonmonotonic_frames: u32,
    tracking_losses: u32,
    presses: u32,
    releases: u32,
    cancels: u32,
    suppressed_held_reacquire: u32,
    profile_changes: u32,
    profile_query_errors: u32,
    bound_source_enumeration_errors: u32,
    bound_source_path_errors: u32,
    lifecycle_resets: u32,
    reference_space_changes_scheduled: u32,
    reference_space_resets: u32,
    trigger_min: f32,
    trigger_max: f32,
    grip_tip_min: f32,
    grip_tip_max: f32,
    grip_tip_sum: f64,
    aim_prop_angle_max: f32,
    quaternion_norm_max_error: f32,
}

impl InputTelemetry {
    fn new() -> Self {
        Self {
            trigger_min: f32::INFINITY,
            trigger_max: f32::NEG_INFINITY,
            grip_tip_min: f32::INFINITY,
            ..Self::default()
        }
    }
}

pub(super) struct WandInput {
    // Keep spaces before actions/action-set so their OpenXR handles are destroyed first.
    grip_space: xr::Space,
    aim_space: xr::Space,
    grip_action: xr::Action<xr::Posef>,
    aim_action: xr::Action<xr::Posef>,
    trigger_action: xr::Action<f32>,
    cast_action: xr::Action<bool>,
    move_action: xr::Action<xr::Vector2f>,
    turn_action: xr::Action<xr::Vector2f>,
    action_set: xr::ActionSet,
    left_hand: xr::Path,
    right_hand: xr::Path,
    calibration: WandCalibration,
    cast_latch: CastLatch,
    telemetry: InputTelemetry,
    last_tracked_time_ns: Option<i64>,
    pose_was_valid: bool,
    current_profile: Option<String>,
    profile_query_attempted: bool,
    profile_is_null: bool,
    session_generation: u32,
    pending_local_changes_ns: Vec<i64>,
    local_reset_applied_this_sample: bool,
}

impl WandInput {
    pub(super) fn new<G>(
        instance: &xr::Instance,
        session: &xr::Session<G>,
        meta_touch_plus_enabled: bool,
    ) -> Result<Self, Box<dyn Error>> {
        let left_hand = instance.string_to_path(LEFT_HAND_PATH)?;
        let right_hand = instance.string_to_path(RIGHT_HAND_PATH)?;
        let action_set = instance.create_action_set("hpvr_wand", "HPVR Wand", 0)?;
        let grip_action = action_set.create_action::<xr::Posef>(
            "right_grip_pose",
            "Right Wand Grip Pose",
            &[right_hand],
        )?;
        let aim_action = action_set.create_action::<xr::Posef>(
            "right_aim_pose",
            "Right Wand Aim Pose",
            &[right_hand],
        )?;
        let trigger_action = action_set.create_action::<f32>(
            "right_trigger_value",
            "Right Wand Trigger Value",
            &[right_hand],
        )?;
        let cast_action =
            action_set.create_action::<bool>("right_cast", "Right Wand Cast", &[right_hand])?;
        let move_action = action_set.create_action::<xr::Vector2f>(
            "left_move",
            "Left Stick Move",
            &[left_hand],
        )?;
        let turn_action = action_set.create_action::<xr::Vector2f>(
            "right_turn",
            "Right Stick Turn",
            &[right_hand],
        )?;

        if meta_touch_plus_enabled {
            suggest_bindings(
                instance,
                META_TOUCH_PLUS_EXTENSION_PROFILE,
                &grip_action,
                &aim_action,
                &trigger_action,
                &cast_action,
                &move_action,
                &turn_action,
            )?;
        }
        suggest_bindings(
            instance,
            OCULUS_TOUCH_PROFILE,
            &grip_action,
            &aim_action,
            &trigger_action,
            &cast_action,
            &move_action,
            &turn_action,
        )?;

        session.attach_action_sets(&[&action_set])?;
        let grip_space = grip_action.create_space(session, right_hand, xr::Posef::IDENTITY)?;
        let aim_space = aim_action.create_space(session, right_hand, xr::Posef::IDENTITY)?;
        let calibration = WandCalibration::default();
        println!(
            "[wand.bindings] api=1.1 meta_ext_enabled={meta_touch_plus_enabled} meta_profile={} fallback_profile={} left_path={} right_path={} grip={} aim={} trigger={} move={} turn={}",
            if meta_touch_plus_enabled {
                META_TOUCH_PLUS_EXTENSION_PROFILE
            } else {
                "disabled"
            },
            OCULUS_TOUCH_PROFILE,
            LEFT_HAND_PATH,
            RIGHT_HAND_PATH,
            RIGHT_GRIP_PATH,
            RIGHT_AIM_PATH,
            RIGHT_TRIGGER_PATH,
            LEFT_THUMBSTICK_PATH,
            RIGHT_THUMBSTICK_PATH
        );
        println!(
            "[wand.calibration] status=PROVISIONAL grip_to_prop_translation=({:.3},{:.3},{:.3})m grip_to_prop_rotation_xyzw=({:.5},{:.5},{:.5},{:.5}) length={:.3}m forward=-Z",
            calibration.grip_to_prop_translation.x,
            calibration.grip_to_prop_translation.y,
            calibration.grip_to_prop_translation.z,
            calibration.grip_to_prop_rotation.x,
            calibration.grip_to_prop_rotation.y,
            calibration.grip_to_prop_rotation.z,
            calibration.grip_to_prop_rotation.w,
            calibration.length_meters
        );

        Ok(Self {
            grip_space,
            aim_space,
            grip_action,
            aim_action,
            trigger_action,
            cast_action,
            move_action,
            turn_action,
            action_set,
            left_hand,
            right_hand,
            calibration,
            cast_latch: CastLatch::default(),
            telemetry: InputTelemetry::new(),
            last_tracked_time_ns: None,
            pose_was_valid: false,
            current_profile: None,
            profile_query_attempted: false,
            profile_is_null: false,
            session_generation: 0,
            pending_local_changes_ns: Vec::new(),
            local_reset_applied_this_sample: false,
        })
    }

    pub(super) fn on_session_begin(&mut self, generation: u32) {
        self.session_generation = generation;
        self.pending_local_changes_ns.clear();
        self.reset_continuity();
        self.telemetry.lifecycle_resets += 1;
        println!(
            "[wand.lifecycle] session_begin generation={generation}; cast disarmed until observed release"
        );
    }

    pub(super) fn on_session_end(&mut self) {
        self.pending_local_changes_ns.clear();
        self.reset_continuity();
        self.telemetry.lifecycle_resets += 1;
        println!(
            "[wand.lifecycle] session_end generation={}; live stroke canceled",
            self.session_generation
        );
    }

    pub(super) fn schedule_reference_space_change(&mut self, change_time: xr::Time) {
        let change_ns = change_time.as_nanos();
        insert_pending_change(&mut self.pending_local_changes_ns, change_ns);
        self.telemetry.reference_space_changes_scheduled += 1;
        println!(
            "[wand.lifecycle] LOCAL reference-space reset scheduled for change_ns={change_ns}"
        );
    }

    pub(super) fn on_interaction_profile_changed<G>(&mut self, session: &xr::Session<G>) {
        self.telemetry.profile_changes += 1;
        self.reset_continuity();
        self.refresh_profile_best_effort(session);
        let grip = bound_source_names_best_effort(&self.grip_action, session, "grip");
        let aim = bound_source_names_best_effort(&self.aim_action, session, "aim");
        let trigger = bound_source_names_best_effort(&self.trigger_action, session, "trigger");
        let cast = bound_source_names_best_effort(&self.cast_action, session, "cast");
        let movement = bound_source_names_best_effort(&self.move_action, session, "move");
        let turn = bound_source_names_best_effort(&self.turn_action, session, "turn");
        for report in [&grip, &aim, &trigger, &cast, &movement, &turn] {
            self.telemetry.bound_source_enumeration_errors += report.enumeration_errors;
            self.telemetry.bound_source_path_errors += report.path_errors;
        }
        println!(
            "[wand.profile] change={} current={} grip_sources={} aim_sources={} trigger_sources={} cast_sources={} move_sources={} turn_sources={}",
            self.telemetry.profile_changes,
            self.profile_label(),
            grip.names,
            aim.names,
            trigger.names,
            cast.names,
            movement.names,
            turn.names
        );
    }

    pub(super) fn sync_and_sample<G>(
        &mut self,
        session: &xr::Session<G>,
        local_space: &xr::Space,
        predicted_display_time: xr::Time,
        session_focused: bool,
    ) -> Result<(WandFrame, LocomotionSample), Box<dyn Error>> {
        self.local_reset_applied_this_sample = false;
        session
            .sync_actions(&[xr::ActiveActionSet::new(&self.action_set)])
            .map_err(|error| format!("xrSyncActions for wand and locomotion failed: {error}"))?;
        self.telemetry.action_syncs += 1;

        let grip_active = self
            .grip_action
            .is_active(session, self.right_hand)
            .map_err(|error| format!("xrGetActionStatePose(grip) failed: {error}"))?;
        let aim_active = self
            .aim_action
            .is_active(session, self.right_hand)
            .map_err(|error| format!("xrGetActionStatePose(aim) failed: {error}"))?;
        let trigger_state = self
            .trigger_action
            .state(session, self.right_hand)
            .map_err(|error| format!("xrGetActionStateFloat(trigger) failed: {error}"))?;
        let cast_state = self
            .cast_action
            .state(session, self.right_hand)
            .map_err(|error| format!("xrGetActionStateBoolean(cast) failed: {error}"))?;
        let move_state = self
            .move_action
            .state(session, self.left_hand)
            .map_err(|error| format!("xrGetActionStateVector2f(move) failed: {error}"))?;
        let turn_state = self
            .turn_action
            .state(session, self.right_hand)
            .map_err(|error| format!("xrGetActionStateVector2f(turn) failed: {error}"))?;
        self.telemetry.grip_active_frames += u32::from(grip_active);
        self.telemetry.aim_active_frames += u32::from(aim_active);
        self.telemetry.trigger_active_frames += u32::from(trigger_state.is_active);
        self.telemetry.cast_active_frames += u32::from(cast_state.is_active);
        self.telemetry.move_active_frames += u32::from(move_state.is_active);
        self.telemetry.turn_active_frames += u32::from(turn_state.is_active);

        let mut locomotion = LocomotionSample {
            move_active: session_focused && move_state.is_active,
            turn_active: session_focused && turn_state.is_active,
            ..LocomotionSample::default()
        };
        if locomotion.move_active {
            locomotion.move_axis =
                Vec2::new(move_state.current_state.x, move_state.current_state.y);
        }
        if locomotion.turn_active {
            locomotion.turn_axis = turn_state.current_state.x;
        }
        if !locomotion.move_axis.is_finite() || !locomotion.turn_axis.is_finite() {
            self.telemetry.nonfinite_frames += 1;
            locomotion = LocomotionSample::default();
        }
        self.telemetry.locomotion_nonzero_frames += u32::from(
            locomotion.move_axis.length_squared() > 1.0e-6 || locomotion.turn_axis.abs() > 1.0e-3,
        );

        let predicted_ns = predicted_display_time.as_nanos();
        let due_changes =
            take_due_reference_space_changes(&mut self.pending_local_changes_ns, predicted_ns);
        if due_changes != 0 {
            self.local_reset_applied_this_sample = true;
            self.reset_continuity();
            self.telemetry.reference_space_resets = self
                .telemetry
                .reference_space_resets
                .saturating_add(due_changes as u32);
            println!(
                "[wand.lifecycle] LOCAL reference-space continuity reset at predicted_ns={predicted_ns} due_changes={due_changes} remaining_pending={}",
                self.pending_local_changes_ns.len()
            );
            return Ok((WandFrame::Unavailable, LocomotionSample::default()));
        }
        if !session_focused {
            self.telemetry.nonfocused_frames += 1;
            return Ok((self.invalidate_pose(), LocomotionSample::default()));
        }
        if !grip_active || !aim_active {
            self.telemetry.inactive_frames += 1;
            return Ok((self.invalidate_pose(), locomotion));
        }

        let grip_location = self
            .grip_space
            .locate(local_space, predicted_display_time)
            .map_err(|error| format!("xrLocateSpace(grip -> LOCAL) failed: {error}"))?;
        let aim_location = self
            .aim_space
            .locate(local_space, predicted_display_time)
            .map_err(|error| format!("xrLocateSpace(aim -> LOCAL) failed: {error}"))?;
        let required = required_pose_flags();
        if !grip_location.location_flags.contains(required)
            || !aim_location.location_flags.contains(required)
        {
            self.telemetry.invalid_flag_frames += 1;
            return Ok((self.invalidate_pose(), locomotion));
        }

        let Some((grip_position, grip_orientation, grip_norm_error)) =
            checked_pose(grip_location.pose)
        else {
            self.telemetry.nonfinite_frames += 1;
            return Ok((self.invalidate_pose(), locomotion));
        };
        let Some((_aim_position, aim_orientation, aim_norm_error)) =
            checked_pose(aim_location.pose)
        else {
            self.telemetry.nonfinite_frames += 1;
            return Ok((self.invalidate_pose(), locomotion));
        };
        self.telemetry.quaternion_norm_max_error = self
            .telemetry
            .quaternion_norm_max_error
            .max(grip_norm_error.max(aim_norm_error));
        if grip_norm_error > MAX_QUATERNION_NORM_ERROR || aim_norm_error > MAX_QUATERNION_NORM_ERROR
        {
            self.telemetry.invalid_value_frames += 1;
            return Ok((self.invalidate_pose(), locomotion));
        }
        if self
            .last_tracked_time_ns
            .is_some_and(|last| predicted_ns <= last)
        {
            self.telemetry.nonmonotonic_frames += 1;
            return Ok((self.invalidate_pose(), locomotion));
        }

        if !self.profile_query_attempted {
            self.refresh_profile_best_effort(session);
        }
        // The runtime aim pose is the authoritative pointing axis.  Rendering
        // the prop from the grip orientation while targeting from the aim
        // orientation produced the visibly diverging brown wand/cyan ray seen
        // in the B18 headset check.
        let (prop_root, prop_orientation, tip, aim_direction) = canonical_wand_pose(
            grip_position,
            grip_orientation,
            aim_orientation,
            self.calibration,
        );
        if !prop_root.is_finite()
            || !prop_orientation.is_finite()
            || !tip.is_finite()
            || !aim_direction.is_finite()
        {
            self.telemetry.nonfinite_frames += 1;
            return Ok((self.invalidate_pose(), locomotion));
        }

        let trigger_value = if trigger_state.is_active {
            if !trigger_state.current_state.is_finite() {
                self.telemetry.nonfinite_frames += 1;
                return Ok((self.invalidate_pose(), locomotion));
            }
            self.telemetry.trigger_min =
                self.telemetry.trigger_min.min(trigger_state.current_state);
            self.telemetry.trigger_max =
                self.telemetry.trigger_max.max(trigger_state.current_state);
            trigger_state.current_state
        } else {
            0.0
        };
        let edges = self
            .cast_latch
            .update(cast_state.is_active, cast_state.current_state);
        self.telemetry.presses += u32::from(edges.pressed);
        self.telemetry.releases += u32::from(edges.released);
        self.telemetry.suppressed_held_reacquire += u32::from(edges.suppressed_held);
        self.telemetry.cancels += u32::from(edges.canceled);

        let grip_tip = grip_position.distance(tip);
        self.telemetry.grip_tip_min = self.telemetry.grip_tip_min.min(grip_tip);
        self.telemetry.grip_tip_max = self.telemetry.grip_tip_max.max(grip_tip);
        self.telemetry.grip_tip_sum += f64::from(grip_tip);
        self.telemetry.aim_prop_angle_max = self.telemetry.aim_prop_angle_max.max(
            aim_direction
                .dot(prop_orientation * Vec3::NEG_Z)
                .clamp(-1.0, 1.0)
                .acos(),
        );
        self.telemetry.tracked_frames += 1;
        self.last_tracked_time_ns = Some(predicted_ns);
        self.pose_was_valid = true;

        Ok((
            WandFrame::Tracked(WandSample {
                raw_grip_pose: grip_location.pose,
                raw_aim_pose: aim_location.pose,
                prop_root,
                prop_orientation,
                tip,
                // Targeting begins at the visible tip, so the rendered wand,
                // cyan guide and gameplay ray share one exact line.
                aim_origin: tip,
                aim_direction,
                trigger_value,
                cast_held: edges.held,
                pressed: edges.pressed,
                released: edges.released,
            }),
            locomotion,
        ))
    }

    pub(super) fn local_reset_applied_this_sample(&self) -> bool {
        self.local_reset_applied_this_sample
    }

    pub(super) fn verify_and_report(&self, frames_begun: u32) -> Result<bool, Box<dyn Error>> {
        if self.telemetry.action_syncs != frames_begun {
            return Err(format!(
                "wand action sync mismatch: syncs={} frames_begun={frames_begun}",
                self.telemetry.action_syncs
            )
            .into());
        }
        if self.telemetry.nonfinite_frames != 0
            || self.telemetry.invalid_value_frames != 0
            || self.telemetry.nonmonotonic_frames != 0
            || self.telemetry.quaternion_norm_max_error > MAX_QUATERNION_NORM_ERROR
        {
            return Err(format!(
                "invalid wand samples: nonfinite={} invalid_values={} nonmonotonic={} quaternion_norm_max_error={:.8}",
                self.telemetry.nonfinite_frames,
                self.telemetry.invalid_value_frames,
                self.telemetry.nonmonotonic_frames,
                self.telemetry.quaternion_norm_max_error
            )
            .into());
        }
        let tracked = self.telemetry.tracked_frames;
        let trigger_range = if self.telemetry.trigger_active_frames == 0 {
            "n/a".to_owned()
        } else {
            format!(
                "{:.3}..{:.3}",
                self.telemetry.trigger_min, self.telemetry.trigger_max
            )
        };
        let grip_tip = if tracked == 0 {
            "n/a".to_owned()
        } else {
            format!(
                "{:.4}/{:.4}/{:.4}m",
                self.telemetry.grip_tip_min,
                self.telemetry.grip_tip_sum as f32 / tracked as f32,
                self.telemetry.grip_tip_max
            )
        };
        println!(
            "[wand.lifecycle] action_set_create=1 attach=1 grip_space_create=1 aim_space_create=1 generation={} resets={} reference_space_changes_scheduled={} reference_space_resets={}",
            self.session_generation,
            self.telemetry.lifecycle_resets,
            self.telemetry.reference_space_changes_scheduled,
            self.telemetry.reference_space_resets
        );
        println!(
            "[wand.input] begun={frames_begun} sync={} nonfocused={} grip_active={} aim_active={} trigger_active={} cast_active={} move_active={} turn_active={} locomotion_nonzero={} tracked={} inactive={} invalid_flags={} presses={} releases={} cancels={} suppressed_held_reacquire={} tracking_losses={} PASS",
            self.telemetry.action_syncs,
            self.telemetry.nonfocused_frames,
            self.telemetry.grip_active_frames,
            self.telemetry.aim_active_frames,
            self.telemetry.trigger_active_frames,
            self.telemetry.cast_active_frames,
            self.telemetry.move_active_frames,
            self.telemetry.turn_active_frames,
            self.telemetry.locomotion_nonzero_frames,
            tracked,
            self.telemetry.inactive_frames,
            self.telemetry.invalid_flag_frames,
            self.telemetry.presses,
            self.telemetry.releases,
            self.telemetry.cancels,
            self.telemetry.suppressed_held_reacquire,
            self.telemetry.tracking_losses
        );
        println!(
            "[wand.pose] profile={} profile_query_errors={} bound_source_enumeration_errors={} bound_source_path_errors={} samples={} grip_tip_min_avg_max={} aim_prop_angle_max_deg={:.2} quaternion_norm_max_error={:.8} trigger_range={} nonfinite={} invalid_values={} nonmonotonic={} {}",
            self.profile_label(),
            self.telemetry.profile_query_errors,
            self.telemetry.bound_source_enumeration_errors,
            self.telemetry.bound_source_path_errors,
            tracked,
            grip_tip,
            self.telemetry.aim_prop_angle_max.to_degrees(),
            self.telemetry.quaternion_norm_max_error,
            trigger_range,
            self.telemetry.nonfinite_frames,
            self.telemetry.invalid_value_frames,
            self.telemetry.nonmonotonic_frames,
            if tracked > 0 { "PASS" } else { "INCONCLUSIVE" }
        );
        Ok(tracked > 0)
    }

    fn invalidate_pose(&mut self) -> WandFrame {
        if self.pose_was_valid {
            self.telemetry.tracking_losses += 1;
        }
        self.pose_was_valid = false;
        self.last_tracked_time_ns = None;
        if self.cast_latch.reset_unknown() {
            self.telemetry.cancels += 1;
        }
        WandFrame::Unavailable
    }

    fn reset_continuity(&mut self) {
        self.pose_was_valid = false;
        self.last_tracked_time_ns = None;
        if self.cast_latch.reset_unknown() {
            self.telemetry.cancels += 1;
        }
    }

    fn refresh_profile_best_effort<G>(&mut self, session: &xr::Session<G>) {
        self.profile_query_attempted = true;
        self.profile_is_null = false;
        self.current_profile = None;
        let profile = match session.current_interaction_profile(self.right_hand) {
            Ok(profile) => profile,
            Err(error) => {
                self.telemetry.profile_query_errors += 1;
                eprintln!(
                    "[wand.profile] warning: xrGetCurrentInteractionProfile(right hand) failed: {error}"
                );
                return;
            }
        };
        if profile == xr::Path::NULL {
            self.profile_is_null = true;
            return;
        }
        match self.grip_action.instance().path_to_string(profile) {
            Ok(name) => self.current_profile = Some(name),
            Err(error) => {
                self.telemetry.profile_query_errors += 1;
                eprintln!(
                    "[wand.profile] warning: xrPathToString(current interaction profile) failed: {error}"
                );
            }
        }
    }

    fn profile_label(&self) -> &str {
        self.current_profile.as_deref().unwrap_or_else(|| {
            if self.profile_is_null {
                "NULL"
            } else if self.profile_query_attempted {
                "UNAVAILABLE"
            } else {
                "UNQUERIED"
            }
        })
    }
}

fn suggest_bindings(
    instance: &xr::Instance,
    profile: &str,
    grip_action: &xr::Action<xr::Posef>,
    aim_action: &xr::Action<xr::Posef>,
    trigger_action: &xr::Action<f32>,
    cast_action: &xr::Action<bool>,
    move_action: &xr::Action<xr::Vector2f>,
    turn_action: &xr::Action<xr::Vector2f>,
) -> Result<(), Box<dyn Error>> {
    let profile_path = instance.string_to_path(profile)?;
    let grip_path = instance.string_to_path(RIGHT_GRIP_PATH)?;
    let aim_path = instance.string_to_path(RIGHT_AIM_PATH)?;
    let trigger_path = instance.string_to_path(RIGHT_TRIGGER_PATH)?;
    let move_path = instance.string_to_path(LEFT_THUMBSTICK_PATH)?;
    let turn_path = instance.string_to_path(RIGHT_THUMBSTICK_PATH)?;
    instance.suggest_interaction_profile_bindings(
        profile_path,
        &[
            xr::Binding::new(grip_action, grip_path),
            xr::Binding::new(aim_action, aim_path),
            xr::Binding::new(trigger_action, trigger_path),
            xr::Binding::new(cast_action, trigger_path),
            xr::Binding::new(move_action, move_path),
            xr::Binding::new(turn_action, turn_path),
        ],
    )?;
    Ok(())
}

struct BoundSourceReport {
    names: String,
    enumeration_errors: u32,
    path_errors: u32,
}

fn bound_source_names_best_effort<T: xr::ActionTy, G>(
    action: &xr::Action<T>,
    session: &xr::Session<G>,
    label: &str,
) -> BoundSourceReport {
    let sources = match action.bound_sources(session) {
        Ok(sources) => sources,
        Err(error) => {
            eprintln!(
                "[wand.profile] warning: xrEnumerateBoundSourcesForAction({label}) failed: {error}"
            );
            return BoundSourceReport {
                names: "[unavailable]".to_owned(),
                enumeration_errors: 1,
                path_errors: 0,
            };
        }
    };
    if sources.is_empty() {
        return BoundSourceReport {
            names: "[]".to_owned(),
            enumeration_errors: 0,
            path_errors: 0,
        };
    }
    let mut names = Vec::with_capacity(sources.len());
    let mut path_errors = 0_u32;
    for source in sources {
        if source == xr::Path::NULL {
            path_errors += 1;
            names.push("<NULL>".to_owned());
            eprintln!(
                "[wand.profile] warning: runtime returned XR_NULL_PATH for bound source {label}"
            );
            continue;
        }
        match action.instance().path_to_string(source) {
            Ok(name) => names.push(name),
            Err(error) => {
                path_errors += 1;
                names.push("<invalid-XrPath>".to_owned());
                eprintln!(
                    "[wand.profile] warning: xrPathToString(bound source {label}) failed: {error}"
                );
            }
        }
    }
    BoundSourceReport {
        names: format!("[{}]", names.join(",")),
        enumeration_errors: 0,
        path_errors,
    }
}

fn required_pose_flags() -> xr::SpaceLocationFlags {
    xr::SpaceLocationFlags::ORIENTATION_VALID
        | xr::SpaceLocationFlags::POSITION_VALID
        | xr::SpaceLocationFlags::ORIENTATION_TRACKED
        | xr::SpaceLocationFlags::POSITION_TRACKED
}

fn checked_pose(pose: xr::Posef) -> Option<(Vec3, Quat, f32)> {
    let position = Vec3::new(pose.position.x, pose.position.y, pose.position.z);
    let raw = Quat::from_xyzw(
        pose.orientation.x,
        pose.orientation.y,
        pose.orientation.z,
        pose.orientation.w,
    );
    if !position.is_finite() || !raw.is_finite() || raw.length_squared() < 1.0e-12 {
        return None;
    }
    let norm_error = (raw.length() - 1.0).abs();
    Some((position, raw.normalize(), norm_error))
}

fn insert_pending_change(pending: &mut Vec<i64>, change_ns: i64) {
    if let Err(index) = pending.binary_search(&change_ns) {
        pending.insert(index, change_ns);
    }
}

fn take_due_reference_space_changes(pending: &mut Vec<i64>, predicted_ns: i64) -> usize {
    let due = pending.partition_point(|change_ns| *change_ns <= predicted_ns);
    pending.drain(..due);
    due
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn explicit_identity_mount_places_tip_along_grip_negative_z() {
        let calibration = WandCalibration::default();
        let grip_position = Vec3::new(0.2, 1.3, -0.4);
        let grip_orientation = Quat::from_rotation_y(0.5);
        let (prop_root, _, tip, aim_direction) = canonical_wand_pose(
            grip_position,
            grip_orientation,
            grip_orientation,
            calibration,
        );
        assert!((tip.distance(prop_root) - 0.34).abs() < 1.0e-6);
        assert!((tip - (grip_position + grip_orientation * Vec3::NEG_Z * 0.34)).length() < 1.0e-6);
        assert!(aim_direction.abs_diff_eq((tip - prop_root).normalize(), 1.0e-6));
    }

    #[test]
    fn runtime_aim_orientation_drives_both_prop_and_target_ray() {
        let calibration = WandCalibration::default();
        let grip_orientation = Quat::from_rotation_x(-0.7);
        let aim_orientation = Quat::from_rotation_y(0.9);
        let (root, orientation, tip, direction) = canonical_wand_pose(
            Vec3::new(0.2, 1.1, -0.4),
            grip_orientation,
            aim_orientation,
            calibration,
        );
        assert!(direction.abs_diff_eq(aim_orientation * Vec3::NEG_Z, 1.0e-6));
        assert!(direction.abs_diff_eq(orientation * Vec3::NEG_Z, 1.0e-6));
        assert!(direction.abs_diff_eq((tip - root).normalize(), 1.0e-6));
        assert!(direction.dot(grip_orientation * Vec3::NEG_Z) < 0.9);
    }

    #[test]
    fn cast_is_armed_only_after_an_observed_release() {
        let mut latch = CastLatch::default();
        assert!(latch.update(true, true).suppressed_held);
        assert_eq!(latch.update(true, false), CastEdges::default());
        let press = latch.update(true, true);
        assert!(press.pressed && press.held);
        let release = latch.update(true, false);
        assert!(release.released && !release.held);
    }

    #[test]
    fn invalid_gap_cancels_and_requires_release_before_reacquire() {
        let mut latch = CastLatch::default();
        latch.update(true, false);
        assert!(latch.update(true, true).pressed);
        assert!(latch.reset_unknown());
        let held_after_gap = latch.update(true, true);
        assert!(held_after_gap.suppressed_held);
        assert!(!held_after_gap.pressed && !held_after_gap.held);
        latch.update(true, false);
        assert!(latch.update(true, true).pressed);
    }

    #[test]
    fn inactive_cast_action_reports_cancellation() {
        let mut latch = CastLatch::default();
        latch.update(true, false);
        assert!(latch.update(true, true).pressed);
        let inactive = latch.update(false, false);
        assert!(inactive.canceled);
        assert!(!inactive.held);
        assert!(latch.update(true, true).suppressed_held);
    }

    #[test]
    fn extension_and_core_profile_names_are_not_inverted() {
        assert_eq!(
            META_TOUCH_PLUS_EXTENSION_PROFILE,
            "/interaction_profiles/meta/touch_controller_plus"
        );
        assert_ne!(
            META_TOUCH_PLUS_EXTENSION_PROFILE,
            "/interaction_profiles/meta/touch_plus_controller"
        );
    }

    #[test]
    fn pending_local_change_is_applied_at_change_time_not_event_time() {
        let mut pending = Vec::new();
        insert_pending_change(&mut pending, 1_000);
        assert_eq!(take_due_reference_space_changes(&mut pending, 999), 0);
        assert_eq!(pending, [1_000]);
        assert_eq!(take_due_reference_space_changes(&mut pending, 1_000), 1);
        assert!(pending.is_empty());
    }

    #[test]
    fn later_pending_local_change_survives_an_earlier_boundary() {
        let mut pending = Vec::new();
        insert_pending_change(&mut pending, 2_000);
        insert_pending_change(&mut pending, 1_500);
        insert_pending_change(&mut pending, 2_000);
        assert_eq!(pending, [1_500, 2_000]);
        assert_eq!(take_due_reference_space_changes(&mut pending, 1_500), 1);
        assert_eq!(pending, [2_000]);
        assert_eq!(take_due_reference_space_changes(&mut pending, 1_999), 0);
        assert_eq!(take_due_reference_space_changes(&mut pending, 2_000), 1);
        assert!(pending.is_empty());
    }
}
