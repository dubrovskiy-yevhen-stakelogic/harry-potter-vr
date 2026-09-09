use std::{error::Error, fs, path::Path};

#[cfg(feature = "gesture-projection")]
use std::ffi::{CStr, CString};

use glam::Vec3;

const MAX_FILE_BYTES: usize = 64 * 1024 * 1024;
const MAX_POINTS: usize = 1_000_000;
const MAX_WEDGES: usize = 2_000_000;
const MAX_FACES: usize = 2_000_000;
const MAX_MATERIALS: usize = 64;
const TEXTURE_SIZE: usize = 256;
#[cfg(feature = "gesture-projection")]
const ANIMATION_FRAME_COUNT: u32 = 16;

#[cfg(feature = "gesture-projection")]
const STATUS_OK: u32 = 0;
#[cfg(feature = "gesture-projection")]
const STATUS_BUFFER_TOO_SMALL: u32 = 101;
#[cfg(feature = "gesture-projection")]
const SKELETAL_ABI_VERSION: u32 = 2;
#[cfg(feature = "gesture-projection")]
const ANIMATION_ABI_VERSION: u32 = 1;
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
#[derive(Clone, Copy, Default)]
struct FfiAnimationPoint {
    position_m: [f32; 3],
}

#[cfg(feature = "gesture-projection")]
#[repr(C)]
#[derive(Clone, Copy)]
struct FfiAnimationReport {
    abi_version: u32,
    status: u32,
    required_position_count: u32,
    written_position_count: u32,
    point_count: u32,
    frame_count: u32,
    animation_reference: i32,
    sequence_index: u32,
    duration_seconds: f32,
    sequence_name: [u8; 128],
    error: [u8; ERROR_CAPACITY],
}

#[cfg(feature = "gesture-projection")]
impl Default for FfiAnimationReport {
    fn default() -> Self {
        Self {
            abi_version: 0,
            status: 0,
            required_position_count: 0,
            written_position_count: 0,
            point_count: 0,
            frame_count: 0,
            animation_reference: 0,
            sequence_index: 0,
            duration_seconds: 0.0,
            sequence_name: [0; 128],
            error: [0; ERROR_CAPACITY],
        }
    }
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
    fn hpvr_hp1_load_skeletal_mesh_utf8(
        mesh_package_utf8: *const std::ffi::c_char,
        mesh_reference: i32,
        meters_per_unreal_unit: f32,
        output_vertices: *mut FfiSkeletalVertex,
        vertex_capacity: u32,
        output_texture_rgba8: *mut u8,
        texture_byte_capacity: u32,
        output_report: *mut FfiSkeletalReport,
    ) -> u32;
    fn hpvr_hp1_load_skeletal_animation_utf8(
        mesh_package_utf8: *const std::ffi::c_char,
        mesh_reference: i32,
        meters_per_unreal_unit: f32,
        frame_count: u32,
        output_positions: *mut FfiAnimationPoint,
        position_capacity: u32,
        output_report: *mut FfiAnimationReport,
    ) -> u32;
}

#[derive(Clone, Copy)]
pub(super) struct NpcPreviewVertex {
    pub position_m: Vec3,
    pub texture_uv: [f32; 2],
    pub material_index: u32,
    #[cfg_attr(not(feature = "gesture-projection"), allow(dead_code))]
    pub point_index: usize,
}

pub(super) struct NpcPreview {
    pub vertices: Vec<NpcPreviewVertex>,
    pub texture_rgba8: Vec<u8>,
    pub texture_layer_count: u32,
    pub point_count: usize,
    pub wedge_count: usize,
    pub face_count: usize,
    pub material_names: Vec<String>,
    // Translation already applied to vertices for the legacy front-facing A/B
    // mode. Actor placement removes it before applying the level transform.
    pub diagnostic_translation: Vec3,
    #[cfg_attr(not(feature = "gesture-projection"), allow(dead_code))]
    pub animation_frames: Vec<Vec<Vec3>>,
    #[cfg_attr(not(feature = "gesture-projection"), allow(dead_code))]
    pub animation_sequence: String,
    #[cfg_attr(not(feature = "gesture-projection"), allow(dead_code))]
    pub animation_duration_seconds: f32,
}

#[derive(Clone, Copy)]
struct Wedge {
    point_index: usize,
    uv: [f32; 2],
}

#[derive(Clone, Copy)]
struct Face {
    wedge_indices: [usize; 3],
    material_index: usize,
}

struct Chunk<'a> {
    id: &'a str,
    item_size: usize,
    item_count: usize,
    data: &'a [u8],
}

impl NpcPreview {
    #[cfg(not(feature = "gesture-projection"))]
    pub(super) fn load_package(
        _package_path: &Path,
        _mesh_reference: i32,
        _meters_per_unit: f32,
    ) -> Result<Self, Box<dyn Error>> {
        Err("direct NPC package loading requires feature 'gesture-projection'".into())
    }

    #[cfg(feature = "gesture-projection")]
    pub(super) fn load_package(
        package_path: &Path,
        mesh_reference: i32,
        meters_per_unit: f32,
    ) -> Result<Self, Box<dyn Error>> {
        if mesh_reference <= 0 || !meters_per_unit.is_finite() || meters_per_unit <= 0.0 {
            return Err("direct NPC package arguments are invalid".into());
        }
        let package = CString::new(
            package_path
                .to_str()
                .ok_or("NPC package path is not valid Unicode")?,
        )?;
        let mut query = FfiSkeletalReport::default();
        // SAFETY: the path and report remain live; both null buffers carry
        // zero capacities and request exact output sizes only.
        let query_status = unsafe {
            hpvr_hp1_load_skeletal_mesh_utf8(
                package.as_ptr(),
                mesh_reference,
                meters_per_unit,
                std::ptr::null_mut(),
                0,
                std::ptr::null_mut(),
                0,
                &mut query,
            )
        };
        validate_direct_report(query_status, &query, true)?;

        let vertex_count = usize::try_from(query.required_vertex_count)?;
        let texture_bytes = usize::try_from(query.required_texture_bytes)?;
        let mut ffi_vertices = vec![FfiSkeletalVertex::default(); vertex_count];
        let mut texture_rgba8 = vec![0_u8; texture_bytes];
        let mut loaded = FfiSkeletalReport::default();
        // SAFETY: both output pointers address exactly the capacities passed
        // to the C ABI and all inputs remain valid for the duration of call.
        let load_status = unsafe {
            hpvr_hp1_load_skeletal_mesh_utf8(
                package.as_ptr(),
                mesh_reference,
                meters_per_unit,
                ffi_vertices.as_mut_ptr(),
                query.required_vertex_count,
                texture_rgba8.as_mut_ptr(),
                query.required_texture_bytes,
                &mut loaded,
            )
        };
        validate_direct_report(load_status, &loaded, false)?;
        if query.required_vertex_count != loaded.required_vertex_count
            || query.required_texture_bytes != loaded.required_texture_bytes
            || query.point_count != loaded.point_count
            || query.wedge_count != loaded.wedge_count
            || query.face_count != loaded.face_count
            || query.material_count != loaded.material_count
            || query.bounds_min_m != loaded.bounds_min_m
            || query.bounds_max_m != loaded.bounds_max_m
        {
            return Err("NPC package changed between size query and load".into());
        }

        let mut animation_query = FfiAnimationReport::default();
        // SAFETY: the path/report remain live and a null zero-capacity buffer
        // requests the exact bounded frame-major size.
        let animation_query_status = unsafe {
            hpvr_hp1_load_skeletal_animation_utf8(
                package.as_ptr(),
                mesh_reference,
                meters_per_unit,
                ANIMATION_FRAME_COUNT,
                std::ptr::null_mut(),
                0,
                &mut animation_query,
            )
        };
        validate_animation_report(animation_query_status, &animation_query, true)?;
        let mut ffi_animation = vec![
            FfiAnimationPoint::default();
            usize::try_from(animation_query.required_position_count)?
        ];
        let mut animation_loaded = FfiAnimationReport::default();
        // SAFETY: the allocation exactly matches the reported position count.
        let animation_load_status = unsafe {
            hpvr_hp1_load_skeletal_animation_utf8(
                package.as_ptr(),
                mesh_reference,
                meters_per_unit,
                ANIMATION_FRAME_COUNT,
                ffi_animation.as_mut_ptr(),
                animation_query.required_position_count,
                &mut animation_loaded,
            )
        };
        validate_animation_report(animation_load_status, &animation_loaded, false)?;
        if animation_query.required_position_count != animation_loaded.required_position_count
            || animation_query.point_count != animation_loaded.point_count
            || animation_query.frame_count != animation_loaded.frame_count
            || animation_query.animation_reference != animation_loaded.animation_reference
            || animation_query.sequence_index != animation_loaded.sequence_index
            || animation_query.duration_seconds != animation_loaded.duration_seconds
        {
            return Err("NPC animation changed between size query and load".into());
        }
        let animation_frames = ffi_animation
            .chunks_exact(usize::try_from(animation_loaded.point_count)?)
            .map(|frame| {
                frame
                    .iter()
                    .map(|point| Vec3::from_array(point.position_m))
                    .collect::<Vec<_>>()
            })
            .collect::<Vec<_>>();
        if animation_frames.len() != usize::try_from(ANIMATION_FRAME_COUNT)?
            || animation_frames
                .iter()
                .flatten()
                .any(|point| !point.is_finite())
        {
            return Err("direct NPC animation returned invalid frames".into());
        }
        let animation_sequence = ffi_text(&animation_loaded.sequence_name, "animation sequence")?;

        let minimum = Vec3::from_array(loaded.bounds_min_m);
        let maximum = Vec3::from_array(loaded.bounds_max_m);
        let horizontal_center = Vec3::new(
            (minimum.x + maximum.x) * 0.5,
            minimum.y,
            (minimum.z + maximum.z) * 0.5,
        );
        let translation = Vec3::new(0.0, -1.55, -2.50) - horizontal_center;
        let vertices = ffi_vertices
            .into_iter()
            .map(|vertex| NpcPreviewVertex {
                position_m: Vec3::from_array(vertex.position_m) + translation,
                texture_uv: vertex.texture_uv,
                material_index: vertex.texture_layer,
                point_index: usize::try_from(vertex.point_index).unwrap_or(usize::MAX),
            })
            .collect::<Vec<_>>();
        if vertices.iter().any(|vertex| {
            !vertex.position_m.is_finite()
                || vertex.texture_uv.iter().any(|value| !value.is_finite())
                || vertex.material_index >= loaded.texture_layer_count
                || vertex.point_index >= loaded.point_count as usize
        }) {
            return Err("direct NPC package returned an invalid vertex".into());
        }
        Ok(Self {
            vertices,
            texture_rgba8,
            texture_layer_count: loaded.texture_layer_count,
            point_count: loaded.point_count as usize,
            wedge_count: loaded.wedge_count as usize,
            face_count: loaded.face_count as usize,
            material_names: (0..loaded.material_count)
                .map(|index| format!("package-material-{index}"))
                .collect(),
            diagnostic_translation: translation,
            animation_frames,
            animation_sequence,
            animation_duration_seconds: animation_loaded.duration_seconds,
        })
    }

    pub(super) fn load(
        psk_path: &Path,
        texture_directory: &Path,
        meters_per_unit: f32,
    ) -> Result<Self, Box<dyn Error>> {
        if !meters_per_unit.is_finite() || meters_per_unit <= 0.0 {
            return Err("NPC preview scale must be finite and positive".into());
        }
        let bytes = fs::read(psk_path)?;
        if bytes.is_empty() || bytes.len() > MAX_FILE_BYTES {
            return Err("NPC PSK is empty or exceeds the bounded size".into());
        }
        let chunks = parse_chunks(&bytes)?;
        let points_chunk = required_chunk(&chunks, "PNTS0000", 12, MAX_POINTS)?;
        let wedges_chunk = required_chunk(&chunks, "VTXW0000", 16, MAX_WEDGES)?;
        let faces_chunk = required_chunk(&chunks, "FACE0000", 12, MAX_FACES)?;
        let materials_chunk = required_chunk(&chunks, "MATT0000", 88, MAX_MATERIALS)?;

        let mut points = Vec::with_capacity(points_chunk.item_count);
        for item in points_chunk.data.chunks_exact(12) {
            let point = Vec3::new(f32_at(item, 0), f32_at(item, 4), f32_at(item, 8));
            if !point.is_finite() {
                return Err("NPC PSK contains a non-finite point".into());
            }
            points.push(point);
        }
        let mut wedges = Vec::with_capacity(wedges_chunk.item_count);
        for item in wedges_chunk.data.chunks_exact(16) {
            let point_index = usize::try_from(i32_at(item, 0))
                .map_err(|_| "NPC PSK wedge has a negative point index")?;
            let uv = [f32_at(item, 4), f32_at(item, 8)];
            if point_index >= points.len() || uv.iter().any(|value| !value.is_finite()) {
                return Err("NPC PSK wedge is outside the point array or non-finite".into());
            }
            wedges.push(Wedge { point_index, uv });
        }
        let mut faces = Vec::with_capacity(faces_chunk.item_count);
        for item in faces_chunk.data.chunks_exact(12) {
            let face = Face {
                wedge_indices: [
                    usize::from(u16_at(item, 0)),
                    usize::from(u16_at(item, 2)),
                    usize::from(u16_at(item, 4)),
                ],
                material_index: usize::from(item[6]),
            };
            if face
                .wedge_indices
                .iter()
                .any(|index| *index >= wedges.len())
                || face.material_index >= materials_chunk.item_count
            {
                return Err("NPC PSK face references an invalid wedge or material".into());
            }
            faces.push(face);
        }
        let material_names = materials_chunk
            .data
            .chunks_exact(88)
            .map(|item| c_string(&item[..64]))
            .collect::<Result<Vec<_>, _>>()?;

        let mut texture_rgba8 =
            Vec::with_capacity(material_names.len() * TEXTURE_SIZE * TEXTURE_SIZE * 4);
        for name in &material_names {
            let texture_path = texture_directory.join(format!("{name}.tga"));
            let image = decode_tga(&fs::read(&texture_path)?)?;
            texture_rgba8.extend_from_slice(&resample_rgba(
                &image.pixels,
                image.width,
                image.height,
                TEXTURE_SIZE,
                TEXTURE_SIZE,
            ));
        }

        let mut minimum = Vec3::splat(f32::INFINITY);
        let mut maximum = Vec3::splat(f32::NEG_INFINITY);
        for point in &points {
            let mapped = Vec3::new(point.y, point.z, -point.x) * meters_per_unit;
            minimum = minimum.min(mapped);
            maximum = maximum.max(mapped);
        }
        let horizontal_center = Vec3::new(
            (minimum.x + maximum.x) * 0.5,
            minimum.y,
            (minimum.z + maximum.z) * 0.5,
        );
        let diagnostic_origin = Vec3::new(0.0, -1.55, -2.50);
        let translation = diagnostic_origin - horizontal_center;
        let mut vertices = Vec::with_capacity(faces.len() * 3);
        for face in &faces {
            // Preserve the exported ActorX winding. The diagnostic pipeline is
            // deliberately two-sided while the native UE1 winding is proven.
            for wedge_index in face.wedge_indices {
                let wedge = wedges[wedge_index];
                let point = points[wedge.point_index];
                let position_m =
                    Vec3::new(point.y, point.z, -point.x) * meters_per_unit + translation;
                vertices.push(NpcPreviewVertex {
                    position_m,
                    texture_uv: wedge.uv,
                    material_index: face.material_index as u32,
                    point_index: wedge.point_index,
                });
            }
        }
        Ok(Self {
            vertices,
            texture_rgba8,
            texture_layer_count: material_names.len() as u32,
            point_count: points.len(),
            wedge_count: wedges.len(),
            face_count: faces.len(),
            material_names,
            diagnostic_translation: translation,
            animation_frames: Vec::new(),
            animation_sequence: String::new(),
            animation_duration_seconds: 0.0,
        })
    }
}

#[cfg(feature = "gesture-projection")]
fn ffi_text<const N: usize>(bytes: &[u8; N], field: &str) -> Result<String, Box<dyn Error>> {
    let nul = bytes
        .iter()
        .position(|byte| *byte == 0)
        .ok_or_else(|| format!("direct NPC {field} is not terminated"))?;
    Ok(CStr::from_bytes_with_nul(&bytes[..=nul])?
        .to_str()
        .map_err(|_| format!("direct NPC {field} is not UTF-8"))?
        .to_owned())
}

#[cfg(feature = "gesture-projection")]
fn validate_animation_report(
    returned_status: u32,
    report: &FfiAnimationReport,
    query: bool,
) -> Result<(), Box<dyn Error>> {
    if returned_status != report.status || report.abi_version != ANIMATION_ABI_VERSION {
        return Err("direct NPC animation ABI status/version mismatch".into());
    }
    let expected_status = if query {
        STATUS_BUFFER_TOO_SMALL
    } else {
        STATUS_OK
    };
    if returned_status != expected_status {
        return Err(format!(
            "direct NPC animation ABI failed: {}",
            ffi_text(&report.error, "animation error")?
        )
        .into());
    }
    if report.frame_count != ANIMATION_FRAME_COUNT
        || report.point_count == 0
        || report.required_position_count
            != report
                .point_count
                .checked_mul(report.frame_count)
                .ok_or("direct NPC animation count overflow")?
        || report.animation_reference <= 0
        || !report.duration_seconds.is_finite()
        || report.duration_seconds <= 0.0
        || (!query && report.written_position_count != report.required_position_count)
    {
        return Err("direct NPC animation report invariants failed".into());
    }
    Ok(())
}

#[cfg(feature = "gesture-projection")]
fn validate_direct_report(
    returned_status: u32,
    report: &FfiSkeletalReport,
    query: bool,
) -> Result<(), Box<dyn Error>> {
    if returned_status != report.status || report.abi_version != SKELETAL_ABI_VERSION {
        return Err("direct NPC C ABI status/version mismatch".into());
    }
    let expected_status = if query {
        STATUS_BUFFER_TOO_SMALL
    } else {
        STATUS_OK
    };
    if returned_status != expected_status {
        // SAFETY: error is an in-struct fixed array written by the native ABI;
        // validation requires a NUL within its exact bounds before CStr use.
        let nul = report
            .error
            .iter()
            .position(|byte| *byte == 0)
            .ok_or("direct NPC C ABI error is not terminated")?;
        let message = CStr::from_bytes_with_nul(&report.error[..=nul])?.to_string_lossy();
        return Err(format!("direct NPC C ABI failed: {message}").into());
    }
    let expected_vertices = report
        .face_count
        .checked_mul(3)
        .ok_or("direct NPC face count overflow")?;
    let expected_texture_bytes = report
        .texture_layer_count
        .checked_mul(TEXTURE_SIZE as u32)
        .and_then(|value| value.checked_mul(TEXTURE_SIZE as u32))
        .and_then(|value| value.checked_mul(4))
        .ok_or("direct NPC texture size overflow")?;
    if report.required_vertex_count != expected_vertices
        || report.required_texture_bytes != expected_texture_bytes
        || report.texture_layer_width != TEXTURE_SIZE as u32
        || report.texture_layer_height != TEXTURE_SIZE as u32
        || report.texture_layer_count != report.material_count
        || report.decoded_texture_count != report.material_count
        || report.point_count == 0
        || report.wedge_count == 0
        || report.face_count == 0
        || report.material_count == 0
        || report.bounds_min_m.iter().any(|value| !value.is_finite())
        || report.bounds_max_m.iter().any(|value| !value.is_finite())
        || (!query
            && (report.written_vertex_count != report.required_vertex_count
                || report.written_texture_bytes != report.required_texture_bytes))
    {
        return Err("direct NPC C ABI report invariants failed".into());
    }
    Ok(())
}

fn parse_chunks(bytes: &[u8]) -> Result<Vec<Chunk<'_>>, Box<dyn Error>> {
    let mut chunks = Vec::new();
    let mut offset = 0_usize;
    while offset < bytes.len() {
        let header_end = offset.checked_add(32).ok_or("PSK header overflow")?;
        if header_end > bytes.len() {
            return Err("NPC PSK has a truncated chunk header".into());
        }
        let id = std::str::from_utf8(&bytes[offset..offset + 20])?.trim_end_matches('\0');
        let item_size = usize::try_from(i32_at(bytes, offset + 24))
            .map_err(|_| "NPC PSK has a negative chunk item size")?;
        let item_count = usize::try_from(i32_at(bytes, offset + 28))
            .map_err(|_| "NPC PSK has a negative chunk item count")?;
        let byte_count = item_size
            .checked_mul(item_count)
            .ok_or("NPC PSK chunk size overflow")?;
        let data_end = header_end
            .checked_add(byte_count)
            .ok_or("NPC PSK chunk end overflow")?;
        if data_end > bytes.len() {
            return Err("NPC PSK chunk exceeds the file".into());
        }
        chunks.push(Chunk {
            id,
            item_size,
            item_count,
            data: &bytes[header_end..data_end],
        });
        offset = data_end;
    }
    Ok(chunks)
}

fn required_chunk<'a>(
    chunks: &'a [Chunk<'a>],
    id: &str,
    item_size: usize,
    maximum_count: usize,
) -> Result<&'a Chunk<'a>, Box<dyn Error>> {
    let mut matches = chunks.iter().filter(|chunk| chunk.id == id);
    let chunk = matches
        .next()
        .ok_or("NPC PSK is missing a required chunk")?;
    if matches.next().is_some()
        || chunk.item_size != item_size
        || chunk.item_count == 0
        || chunk.item_count > maximum_count
    {
        return Err("NPC PSK required chunk has invalid framing".into());
    }
    Ok(chunk)
}

fn c_string(bytes: &[u8]) -> Result<String, Box<dyn Error>> {
    let end = bytes
        .iter()
        .position(|value| *value == 0)
        .unwrap_or(bytes.len());
    if end == 0 {
        return Err("NPC PSK material name is empty".into());
    }
    Ok(std::str::from_utf8(&bytes[..end])?.to_owned())
}

struct RgbaImage {
    width: usize,
    height: usize,
    pixels: Vec<u8>,
}

fn decode_tga(bytes: &[u8]) -> Result<RgbaImage, Box<dyn Error>> {
    if bytes.len() < 18
        || bytes.len() > MAX_FILE_BYTES
        || bytes[1] != 0
        || bytes[2] != 10
        || bytes[16] != 32
    {
        return Err("NPC texture is not a bounded RLE 32-bit true-color TGA".into());
    }
    let width = usize::from(u16_at(bytes, 12));
    let height = usize::from(u16_at(bytes, 14));
    if width == 0 || height == 0 || width > 4096 || height > 4096 {
        return Err("NPC TGA dimensions are invalid".into());
    }
    let pixel_count = width.checked_mul(height).ok_or("NPC TGA size overflow")?;
    let mut source = Vec::with_capacity(pixel_count * 4);
    let mut offset = 18_usize
        .checked_add(usize::from(bytes[0]))
        .ok_or("NPC TGA data offset overflow")?;
    while source.len() / 4 < pixel_count {
        let header = *bytes.get(offset).ok_or("NPC TGA RLE packet is truncated")?;
        offset += 1;
        let count = usize::from(header & 0x7f) + 1;
        if source.len() / 4 + count > pixel_count {
            return Err("NPC TGA RLE packet exceeds the image".into());
        }
        if header & 0x80 != 0 {
            let pixel = bytes
                .get(offset..offset + 4)
                .ok_or("NPC TGA RLE color is truncated")?;
            offset += 4;
            for _ in 0..count {
                source.extend_from_slice(&[pixel[2], pixel[1], pixel[0], pixel[3]]);
            }
        } else {
            let byte_count = count * 4;
            let packet = bytes
                .get(offset..offset + byte_count)
                .ok_or("NPC TGA raw packet is truncated")?;
            offset += byte_count;
            for pixel in packet.chunks_exact(4) {
                source.extend_from_slice(&[pixel[2], pixel[1], pixel[0], pixel[3]]);
            }
        }
    }
    let top_origin = bytes[17] & 0x20 != 0;
    let right_origin = bytes[17] & 0x10 != 0;
    let mut pixels = vec![0_u8; source.len()];
    for source_y in 0..height {
        for source_x in 0..width {
            let destination_x = if right_origin {
                width - 1 - source_x
            } else {
                source_x
            };
            let destination_y = if top_origin {
                source_y
            } else {
                height - 1 - source_y
            };
            let source_offset = (source_y * width + source_x) * 4;
            let destination_offset = (destination_y * width + destination_x) * 4;
            pixels[destination_offset..destination_offset + 4]
                .copy_from_slice(&source[source_offset..source_offset + 4]);
        }
    }
    Ok(RgbaImage {
        width,
        height,
        pixels,
    })
}

fn resample_rgba(
    source: &[u8],
    source_width: usize,
    source_height: usize,
    width: usize,
    height: usize,
) -> Vec<u8> {
    let mut result = vec![0_u8; width * height * 4];
    for y in 0..height {
        let source_y = y * source_height / height;
        for x in 0..width {
            let source_x = x * source_width / width;
            let source_offset = (source_y * source_width + source_x) * 4;
            let destination_offset = (y * width + x) * 4;
            result[destination_offset..destination_offset + 4]
                .copy_from_slice(&source[source_offset..source_offset + 4]);
        }
    }
    result
}

fn i32_at(bytes: &[u8], offset: usize) -> i32 {
    i32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap())
}

fn u16_at(bytes: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes(bytes[offset..offset + 2].try_into().unwrap())
}

fn f32_at(bytes: &[u8], offset: usize) -> f32 {
    f32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[cfg(feature = "gesture-projection")]
    #[test]
    fn direct_package_ffi_layout_matches_c_header() {
        assert_eq!(std::mem::size_of::<FfiSkeletalVertex>(), 36);
        assert_eq!(std::mem::align_of::<FfiSkeletalVertex>(), 4);
        assert_eq!(std::mem::offset_of!(FfiSkeletalVertex, texture_uv), 12);
        assert_eq!(std::mem::offset_of!(FfiSkeletalVertex, texture_layer), 20);
        assert_eq!(std::mem::offset_of!(FfiSkeletalVertex, point_index), 32);
        assert_eq!(std::mem::size_of::<FfiSkeletalReport>(), 336);
        assert_eq!(std::mem::align_of::<FfiSkeletalReport>(), 4);
        assert_eq!(std::mem::offset_of!(FfiSkeletalReport, bounds_min_m), 56);
        assert_eq!(std::mem::offset_of!(FfiSkeletalReport, error), 80);
        assert_eq!(std::mem::size_of::<FfiAnimationPoint>(), 12);
        assert_eq!(std::mem::size_of::<FfiAnimationReport>(), 420);
        assert_eq!(std::mem::offset_of!(FfiAnimationReport, sequence_name), 36);
        assert_eq!(std::mem::offset_of!(FfiAnimationReport, error), 164);
    }

    fn append_chunk(bytes: &mut Vec<u8>, id: &str, item_size: i32, items: &[u8]) {
        let item_size_usize = usize::try_from(item_size).unwrap();
        assert_eq!(items.len() % item_size_usize, 0);
        let mut name = [0_u8; 20];
        name[..id.len()].copy_from_slice(id.as_bytes());
        bytes.extend_from_slice(&name);
        bytes.extend_from_slice(&0_i32.to_le_bytes());
        bytes.extend_from_slice(&item_size.to_le_bytes());
        bytes.extend_from_slice(
            &i32::try_from(items.len() / item_size_usize)
                .unwrap()
                .to_le_bytes(),
        );
        bytes.extend_from_slice(items);
    }

    #[test]
    fn rle_tga_is_oriented_and_converted_to_rgba() {
        let mut bytes = vec![0_u8; 18];
        bytes[2] = 10;
        bytes[12..14].copy_from_slice(&2_u16.to_le_bytes());
        bytes[14..16].copy_from_slice(&1_u16.to_le_bytes());
        bytes[16] = 32;
        bytes.push(1); // two raw pixels
        bytes.extend_from_slice(&[3, 2, 1, 4, 7, 6, 5, 8]);
        let image = decode_tga(&bytes).unwrap();
        assert_eq!(image.width, 2);
        assert_eq!(image.height, 1);
        assert_eq!(image.pixels, [1, 2, 3, 4, 5, 6, 7, 8]);
    }

    #[test]
    fn nearest_resample_preserves_corners() {
        let source = [1, 0, 0, 255, 2, 0, 0, 255, 3, 0, 0, 255, 4, 0, 0, 255];
        let resized = resample_rgba(&source, 2, 2, 4, 4);
        assert_eq!(&resized[..4], &[1, 0, 0, 255]);
        assert_eq!(&resized[60..64], &[4, 0, 0, 255]);
    }

    #[test]
    fn bounded_psk_and_tga_form_triangle_preview() {
        let root = std::env::temp_dir().join(format!("hpvr-npc-preview-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(&root).unwrap();

        let mut psk = Vec::new();
        let mut points = Vec::new();
        for point in [[0.0_f32, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 0.0, 1.0]] {
            for component in point {
                points.extend_from_slice(&component.to_le_bytes());
            }
        }
        append_chunk(&mut psk, "PNTS0000", 12, &points);
        let mut wedges = Vec::new();
        for (index, uv) in [(0_i32, [0.0_f32, 0.0]), (1, [1.0, 0.0]), (2, [0.0, 1.0])] {
            wedges.extend_from_slice(&index.to_le_bytes());
            wedges.extend_from_slice(&uv[0].to_le_bytes());
            wedges.extend_from_slice(&uv[1].to_le_bytes());
            wedges.extend_from_slice(&[0, 0, 0, 0]);
        }
        append_chunk(&mut psk, "VTXW0000", 16, &wedges);
        let mut face = Vec::new();
        for index in [0_u16, 1, 2] {
            face.extend_from_slice(&index.to_le_bytes());
        }
        face.extend_from_slice(&[0, 0]);
        face.extend_from_slice(&0_u32.to_le_bytes());
        append_chunk(&mut psk, "FACE0000", 12, &face);
        let mut material = vec![0_u8; 88];
        material[..4].copy_from_slice(b"Test");
        append_chunk(&mut psk, "MATT0000", 88, &material);
        let psk_path = root.join("Test.psk");
        fs::write(&psk_path, psk).unwrap();

        let mut tga = vec![0_u8; 18];
        tga[2] = 10;
        tga[12..14].copy_from_slice(&1_u16.to_le_bytes());
        tga[14..16].copy_from_slice(&1_u16.to_le_bytes());
        tga[16] = 32;
        tga.extend_from_slice(&[0x80, 30, 20, 10, 255]);
        fs::write(root.join("Test.tga"), tga).unwrap();

        let preview = NpcPreview::load(&psk_path, &root, 0.02).unwrap();
        assert_eq!(preview.point_count, 3);
        assert_eq!(preview.wedge_count, 3);
        assert_eq!(preview.face_count, 1);
        assert_eq!(preview.vertices.len(), 3);
        assert_eq!(preview.texture_layer_count, 1);
        assert_eq!(preview.texture_rgba8.len(), 256 * 256 * 4);
        assert!(
            preview
                .vertices
                .iter()
                .all(|vertex| vertex.position_m.is_finite())
        );

        fs::remove_dir_all(root).unwrap();
    }
}
