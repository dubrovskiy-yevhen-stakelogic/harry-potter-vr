use std::{error::Error, path::Path};

#[cfg(feature = "gesture-projection")]
use std::ffi::{CStr, CString};

use glam::Vec3;

#[cfg(feature = "gesture-projection")]
const HARRY_WAND_LENGTH_METERS: f32 = 0.34;
#[cfg(feature = "gesture-projection")]
const WAND_MESH_REFERENCE: i32 = 1092;
#[cfg(feature = "gesture-projection")]
const STATUS_OK: u32 = 0;
#[cfg(feature = "gesture-projection")]
const STATUS_BUFFER_TOO_SMALL: u32 = 101;
#[cfg(feature = "gesture-projection")]
const SKELETAL_ABI_VERSION: u32 = 2;
#[cfg(feature = "gesture-projection")]
const ERROR_CAPACITY: usize = 256;

#[cfg(feature = "gesture-projection")]
#[repr(C)]
#[derive(Clone, Copy, Default)]
struct FfiSkeletalVertex {
    position_m: [f32; 3],
    texture_uv: [f32; 2],
    texture_layer: u32,
    polygon_flags: u32,
    face_index: u32,
    point_index: u32,
}

#[cfg(feature = "gesture-projection")]
#[repr(C)]
#[derive(Clone, Copy)]
struct FfiSkeletalReport {
    abi_version: u32,
    status: u32,
    required_vertex_count: u32,
    written_vertex_count: u32,
    required_texture_bytes: u32,
    written_texture_bytes: u32,
    point_count: u32,
    wedge_count: u32,
    face_count: u32,
    material_count: u32,
    texture_layer_width: u32,
    texture_layer_height: u32,
    texture_layer_count: u32,
    decoded_texture_count: u32,
    bounds_min_m: [f32; 3],
    bounds_max_m: [f32; 3],
    error: [u8; ERROR_CAPACITY],
}

#[cfg(feature = "gesture-projection")]
impl Default for FfiSkeletalReport {
    fn default() -> Self {
        Self {
            abi_version: 0,
            status: 0,
            required_vertex_count: 0,
            written_vertex_count: 0,
            required_texture_bytes: 0,
            written_texture_bytes: 0,
            point_count: 0,
            wedge_count: 0,
            face_count: 0,
            material_count: 0,
            texture_layer_width: 0,
            texture_layer_height: 0,
            texture_layer_count: 0,
            decoded_texture_count: 0,
            bounds_min_m: [0.0; 3],
            bounds_max_m: [0.0; 3],
            error: [0; ERROR_CAPACITY],
        }
    }
}

#[cfg(feature = "gesture-projection")]
unsafe extern "C" {
    fn hpvr_hp1_load_skeletal_geometry_utf8(
        mesh_package_utf8: *const std::ffi::c_char,
        mesh_reference: i32,
        meters_per_unreal_unit: f32,
        output_vertices: *mut FfiSkeletalVertex,
        vertex_capacity: u32,
        output_report: *mut FfiSkeletalReport,
    ) -> u32;
}

#[derive(Clone, Copy, Debug)]
pub(super) struct HarryWandVertex {
    pub local_position: Vec3,
    pub color: Vec3,
}

pub(super) struct HarryWandModel {
    pub vertices: Vec<HarryWandVertex>,
}

impl HarryWandModel {
    #[cfg(not(feature = "gesture-projection"))]
    pub(super) fn load(_data_root: &Path) -> Result<Self, Box<dyn Error>> {
        Err("HP1 WandMesh loading requires feature 'gesture-projection'".into())
    }

    #[cfg(feature = "gesture-projection")]
    pub(super) fn load(data_root: &Path) -> Result<Self, Box<dyn Error>> {
        let package_path = data_root.join("system").join("HPBase.u");
        let package = CString::new(
            package_path
                .to_str()
                .ok_or("HPBase.u path is not valid Unicode")?,
        )?;
        let mut query = FfiSkeletalReport::default();
        // SAFETY: null/zero output requests exact bounded geometry capacity.
        let query_status = unsafe {
            hpvr_hp1_load_skeletal_geometry_utf8(
                package.as_ptr(),
                WAND_MESH_REFERENCE,
                1.0,
                std::ptr::null_mut(),
                0,
                &mut query,
            )
        };
        validate_report(query_status, &query, true)?;
        let mut ffi_vertices =
            vec![FfiSkeletalVertex::default(); usize::try_from(query.required_vertex_count)?];
        let mut loaded = FfiSkeletalReport::default();
        // SAFETY: output allocation exactly matches the queried capacity.
        let load_status = unsafe {
            hpvr_hp1_load_skeletal_geometry_utf8(
                package.as_ptr(),
                WAND_MESH_REFERENCE,
                1.0,
                ffi_vertices.as_mut_ptr(),
                query.required_vertex_count,
                &mut loaded,
            )
        };
        validate_report(load_status, &loaded, false)?;
        if query.required_vertex_count != loaded.required_vertex_count
            || query.point_count != loaded.point_count
            || query.face_count != loaded.face_count
            || query.bounds_min_m != loaded.bounds_min_m
            || query.bounds_max_m != loaded.bounds_max_m
        {
            return Err("HPBase.WandMesh changed between query and load".into());
        }
        if loaded.required_vertex_count == 0
            || loaded.required_vertex_count % 3 != 0
            || loaded.face_count * 3 != loaded.required_vertex_count
        {
            return Err("HPBase.WandMesh returned invalid triangle topology".into());
        }

        let bounds_min = Vec3::from_array(loaded.bounds_min_m);
        let bounds_max = Vec3::from_array(loaded.bounds_max_m);
        let extent = bounds_max - bounds_min;
        if !bounds_min.is_finite() || !bounds_max.is_finite() || extent.cmple(Vec3::ZERO).any() {
            return Err("HPBase.WandMesh returned invalid bounds".into());
        }
        let long_axis = if extent.x >= extent.y && extent.x >= extent.z {
            0
        } else if extent.y >= extent.z {
            1
        } else {
            2
        };
        let cross_axes = match long_axis {
            0 => [1, 2],
            1 => [0, 2],
            _ => [0, 1],
        };
        let long_min = bounds_min[long_axis];
        let long_max = bounds_max[long_axis];
        let long_extent = extent[long_axis];
        let cross_center = (bounds_min + bounds_max) * 0.5;
        let end_band = long_extent * 0.18;
        let radial_at = |maximum_end: bool| {
            let mut sum = 0.0_f32;
            let mut count = 0_u32;
            for vertex in &ffi_vertices {
                let point = Vec3::from_array(vertex.position_m);
                let at_end = if maximum_end {
                    point[long_axis] >= long_max - end_band
                } else {
                    point[long_axis] <= long_min + end_band
                };
                if at_end {
                    let u = point[cross_axes[0]] - cross_center[cross_axes[0]];
                    let v = point[cross_axes[1]] - cross_center[cross_axes[1]];
                    sum += u * u + v * v;
                    count += 1;
                }
            }
            sum / count.max(1) as f32
        };
        let handle_is_max = radial_at(true) > radial_at(false);
        let scale = HARRY_WAND_LENGTH_METERS / long_extent;
        let vertices = ffi_vertices
            .into_iter()
            .map(|vertex| {
                let point = Vec3::from_array(vertex.position_m);
                let t = if handle_is_max {
                    (long_max - point[long_axis]) / long_extent
                } else {
                    (point[long_axis] - long_min) / long_extent
                }
                .clamp(0.0, 1.0);
                let local_position = Vec3::new(
                    (point[cross_axes[0]] - cross_center[cross_axes[0]]) * scale,
                    (point[cross_axes[1]] - cross_center[cross_axes[1]]) * scale,
                    -t * HARRY_WAND_LENGTH_METERS,
                );
                HarryWandVertex {
                    local_position,
                    color: Vec3::new(0.22 + 0.22 * t, 0.055 + 0.075 * t, 0.015),
                }
            })
            .collect::<Vec<_>>();
        if vertices
            .iter()
            .any(|vertex| !vertex.local_position.is_finite() || !vertex.color.is_finite())
        {
            return Err("normalized HPBase.WandMesh contains non-finite vertices".into());
        }
        println!(
            "[hp1.wand.model] class=HPBase.baseWand mesh=HPBase.WandMesh mesh_ref={} vertices={} triangles={} source_bounds_min={:?} source_bounds_max={:?} longitudinal_axis={} handle_end={} rendered_length_m={:.3} geometry=EXTERNAL_READ_ONLY material=PROCEDURAL_BROWN",
            WAND_MESH_REFERENCE,
            vertices.len(),
            vertices.len() / 3,
            bounds_min,
            bounds_max,
            long_axis,
            if handle_is_max { "max" } else { "min" },
            HARRY_WAND_LENGTH_METERS,
        );
        Ok(Self { vertices })
    }
}

#[cfg(feature = "gesture-projection")]
fn validate_report(
    returned_status: u32,
    report: &FfiSkeletalReport,
    query: bool,
) -> Result<(), Box<dyn Error>> {
    if report.abi_version != SKELETAL_ABI_VERSION || report.status != returned_status {
        return Err("HP1 skeletal geometry ABI/report mismatch".into());
    }
    let expected = if query {
        STATUS_BUFFER_TOO_SMALL
    } else {
        STATUS_OK
    };
    if returned_status != expected {
        let error = unsafe { CStr::from_ptr(report.error.as_ptr().cast()) }
            .to_string_lossy()
            .into_owned();
        return Err(
            format!("HPBase.WandMesh load failed: status={returned_status} {error}").into(),
        );
    }
    if query && report.written_vertex_count != 0 {
        return Err("HPBase.WandMesh query unexpectedly wrote vertices".into());
    }
    if !query && report.written_vertex_count != report.required_vertex_count {
        return Err("HPBase.WandMesh load returned a short vertex stream".into());
    }
    Ok(())
}

#[cfg(all(test, feature = "gesture-projection"))]
mod tests {
    use super::*;

    #[test]
    fn skeletal_geometry_ffi_layout_matches_character_mesh_abi() {
        assert_eq!(std::mem::size_of::<FfiSkeletalVertex>(), 36);
        assert_eq!(std::mem::size_of::<FfiSkeletalReport>(), 336);
        assert_eq!(std::mem::offset_of!(FfiSkeletalReport, bounds_min_m), 56);
        assert_eq!(std::mem::offset_of!(FfiSkeletalReport, error), 80);
    }
}
