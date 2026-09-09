use std::error::Error;
use std::ffi::{CStr, CString};
use std::fmt;
use std::path::Path;

use glam::Vec2;

pub(super) const ABI_VERSION: u32 = 1;
const MAX_TEMPLATE_POINTS: usize = 1024;
const MAX_SEGMENTS: usize = 4096;
const PASS_MARK_COUNT: usize = 10;
const GESTURE_NAME_CAPACITY: usize = 64;
const ACTOR_NAME_CAPACITY: usize = 128;
const ERROR_CAPACITY: usize = 256;

const PROFILE_OK: u32 = 0;
const PROFILE_INVALID_ARGUMENT: u32 = 100;
const PROFILE_BUFFER_TOO_SMALL: u32 = 101;
const PROFILE_ALLOCATION_FAILURE: u32 = 102;
const PROFILE_INTERNAL_ERROR: u32 = 103;
const SCORE_OK: u32 = 0;
const SCORE_INVALID_ARGUMENT: u32 = 100;
const SCORE_ALLOCATION_FAILURE: u32 = 102;
const SCORE_INTERNAL_ERROR: u32 = 103;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
struct FfiVec2 {
    x: f32,
    y: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
struct FfiProfileReport {
    abi_version: u32,
    status: u32,
    template_point_count: u32,
    segment_count: u32,
    pass_mark_count: u32,
    base_package_version: u32,
    lesson_package_version: u32,
    accuracy_radius: f32,
    very_good_threshold: f32,
    very_bad_threshold: f32,
    default_draw_time_seconds: f32,
    draw_time_seconds: f32,
    pass_marks: [f32; PASS_MARK_COUNT],
    gesture_name: [u8; GESTURE_NAME_CAPACITY],
    lesson_actor_name: [u8; ACTOR_NAME_CAPACITY],
    error: [u8; ERROR_CAPACITY],
}

impl Default for FfiProfileReport {
    fn default() -> Self {
        Self {
            abi_version: 0,
            status: 0,
            template_point_count: 0,
            segment_count: 0,
            pass_mark_count: 0,
            base_package_version: 0,
            lesson_package_version: 0,
            accuracy_radius: 0.0,
            very_good_threshold: 0.0,
            very_bad_threshold: 0.0,
            default_draw_time_seconds: 0.0,
            draw_time_seconds: 0.0,
            pass_marks: [0.0; PASS_MARK_COUNT],
            gesture_name: [0; GESTURE_NAME_CAPACITY],
            lesson_actor_name: [0; ACTOR_NAME_CAPACITY],
            error: [0; ERROR_CAPACITY],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
struct FfiScoreReport {
    status: u32,
    score: f32,
    input_point_count: u32,
    unique_input_point_count: u32,
    dense_template_point_count: u32,
}

unsafe extern "C" {
    fn hpvr_hp1_gesture_abi_version() -> u32;
    fn hpvr_hp1_load_spell_profile_utf8(
        base_package_utf8: *const std::ffi::c_char,
        lesson_package_utf8: *const std::ffi::c_char,
        gesture_name_utf8: *const std::ffi::c_char,
        spell_class_name_utf8: *const std::ffi::c_char,
        output_template_points: *mut FfiVec2,
        template_point_capacity: u32,
        output_segments: *mut i32,
        segment_capacity: u32,
        output_report: *mut FfiProfileReport,
    ) -> u32;
    fn hpvr_hp1_compare_gesture(
        drawn_points: *const FfiVec2,
        drawn_point_count: u32,
        template_points: *const FfiVec2,
        template_point_count: u32,
        accuracy_radius: f32,
        output_report: *mut FfiScoreReport,
    ) -> u32;
    fn hpvr_hp1_lesson_resampling_period_ns(draw_time_seconds: f32) -> i64;
}

#[derive(Debug)]
pub(super) struct BridgeError(String);

impl fmt::Display for BridgeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

impl Error for BridgeError {}

#[derive(Debug)]
pub(super) struct SpellProfile {
    pub gesture_name: String,
    pub lesson_actor_name: String,
    pub template_points: Vec<Vec2>,
    pub segment_count: usize,
    pub pass_marks: [f32; PASS_MARK_COUNT],
    pub pass_mark_count: usize,
    pub accuracy_radius: f32,
    pub very_good_threshold: f32,
    pub very_bad_threshold: f32,
    pub default_draw_time_seconds: f32,
    pub draw_time_seconds: f32,
    pub resampling_period_ns: i64,
    pub base_package_version: u32,
    pub lesson_package_version: u32,
}

impl SpellProfile {
    pub(super) fn first_lesson_pass_mark(&self) -> Result<f32, BridgeError> {
        self.pass_marks
            .first()
            .copied()
            .filter(|_| self.pass_mark_count != 0)
            .ok_or_else(|| BridgeError("HP1 profile has no first-lesson pass mark".to_owned()))
    }
}

#[derive(Clone, Copy, Debug)]
pub(super) struct GestureScore {
    pub score: f32,
    pub input_point_count: u32,
    pub unique_input_point_count: u32,
    pub dense_template_point_count: u32,
}

pub(super) fn abi_version() -> u32 {
    // SAFETY: no parameters and a scalar return value.
    unsafe { hpvr_hp1_gesture_abi_version() }
}

pub(super) fn load_flipendo(data_root: &Path) -> Result<SpellProfile, BridgeError> {
    load_spell_profile(
        &data_root.join("system").join("HPBase.u"),
        &data_root.join("maps").join("Lev_Tut1.unr"),
        "FlipPattern",
        "spellFlip",
    )
}

fn load_spell_profile(
    base_package: &Path,
    lesson_package: &Path,
    gesture_name: &str,
    spell_class_name: &str,
) -> Result<SpellProfile, BridgeError> {
    if abi_version() != ABI_VERSION {
        return Err(BridgeError(format!(
            "hpvr_hp1 ABI mismatch: Rust expects {ABI_VERSION}, native reports {}",
            abi_version()
        )));
    }
    let base = path_c_string(base_package)?;
    let lesson = path_c_string(lesson_package)?;
    let gesture = CString::new(gesture_name)
        .map_err(|_| BridgeError("gesture name contains an interior NUL".to_owned()))?;
    let spell = CString::new(spell_class_name)
        .map_err(|_| BridgeError("spell class name contains an interior NUL".to_owned()))?;
    let mut points = vec![FfiVec2::default(); MAX_TEMPLATE_POINTS];
    let mut segments = vec![0_i32; MAX_SEGMENTS];
    let mut report = FfiProfileReport::default();
    // SAFETY: every pointer refers to live repr(C) storage with the exact
    // capacity passed alongside it, and all strings are null terminated.
    let returned_status = unsafe {
        hpvr_hp1_load_spell_profile_utf8(
            base.as_ptr(),
            lesson.as_ptr(),
            gesture.as_ptr(),
            spell.as_ptr(),
            points.as_mut_ptr(),
            points.len() as u32,
            segments.as_mut_ptr(),
            segments.len() as u32,
            &mut report,
        )
    };
    if returned_status != report.status {
        return Err(BridgeError(format!(
            "hpvr_hp1 returned status {returned_status} but report contains {}",
            report.status
        )));
    }
    if report.abi_version != ABI_VERSION {
        return Err(BridgeError(format!(
            "hpvr_hp1 profile report ABI mismatch: {}",
            report.abi_version
        )));
    }
    if returned_status != PROFILE_OK {
        let detail = ffi_text(&report.error, "profile error")?;
        let category = match returned_status {
            PROFILE_INVALID_ARGUMENT => "invalid_argument",
            PROFILE_BUFFER_TOO_SMALL => "buffer_too_small",
            PROFILE_ALLOCATION_FAILURE => "allocation_failure",
            PROFILE_INTERNAL_ERROR => "internal_error",
            _ => "package_or_profile_error",
        };
        return Err(BridgeError(format!(
            "HP1 profile load failed ({category}, status={returned_status}): {detail}"
        )));
    }

    let point_count = usize::try_from(report.template_point_count)
        .map_err(|_| BridgeError("native template count does not fit usize".to_owned()))?;
    let segment_count = usize::try_from(report.segment_count)
        .map_err(|_| BridgeError("native segment count does not fit usize".to_owned()))?;
    let pass_mark_count = usize::try_from(report.pass_mark_count)
        .map_err(|_| BridgeError("native pass-mark count does not fit usize".to_owned()))?;
    if point_count > points.len()
        || segment_count > segments.len()
        || pass_mark_count > PASS_MARK_COUNT
        || point_count < 2
    {
        return Err(BridgeError(
            "native HP1 profile counts exceed checked Rust buffers".to_owned(),
        ));
    }
    let template_points = points[..point_count]
        .iter()
        .map(|point| Vec2::new(point.x, point.y))
        .collect::<Vec<_>>();
    if template_points.iter().any(|point| !point.is_finite())
        || !report.accuracy_radius.is_finite()
        || report.accuracy_radius <= 0.0
        || !report.draw_time_seconds.is_finite()
        || report.draw_time_seconds <= 0.0
        || report.pass_marks[..pass_mark_count]
            .iter()
            .any(|value| !value.is_finite())
    {
        return Err(BridgeError(
            "native HP1 profile contains invalid numeric values".to_owned(),
        ));
    }
    // SAFETY: the native function accepts and returns scalar values only.
    let resampling_period_ns =
        unsafe { hpvr_hp1_lesson_resampling_period_ns(report.draw_time_seconds) };
    if resampling_period_ns <= 0 {
        return Err(BridgeError(
            "native HP1 profile produced an invalid resampling period".to_owned(),
        ));
    }

    Ok(SpellProfile {
        gesture_name: ffi_text(&report.gesture_name, "gesture name")?,
        lesson_actor_name: ffi_text(&report.lesson_actor_name, "lesson actor name")?,
        template_points,
        segment_count,
        pass_marks: report.pass_marks,
        pass_mark_count,
        accuracy_radius: report.accuracy_radius,
        very_good_threshold: report.very_good_threshold,
        very_bad_threshold: report.very_bad_threshold,
        default_draw_time_seconds: report.default_draw_time_seconds,
        draw_time_seconds: report.draw_time_seconds,
        resampling_period_ns,
        base_package_version: report.base_package_version,
        lesson_package_version: report.lesson_package_version,
    })
}

pub(super) fn compare_gesture(
    drawn_points: &[Vec2],
    template_points: &[Vec2],
    accuracy_radius: f32,
) -> Result<GestureScore, BridgeError> {
    let drawn_count = u32::try_from(drawn_points.len())
        .map_err(|_| BridgeError("drawn point count exceeds the C ABI range".to_owned()))?;
    let template_count = u32::try_from(template_points.len())
        .map_err(|_| BridgeError("template point count exceeds the C ABI range".to_owned()))?;
    let drawn = drawn_points
        .iter()
        .map(|point| FfiVec2 {
            x: point.x,
            y: point.y,
        })
        .collect::<Vec<_>>();
    let authored = template_points
        .iter()
        .map(|point| FfiVec2 {
            x: point.x,
            y: point.y,
        })
        .collect::<Vec<_>>();
    let mut report = FfiScoreReport::default();
    // SAFETY: all pointers refer to live, aligned repr(C) arrays for the call.
    let returned_status = unsafe {
        hpvr_hp1_compare_gesture(
            drawn.as_ptr(),
            drawn_count,
            authored.as_ptr(),
            template_count,
            accuracy_radius,
            &mut report,
        )
    };
    if returned_status != report.status {
        return Err(BridgeError(format!(
            "hpvr_hp1 scorer returned status {returned_status} but report contains {}",
            report.status
        )));
    }
    if returned_status != SCORE_OK {
        let category = match returned_status {
            SCORE_INVALID_ARGUMENT => "invalid_argument",
            SCORE_ALLOCATION_FAILURE => "allocation_failure",
            SCORE_INTERNAL_ERROR => "internal_error",
            _ => "invalid_score_input",
        };
        return Err(BridgeError(format!(
            "HP1 gesture scorer failed ({category}, status={returned_status})"
        )));
    }
    if !report.score.is_finite() || !(0.0..=1.0).contains(&report.score) {
        return Err(BridgeError(
            "HP1 gesture scorer returned an invalid score".to_owned(),
        ));
    }
    Ok(GestureScore {
        score: report.score,
        input_point_count: report.input_point_count,
        unique_input_point_count: report.unique_input_point_count,
        dense_template_point_count: report.dense_template_point_count,
    })
}

fn path_c_string(path: &Path) -> Result<CString, BridgeError> {
    let text = path
        .to_str()
        .ok_or_else(|| BridgeError(format!("path is not valid Unicode: {}", path.display())))?;
    CString::new(text.as_bytes())
        .map_err(|_| BridgeError(format!("path contains an interior NUL: {}", path.display())))
}

fn ffi_text<const N: usize>(bytes: &[u8; N], field: &str) -> Result<String, BridgeError> {
    if bytes.last().copied() != Some(0) && !bytes.contains(&0) {
        return Err(BridgeError(format!(
            "native {field} is not null terminated"
        )));
    }
    // SAFETY: the byte array is checked to contain a terminator and remains
    // live for the conversion; CStr performs no write.
    let value = unsafe { CStr::from_ptr(bytes.as_ptr().cast()) };
    value
        .to_str()
        .map(str::to_owned)
        .map_err(|_| BridgeError(format!("native {field} is not UTF-8")))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ffi_layout_and_version_match_the_c_header() {
        assert_eq!(abi_version(), ABI_VERSION);
        assert_eq!(std::mem::size_of::<FfiVec2>(), 8);
        assert_eq!(std::mem::align_of::<FfiVec2>(), 4);
        assert_eq!(std::mem::size_of::<FfiProfileReport>(), 536);
        assert_eq!(std::mem::align_of::<FfiProfileReport>(), 4);
        assert_eq!(std::mem::offset_of!(FfiProfileReport, status), 4);
        assert_eq!(std::mem::offset_of!(FfiProfileReport, accuracy_radius), 28);
        assert_eq!(std::mem::offset_of!(FfiProfileReport, pass_marks), 48);
        assert_eq!(std::mem::offset_of!(FfiProfileReport, gesture_name), 88);
        assert_eq!(
            std::mem::offset_of!(FfiProfileReport, lesson_actor_name),
            152
        );
        assert_eq!(std::mem::offset_of!(FfiProfileReport, error), 280);
        assert_eq!(std::mem::size_of::<FfiScoreReport>(), 20);
        assert_eq!(std::mem::align_of::<FfiScoreReport>(), 4);
    }

    #[test]
    fn scorer_bridge_accepts_perfect_synthetic_coverage() {
        let authored = [
            Vec2::new(0.0, 0.5),
            Vec2::new(0.5, 0.5),
            Vec2::new(1.0, 0.5),
        ];
        let drawn = (0..=16)
            .map(|index| Vec2::new(index as f32 / 16.0, 0.5))
            .collect::<Vec<_>>();
        let score = compare_gesture(&drawn, &authored, 0.03).unwrap();
        assert_eq!(score.score, 1.0);
        assert_eq!(score.input_point_count, 17);
        assert_eq!(score.unique_input_point_count, 17);
        assert_eq!(score.dense_template_point_count, 17);
    }

    #[test]
    fn native_period_uses_five_hundred_lesson_slots() {
        // SAFETY: scalar-only native call.
        assert_eq!(
            unsafe { hpvr_hp1_lesson_resampling_period_ns(12.0) },
            24_000_000
        );
    }

    #[test]
    fn opt_in_external_profile_crosses_the_complete_ffi_boundary() {
        let Some(root) = std::env::var_os("HPVR_TEST_HP1_DATA_ROOT") else {
            return;
        };
        let profile = load_flipendo(Path::new(&root)).unwrap();
        assert_eq!(profile.gesture_name, "FlipPattern");
        assert_eq!(profile.lesson_actor_name, "SpellLearnTrigger0");
        assert!(!profile.template_points.is_empty());
        assert!(profile.segment_count != 0);
        assert!(profile.first_lesson_pass_mark().unwrap().is_finite());
        assert!(profile.resampling_period_ns > 0);
    }
}
