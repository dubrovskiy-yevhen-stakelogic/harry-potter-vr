use std::error::Error;
use std::fmt;

use glam::{Vec2, Vec3};

pub(super) const ABI_VERSION: u32 = 1;
pub(super) const AUTHORED_POINT_CAPACITY: usize = 500;

const STATUS_OK: u32 = 0;
const STATUS_INVALID_PLANE: u32 = 1;
const STATUS_INVALID_OPTIONS: u32 = 2;
const STATUS_TRACKING_LOST: u32 = 3;
const STATUS_INVALID_TIMING: u32 = 4;
const STATUS_INVALID_ARGUMENT: u32 = 100;
const STATUS_BUFFER_TOO_SMALL: u32 = 101;
const STATUS_ALLOCATION_FAILURE: u32 = 102;
const STATUS_INTERNAL_ERROR: u32 = 103;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
struct FfiVec2 {
    x: f32,
    y: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
struct FfiTrackedTipSample {
    predicted_display_time_ns: i64,
    position_x_m: f32,
    position_y_m: f32,
    position_z_m: f32,
    pose_valid: u8,
    reserved: [u8; 3],
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
struct FfiLessonPlane {
    origin_x_m: f32,
    origin_y_m: f32,
    origin_z_m: f32,
    right_x: f32,
    right_y: f32,
    right_z: f32,
    up_x: f32,
    up_y: f32,
    up_z: f32,
    width_m: f32,
    height_m: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
struct FfiProjectionOptions {
    max_points: u32,
    minimum_tip_distance_m: f32,
    resampling_period_ns: i64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
struct FfiProjectionReport {
    status: u32,
    output_point_count: u32,
    input_sample_count: u32,
    invalid_sample_count: u32,
    non_monotonic_sample_count: u32,
    jitter_rejected_count: u32,
    resampled_away_count: u32,
    interpolated_point_count: u32,
}

unsafe extern "C" {
    fn hpvr_wand_abi_version() -> u32;
    fn hpvr_wand_project_trajectory(
        samples: *const FfiTrackedTipSample,
        sample_count: u32,
        plane: *const FfiLessonPlane,
        options: *const FfiProjectionOptions,
        output_points: *mut FfiVec2,
        output_capacity: u32,
        output_report: *mut FfiProjectionReport,
    ) -> u32;
}

#[derive(Clone, Copy, Debug)]
pub(super) struct TrackedTipSample {
    pub predicted_display_time_ns: i64,
    pub position_m: Vec3,
    pub pose_valid: bool,
}

#[derive(Clone, Copy, Debug)]
pub(super) struct LessonPlane {
    pub origin_m: Vec3,
    pub right: Vec3,
    pub up: Vec3,
    pub width_m: f32,
    pub height_m: f32,
}

#[derive(Clone, Copy, Debug)]
pub(super) struct ProjectionOptions {
    pub max_points: u32,
    pub minimum_tip_distance_m: f32,
    pub resampling_period_ns: i64,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(super) enum ProjectionStatus {
    Ok,
    InvalidPlane,
    InvalidOptions,
    TrackingLost,
    InvalidTiming,
}

impl ProjectionStatus {
    pub(super) fn label(self) -> &'static str {
        match self {
            Self::Ok => "ok",
            Self::InvalidPlane => "invalid_plane",
            Self::InvalidOptions => "invalid_options",
            Self::TrackingLost => "tracking_lost",
            Self::InvalidTiming => "invalid_timing",
        }
    }
}

#[derive(Clone, Copy, Debug, Default)]
pub(super) struct ProjectionReport {
    pub output_point_count: u32,
    pub input_sample_count: u32,
    pub invalid_sample_count: u32,
    pub non_monotonic_sample_count: u32,
    pub jitter_rejected_count: u32,
    pub resampled_away_count: u32,
    pub interpolated_point_count: u32,
}

#[derive(Debug)]
pub(super) struct ProjectedTrajectory {
    pub status: ProjectionStatus,
    pub points: Vec<Vec2>,
    pub report: ProjectionReport,
}

#[derive(Debug)]
pub(super) struct BridgeError(String);

impl fmt::Display for BridgeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

impl Error for BridgeError {}

pub(super) fn abi_version() -> u32 {
    // SAFETY: the function has no parameters, returns a scalar, and the linked
    // implementation is compiled from the matching repository header.
    unsafe { hpvr_wand_abi_version() }
}

pub(super) fn project_trajectory(
    samples: &[TrackedTipSample],
    plane: LessonPlane,
    options: ProjectionOptions,
) -> Result<ProjectedTrajectory, BridgeError> {
    if abi_version() != ABI_VERSION {
        return Err(BridgeError(format!(
            "hpvr_wand ABI mismatch: Rust expects {ABI_VERSION}, native reports {}",
            abi_version()
        )));
    }
    let sample_count = u32::try_from(samples.len())
        .map_err(|_| BridgeError("wand sample count exceeds the C ABI range".to_owned()))?;
    let ffi_samples = samples
        .iter()
        .map(|sample| FfiTrackedTipSample {
            predicted_display_time_ns: sample.predicted_display_time_ns,
            position_x_m: sample.position_m.x,
            position_y_m: sample.position_m.y,
            position_z_m: sample.position_m.z,
            pose_valid: u8::from(sample.pose_valid),
            reserved: [0; 3],
        })
        .collect::<Vec<_>>();
    let ffi_plane = FfiLessonPlane {
        origin_x_m: plane.origin_m.x,
        origin_y_m: plane.origin_m.y,
        origin_z_m: plane.origin_m.z,
        right_x: plane.right.x,
        right_y: plane.right.y,
        right_z: plane.right.z,
        up_x: plane.up.x,
        up_y: plane.up.y,
        up_z: plane.up.z,
        width_m: plane.width_m,
        height_m: plane.height_m,
    };
    let ffi_options = FfiProjectionOptions {
        max_points: options.max_points,
        minimum_tip_distance_m: options.minimum_tip_distance_m,
        resampling_period_ns: options.resampling_period_ns,
    };
    let mut output = vec![FfiVec2::default(); AUTHORED_POINT_CAPACITY];
    let mut report = FfiProjectionReport::default();
    // SAFETY: all pointers refer to live, correctly aligned repr(C) storage for
    // the duration of the call. The output buffer exposes exactly its capacity.
    let returned_status = unsafe {
        hpvr_wand_project_trajectory(
            ffi_samples.as_ptr(),
            sample_count,
            &ffi_plane,
            &ffi_options,
            output.as_mut_ptr(),
            output.len() as u32,
            &mut report,
        )
    };
    if returned_status != report.status {
        return Err(BridgeError(format!(
            "hpvr_wand returned status {returned_status} but report contains {}",
            report.status
        )));
    }
    let status = match returned_status {
        STATUS_OK => ProjectionStatus::Ok,
        STATUS_INVALID_PLANE => ProjectionStatus::InvalidPlane,
        STATUS_INVALID_OPTIONS => ProjectionStatus::InvalidOptions,
        STATUS_TRACKING_LOST => ProjectionStatus::TrackingLost,
        STATUS_INVALID_TIMING => ProjectionStatus::InvalidTiming,
        STATUS_INVALID_ARGUMENT => {
            return Err(BridgeError(
                "hpvr_wand rejected the Rust ABI arguments".to_owned(),
            ));
        }
        STATUS_BUFFER_TOO_SMALL => {
            return Err(BridgeError(format!(
                "hpvr_wand requires {} output points, fixed capacity is {AUTHORED_POINT_CAPACITY}",
                report.output_point_count
            )));
        }
        STATUS_ALLOCATION_FAILURE => {
            return Err(BridgeError("hpvr_wand native allocation failed".to_owned()));
        }
        STATUS_INTERNAL_ERROR => {
            return Err(BridgeError(
                "hpvr_wand native bridge caught an internal error".to_owned(),
            ));
        }
        unknown => {
            return Err(BridgeError(format!(
                "hpvr_wand returned unknown status {unknown}"
            )));
        }
    };
    let output_count = usize::try_from(report.output_point_count)
        .map_err(|_| BridgeError("native output count does not fit usize".to_owned()))?;
    if output_count > output.len() {
        return Err(BridgeError(format!(
            "native output count {output_count} exceeds buffer {}",
            output.len()
        )));
    }
    let points = if status == ProjectionStatus::Ok {
        output[..output_count]
            .iter()
            .map(|point| Vec2::new(point.x, point.y))
            .collect()
    } else {
        Vec::new()
    };
    Ok(ProjectedTrajectory {
        status,
        points,
        report: ProjectionReport {
            output_point_count: report.output_point_count,
            input_sample_count: report.input_sample_count,
            invalid_sample_count: report.invalid_sample_count,
            non_monotonic_sample_count: report.non_monotonic_sample_count,
            jitter_rejected_count: report.jitter_rejected_count,
            resampled_away_count: report.resampled_away_count,
            interpolated_point_count: report.interpolated_point_count,
        },
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn plane() -> LessonPlane {
        LessonPlane {
            origin_m: Vec3::ZERO,
            right: Vec3::X,
            up: Vec3::Y,
            width_m: 2.0,
            height_m: 2.0,
        }
    }

    fn options() -> ProjectionOptions {
        ProjectionOptions {
            max_points: AUTHORED_POINT_CAPACITY as u32,
            minimum_tip_distance_m: 0.0,
            resampling_period_ns: 50,
        }
    }

    #[test]
    fn ffi_layout_and_version_match_the_c_header() {
        assert_eq!(abi_version(), ABI_VERSION);
        assert_eq!(std::mem::size_of::<FfiVec2>(), 8);
        assert_eq!(std::mem::align_of::<FfiVec2>(), 4);
        assert_eq!(std::mem::offset_of!(FfiVec2, x), 0);
        assert_eq!(std::mem::offset_of!(FfiVec2, y), 4);
        assert_eq!(std::mem::size_of::<FfiTrackedTipSample>(), 24);
        assert_eq!(std::mem::align_of::<FfiTrackedTipSample>(), 8);
        assert_eq!(
            std::mem::offset_of!(FfiTrackedTipSample, predicted_display_time_ns),
            0
        );
        assert_eq!(std::mem::offset_of!(FfiTrackedTipSample, position_x_m), 8);
        assert_eq!(std::mem::offset_of!(FfiTrackedTipSample, position_y_m), 12);
        assert_eq!(std::mem::offset_of!(FfiTrackedTipSample, position_z_m), 16);
        assert_eq!(std::mem::offset_of!(FfiTrackedTipSample, pose_valid), 20);
        assert_eq!(std::mem::size_of::<FfiLessonPlane>(), 44);
        assert_eq!(std::mem::align_of::<FfiLessonPlane>(), 4);
        assert_eq!(std::mem::offset_of!(FfiLessonPlane, origin_x_m), 0);
        assert_eq!(std::mem::offset_of!(FfiLessonPlane, right_x), 12);
        assert_eq!(std::mem::offset_of!(FfiLessonPlane, up_x), 24);
        assert_eq!(std::mem::offset_of!(FfiLessonPlane, width_m), 36);
        assert_eq!(std::mem::offset_of!(FfiLessonPlane, height_m), 40);
        assert_eq!(std::mem::size_of::<FfiProjectionOptions>(), 16);
        assert_eq!(std::mem::align_of::<FfiProjectionOptions>(), 8);
        assert_eq!(std::mem::offset_of!(FfiProjectionOptions, max_points), 0);
        assert_eq!(
            std::mem::offset_of!(FfiProjectionOptions, minimum_tip_distance_m),
            4
        );
        assert_eq!(
            std::mem::offset_of!(FfiProjectionOptions, resampling_period_ns),
            8
        );
        assert_eq!(std::mem::size_of::<FfiProjectionReport>(), 32);
        assert_eq!(std::mem::align_of::<FfiProjectionReport>(), 4);
        assert_eq!(std::mem::offset_of!(FfiProjectionReport, status), 0);
        assert_eq!(
            std::mem::offset_of!(FfiProjectionReport, output_point_count),
            4
        );
        assert_eq!(
            std::mem::offset_of!(FfiProjectionReport, interpolated_point_count),
            28
        );
    }

    #[test]
    fn safe_bridge_projects_and_preserves_endpoints() {
        let samples = [
            TrackedTipSample {
                predicted_display_time_ns: 0,
                position_m: Vec3::ZERO,
                pose_valid: true,
            },
            TrackedTipSample {
                predicted_display_time_ns: 50,
                position_m: Vec3::new(0.5, 0.0, 0.0),
                pose_valid: true,
            },
            TrackedTipSample {
                predicted_display_time_ns: 100,
                position_m: Vec3::X,
                pose_valid: true,
            },
        ];
        let result = project_trajectory(&samples, plane(), options()).unwrap();
        assert_eq!(result.status, ProjectionStatus::Ok);
        assert_eq!(result.report.input_sample_count, 3);
        assert_eq!(result.points.len(), 3);
        assert_eq!(result.points[0], Vec2::new(0.5, 0.5));
        assert_eq!(result.points[2], Vec2::new(1.0, 0.5));
    }

    #[test]
    fn safe_bridge_preserves_native_timing_rejection() {
        let samples = [
            TrackedTipSample {
                predicted_display_time_ns: 100,
                position_m: Vec3::ZERO,
                pose_valid: true,
            },
            TrackedTipSample {
                predicted_display_time_ns: 100,
                position_m: Vec3::X,
                pose_valid: true,
            },
        ];
        let result = project_trajectory(&samples, plane(), options()).unwrap();
        assert_eq!(result.status, ProjectionStatus::InvalidTiming);
        assert_eq!(result.report.non_monotonic_sample_count, 1);
        assert!(result.points.is_empty());
    }
}
