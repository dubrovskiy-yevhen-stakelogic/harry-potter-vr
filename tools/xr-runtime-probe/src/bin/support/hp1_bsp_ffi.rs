use std::error::Error;
use std::fmt;
use std::path::Path;

use glam::Vec3;

#[cfg(feature = "gesture-projection")]
use std::ffi::{CStr, CString};

#[cfg(feature = "gesture-projection")]
pub(super) const ABI_VERSION: u32 = 1;
pub(super) const MAX_SLICE_TRIANGLES: u32 = 100_000;

#[cfg(feature = "gesture-projection")]
const STATUS_OK: u32 = 0;
#[cfg(feature = "gesture-projection")]
const STATUS_INVALID_ARGUMENT: u32 = 100;
#[cfg(feature = "gesture-projection")]
const STATUS_BUFFER_TOO_SMALL: u32 = 101;
#[cfg(feature = "gesture-projection")]
const STATUS_ALLOCATION_FAILURE: u32 = 102;
#[cfg(feature = "gesture-projection")]
const STATUS_INTERNAL_ERROR: u32 = 103;
#[cfg(any(feature = "gesture-projection", test))]
const ERROR_CAPACITY: usize = 256;

#[cfg(any(feature = "gesture-projection", test))]
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
struct FfiBspSliceVertex {
    position_m: [f32; 3],
    normal: [f32; 3],
    polygon_flags: u32,
    node_index: u32,
    surface_index: u32,
}

#[cfg(any(feature = "gesture-projection", test))]
#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
struct FfiTexturedBspVertex {
    position_m: [f32; 3],
    normal: [f32; 3],
    texture_uv: [f32; 2],
    texture_layer: u32,
    polygon_flags: u32,
    node_index: u32,
    surface_index: u32,
}

#[cfg(any(feature = "gesture-projection", test))]
#[repr(C)]
#[derive(Clone, Copy, Debug)]
struct FfiTexturedBspReport {
    abi_version: u32,
    status: u32,
    required_vertex_count: u32,
    written_vertex_count: u32,
    required_texture_bytes: u32,
    written_texture_bytes: u32,
    available_triangle_count: u32,
    selected_triangle_count: u32,
    omitted_triangle_count: u32,
    texture_layer_width: u32,
    texture_layer_height: u32,
    texture_layer_count: u32,
    decoded_texture_count: u32,
    fallback_material_count: u32,
    fallback_triangle_count: u32,
    bounds_min_m: [f32; 3],
    bounds_max_m: [f32; 3],
    error: [u8; ERROR_CAPACITY],
}

#[cfg(any(feature = "gesture-projection", test))]
impl Default for FfiTexturedBspReport {
    fn default() -> Self {
        Self {
            abi_version: 0,
            status: 0,
            required_vertex_count: 0,
            written_vertex_count: 0,
            required_texture_bytes: 0,
            written_texture_bytes: 0,
            available_triangle_count: 0,
            selected_triangle_count: 0,
            omitted_triangle_count: 0,
            texture_layer_width: 0,
            texture_layer_height: 0,
            texture_layer_count: 0,
            decoded_texture_count: 0,
            fallback_material_count: 0,
            fallback_triangle_count: 0,
            bounds_min_m: [0.0; 3],
            bounds_max_m: [0.0; 3],
            error: [0; ERROR_CAPACITY],
        }
    }
}

#[cfg(any(feature = "gesture-projection", test))]
#[repr(C)]
#[derive(Clone, Copy, Debug)]
struct FfiBspSliceReport {
    abi_version: u32,
    status: u32,
    required_vertex_count: u32,
    written_vertex_count: u32,
    available_triangle_count: u32,
    selected_triangle_count: u32,
    omitted_triangle_count: u32,
    degenerate_triangle_count: u32,
    winding_reversal_count: u32,
    zero_flag_triangle_count: u32,
    nonzero_flag_triangle_count: u32,
    imported_texture_triangle_count: u32,
    local_texture_triangle_count: u32,
    no_texture_triangle_count: u32,
    imported_actor_triangle_count: u32,
    local_actor_triangle_count: u32,
    no_actor_triangle_count: u32,
    bounds_min_m: [f32; 3],
    bounds_max_m: [f32; 3],
    error: [u8; ERROR_CAPACITY],
}

#[cfg(any(feature = "gesture-projection", test))]
#[repr(C)]
#[derive(Clone, Copy, Debug)]
struct FfiPlayerStartReport {
    abi_version: u32,
    status: u32,
    available_player_start_count: u32,
    selected_ordinal: u32,
    actor_slot_index: u32,
    actor_reference: i32,
    position_m: [f32; 3],
    rotation_units: [i32; 3],
    location_serialized: u32,
    rotation_serialized: u32,
    object_name: [u8; 128],
    error: [u8; ERROR_CAPACITY],
}

#[cfg(any(feature = "gesture-projection", test))]
#[repr(C)]
#[derive(Clone, Copy, Debug)]
struct FfiActorVisualReport {
    abi_version: u32,
    status: u32,
    actor_slot_index: u32,
    actor_reference: i32,
    position_m: [f32; 3],
    rotation_units: [i32; 3],
    draw_scale: f32,
    location_serialized: u32,
    rotation_serialized: u32,
    draw_scale_serialized: u32,
    object_name: [u8; 128],
    error: [u8; ERROR_CAPACITY],
}

#[cfg(any(feature = "gesture-projection", test))]
impl Default for FfiActorVisualReport {
    fn default() -> Self {
        Self {
            abi_version: 0,
            status: 0,
            actor_slot_index: 0,
            actor_reference: 0,
            position_m: [0.0; 3],
            rotation_units: [0; 3],
            draw_scale: 0.0,
            location_serialized: 0,
            rotation_serialized: 0,
            draw_scale_serialized: 0,
            object_name: [0; 128],
            error: [0; ERROR_CAPACITY],
        }
    }
}

#[cfg(any(feature = "gesture-projection", test))]
impl Default for FfiPlayerStartReport {
    fn default() -> Self {
        Self {
            abi_version: 0,
            status: 0,
            available_player_start_count: 0,
            selected_ordinal: 0,
            actor_slot_index: 0,
            actor_reference: 0,
            position_m: [0.0; 3],
            rotation_units: [0; 3],
            location_serialized: 0,
            rotation_serialized: 0,
            object_name: [0; 128],
            error: [0; ERROR_CAPACITY],
        }
    }
}

#[cfg(any(feature = "gesture-projection", test))]
impl Default for FfiBspSliceReport {
    fn default() -> Self {
        Self {
            abi_version: 0,
            status: 0,
            required_vertex_count: 0,
            written_vertex_count: 0,
            available_triangle_count: 0,
            selected_triangle_count: 0,
            omitted_triangle_count: 0,
            degenerate_triangle_count: 0,
            winding_reversal_count: 0,
            zero_flag_triangle_count: 0,
            nonzero_flag_triangle_count: 0,
            imported_texture_triangle_count: 0,
            local_texture_triangle_count: 0,
            no_texture_triangle_count: 0,
            imported_actor_triangle_count: 0,
            local_actor_triangle_count: 0,
            no_actor_triangle_count: 0,
            bounds_min_m: [0.0; 3],
            bounds_max_m: [0.0; 3],
            error: [0; ERROR_CAPACITY],
        }
    }
}

#[cfg(feature = "gesture-projection")]
unsafe extern "C" {
    fn hpvr_hp1_load_bsp_slice_utf8(
        map_package_utf8: *const std::ffi::c_char,
        meters_per_unreal_unit: f32,
        maximum_triangle_count: u32,
        output_vertices: *mut FfiBspSliceVertex,
        vertex_capacity: u32,
        output_report: *mut FfiBspSliceReport,
    ) -> u32;
    fn hpvr_hp1_load_textured_bsp_utf8(
        data_root_utf8: *const std::ffi::c_char,
        map_package_utf8: *const std::ffi::c_char,
        meters_per_unreal_unit: f32,
        maximum_triangle_count: u32,
        output_vertices: *mut FfiTexturedBspVertex,
        vertex_capacity: u32,
        output_texture_rgba8: *mut u8,
        texture_byte_capacity: u32,
        output_report: *mut FfiTexturedBspReport,
    ) -> u32;
    fn hpvr_hp1_load_player_start_utf8(
        map_package_utf8: *const std::ffi::c_char,
        meters_per_unreal_unit: f32,
        player_start_ordinal: u32,
        output_report: *mut FfiPlayerStartReport,
    ) -> u32;
    fn hpvr_hp1_load_actor_visual_utf8(
        map_package_utf8: *const std::ffi::c_char,
        meters_per_unreal_unit: f32,
        actor_reference: i32,
        output_report: *mut FfiActorVisualReport,
    ) -> u32;
}

#[derive(Debug)]
pub(super) struct BridgeError(String);

impl fmt::Display for BridgeError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(&self.0)
    }
}

impl Error for BridgeError {}

#[derive(Clone, Copy, Debug)]
#[cfg_attr(not(feature = "gesture-projection"), allow(dead_code))]
pub(super) struct MapSliceVertex {
    pub position_m: Vec3,
    pub normal: Vec3,
    pub polygon_flags: u32,
    pub node_index: u32,
    pub surface_index: u32,
    pub texture_uv: [f32; 2],
    pub texture_layer: u32,
}

#[derive(Clone, Copy, Debug)]
pub(super) struct MapSliceReport {
    pub available_triangle_count: u32,
    pub selected_triangle_count: u32,
    pub omitted_triangle_count: u32,
    pub degenerate_triangle_count: u32,
    pub winding_reversal_count: u32,
    pub zero_flag_triangle_count: u32,
    pub nonzero_flag_triangle_count: u32,
    pub imported_texture_triangle_count: u32,
    pub local_texture_triangle_count: u32,
    pub no_texture_triangle_count: u32,
    pub imported_actor_triangle_count: u32,
    pub local_actor_triangle_count: u32,
    pub no_actor_triangle_count: u32,
    pub bounds_min_m: Vec3,
    pub bounds_max_m: Vec3,
}

#[derive(Debug)]
pub(super) struct MapSlice {
    pub vertices: Vec<MapSliceVertex>,
    pub report: MapSliceReport,
    pub texture_layer_width: u32,
    pub texture_layer_height: u32,
    pub texture_layer_count: u32,
    pub texture_rgba8: Vec<u8>,
    pub decoded_texture_count: u32,
    pub fallback_material_count: u32,
    pub fallback_triangle_count: u32,
}

#[derive(Clone, Debug)]
pub(super) struct PlayerStart {
    pub available_count: u32,
    pub ordinal: u32,
    pub actor_slot_index: u32,
    pub actor_reference: i32,
    pub position_m: Vec3,
    pub rotation_units: [i32; 3],
    pub location_serialized: bool,
    pub rotation_serialized: bool,
    pub object_name: String,
}

#[derive(Clone, Debug)]
pub(super) struct ActorVisual {
    pub actor_slot_index: u32,
    pub actor_reference: i32,
    pub position_m: Vec3,
    pub rotation_units: [i32; 3],
    pub draw_scale: f32,
    pub location_serialized: bool,
    pub rotation_serialized: bool,
    pub draw_scale_serialized: bool,
    pub object_name: String,
}

#[cfg(feature = "gesture-projection")]
impl MapSlice {
    pub(super) fn load(
        map_package: &Path,
        meters_per_unreal_unit: f32,
        maximum_triangle_count: u32,
    ) -> Result<Self, BridgeError> {
        if !meters_per_unreal_unit.is_finite() || meters_per_unreal_unit <= 0.0 {
            return Err(BridgeError(
                "HP1 map scale must be finite and greater than zero".to_owned(),
            ));
        }
        if maximum_triangle_count == 0 || maximum_triangle_count > MAX_SLICE_TRIANGLES {
            return Err(BridgeError(format!(
                "HP1 map triangle limit must be in 1..={MAX_SLICE_TRIANGLES}"
            )));
        }
        let package = path_c_string(map_package)?;
        let mut query = FfiBspSliceReport::default();
        // SAFETY: the output vertex pointer is null with a zero capacity; the
        // live repr(C) report and null-terminated path remain valid for the call.
        let query_status = unsafe {
            hpvr_hp1_load_bsp_slice_utf8(
                package.as_ptr(),
                meters_per_unreal_unit,
                maximum_triangle_count,
                std::ptr::null_mut(),
                0,
                &mut query,
            )
        };
        require_matching_status(query_status, &query)?;
        if query_status != STATUS_OK && query_status != STATUS_BUFFER_TOO_SMALL {
            return Err(native_error("query", &query));
        }
        validate_report(&query)?;
        if query.required_vertex_count != 0 && query_status != STATUS_BUFFER_TOO_SMALL {
            return Err(BridgeError(
                "HP1 BSP size query unexpectedly accepted a null output buffer".to_owned(),
            ));
        }

        let vertex_count = usize::try_from(query.required_vertex_count)
            .map_err(|_| BridgeError("HP1 BSP vertex count does not fit usize".to_owned()))?;
        let mut ffi_vertices = vec![FfiBspSliceVertex::default(); vertex_count];
        let mut loaded = FfiBspSliceReport::default();
        let output_pointer = if ffi_vertices.is_empty() {
            std::ptr::null_mut()
        } else {
            ffi_vertices.as_mut_ptr()
        };
        // SAFETY: the output pointer refers to exactly vertex_count live,
        // aligned repr(C) elements and the capacity is passed without truncation.
        let load_status = unsafe {
            hpvr_hp1_load_bsp_slice_utf8(
                package.as_ptr(),
                meters_per_unreal_unit,
                maximum_triangle_count,
                output_pointer,
                query.required_vertex_count,
                &mut loaded,
            )
        };
        require_matching_status(load_status, &loaded)?;
        if load_status != STATUS_OK {
            return Err(native_error("load", &loaded));
        }
        validate_report(&loaded)?;
        if loaded.written_vertex_count != loaded.required_vertex_count
            || loaded.required_vertex_count != query.required_vertex_count
            || !same_slice_metadata(&query, &loaded)
        {
            return Err(BridgeError(
                "HP1 BSP package changed or returned inconsistent metadata between query and load"
                    .to_owned(),
            ));
        }

        let diagnostic_vertices = ffi_vertices
            .into_iter()
            .map(|vertex| MapSliceVertex {
                position_m: Vec3::from_array(vertex.position_m),
                normal: Vec3::from_array(vertex.normal),
                polygon_flags: vertex.polygon_flags,
                node_index: vertex.node_index,
                surface_index: vertex.surface_index,
                texture_uv: [0.0; 2],
                texture_layer: 0,
            })
            .collect::<Vec<_>>();
        if diagnostic_vertices.iter().any(|vertex| {
            !vertex.position_m.is_finite()
                || !vertex.normal.is_finite()
                || (vertex.normal.length_squared() - 1.0).abs() > 1.0e-3
        }) {
            return Err(BridgeError(
                "native HP1 BSP slice contains a non-finite or non-unit vertex".to_owned(),
            ));
        }

        let data_root_path = map_package.parent().and_then(Path::parent).ok_or_else(|| {
            BridgeError(format!(
                "HP1 map path has no data-root grandparent: {}",
                map_package.display()
            ))
        })?;
        let data_root = path_c_string(data_root_path)?;
        let mut texture_query = FfiTexturedBspReport::default();
        // SAFETY: both paths and the report remain live; null buffers have zero
        // capacities and are used only for the exact size query.
        let texture_query_status = unsafe {
            hpvr_hp1_load_textured_bsp_utf8(
                data_root.as_ptr(),
                package.as_ptr(),
                meters_per_unreal_unit,
                maximum_triangle_count,
                std::ptr::null_mut(),
                0,
                std::ptr::null_mut(),
                0,
                &mut texture_query,
            )
        };
        validate_textured_report(texture_query_status, &texture_query, "query")?;
        if texture_query_status != STATUS_BUFFER_TOO_SMALL {
            return Err(BridgeError(
                "HP1 textured BSP size query unexpectedly accepted null buffers".to_owned(),
            ));
        }
        let textured_vertex_count = usize::try_from(texture_query.required_vertex_count)
            .map_err(|_| BridgeError("HP1 textured vertex count does not fit usize".to_owned()))?;
        let texture_byte_count = usize::try_from(texture_query.required_texture_bytes)
            .map_err(|_| BridgeError("HP1 texture byte count does not fit usize".to_owned()))?;
        let mut textured_vertices = vec![FfiTexturedBspVertex::default(); textured_vertex_count];
        let mut texture_rgba8 = vec![0_u8; texture_byte_count];
        let mut texture_loaded = FfiTexturedBspReport::default();
        // SAFETY: both caller-owned buffers have exactly the capacities passed,
        // and all C++ exceptions are contained by the native boundary.
        let texture_load_status = unsafe {
            hpvr_hp1_load_textured_bsp_utf8(
                data_root.as_ptr(),
                package.as_ptr(),
                meters_per_unreal_unit,
                maximum_triangle_count,
                textured_vertices.as_mut_ptr(),
                texture_query.required_vertex_count,
                texture_rgba8.as_mut_ptr(),
                texture_query.required_texture_bytes,
                &mut texture_loaded,
            )
        };
        validate_textured_report(texture_load_status, &texture_loaded, "load")?;
        if texture_load_status != STATUS_OK
            || texture_loaded.written_vertex_count != texture_loaded.required_vertex_count
            || texture_loaded.written_texture_bytes != texture_loaded.required_texture_bytes
            || !same_textured_metadata(&texture_query, &texture_loaded)
            || texture_loaded.required_vertex_count != loaded.required_vertex_count
            || texture_loaded.available_triangle_count != loaded.available_triangle_count
            || texture_loaded.selected_triangle_count != loaded.selected_triangle_count
            || texture_loaded.omitted_triangle_count != loaded.omitted_triangle_count
            || texture_loaded.bounds_min_m != loaded.bounds_min_m
            || texture_loaded.bounds_max_m != loaded.bounds_max_m
        {
            return Err(BridgeError(
                "HP1 textured BSP disagrees across calls or with diagnostic geometry".to_owned(),
            ));
        }
        let vertices = textured_vertices
            .into_iter()
            .map(|vertex| MapSliceVertex {
                position_m: Vec3::from_array(vertex.position_m),
                normal: Vec3::from_array(vertex.normal),
                polygon_flags: vertex.polygon_flags,
                node_index: vertex.node_index,
                surface_index: vertex.surface_index,
                texture_uv: vertex.texture_uv,
                texture_layer: vertex.texture_layer,
            })
            .collect::<Vec<_>>();
        if vertices.len() != diagnostic_vertices.len()
            || vertices
                .iter()
                .zip(&diagnostic_vertices)
                .any(|(textured, diagnostic)| {
                    textured.position_m != diagnostic.position_m
                        || textured.normal != diagnostic.normal
                        || textured.polygon_flags != diagnostic.polygon_flags
                        || textured.node_index != diagnostic.node_index
                        || textured.surface_index != diagnostic.surface_index
                        || !textured.texture_uv[0].is_finite()
                        || !textured.texture_uv[1].is_finite()
                        || textured.texture_layer >= texture_loaded.texture_layer_count
                })
        {
            return Err(BridgeError(
                "HP1 textured vertices do not preserve validated BSP geometry".to_owned(),
            ));
        }

        Ok(Self {
            vertices,
            report: MapSliceReport::from_ffi(&loaded),
            texture_layer_width: texture_loaded.texture_layer_width,
            texture_layer_height: texture_loaded.texture_layer_height,
            texture_layer_count: texture_loaded.texture_layer_count,
            texture_rgba8,
            decoded_texture_count: texture_loaded.decoded_texture_count,
            fallback_material_count: texture_loaded.fallback_material_count,
            fallback_triangle_count: texture_loaded.fallback_triangle_count,
        })
    }
}

#[cfg(feature = "gesture-projection")]
impl PlayerStart {
    pub(super) fn load(
        map_package: &Path,
        meters_per_unreal_unit: f32,
        ordinal: u32,
    ) -> Result<Self, BridgeError> {
        if !meters_per_unreal_unit.is_finite() || meters_per_unreal_unit <= 0.0 {
            return Err(BridgeError(
                "HP1 PlayerStart scale must be finite and greater than zero".to_owned(),
            ));
        }
        let package = path_c_string(map_package)?;
        let mut report = FfiPlayerStartReport::default();
        // SAFETY: the path is null terminated and the live output is an exact
        // repr(C) report. The native function contains all C++ exceptions.
        let returned_status = unsafe {
            hpvr_hp1_load_player_start_utf8(
                package.as_ptr(),
                meters_per_unreal_unit,
                ordinal,
                &mut report,
            )
        };
        if returned_status != report.status {
            return Err(BridgeError(format!(
                "HP1 PlayerStart bridge returned status {returned_status} but report contains {}",
                report.status
            )));
        }
        if report.abi_version != ABI_VERSION {
            return Err(BridgeError(format!(
                "HP1 PlayerStart ABI mismatch: Rust expects {ABI_VERSION}, native reports {}",
                report.abi_version
            )));
        }
        if returned_status != STATUS_OK {
            let detail = ffi_text(&report.error, "PlayerStart error")
                .unwrap_or_else(|error| error.to_string());
            return Err(BridgeError(format!(
                "HP1 PlayerStart load failed (status={returned_status}, available={}): {detail}",
                report.available_player_start_count
            )));
        }
        let position_m = Vec3::from_array(report.position_m);
        if report.available_player_start_count == 0
            || report.selected_ordinal != ordinal
            || ordinal >= report.available_player_start_count
            || report.actor_reference <= 0
            || !position_m.is_finite()
            || report.location_serialized > 1
            || report.rotation_serialized > 1
            || report.location_serialized == 0
        {
            return Err(BridgeError(
                "HP1 PlayerStart report contains inconsistent identity or transform data"
                    .to_owned(),
            ));
        }
        Ok(Self {
            available_count: report.available_player_start_count,
            ordinal: report.selected_ordinal,
            actor_slot_index: report.actor_slot_index,
            actor_reference: report.actor_reference,
            position_m,
            rotation_units: report.rotation_units,
            location_serialized: report.location_serialized != 0,
            rotation_serialized: report.rotation_serialized != 0,
            object_name: ffi_text(&report.object_name, "PlayerStart object name")?,
        })
    }
}

#[cfg(feature = "gesture-projection")]
impl ActorVisual {
    pub(super) fn load(
        map_package: &Path,
        meters_per_unreal_unit: f32,
        actor_reference: i32,
    ) -> Result<Self, BridgeError> {
        if actor_reference <= 0
            || !meters_per_unreal_unit.is_finite()
            || meters_per_unreal_unit <= 0.0
        {
            return Err(BridgeError(
                "HP1 actor placement arguments are invalid".to_owned(),
            ));
        }
        let package = path_c_string(map_package)?;
        let mut report = FfiActorVisualReport::default();
        // SAFETY: the path is null terminated and output is an exact live
        // repr(C) report; native code contains all C++ exceptions.
        let returned_status = unsafe {
            hpvr_hp1_load_actor_visual_utf8(
                package.as_ptr(),
                meters_per_unreal_unit,
                actor_reference,
                &mut report,
            )
        };
        if returned_status != report.status || report.abi_version != ABI_VERSION {
            return Err(BridgeError(
                "HP1 actor placement ABI status/version mismatch".to_owned(),
            ));
        }
        if returned_status != STATUS_OK {
            let detail = ffi_text(&report.error, "actor placement error")
                .unwrap_or_else(|error| error.to_string());
            return Err(BridgeError(format!(
                "HP1 actor placement failed (status={returned_status}): {detail}"
            )));
        }
        const SERIALIZED_MASK_MAX: u32 = 1;
        let position_m = Vec3::from_array(report.position_m);
        if report.actor_reference != actor_reference
            || !position_m.is_finite()
            || !report.draw_scale.is_finite()
            || report.draw_scale <= 0.0
            || report.location_serialized > SERIALIZED_MASK_MAX
            || report.rotation_serialized > SERIALIZED_MASK_MAX
            || report.draw_scale_serialized > SERIALIZED_MASK_MAX
            || report.location_serialized == 0
        {
            return Err(BridgeError(
                "HP1 actor placement report contains inconsistent data".to_owned(),
            ));
        }
        Ok(Self {
            actor_slot_index: report.actor_slot_index,
            actor_reference: report.actor_reference,
            position_m,
            rotation_units: report.rotation_units,
            draw_scale: report.draw_scale,
            location_serialized: report.location_serialized != 0,
            rotation_serialized: report.rotation_serialized != 0,
            draw_scale_serialized: report.draw_scale_serialized != 0,
            object_name: ffi_text(&report.object_name, "actor object name")?,
        })
    }
}

#[cfg(not(feature = "gesture-projection"))]
impl PlayerStart {
    pub(super) fn load(
        _map_package: &Path,
        _meters_per_unreal_unit: f32,
        _ordinal: u32,
    ) -> Result<Self, BridgeError> {
        Err(BridgeError(
            "HP1 PlayerStart loading requires Cargo feature 'gesture-projection'".to_owned(),
        ))
    }
}

#[cfg(not(feature = "gesture-projection"))]
impl ActorVisual {
    pub(super) fn load(
        _map_package: &Path,
        _meters_per_unreal_unit: f32,
        _actor_reference: i32,
    ) -> Result<Self, BridgeError> {
        Err(BridgeError(
            "HP1 actor placement requires Cargo feature 'gesture-projection'".to_owned(),
        ))
    }
}

#[cfg(not(feature = "gesture-projection"))]
impl MapSlice {
    pub(super) fn load(
        _map_package: &Path,
        _meters_per_unreal_unit: f32,
        _maximum_triangle_count: u32,
    ) -> Result<Self, BridgeError> {
        Err(BridgeError(
            "HP1 BSP loading requires Cargo feature 'gesture-projection'".to_owned(),
        ))
    }
}

#[cfg(feature = "gesture-projection")]
impl MapSliceReport {
    fn from_ffi(report: &FfiBspSliceReport) -> Self {
        Self {
            available_triangle_count: report.available_triangle_count,
            selected_triangle_count: report.selected_triangle_count,
            omitted_triangle_count: report.omitted_triangle_count,
            degenerate_triangle_count: report.degenerate_triangle_count,
            winding_reversal_count: report.winding_reversal_count,
            zero_flag_triangle_count: report.zero_flag_triangle_count,
            nonzero_flag_triangle_count: report.nonzero_flag_triangle_count,
            imported_texture_triangle_count: report.imported_texture_triangle_count,
            local_texture_triangle_count: report.local_texture_triangle_count,
            no_texture_triangle_count: report.no_texture_triangle_count,
            imported_actor_triangle_count: report.imported_actor_triangle_count,
            local_actor_triangle_count: report.local_actor_triangle_count,
            no_actor_triangle_count: report.no_actor_triangle_count,
            bounds_min_m: Vec3::from_array(report.bounds_min_m),
            bounds_max_m: Vec3::from_array(report.bounds_max_m),
        }
    }
}

#[cfg(feature = "gesture-projection")]
fn require_matching_status(
    returned_status: u32,
    report: &FfiBspSliceReport,
) -> Result<(), BridgeError> {
    if returned_status == report.status {
        Ok(())
    } else {
        Err(BridgeError(format!(
            "HP1 BSP bridge returned status {returned_status} but report contains {}",
            report.status
        )))
    }
}

#[cfg(feature = "gesture-projection")]
fn validate_report(report: &FfiBspSliceReport) -> Result<(), BridgeError> {
    if report.abi_version != ABI_VERSION {
        return Err(BridgeError(format!(
            "HP1 BSP report ABI mismatch: Rust expects {ABI_VERSION}, native reports {}",
            report.abi_version
        )));
    }
    if report.selected_triangle_count > report.available_triangle_count
        || report.omitted_triangle_count
            != report.available_triangle_count - report.selected_triangle_count
        || report.required_vertex_count != report.selected_triangle_count.saturating_mul(3)
        || report.selected_triangle_count > MAX_SLICE_TRIANGLES
        || report.zero_flag_triangle_count + report.nonzero_flag_triangle_count
            != report.selected_triangle_count
        || report.imported_texture_triangle_count
            + report.local_texture_triangle_count
            + report.no_texture_triangle_count
            != report.selected_triangle_count
        || report.imported_actor_triangle_count
            + report.local_actor_triangle_count
            + report.no_actor_triangle_count
            != report.selected_triangle_count
    {
        return Err(BridgeError(
            "HP1 BSP report contains inconsistent bounded counters".to_owned(),
        ));
    }
    let minimum = Vec3::from_array(report.bounds_min_m);
    let maximum = Vec3::from_array(report.bounds_max_m);
    if report.selected_triangle_count != 0
        && (!minimum.is_finite()
            || !maximum.is_finite()
            || minimum.cmple(maximum).bitmask() != 0b111)
    {
        return Err(BridgeError(
            "HP1 BSP report contains invalid metric bounds".to_owned(),
        ));
    }
    Ok(())
}

#[cfg(feature = "gesture-projection")]
fn same_slice_metadata(left: &FfiBspSliceReport, right: &FfiBspSliceReport) -> bool {
    left.required_vertex_count == right.required_vertex_count
        && left.available_triangle_count == right.available_triangle_count
        && left.selected_triangle_count == right.selected_triangle_count
        && left.omitted_triangle_count == right.omitted_triangle_count
        && left.degenerate_triangle_count == right.degenerate_triangle_count
        && left.winding_reversal_count == right.winding_reversal_count
        && left.zero_flag_triangle_count == right.zero_flag_triangle_count
        && left.nonzero_flag_triangle_count == right.nonzero_flag_triangle_count
        && left.imported_texture_triangle_count == right.imported_texture_triangle_count
        && left.local_texture_triangle_count == right.local_texture_triangle_count
        && left.no_texture_triangle_count == right.no_texture_triangle_count
        && left.imported_actor_triangle_count == right.imported_actor_triangle_count
        && left.local_actor_triangle_count == right.local_actor_triangle_count
        && left.no_actor_triangle_count == right.no_actor_triangle_count
        && left.bounds_min_m == right.bounds_min_m
        && left.bounds_max_m == right.bounds_max_m
}

#[cfg(feature = "gesture-projection")]
fn native_error(stage: &str, report: &FfiBspSliceReport) -> BridgeError {
    let category = match report.status {
        STATUS_INVALID_ARGUMENT => "invalid_argument",
        STATUS_BUFFER_TOO_SMALL => "buffer_too_small",
        STATUS_ALLOCATION_FAILURE => "allocation_failure",
        STATUS_INTERNAL_ERROR => "internal_error",
        _ => "package_or_topology_error",
    };
    let detail = ffi_text(&report.error, "BSP error").unwrap_or_else(|error| error.to_string());
    BridgeError(format!(
        "HP1 BSP {stage} failed ({category}, status={}): {detail}",
        report.status
    ))
}

#[cfg(feature = "gesture-projection")]
fn validate_textured_report(
    returned_status: u32,
    report: &FfiTexturedBspReport,
    stage: &str,
) -> Result<(), BridgeError> {
    if returned_status != report.status {
        return Err(BridgeError(format!(
            "HP1 textured BSP bridge returned status {returned_status} but report contains {}",
            report.status
        )));
    }
    if report.abi_version != ABI_VERSION {
        return Err(BridgeError(format!(
            "HP1 textured BSP ABI mismatch: Rust expects {ABI_VERSION}, native reports {}",
            report.abi_version
        )));
    }
    if returned_status != STATUS_OK && returned_status != STATUS_BUFFER_TOO_SMALL {
        let detail =
            ffi_text(&report.error, "textured BSP error").unwrap_or_else(|error| error.to_string());
        return Err(BridgeError(format!(
            "HP1 textured BSP {stage} failed (status={returned_status}): {detail}"
        )));
    }
    let expected_texture_bytes = u64::from(report.texture_layer_width)
        * u64::from(report.texture_layer_height)
        * u64::from(report.texture_layer_count)
        * 4;
    if report.selected_triangle_count > report.available_triangle_count
        || report.omitted_triangle_count
            != report.available_triangle_count - report.selected_triangle_count
        || report.required_vertex_count != report.selected_triangle_count.saturating_mul(3)
        || report.selected_triangle_count > MAX_SLICE_TRIANGLES
        || report.texture_layer_width == 0
        || report.texture_layer_height == 0
        || report.texture_layer_count == 0
        || expected_texture_bytes != u64::from(report.required_texture_bytes)
        || report.decoded_texture_count.saturating_add(1) > report.texture_layer_count
        || report.fallback_triangle_count > report.selected_triangle_count
    {
        return Err(BridgeError(
            "HP1 textured BSP report contains inconsistent bounded counters".to_owned(),
        ));
    }
    let minimum = Vec3::from_array(report.bounds_min_m);
    let maximum = Vec3::from_array(report.bounds_max_m);
    if !minimum.is_finite() || !maximum.is_finite() || minimum.cmple(maximum).bitmask() != 0b111 {
        return Err(BridgeError(
            "HP1 textured BSP report contains invalid metric bounds".to_owned(),
        ));
    }
    Ok(())
}

#[cfg(feature = "gesture-projection")]
fn same_textured_metadata(left: &FfiTexturedBspReport, right: &FfiTexturedBspReport) -> bool {
    left.required_vertex_count == right.required_vertex_count
        && left.required_texture_bytes == right.required_texture_bytes
        && left.available_triangle_count == right.available_triangle_count
        && left.selected_triangle_count == right.selected_triangle_count
        && left.omitted_triangle_count == right.omitted_triangle_count
        && left.texture_layer_width == right.texture_layer_width
        && left.texture_layer_height == right.texture_layer_height
        && left.texture_layer_count == right.texture_layer_count
        && left.decoded_texture_count == right.decoded_texture_count
        && left.fallback_material_count == right.fallback_material_count
        && left.fallback_triangle_count == right.fallback_triangle_count
        && left.bounds_min_m == right.bounds_min_m
        && left.bounds_max_m == right.bounds_max_m
}

#[cfg(feature = "gesture-projection")]
fn path_c_string(path: &Path) -> Result<CString, BridgeError> {
    let text = path
        .to_str()
        .ok_or_else(|| BridgeError(format!("path is not valid Unicode: {}", path.display())))?;
    CString::new(text.as_bytes())
        .map_err(|_| BridgeError(format!("path contains an interior NUL: {}", path.display())))
}

#[cfg(feature = "gesture-projection")]
fn ffi_text<const N: usize>(bytes: &[u8; N], field: &str) -> Result<String, BridgeError> {
    if !bytes.contains(&0) {
        return Err(BridgeError(format!(
            "native HP1 {field} is not null terminated"
        )));
    }
    // SAFETY: the checked array contains a terminator and remains live.
    let value = unsafe { CStr::from_ptr(bytes.as_ptr().cast()) };
    value
        .to_str()
        .map(str::to_owned)
        .map_err(|_| BridgeError(format!("native HP1 {field} is not UTF-8")))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ffi_layout_matches_the_c_header() {
        assert_eq!(std::mem::size_of::<FfiBspSliceVertex>(), 36);
        assert_eq!(std::mem::align_of::<FfiBspSliceVertex>(), 4);
        assert_eq!(std::mem::offset_of!(FfiBspSliceVertex, polygon_flags), 24);
        assert_eq!(std::mem::offset_of!(FfiBspSliceVertex, surface_index), 32);
        assert_eq!(std::mem::size_of::<FfiBspSliceReport>(), 348);
        assert_eq!(std::mem::align_of::<FfiBspSliceReport>(), 4);
        assert_eq!(std::mem::offset_of!(FfiBspSliceReport, bounds_min_m), 68);
        assert_eq!(std::mem::offset_of!(FfiBspSliceReport, error), 92);
        assert_eq!(std::mem::size_of::<FfiTexturedBspVertex>(), 48);
        assert_eq!(std::mem::align_of::<FfiTexturedBspVertex>(), 4);
        assert_eq!(std::mem::offset_of!(FfiTexturedBspVertex, texture_uv), 24);
        assert_eq!(
            std::mem::offset_of!(FfiTexturedBspVertex, texture_layer),
            32
        );
        assert_eq!(
            std::mem::offset_of!(FfiTexturedBspVertex, surface_index),
            44
        );
        assert_eq!(std::mem::size_of::<FfiTexturedBspReport>(), 340);
        assert_eq!(std::mem::align_of::<FfiTexturedBspReport>(), 4);
        assert_eq!(std::mem::offset_of!(FfiTexturedBspReport, bounds_min_m), 60);
        assert_eq!(std::mem::offset_of!(FfiTexturedBspReport, error), 84);
        assert_eq!(std::mem::size_of::<FfiPlayerStartReport>(), 440);
        assert_eq!(std::mem::align_of::<FfiPlayerStartReport>(), 4);
        assert_eq!(std::mem::offset_of!(FfiPlayerStartReport, position_m), 24);
        assert_eq!(
            std::mem::offset_of!(FfiPlayerStartReport, rotation_units),
            36
        );
        assert_eq!(std::mem::offset_of!(FfiPlayerStartReport, object_name), 56);
        assert_eq!(std::mem::offset_of!(FfiPlayerStartReport, error), 184);
        assert_eq!(std::mem::size_of::<FfiActorVisualReport>(), 440);
        assert_eq!(std::mem::align_of::<FfiActorVisualReport>(), 4);
        assert_eq!(std::mem::offset_of!(FfiActorVisualReport, position_m), 16);
        assert_eq!(std::mem::offset_of!(FfiActorVisualReport, object_name), 56);
        assert_eq!(std::mem::offset_of!(FfiActorVisualReport, error), 184);
    }

    #[cfg(feature = "gesture-projection")]
    #[test]
    fn opt_in_external_map_crosses_the_two_call_ffi_boundary() {
        let Some(package) = std::env::var_os("HPVR_TEST_HP1_MAP") else {
            return;
        };
        let scale: f32 = std::env::var("HPVR_TEST_HP1_METERS_PER_UNIT")
            .expect("HPVR_TEST_HP1_METERS_PER_UNIT is required with HPVR_TEST_HP1_MAP")
            .parse()
            .expect("HPVR_TEST_HP1_METERS_PER_UNIT must be a float");
        let slice = MapSlice::load(Path::new(&package), scale, 64).unwrap();
        let unit_slice = MapSlice::load(Path::new(&package), 1.0, 64).unwrap();
        assert_eq!(slice.vertices.len(), 64 * 3);
        assert_eq!(slice.report.selected_triangle_count, 64);
        assert!(slice.report.available_triangle_count >= 64);
        assert!(slice.texture_layer_width > 0);
        assert!(slice.texture_layer_height > 0);
        assert!(slice.texture_layer_count > 1);
        assert!(slice.decoded_texture_count > 0);
        assert_eq!(
            slice.texture_rgba8.len(),
            slice.texture_layer_width as usize
                * slice.texture_layer_height as usize
                * slice.texture_layer_count as usize
                * 4
        );
        assert_eq!(
            slice.report.available_triangle_count,
            unit_slice.report.available_triangle_count
        );
        assert_eq!(
            slice.report.degenerate_triangle_count,
            unit_slice.report.degenerate_triangle_count
        );
        assert_eq!(
            slice.report.winding_reversal_count,
            unit_slice.report.winding_reversal_count
        );
        for (scaled, unit) in slice.vertices.iter().zip(&unit_slice.vertices) {
            assert_eq!(scaled.node_index, unit.node_index);
            assert_eq!(scaled.surface_index, unit.surface_index);
            assert_eq!(scaled.polygon_flags, unit.polygon_flags);
            assert_eq!(scaled.texture_layer, unit.texture_layer);
            assert_eq!(scaled.texture_uv, unit.texture_uv);
            assert!(scaled.normal.abs_diff_eq(unit.normal, 1.0e-5));
            assert!(
                scaled
                    .position_m
                    .abs_diff_eq(unit.position_m * scale, 1.0e-4)
            );
        }
        assert!(
            slice
                .report
                .bounds_min_m
                .cmple(slice.report.bounds_max_m)
                .all()
        );
        let player_start = PlayerStart::load(Path::new(&package), scale, 0).unwrap();
        assert!(player_start.available_count >= 1);
        assert_eq!(player_start.ordinal, 0);
        assert!(player_start.actor_reference > 0);
        assert!(player_start.location_serialized);
        assert!(!player_start.object_name.is_empty());
        assert!(player_start.position_m.is_finite());
    }
}
