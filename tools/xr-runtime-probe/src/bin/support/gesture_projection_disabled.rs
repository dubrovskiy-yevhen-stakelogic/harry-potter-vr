use std::error::Error;
use std::path::Path;

use glam::Vec3;

use super::wand_input::WandFrame;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[allow(dead_code)]
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

pub(super) struct GestureProjectionCapture;

impl GestureProjectionCapture {
    pub(super) fn new_with_test_assist(
        _flipendo_data_root: Option<&Path>,
        _test_assist: bool,
    ) -> Result<Self, Box<dyn Error>> {
        Err("gesture projection was not compiled into this executable".into())
    }

    pub(super) fn on_session_begin(&mut self) {}

    pub(super) fn on_session_end(&mut self) {}

    pub(super) fn on_interaction_profile_changed(&mut self) -> ProjectionVisualState {
        ProjectionVisualState::Idle
    }

    pub(super) fn on_local_reference_space_reset(&mut self) -> ProjectionVisualState {
        ProjectionVisualState::Idle
    }

    pub(super) fn observe(
        &mut self,
        _frame: WandFrame,
        _predicted_display_time_ns: i64,
    ) -> Result<ProjectionVisualState, Box<dyn Error>> {
        Err("gesture projection was not compiled into this executable".into())
    }

    pub(super) fn verify_and_report(&self) -> Result<bool, Box<dyn Error>> {
        Err("gesture projection was not compiled into this executable".into())
    }

    pub(super) fn template_world_points(&self) -> Vec<Vec3> {
        Vec::new()
    }

    pub(super) fn take_spell_event(&mut self) -> Option<SpellCastEvent> {
        None
    }
}
