use std::{error::Error, path::Path};

use glam::{Vec2, Vec3};

use super::hp1_gesture_ffi::{self, SpellProfile};
use super::wand_input::{WandFrame, WandSample};
use super::wand_trajectory_ffi::{
    self, AUTHORED_POINT_CAPACITY, LessonPlane, ProjectionOptions, ProjectionStatus,
    TrackedTipSample,
};

const MAX_RAW_SAMPLES: usize = 16_384;
const MAX_SAMPLE_GAP_NS: i64 = 100_000_000;
const MAX_SAMPLE_JUMP_METERS: f32 = 0.25;
const PROVISIONAL_PLANE_EXTENT_METERS: f32 = 0.70;
const FLIPENDO_CALIBRATION_EXTENT_METERS: f32 = 0.42;
const PROVISIONAL_JITTER_METERS: f32 = 0.004;
const PROVISIONAL_RESAMPLING_PERIOD_NS: i64 = 13_888_889;
const MINIMUM_RAW_SAMPLES: usize = 2;
const TEST_ASSIST_ACCURACY_MULTIPLIER: f32 = 1.75;

fn effective_accuracy_radius(authored_radius: f32, test_assist: bool) -> f32 {
    authored_radius
        * if test_assist {
            TEST_ASSIST_ACCURACY_MULTIPLIER
        } else {
            1.0
        }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(super) enum ProjectionVisualState {
    #[default]
    Idle,
    Recording,
    TrajectoryProjectedNotSpell,
    FlipendoAccepted,
    FlipendoRejected,
    ProjectionRejected,
    Canceled,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(super) enum SpellKind {
    Flipendo,
}

impl SpellKind {
    pub(super) fn label(self) -> &'static str {
        match self {
            Self::Flipendo => "Flipendo",
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub(super) struct SpellCastEvent {
    pub serial: u64,
    pub spell: SpellKind,
    pub predicted_display_time_ns: i64,
    pub tip: Vec3,
    pub aim_origin: Vec3,
    pub aim_direction: Vec3,
    pub score: f32,
    pub threshold: f32,
}

#[derive(Clone, Copy, Debug)]
struct LockedAim {
    tip: Vec3,
    origin: Vec3,
    direction: Vec3,
}

#[derive(Default)]
struct ProjectionTelemetry {
    attempts_started: u32,
    releases_observed: u32,
    projection_calls: u32,
    projections_ok: u32,
    native_rejections: u32,
    scoring_calls: u32,
    scores_ok: u32,
    score_rejections: u32,
    flipendo_accepted: u32,
    flipendo_rejected: u32,
    spell_events_emitted: u32,
    spell_events_consumed: u32,
    spell_events_discarded: u32,
    attempts_canceled: u32,
    too_short: u32,
    tracking_cancels: u32,
    protocol_cancels: u32,
    gap_cancels: u32,
    teleport_cancels: u32,
    overflow_cancels: u32,
    profile_cancels: u32,
    local_reference_cancels: u32,
    session_cancels: u32,
    raw_samples_total: u64,
    max_raw_samples: usize,
    max_output_points: usize,
    session_resets: u32,
    profile_resets: u32,
    local_reference_resets: u32,
}

struct RetainedTrajectory {
    raw_samples: Vec<TrackedTipSample>,
    projected_points: Vec<Vec2>,
    status: ProjectionStatus,
    raw_hash: u64,
    projected_hash: u64,
    score: Option<f32>,
    threshold: Option<f32>,
    accepted: Option<bool>,
}

pub(super) struct GestureProjectionCapture {
    raw_samples: Vec<TrackedTipSample>,
    frozen_plane: Option<LessonPlane>,
    locked_aim: Option<LockedAim>,
    active: bool,
    blocked_until_release: bool,
    last_time_ns: Option<i64>,
    last_tip: Option<Vec3>,
    visual_state: ProjectionVisualState,
    telemetry: ProjectionTelemetry,
    last_completed: Option<RetainedTrajectory>,
    spell_profile: Option<SpellProfile>,
    pending_spell_event: Option<SpellCastEvent>,
    next_spell_event_serial: u64,
    test_assist: bool,
}

impl GestureProjectionCapture {
    #[cfg(test)]
    pub(super) fn new(flipendo_data_root: Option<&Path>) -> Result<Self, Box<dyn Error>> {
        Self::new_with_test_assist(flipendo_data_root, false)
    }

    pub(super) fn new_with_test_assist(
        flipendo_data_root: Option<&Path>,
        test_assist: bool,
    ) -> Result<Self, Box<dyn Error>> {
        let native_version = wand_trajectory_ffi::abi_version();
        if native_version != wand_trajectory_ffi::ABI_VERSION {
            return Err(format!(
                "hpvr_wand ABI mismatch: Rust={} native={native_version}",
                wand_trajectory_ffi::ABI_VERSION
            )
            .into());
        }
        let spell_profile = flipendo_data_root
            .map(hp1_gesture_ffi::load_flipendo)
            .transpose()?;
        if let Some(profile) = spell_profile.as_ref() {
            let threshold = profile.first_lesson_pass_mark()?;
            println!(
                "[gesture.bridge] trajectory_abi={} trajectory_native={} hp1_abi={} hp1_native={} raw_capacity={} authored_output_capacity={} scorer=LINKED template=EXTERNAL_HP1_PACKAGE",
                wand_trajectory_ffi::ABI_VERSION,
                native_version,
                hp1_gesture_ffi::ABI_VERSION,
                hp1_gesture_ffi::abi_version(),
                MAX_RAW_SAMPLES,
                AUTHORED_POINT_CAPACITY
            );
            println!(
                "[gesture.profile] status=ok source=EXTERNAL_READ_ONLY base_version={} lesson_version={} gesture={} actor={} points={} segments={} accuracy={:.6} very_good={:.6} very_bad={:.6} default_draw_time_s={:.3} lesson_draw_time_s={:.3} resampling_period_ns={} lesson_level=0 pass_mark={:.6} plane_extent_m={:.3} extent_status=CALIBRATION_REQUIRED",
                profile.base_package_version,
                profile.lesson_package_version,
                profile.gesture_name,
                profile.lesson_actor_name,
                profile.template_points.len(),
                profile.segment_count,
                profile.accuracy_radius,
                profile.very_good_threshold,
                profile.very_bad_threshold,
                profile.default_draw_time_seconds,
                profile.draw_time_seconds,
                profile.resampling_period_ns,
                threshold,
                FLIPENDO_CALIBRATION_EXTENT_METERS
            );
            println!(
                "[gesture.feedback] cyan=external_template orange=recording green=flipendo_accepted red=flipendo_rejected amber=capture_canceled"
            );
            println!(
                "[gesture.assist] enabled={} authored_accuracy={:.6} effective_accuracy={:.6} multiplier={:.2} threshold=AUTHORED_UNCHANGED",
                u8::from(test_assist),
                profile.accuracy_radius,
                effective_accuracy_radius(profile.accuracy_radius, test_assist),
                if test_assist {
                    TEST_ASSIST_ACCURACY_MULTIPLIER
                } else {
                    1.0
                },
            );
        } else {
            println!(
                "[gesture.bridge] abi={} native={} raw_capacity={} authored_output_capacity={} scorer=NOT_LINKED template=NOT_LOADED",
                wand_trajectory_ffi::ABI_VERSION,
                native_version,
                MAX_RAW_SAMPLES,
                AUTHORED_POINT_CAPACITY
            );
            println!(
                "[gesture.feedback] orange=recording green=trajectory_projected_not_spell red=trajectory_rejected amber=capture_canceled"
            );
        }
        Ok(Self {
            raw_samples: Vec::with_capacity(1024),
            frozen_plane: None,
            locked_aim: None,
            active: false,
            blocked_until_release: false,
            last_time_ns: None,
            last_tip: None,
            visual_state: ProjectionVisualState::Idle,
            telemetry: ProjectionTelemetry::default(),
            last_completed: None,
            spell_profile,
            pending_spell_event: None,
            next_spell_event_serial: 1,
            test_assist,
        })
    }

    pub(super) fn on_session_begin(&mut self) {
        if self.active {
            self.cancel_active(CancelReason::Session);
        }
        self.discard_pending_spell_event("session_begin");
        self.reset_for_session();
        println!("[gesture.lifecycle] session_begin; raw attempt state reset");
    }

    pub(super) fn on_session_end(&mut self) {
        if self.active {
            self.cancel_active(CancelReason::Session);
        }
        self.discard_pending_spell_event("session_end");
        self.reset_for_session();
        println!("[gesture.lifecycle] session_end; raw attempt state reset");
    }

    pub(super) fn on_interaction_profile_changed(&mut self) -> ProjectionVisualState {
        self.discard_pending_spell_event("interaction_profile_change");
        let canceled_active_attempt = self.reset_for_external_discontinuity(CancelReason::Profile);
        self.telemetry.profile_resets += 1;
        println!(
            "[gesture.lifecycle] interaction_profile_changed canceled_active={} blocked_until_neutral=true",
            u8::from(canceled_active_attempt)
        );
        self.visual_state
    }

    pub(super) fn on_local_reference_space_reset(&mut self) -> ProjectionVisualState {
        self.discard_pending_spell_event("local_reference_space_reset");
        let canceled_active_attempt =
            self.reset_for_external_discontinuity(CancelReason::LocalReference);
        self.telemetry.local_reference_resets += 1;
        println!(
            "[gesture.lifecycle] local_reference_space_reset canceled_active={} blocked_until_neutral=true",
            u8::from(canceled_active_attempt)
        );
        self.visual_state
    }

    pub(super) fn observe(
        &mut self,
        frame: WandFrame,
        predicted_display_time_ns: i64,
    ) -> Result<ProjectionVisualState, Box<dyn Error>> {
        match frame {
            WandFrame::Disabled => {
                return Err("gesture capture received a disabled wand frame".into());
            }
            WandFrame::Unavailable => {
                if self.active {
                    self.cancel_active(CancelReason::Tracking);
                } else {
                    self.visual_state = ProjectionVisualState::Idle;
                }
                self.blocked_until_release = true;
                return Ok(self.visual_state);
            }
            WandFrame::Tracked(sample) => {
                if self.blocked_until_release {
                    if !sample.cast_held && !sample.pressed {
                        self.blocked_until_release = false;
                        self.visual_state = ProjectionVisualState::Idle;
                    }
                    return Ok(self.visual_state);
                }

                if sample.pressed {
                    if self.active {
                        self.cancel_active(CancelReason::Protocol);
                    }
                    self.begin_attempt(sample, predicted_display_time_ns)?;
                    return Ok(self.visual_state);
                }

                if !self.active {
                    return Ok(self.visual_state);
                }

                if sample.released {
                    self.telemetry.releases_observed += 1;
                    if !self.append_sample(sample, predicted_display_time_ns) {
                        return Ok(self.visual_state);
                    }
                    self.complete_attempt(sample)?;
                    return Ok(self.visual_state);
                }

                if !sample.cast_held {
                    self.cancel_active(CancelReason::Protocol);
                    self.blocked_until_release = false;
                    return Ok(self.visual_state);
                }

                self.append_sample(sample, predicted_display_time_ns);
                Ok(self.visual_state)
            }
        }
    }

    pub(super) fn verify_and_report(&self) -> Result<bool, Box<dyn Error>> {
        if self.active {
            return Err("gesture projection still has an open raw attempt".into());
        }
        let cancellation_reason_total = self.telemetry.too_short
            + self.telemetry.tracking_cancels
            + self.telemetry.protocol_cancels
            + self.telemetry.gap_cancels
            + self.telemetry.teleport_cancels
            + self.telemetry.overflow_cancels
            + self.telemetry.profile_cancels
            + self.telemetry.local_reference_cancels
            + self.telemetry.session_cancels;
        if self.telemetry.attempts_started
            != self.telemetry.projection_calls + self.telemetry.attempts_canceled
            || self.telemetry.projection_calls
                != self.telemetry.projections_ok + self.telemetry.native_rejections
            || self.telemetry.attempts_canceled != cancellation_reason_total
            || self.telemetry.releases_observed
                < self.telemetry.projection_calls + self.telemetry.too_short
            || self.telemetry.releases_observed > self.telemetry.attempts_started
            || self.telemetry.max_raw_samples > MAX_RAW_SAMPLES
        {
            return Err(format!(
                "gesture projection accounting mismatch: started={} releases={} calls={} ok={} native_rejections={} canceled={} cancellation_reasons={} max_raw={}",
                self.telemetry.attempts_started,
                self.telemetry.releases_observed,
                self.telemetry.projection_calls,
                self.telemetry.projections_ok,
                self.telemetry.native_rejections,
                self.telemetry.attempts_canceled,
                cancellation_reason_total,
                self.telemetry.max_raw_samples
            )
            .into());
        }
        if self.spell_profile.is_some() {
            if self.telemetry.scoring_calls != self.telemetry.projections_ok
                || self.telemetry.scoring_calls
                    != self.telemetry.scores_ok + self.telemetry.score_rejections
                || self.telemetry.scores_ok
                    != self.telemetry.flipendo_accepted + self.telemetry.flipendo_rejected
                || self.telemetry.flipendo_accepted != self.telemetry.spell_events_emitted
                || self.telemetry.spell_events_emitted
                    != self.telemetry.spell_events_consumed
                        + self.telemetry.spell_events_discarded
                        + u32::from(self.pending_spell_event.is_some())
            {
                return Err(format!(
                    "Flipendo scoring/event accounting mismatch: projected={} calls={} ok={} score_rejections={} accepted={} rejected={} events_emitted={} events_consumed={} events_discarded={} pending={}",
                    self.telemetry.projections_ok,
                    self.telemetry.scoring_calls,
                    self.telemetry.scores_ok,
                    self.telemetry.score_rejections,
                    self.telemetry.flipendo_accepted,
                    self.telemetry.flipendo_rejected,
                    self.telemetry.spell_events_emitted,
                    self.telemetry.spell_events_consumed,
                    self.telemetry.spell_events_discarded,
                    u8::from(self.pending_spell_event.is_some())
                )
                .into());
            }
        } else if self.telemetry.scoring_calls != 0
            || self.telemetry.scores_ok != 0
            || self.telemetry.score_rejections != 0
            || self.telemetry.flipendo_accepted != 0
            || self.telemetry.flipendo_rejected != 0
        {
            return Err("A5a projection-only mode unexpectedly scored a spell".into());
        }
        println!(
            "[gesture.capture] started={} releases={} calls={} projected={} native_rejections={} canceled={} too_short={} tracking_cancels={} protocol_cancels={} gap_cancels={} teleport_cancels={} overflow_cancels={} profile_cancels={} local_reference_cancels={} session_cancels={} raw_samples={} max_raw={} max_output={} session_resets={} profile_resets={} local_reference_resets={} PASS",
            self.telemetry.attempts_started,
            self.telemetry.releases_observed,
            self.telemetry.projection_calls,
            self.telemetry.projections_ok,
            self.telemetry.native_rejections,
            self.telemetry.attempts_canceled,
            self.telemetry.too_short,
            self.telemetry.tracking_cancels,
            self.telemetry.protocol_cancels,
            self.telemetry.gap_cancels,
            self.telemetry.teleport_cancels,
            self.telemetry.overflow_cancels,
            self.telemetry.profile_cancels,
            self.telemetry.local_reference_cancels,
            self.telemetry.session_cancels,
            self.telemetry.raw_samples_total,
            self.telemetry.max_raw_samples,
            self.telemetry.max_output_points,
            self.telemetry.session_resets,
            self.telemetry.profile_resets,
            self.telemetry.local_reference_resets
        );
        let conclusive = if self.spell_profile.is_some() {
            self.telemetry.scores_ok != 0
        } else {
            self.telemetry.projections_ok != 0
        };
        if self.spell_profile.is_some() {
            println!(
                "[gesture.score] calls={} ok={} rejected={} flipendo_accepted={} flipendo_rejected={} events_emitted={} events_consumed={} events_discarded={} pending_events={} PASS",
                self.telemetry.scoring_calls,
                self.telemetry.scores_ok,
                self.telemetry.score_rejections,
                self.telemetry.flipendo_accepted,
                self.telemetry.flipendo_rejected,
                self.telemetry.spell_events_emitted,
                self.telemetry.spell_events_consumed,
                self.telemetry.spell_events_discarded,
                u8::from(self.pending_spell_event.is_some())
            );
            println!(
                "[gesture.result] bridge=PASS live_trajectory_projection={} authored_template=EXTERNAL_HP1_PACKAGE live_flipendo_scoring={} spell_event_contract={} green_means=FLIPENDO_ACCEPTED red_means=FLIPENDO_REJECTED gameplay_adapter=TEST_WORLD_ONLY",
                if self.telemetry.projections_ok != 0 {
                    "PASS"
                } else {
                    "INCONCLUSIVE"
                },
                if conclusive { "PASS" } else { "INCONCLUSIVE" },
                if self.telemetry.spell_events_consumed != 0 {
                    "PASS"
                } else {
                    "INCONCLUSIVE"
                }
            );
        } else {
            println!(
                "[gesture.result] bridge=PASS live_trajectory_projection={} green_means=TRAJECTORY_PROJECTED_NOT_SPELL authored_template=NOT_LOADED scoring=NOT_IMPLEMENTED spell_dispatch=NOT_IMPLEMENTED",
                if conclusive { "PASS" } else { "INCONCLUSIVE" }
            );
        }
        if let Some(retained) = self.last_completed.as_ref() {
            let score = retained
                .score
                .map(|value| format!("{value:.6}"))
                .unwrap_or_else(|| "NOT_SUBMITTED".to_owned());
            let threshold = retained
                .threshold
                .map(|value| format!("{value:.6}"))
                .unwrap_or_else(|| "NONE".to_owned());
            let outcome = match retained.accepted {
                Some(true) => "FLIPENDO_ACCEPTED",
                Some(false) => "FLIPENDO_REJECTED",
                None => "NOT_A_SPELL",
            };
            println!(
                "[gesture.retained] status={} raw_3d_points={} projected_2d_points={} raw_hash={:016x} projected_hash={:016x} lifetime=PROCESS_LOCAL score={} threshold={} outcome={} dispatch=NONE",
                retained.status.label(),
                retained.raw_samples.len(),
                retained.projected_points.len(),
                retained.raw_hash,
                retained.projected_hash,
                score,
                threshold,
                outcome
            );
        }
        Ok(conclusive)
    }

    fn reset_for_session(&mut self) {
        self.raw_samples.clear();
        self.frozen_plane = None;
        self.locked_aim = None;
        self.active = false;
        self.blocked_until_release = false;
        self.last_time_ns = None;
        self.last_tip = None;
        self.visual_state = ProjectionVisualState::Idle;
        self.telemetry.session_resets += 1;
    }

    pub(super) fn take_spell_event(&mut self) -> Option<SpellCastEvent> {
        let event = self.pending_spell_event.take();
        if event.is_some() {
            self.telemetry.spell_events_consumed += 1;
        }
        event
    }

    fn reset_for_external_discontinuity(&mut self, reason: CancelReason) -> bool {
        let canceled_active_attempt = self.active;
        if canceled_active_attempt {
            self.cancel_active(reason);
        } else {
            self.raw_samples.clear();
            self.frozen_plane = None;
            self.locked_aim = None;
            self.last_time_ns = None;
            self.last_tip = None;
            self.visual_state = ProjectionVisualState::Idle;
        }
        // Never infer a new press from a trigger that was already held across
        // an OpenXR input or reference-space discontinuity.
        self.blocked_until_release = true;
        canceled_active_attempt
    }

    fn begin_attempt(
        &mut self,
        sample: WandSample,
        predicted_display_time_ns: i64,
    ) -> Result<(), Box<dyn Error>> {
        if !sample.cast_held {
            return Err("gesture press edge was not held".into());
        }
        self.raw_samples.clear();
        let (template_anchor, extent_m) = self
            .spell_profile
            .as_ref()
            .map(|profile| {
                (
                    profile.template_points.first().copied(),
                    FLIPENDO_CALIBRATION_EXTENT_METERS,
                )
            })
            .unwrap_or((None, PROVISIONAL_PLANE_EXTENT_METERS));
        self.frozen_plane = Some(lesson_plane_for_first_tip(
            sample.tip,
            template_anchor,
            extent_m,
        ));
        if !sample.tip.is_finite() || !sample.aim_origin.is_finite() {
            return Err("gesture press has a non-finite aim pose".into());
        }
        let locked_aim = LockedAim {
            tip: sample.tip,
            origin: sample.aim_origin,
            direction: sample
                .aim_direction
                .try_normalize()
                .ok_or("gesture press has an invalid aim direction")?,
        };
        self.locked_aim = Some(locked_aim);
        println!(
            "[gesture.target_lock] status=LOCKED_ON_PRESS predicted_ns={} tip_m=({:.6},{:.6},{:.6}) aim_origin_m=({:.6},{:.6},{:.6}) aim_direction=({:.6},{:.6},{:.6})",
            predicted_display_time_ns,
            sample.tip.x,
            sample.tip.y,
            sample.tip.z,
            sample.aim_origin.x,
            sample.aim_origin.y,
            sample.aim_origin.z,
            locked_aim.direction.x,
            locked_aim.direction.y,
            locked_aim.direction.z,
        );
        self.active = true;
        self.blocked_until_release = false;
        self.last_time_ns = None;
        self.last_tip = None;
        self.visual_state = ProjectionVisualState::Recording;
        self.telemetry.attempts_started += 1;
        if !self.append_sample(sample, predicted_display_time_ns) {
            return Err("first gesture sample was unexpectedly rejected".into());
        }
        Ok(())
    }

    pub(super) fn template_world_points(&self) -> Vec<Vec3> {
        let Some(plane) = self.frozen_plane else {
            return Vec::new();
        };
        let Some(profile) = self.spell_profile.as_ref() else {
            return Vec::new();
        };
        profile
            .template_points
            .iter()
            .copied()
            .map(|point| unproject_lesson_point(point, plane))
            .collect()
    }

    fn append_sample(&mut self, sample: WandSample, predicted_display_time_ns: i64) -> bool {
        if self
            .last_time_ns
            .is_some_and(|last| predicted_display_time_ns <= last)
        {
            self.cancel_active(CancelReason::Protocol);
            self.blocked_until_release = true;
            return false;
        }
        if let Some(last) = self.last_time_ns {
            let Some(delta_ns) = predicted_display_time_ns.checked_sub(last) else {
                self.cancel_active(CancelReason::Protocol);
                self.blocked_until_release = true;
                return false;
            };
            if delta_ns > MAX_SAMPLE_GAP_NS {
                self.cancel_active(CancelReason::Gap);
                self.blocked_until_release = true;
                return false;
            }
        }
        if self
            .last_tip
            .is_some_and(|last| last.distance(sample.tip) > MAX_SAMPLE_JUMP_METERS)
        {
            self.cancel_active(CancelReason::Teleport);
            self.blocked_until_release = true;
            return false;
        }
        if self.raw_samples.len() == MAX_RAW_SAMPLES {
            self.cancel_active(CancelReason::Overflow);
            self.blocked_until_release = true;
            return false;
        }

        self.raw_samples.push(TrackedTipSample {
            predicted_display_time_ns,
            position_m: sample.tip,
            pose_valid: true,
        });
        self.last_time_ns = Some(predicted_display_time_ns);
        self.last_tip = Some(sample.tip);
        self.telemetry.raw_samples_total += 1;
        self.telemetry.max_raw_samples = self.telemetry.max_raw_samples.max(self.raw_samples.len());
        true
    }

    fn complete_attempt(&mut self, release_sample: WandSample) -> Result<(), Box<dyn Error>> {
        self.active = false;
        self.last_time_ns = None;
        self.last_tip = None;
        if self.raw_samples.len() < MINIMUM_RAW_SAMPLES {
            let raw_sample_count = self.raw_samples.len();
            let raw_first = self.raw_samples.first().copied();
            let raw_last = self.raw_samples.last().copied();
            let raw_hash = hash_raw_trajectory(&self.raw_samples);
            self.telemetry.attempts_canceled += 1;
            self.telemetry.too_short += 1;
            self.visual_state = ProjectionVisualState::ProjectionRejected;
            self.raw_samples.clear();
            self.frozen_plane = None;
            self.locked_aim = None;
            println!(
                "[gesture.project] status=too_short raw_3d_points={} raw_first={:?} raw_last={:?} raw_hash={:016x} projected_2d_points=0 score=NOT_SUBMITTED outcome=PROJECTION_REJECTED dispatch=NONE",
                raw_sample_count, raw_first, raw_last, raw_hash
            );
            return Ok(());
        }

        let plane = self
            .frozen_plane
            .ok_or("gesture release has no frozen lesson plane")?;
        let first_time = self
            .raw_samples
            .first()
            .ok_or("gesture release has no first sample")?
            .predicted_display_time_ns;
        let last_time = self
            .raw_samples
            .last()
            .ok_or("gesture release has no last sample")?
            .predicted_display_time_ns;
        let duration_ns = last_time
            .checked_sub(first_time)
            .ok_or("gesture duration overflowed signed nanoseconds")?;
        let result = wand_trajectory_ffi::project_trajectory(
            &self.raw_samples,
            plane,
            ProjectionOptions {
                max_points: AUTHORED_POINT_CAPACITY as u32,
                minimum_tip_distance_m: PROVISIONAL_JITTER_METERS,
                resampling_period_ns: self
                    .spell_profile
                    .as_ref()
                    .map(|profile| profile.resampling_period_ns)
                    .unwrap_or(PROVISIONAL_RESAMPLING_PERIOD_NS),
            },
        )?;
        let raw_first = self
            .raw_samples
            .first()
            .copied()
            .ok_or("projected gesture has no raw first sample")?;
        let raw_last = self
            .raw_samples
            .last()
            .copied()
            .ok_or("projected gesture has no raw last sample")?;
        let projected_first = result.points.first().copied();
        let projected_last = result.points.last().copied();
        let raw_hash = hash_raw_trajectory(&self.raw_samples);
        let projected_hash = hash_projected_trajectory(&result.points);
        self.telemetry.projection_calls += 1;
        self.telemetry.max_output_points =
            self.telemetry.max_output_points.max(result.points.len());
        let mut score_value = None;
        let mut threshold_value = None;
        let mut accepted = None;
        let mut score_status = "NOT_SUBMITTED";
        if result.status == ProjectionStatus::Ok && result.points.len() >= 2 {
            self.telemetry.projections_ok += 1;
            if let Some(profile) = self.spell_profile.as_ref() {
                self.telemetry.scoring_calls += 1;
                let threshold = profile.first_lesson_pass_mark()?;
                threshold_value = Some(threshold);
                let accuracy_radius =
                    effective_accuracy_radius(profile.accuracy_radius, self.test_assist);
                match hp1_gesture_ffi::compare_gesture(
                    &result.points,
                    &profile.template_points,
                    accuracy_radius,
                ) {
                    Ok(score) => {
                        self.telemetry.scores_ok += 1;
                        score_value = Some(score.score);
                        score_status = "ok";
                        let passed = score.score >= threshold;
                        accepted = Some(passed);
                        if passed {
                            self.telemetry.flipendo_accepted += 1;
                            self.visual_state = ProjectionVisualState::FlipendoAccepted;
                            self.emit_spell_event(
                                release_sample,
                                self.locked_aim
                                    .ok_or("accepted gesture lost its press-time target lock")?,
                                raw_last.predicted_display_time_ns,
                                score.score,
                                threshold,
                            )?;
                        } else {
                            self.telemetry.flipendo_rejected += 1;
                            self.visual_state = ProjectionVisualState::FlipendoRejected;
                        }
                        println!(
                            "[gesture.score.detail] status=ok input_points={} unique_points={} dense_template_points={} score={:.6} threshold={:.6} accuracy_radius={:.6} test_assist={} comparison=GREATER_OR_EQUAL outcome={}",
                            score.input_point_count,
                            score.unique_input_point_count,
                            score.dense_template_point_count,
                            score.score,
                            threshold,
                            accuracy_radius,
                            u8::from(self.test_assist),
                            if passed {
                                "FLIPENDO_ACCEPTED"
                            } else {
                                "FLIPENDO_REJECTED"
                            }
                        );
                    }
                    Err(error) => {
                        self.telemetry.score_rejections += 1;
                        self.visual_state = ProjectionVisualState::ProjectionRejected;
                        score_status = "bridge_rejected";
                        eprintln!("[gesture.score.detail] status=bridge_rejected error={error}");
                    }
                }
            } else {
                self.visual_state = ProjectionVisualState::TrajectoryProjectedNotSpell;
            }
        } else {
            self.telemetry.native_rejections += 1;
            self.visual_state = ProjectionVisualState::ProjectionRejected;
        }
        let plane_mode = if self.spell_profile.is_some() {
            "EXTERNAL_TEMPLATE_FIRST_POINT_LOCAL_XY"
        } else {
            "PROVISIONAL_FIRST_TIP_LOCAL_XY"
        };
        let score_text = score_value
            .map(|value| format!("{value:.6}"))
            .unwrap_or_else(|| "NONE".to_owned());
        let threshold_text = threshold_value
            .map(|value| format!("{value:.6}"))
            .unwrap_or_else(|| "NONE".to_owned());
        let outcome = match accepted {
            Some(true) => "FLIPENDO_ACCEPTED",
            Some(false) => "FLIPENDO_REJECTED",
            None if result.status == ProjectionStatus::Ok && result.points.len() >= 2 => {
                if self.spell_profile.is_some() {
                    "SCORER_REJECTED"
                } else {
                    "TRAJECTORY_PROJECTED_NOT_SPELL"
                }
            }
            None => "PROJECTION_REJECTED",
        };
        println!(
            "[gesture.project] attempt={} plane={} plane_origin_m=({:.6},{:.6},{:.6}) plane_right=({:.3},{:.3},{:.3}) plane_up=({:.3},{:.3},{:.3}) extent={:.3}m raw_3d_points={} raw_first_ns={} raw_first_m=({:.6},{:.6},{:.6}) raw_last_ns={} raw_last_m=({:.6},{:.6},{:.6}) raw_hash={:016x} duration_ms={:.3} trajectory_projection_status={} projected_2d_points={} projected_first={:?} projected_last={:?} projected_hash={:016x} invalid={} nonmonotonic={} jitter_rejected={} resampled_away={} interpolated={} score_status={} score={} threshold={} outcome={} dispatch={}",
            self.telemetry.attempts_started,
            plane_mode,
            plane.origin_m.x,
            plane.origin_m.y,
            plane.origin_m.z,
            plane.right.x,
            plane.right.y,
            plane.right.z,
            plane.up.x,
            plane.up.y,
            plane.up.z,
            plane.width_m,
            result.report.input_sample_count,
            raw_first.predicted_display_time_ns,
            raw_first.position_m.x,
            raw_first.position_m.y,
            raw_first.position_m.z,
            raw_last.predicted_display_time_ns,
            raw_last.position_m.x,
            raw_last.position_m.y,
            raw_last.position_m.z,
            raw_hash,
            duration_ns as f64 / 1_000_000.0,
            result.status.label(),
            result.report.output_point_count,
            projected_first,
            projected_last,
            projected_hash,
            result.report.invalid_sample_count,
            result.report.non_monotonic_sample_count,
            result.report.jitter_rejected_count,
            result.report.resampled_away_count,
            result.report.interpolated_point_count,
            score_status,
            score_text,
            threshold_text,
            outcome,
            if accepted == Some(true) {
                "SPELL_EVENT"
            } else {
                "NONE"
            }
        );
        self.last_completed = Some(RetainedTrajectory {
            raw_samples: std::mem::take(&mut self.raw_samples),
            projected_points: result.points,
            status: result.status,
            raw_hash,
            projected_hash,
            score: score_value,
            threshold: threshold_value,
            accepted,
        });
        self.frozen_plane = None;
        self.locked_aim = None;
        Ok(())
    }

    fn emit_spell_event(
        &mut self,
        release_sample: WandSample,
        locked_aim: LockedAim,
        predicted_display_time_ns: i64,
        score: f32,
        threshold: f32,
    ) -> Result<(), Box<dyn Error>> {
        if self.pending_spell_event.is_some() {
            return Err("accepted spell would overwrite an unconsumed event".into());
        }
        let aim_direction = locked_aim.direction;
        if !release_sample.tip.is_finite()
            || !locked_aim.tip.is_finite()
            || !locked_aim.origin.is_finite()
            || !score.is_finite()
            || !threshold.is_finite()
        {
            return Err("accepted spell event contains a non-finite value".into());
        }
        let serial = self.next_spell_event_serial;
        self.next_spell_event_serial = serial
            .checked_add(1)
            .ok_or("spell-event serial exhausted")?;
        self.pending_spell_event = Some(SpellCastEvent {
            serial,
            spell: SpellKind::Flipendo,
            predicted_display_time_ns,
            tip: release_sample.tip,
            aim_origin: locked_aim.origin,
            aim_direction,
            score,
            threshold,
        });
        self.telemetry.spell_events_emitted += 1;
        println!(
            "[spell.event] serial={} spell={} status=EMITTED_ON_ACCEPTED_RELEASE target_lock=PRESS predicted_ns={} score={:.6} threshold={:.6} release_tip_m=({:.6},{:.6},{:.6}) locked_tip_m=({:.6},{:.6},{:.6}) aim_origin_m=({:.6},{:.6},{:.6}) aim_direction=({:.6},{:.6},{:.6})",
            serial,
            SpellKind::Flipendo.label(),
            predicted_display_time_ns,
            score,
            threshold,
            release_sample.tip.x,
            release_sample.tip.y,
            release_sample.tip.z,
            locked_aim.tip.x,
            locked_aim.tip.y,
            locked_aim.tip.z,
            locked_aim.origin.x,
            locked_aim.origin.y,
            locked_aim.origin.z,
            aim_direction.x,
            aim_direction.y,
            aim_direction.z
        );
        Ok(())
    }

    fn discard_pending_spell_event(&mut self, reason: &str) {
        if let Some(event) = self.pending_spell_event.take() {
            self.telemetry.spell_events_discarded += 1;
            println!(
                "[spell.event] serial={} spell={} status=DISCARDED_UNCONSUMED reason={}",
                event.serial,
                event.spell.label(),
                reason
            );
        }
    }

    fn cancel_active(&mut self, reason: CancelReason) {
        if !self.active {
            return;
        }
        let raw_sample_count = self.raw_samples.len();
        let raw_first = self.raw_samples.first().copied();
        let raw_last = self.raw_samples.last().copied();
        let raw_hash = hash_raw_trajectory(&self.raw_samples);
        println!(
            "[gesture.cancel] attempt={} reason={} raw_3d_points={} raw_first={:?} raw_last={:?} raw_hash={:016x} projected_2d_points=0 bridge_call=NONE scorer=UNAVAILABLE dispatch=NONE",
            self.telemetry.attempts_started,
            reason.label(),
            raw_sample_count,
            raw_first,
            raw_last,
            raw_hash
        );
        self.active = false;
        self.raw_samples.clear();
        self.frozen_plane = None;
        self.locked_aim = None;
        self.last_time_ns = None;
        self.last_tip = None;
        self.visual_state = ProjectionVisualState::Canceled;
        self.telemetry.attempts_canceled += 1;
        match reason {
            CancelReason::Tracking => self.telemetry.tracking_cancels += 1,
            CancelReason::Protocol => self.telemetry.protocol_cancels += 1,
            CancelReason::Gap => self.telemetry.gap_cancels += 1,
            CancelReason::Teleport => self.telemetry.teleport_cancels += 1,
            CancelReason::Overflow => self.telemetry.overflow_cancels += 1,
            CancelReason::Profile => self.telemetry.profile_cancels += 1,
            CancelReason::LocalReference => self.telemetry.local_reference_cancels += 1,
            CancelReason::Session => self.telemetry.session_cancels += 1,
        }
    }
}

fn lesson_plane_for_first_tip(
    first_tip: Vec3,
    template_anchor: Option<Vec2>,
    extent_m: f32,
) -> LessonPlane {
    let origin_m = template_anchor.map_or(first_tip, |anchor| {
        first_tip - Vec3::X * ((anchor.x - 0.5) * extent_m)
            + Vec3::Y * ((anchor.y - 0.5) * extent_m)
    });
    LessonPlane {
        origin_m,
        right: Vec3::X,
        up: Vec3::Y,
        width_m: extent_m,
        height_m: extent_m,
    }
}

fn unproject_lesson_point(point: Vec2, plane: LessonPlane) -> Vec3 {
    plane.origin_m
        + plane.right.normalize() * ((point.x - 0.5) * plane.width_m)
        + plane.up.normalize() * ((0.5 - point.y) * plane.height_m)
}

fn hash_raw_trajectory(samples: &[TrackedTipSample]) -> u64 {
    let mut hash = 0xcbf29ce484222325_u64;
    for sample in samples {
        hash = fnv1a_extend(hash, &sample.predicted_display_time_ns.to_le_bytes());
        hash = fnv1a_extend(hash, &sample.position_m.x.to_bits().to_le_bytes());
        hash = fnv1a_extend(hash, &sample.position_m.y.to_bits().to_le_bytes());
        hash = fnv1a_extend(hash, &sample.position_m.z.to_bits().to_le_bytes());
        hash = fnv1a_extend(hash, &[u8::from(sample.pose_valid)]);
    }
    hash
}

fn hash_projected_trajectory(points: &[Vec2]) -> u64 {
    let mut hash = 0xcbf29ce484222325_u64;
    for point in points {
        hash = fnv1a_extend(hash, &point.x.to_bits().to_le_bytes());
        hash = fnv1a_extend(hash, &point.y.to_bits().to_le_bytes());
    }
    hash
}

fn fnv1a_extend(mut hash: u64, bytes: &[u8]) -> u64 {
    for byte in bytes {
        hash ^= u64::from(*byte);
        hash = hash.wrapping_mul(0x100000001b3);
    }
    hash
}

#[derive(Clone, Copy)]
enum CancelReason {
    Tracking,
    Protocol,
    Gap,
    Teleport,
    Overflow,
    Profile,
    LocalReference,
    Session,
}

impl CancelReason {
    fn label(self) -> &'static str {
        match self {
            Self::Tracking => "tracking",
            Self::Protocol => "protocol",
            Self::Gap => "sample_gap",
            Self::Teleport => "tip_jump",
            Self::Overflow => "raw_capacity",
            Self::Profile => "interaction_profile_change",
            Self::LocalReference => "local_reference_space_reset",
            Self::Session => "session_transition",
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use glam::Quat;
    use openxr as xr;

    #[test]
    fn test_assist_expands_spatial_error_without_changing_authored_default() {
        assert!((effective_accuracy_radius(0.03, false) - 0.03).abs() < f32::EPSILON);
        assert!((effective_accuracy_radius(0.03, true) - 0.0525).abs() < f32::EPSILON);
    }

    fn sample(time_tip: Vec3, held: bool, pressed: bool, released: bool) -> WandSample {
        WandSample {
            raw_grip_pose: xr::Posef::IDENTITY,
            raw_aim_pose: xr::Posef::IDENTITY,
            prop_root: time_tip + Vec3::Z * 0.34,
            prop_orientation: Quat::IDENTITY,
            tip: time_tip,
            aim_origin: Vec3::ZERO,
            aim_direction: Vec3::NEG_Z,
            trigger_value: if held { 1.0 } else { 0.0 },
            cast_held: held,
            pressed,
            released,
        }
    }

    fn synthetic_flipendo_profile(points: Vec<Vec2>) -> SpellProfile {
        SpellProfile {
            gesture_name: "SyntheticFlipendo".to_owned(),
            lesson_actor_name: "SyntheticLesson".to_owned(),
            template_points: points,
            segment_count: 0,
            pass_marks: [0.5; 10],
            pass_mark_count: 10,
            accuracy_radius: 0.03,
            very_good_threshold: 0.8,
            very_bad_threshold: 0.6,
            default_draw_time_seconds: 6.0,
            draw_time_seconds: 6.0,
            resampling_period_ns: 24_000_000,
            base_package_version: 76,
            lesson_package_version: 76,
        }
    }

    #[test]
    fn authored_first_point_is_anchored_to_the_physical_press_tip() {
        let first_tip = Vec3::new(0.2, 1.3, -0.8);
        let authored_anchor = Vec2::new(0.17, 0.08);
        let plane = lesson_plane_for_first_tip(
            first_tip,
            Some(authored_anchor),
            FLIPENDO_CALIBRATION_EXTENT_METERS,
        );
        assert!(unproject_lesson_point(authored_anchor, plane).abs_diff_eq(first_tip, 1.0e-6));
        assert!(unproject_lesson_point(Vec2::new(1.0, 0.0), plane).y > plane.origin_m.y);
        assert!(unproject_lesson_point(Vec2::new(0.0, 1.0), plane).y < plane.origin_m.y);
    }

    #[test]
    fn raw_capture_includes_press_and_release_endpoints_then_projects_once() {
        let mut capture = GestureProjectionCapture::new(None).unwrap();
        capture.on_session_begin();
        capture
            .observe(WandFrame::Tracked(sample(Vec3::ZERO, true, true, false)), 1)
            .unwrap();
        capture
            .observe(
                WandFrame::Tracked(sample(Vec3::new(0.1, 0.1, 0.0), true, false, false)),
                20_000_001,
            )
            .unwrap();
        let state = capture
            .observe(
                WandFrame::Tracked(sample(Vec3::new(0.2, 0.0, 0.0), false, false, true)),
                40_000_001,
            )
            .unwrap();
        assert_eq!(state, ProjectionVisualState::TrajectoryProjectedNotSpell);
        assert_eq!(capture.telemetry.attempts_started, 1);
        assert_eq!(capture.telemetry.releases_observed, 1);
        assert_eq!(capture.telemetry.projection_calls, 1);
        assert_eq!(capture.telemetry.projections_ok, 1);
        assert_eq!(capture.telemetry.raw_samples_total, 3);
        let retained = capture.last_completed.as_ref().unwrap();
        assert_eq!(retained.raw_samples.len(), 3);
        assert_eq!(retained.projected_points.len(), 4);
        assert_eq!(
            retained
                .raw_samples
                .first()
                .unwrap()
                .predicted_display_time_ns,
            1
        );
        assert_eq!(
            retained
                .raw_samples
                .last()
                .unwrap()
                .predicted_display_time_ns,
            40_000_001
        );
        assert_eq!(
            retained.projected_points.first().unwrap(),
            &Vec2::splat(0.5)
        );
        assert!(
            retained
                .projected_points
                .last()
                .unwrap()
                .abs_diff_eq(Vec2::new(0.7857143, 0.5), 1.0e-6)
        );
    }

    #[test]
    fn accepted_flipendo_emits_one_event_consumable_exactly_once() {
        let authored = vec![
            Vec2::new(0.50, 0.50),
            Vec2::new(0.75, 0.50),
            Vec2::new(1.00, 0.50),
        ];
        let drawn = (0..=16)
            .map(|index| Vec2::new(0.50 + 0.50 * index as f32 / 16.0, 0.50))
            .collect::<Vec<_>>();
        let first_tip = Vec3::new(0.1, -0.2, -0.8);
        let plane = lesson_plane_for_first_tip(
            first_tip,
            authored.first().copied(),
            FLIPENDO_CALIBRATION_EXTENT_METERS,
        );
        let mut capture = GestureProjectionCapture::new(None).unwrap();
        capture.spell_profile = Some(synthetic_flipendo_profile(authored.clone()));
        let locked_origin = Vec3::new(1.0, 2.0, 3.0);
        let locked_direction = Vec3::X;
        let release_origin = Vec3::new(9.0, 8.0, 7.0);
        let release_direction = Vec3::Z;
        for (index, point) in drawn.iter().copied().enumerate() {
            let first = index == 0;
            let last = index + 1 == drawn.len();
            let mut wand_sample = sample(unproject_lesson_point(point, plane), !last, first, last);
            if first {
                wand_sample.aim_origin = locked_origin;
                wand_sample.aim_direction = locked_direction;
            } else if last {
                wand_sample.aim_origin = release_origin;
                wand_sample.aim_direction = release_direction;
            }
            let state = capture
                .observe(
                    WandFrame::Tracked(wand_sample),
                    1 + index as i64 * 24_000_000,
                )
                .unwrap();
            if last {
                assert_eq!(state, ProjectionVisualState::FlipendoAccepted);
            }
        }

        assert_eq!(capture.telemetry.flipendo_accepted, 1);
        assert_eq!(capture.telemetry.spell_events_emitted, 1);
        let event = capture.take_spell_event().expect("accepted event");
        assert_eq!(event.serial, 1);
        assert_eq!(event.spell, SpellKind::Flipendo);
        assert_eq!(event.aim_origin, locked_origin);
        assert_eq!(event.aim_direction, locked_direction);
        assert_ne!(event.aim_origin, release_origin);
        assert_ne!(event.aim_direction, release_direction);
        assert!(event.score >= event.threshold);
        assert!(capture.take_spell_event().is_none());
        assert_eq!(capture.telemetry.spell_events_consumed, 1);
        capture.verify_and_report().unwrap();
    }

    #[test]
    fn unavailable_cancels_and_requires_a_neutral_sample_before_restart() {
        let mut capture = GestureProjectionCapture::new(None).unwrap();
        capture
            .observe(WandFrame::Tracked(sample(Vec3::ZERO, true, true, false)), 1)
            .unwrap();
        assert_eq!(
            capture.observe(WandFrame::Unavailable, 2).unwrap(),
            ProjectionVisualState::Canceled
        );
        assert!(capture.blocked_until_release);
        capture
            .observe(
                WandFrame::Tracked(sample(Vec3::ZERO, false, false, false)),
                3,
            )
            .unwrap();
        assert!(!capture.blocked_until_release);
        capture
            .observe(WandFrame::Tracked(sample(Vec3::ZERO, true, true, false)), 4)
            .unwrap();
        assert!(capture.active);
        assert_eq!(capture.telemetry.attempts_started, 2);
        assert_eq!(capture.telemetry.tracking_cancels, 1);
    }

    #[test]
    fn unavailable_while_idle_does_not_claim_a_canceled_attempt() {
        let mut capture = GestureProjectionCapture::new(None).unwrap();
        assert_eq!(
            capture.observe(WandFrame::Unavailable, 1).unwrap(),
            ProjectionVisualState::Idle
        );
        assert!(capture.blocked_until_release);
        assert_eq!(capture.telemetry.attempts_started, 0);
        assert_eq!(capture.telemetry.attempts_canceled, 0);
        assert_eq!(capture.telemetry.tracking_cancels, 0);
    }

    #[test]
    fn large_gap_cancels_without_projecting_or_restarting_while_held() {
        let mut capture = GestureProjectionCapture::new(None).unwrap();
        capture
            .observe(WandFrame::Tracked(sample(Vec3::ZERO, true, true, false)), 1)
            .unwrap();
        capture
            .observe(
                WandFrame::Tracked(sample(Vec3::new(0.01, 0.0, 0.0), true, false, false)),
                MAX_SAMPLE_GAP_NS + 2,
            )
            .unwrap();
        assert!(!capture.active);
        assert!(capture.blocked_until_release);
        assert_eq!(capture.telemetry.gap_cancels, 1);
        assert_eq!(capture.telemetry.projection_calls, 0);
    }

    #[test]
    fn teleport_cancels_the_raw_attempt_instead_of_bridging() {
        let mut capture = GestureProjectionCapture::new(None).unwrap();
        capture
            .observe(WandFrame::Tracked(sample(Vec3::ZERO, true, true, false)), 1)
            .unwrap();
        capture
            .observe(
                WandFrame::Tracked(sample(
                    Vec3::new(MAX_SAMPLE_JUMP_METERS + 0.01, 0.0, 0.0),
                    false,
                    false,
                    true,
                )),
                10_000_001,
            )
            .unwrap();
        assert_eq!(capture.telemetry.teleport_cancels, 1);
        assert_eq!(capture.telemetry.projection_calls, 0);
        assert_eq!(capture.visual_state, ProjectionVisualState::Canceled);
    }

    #[test]
    fn timestamp_difference_overflow_cancels_without_panicking() {
        let mut capture = GestureProjectionCapture::new(None).unwrap();
        capture
            .observe(
                WandFrame::Tracked(sample(Vec3::ZERO, true, true, false)),
                i64::MIN,
            )
            .unwrap();
        capture
            .observe(
                WandFrame::Tracked(sample(Vec3::new(0.01, 0.0, 0.0), true, false, false)),
                i64::MAX,
            )
            .unwrap();
        assert!(!capture.active);
        assert!(capture.blocked_until_release);
        assert_eq!(capture.telemetry.protocol_cancels, 1);
        assert_eq!(capture.telemetry.projection_calls, 0);
    }

    #[test]
    fn interaction_profile_change_cancels_and_requires_neutral() {
        let mut capture = GestureProjectionCapture::new(None).unwrap();
        capture
            .observe(WandFrame::Tracked(sample(Vec3::ZERO, true, true, false)), 1)
            .unwrap();
        assert_eq!(
            capture.on_interaction_profile_changed(),
            ProjectionVisualState::Canceled
        );
        assert!(!capture.active);
        assert!(capture.blocked_until_release);
        assert_eq!(capture.telemetry.attempts_canceled, 1);
        assert_eq!(capture.telemetry.profile_cancels, 1);
        capture.verify_and_report().unwrap();
    }
}
