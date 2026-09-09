use std::error::Error;
use std::path::Path;

#[cfg(feature = "gesture-projection")]
use std::path::PathBuf;

use glam::Vec3;

#[cfg(feature = "gesture-projection")]
use glam::Quat;
#[cfg(feature = "gesture-projection")]
use std::collections::BTreeMap;
#[cfg(feature = "gesture-projection")]
use std::collections::BTreeSet;

#[cfg(feature = "gesture-projection")]
use super::hp1_npc_preview::NpcPreview;

#[cfg(feature = "gesture-projection")]
const ABI_VERSION: u32 = 1;
#[cfg(feature = "gesture-projection")]
const STATUS_OK: u32 = 0;
#[cfg(feature = "gesture-projection")]
const MAX_ACTORS: usize = 4096;
#[cfg(feature = "gesture-projection")]
const MAX_TEXTURE_LAYERS: u32 = 256;
#[cfg(any(feature = "gesture-projection", test))]
const ERROR_CAPACITY: usize = 256;
#[cfg(any(feature = "gesture-projection", test))]
const NAME_CAPACITY: usize = 128;
#[cfg(any(feature = "gesture-projection", test))]
const PATH_CAPACITY: usize = 512;

#[cfg(feature = "gesture-projection")]
use std::ffi::{CStr, CString};

#[cfg(any(feature = "gesture-projection", test))]
#[repr(C)]
#[derive(Clone, Copy)]
struct FfiCharacterActor {
    actor_slot_index: u32,
    actor_reference: i32,
    class_reference: i32,
    mesh_reference: i32,
    position_m: [f32; 3],
    rotation_units: [i32; 3],
    draw_scale: f32,
    object_name: [u8; NAME_CAPACITY],
    qualified_class_name: [u8; NAME_CAPACITY],
    mesh_package_utf8: [u8; PATH_CAPACITY],
    mesh_object_path: [u8; NAME_CAPACITY],
}

#[cfg(any(feature = "gesture-projection", test))]
impl Default for FfiCharacterActor {
    fn default() -> Self {
        Self {
            actor_slot_index: 0,
            actor_reference: 0,
            class_reference: 0,
            mesh_reference: 0,
            position_m: [0.0; 3],
            rotation_units: [0; 3],
            draw_scale: 0.0,
            object_name: [0; NAME_CAPACITY],
            qualified_class_name: [0; NAME_CAPACITY],
            mesh_package_utf8: [0; PATH_CAPACITY],
            mesh_object_path: [0; NAME_CAPACITY],
        }
    }
}

#[cfg(any(feature = "gesture-projection", test))]
#[repr(C)]
#[derive(Clone, Copy)]
struct FfiCharacterManifestReport {
    abi_version: u32,
    status: u32,
    required_actor_count: u32,
    written_actor_count: u32,
    inspected_actor_count: u32,
    excluded_actor_count: u32,
    non_character_actor_count: u32,
    missing_location_count: u32,
    unresolved_class_count: u32,
    missing_mesh_count: u32,
    hidden_actor_count: u32,
    error: [u8; ERROR_CAPACITY],
}

#[cfg(any(feature = "gesture-projection", test))]
impl Default for FfiCharacterManifestReport {
    fn default() -> Self {
        Self {
            abi_version: 0,
            status: 0,
            required_actor_count: 0,
            written_actor_count: 0,
            inspected_actor_count: 0,
            excluded_actor_count: 0,
            non_character_actor_count: 0,
            missing_location_count: 0,
            unresolved_class_count: 0,
            missing_mesh_count: 0,
            hidden_actor_count: 0,
            error: [0; ERROR_CAPACITY],
        }
    }
}

#[cfg(feature = "gesture-projection")]
unsafe extern "C" {
    fn hpvr_hp1_load_character_manifest_utf8(
        data_root_utf8: *const std::ffi::c_char,
        map_package_utf8: *const std::ffi::c_char,
        meters_per_unreal_unit: f32,
        excluded_actor_reference: i32,
        output_actors: *mut FfiCharacterActor,
        actor_capacity: u32,
        output_report: *mut FfiCharacterManifestReport,
    ) -> u32;
}

#[derive(Clone, Copy)]
pub(super) struct CharacterVertex {
    pub position_m: Vec3,
    pub texture_uv: [f32; 2],
    pub texture_layer: u32,
}

pub(super) struct CharacterPopulation {
    pub vertices: Vec<CharacterVertex>,
    pub animation_frames: Vec<Vec<Vec3>>,
    pub texture_rgba8: Vec<u8>,
    pub texture_layer_count: u32,
    pub actor_count: usize,
    pub distinct_mesh_count: usize,
    pub total_face_count: usize,
    pub inspected_actor_count: u32,
    pub excluded_actor_count: u32,
    pub unresolved_class_count: u32,
    pub staged_actor_count: usize,
    pub actor_visuals: Vec<CharacterVisual>,
}

#[derive(Clone, Debug)]
pub(super) struct CharacterVisual {
    pub actor_reference: i32,
    pub object_name: String,
    pub qualified_class_name: String,
    pub vertex_start: usize,
    pub vertex_end: usize,
    pub bounds_min_m: Vec3,
    pub bounds_max_m: Vec3,
    pub staged: bool,
}

#[derive(Clone, Copy)]
#[cfg_attr(not(feature = "gesture-projection"), allow(dead_code))]
pub(super) struct CharacterStage {
    pub player_start_position_m: Vec3,
    pub player_start_yaw_radians: f32,
    pub floor_drop_meters: f32,
}

#[cfg(feature = "gesture-projection")]
#[derive(Clone)]
struct CharacterActor {
    actor_reference: i32,
    actor_slot_index: u32,
    object_name: String,
    qualified_class_name: String,
    position_m: Vec3,
    rotation_units: [i32; 3],
    draw_scale: f32,
    mesh_package: PathBuf,
    mesh_reference: i32,
    mesh_object_path: String,
}

#[cfg(feature = "gesture-projection")]
struct LoadedMesh {
    preview: NpcPreview,
    texture_layer_base: u32,
}

impl CharacterPopulation {
    #[cfg(not(feature = "gesture-projection"))]
    pub(super) fn load(
        _data_root: &Path,
        _map_package: &Path,
        _meters_per_unreal_unit: f32,
        _excluded_actor_reference: i32,
        _character_stage: Option<CharacterStage>,
    ) -> Result<Self, Box<dyn Error>> {
        Err("HP1 character population requires feature 'gesture-projection'".into())
    }

    #[cfg(feature = "gesture-projection")]
    pub(super) fn load(
        data_root: &Path,
        map_package: &Path,
        meters_per_unreal_unit: f32,
        excluded_actor_reference: i32,
        character_stage: Option<CharacterStage>,
    ) -> Result<Self, Box<dyn Error>> {
        if !meters_per_unreal_unit.is_finite()
            || meters_per_unreal_unit <= 0.0
            || excluded_actor_reference < 0
        {
            return Err("HP1 character population arguments are invalid".into());
        }
        let data_root = path_c_string(data_root)?;
        let map_package = path_c_string(map_package)?;
        let mut ffi_actors = vec![FfiCharacterActor::default(); MAX_ACTORS];
        let mut report = FfiCharacterManifestReport::default();
        // SAFETY: both UTF-8 paths are null terminated, the actor allocation
        // exactly matches the advertised capacity, and the report is repr(C).
        let returned_status = unsafe {
            hpvr_hp1_load_character_manifest_utf8(
                data_root.as_ptr(),
                map_package.as_ptr(),
                meters_per_unreal_unit,
                excluded_actor_reference,
                ffi_actors.as_mut_ptr(),
                ffi_actors.len() as u32,
                &mut report,
            )
        };
        if returned_status != report.status || report.abi_version != ABI_VERSION {
            return Err("HP1 character manifest ABI status/version mismatch".into());
        }
        if returned_status != STATUS_OK {
            return Err(format!(
                "HP1 character manifest failed (status={returned_status}): {}",
                ffi_text(&report.error, "character manifest error")?
            )
            .into());
        }
        if report.required_actor_count != report.written_actor_count
            || report.written_actor_count as usize > ffi_actors.len()
        {
            return Err("HP1 character manifest returned inconsistent counts".into());
        }
        ffi_actors.truncate(report.written_actor_count as usize);
        let mut actors = Vec::with_capacity(ffi_actors.len());
        for actor in ffi_actors {
            let position_m = Vec3::from_array(actor.position_m);
            if actor.actor_reference <= 0
                || actor.mesh_reference <= 0
                || !position_m.is_finite()
                || !actor.draw_scale.is_finite()
                || actor.draw_scale <= 0.0
                || actor.rotation_units[0] != 0
                || actor.rotation_units[2] != 0
            {
                return Err("HP1 character manifest contains an invalid actor".into());
            }
            actors.push(CharacterActor {
                actor_reference: actor.actor_reference,
                actor_slot_index: actor.actor_slot_index,
                object_name: ffi_text(&actor.object_name, "character object name")?,
                qualified_class_name: ffi_text(
                    &actor.qualified_class_name,
                    "character class name",
                )?,
                position_m,
                rotation_units: actor.rotation_units,
                draw_scale: actor.draw_scale,
                mesh_package: PathBuf::from(ffi_text(
                    &actor.mesh_package_utf8,
                    "character mesh package",
                )?),
                mesh_reference: actor.mesh_reference,
                mesh_object_path: ffi_text(&actor.mesh_object_path, "character mesh object")?,
            });
        }

        let mut meshes = BTreeMap::<(PathBuf, i32), LoadedMesh>::new();
        let mut texture_rgba8 = Vec::new();
        let mut texture_layer_count = 0_u32;
        let mut total_face_count = 0_usize;
        for actor in &actors {
            let key = (actor.mesh_package.clone(), actor.mesh_reference);
            if meshes.contains_key(&key) {
                continue;
            }
            let preview = NpcPreview::load_package(
                &actor.mesh_package,
                actor.mesh_reference,
                meters_per_unreal_unit,
            )?;
            let next_layers = texture_layer_count
                .checked_add(preview.texture_layer_count)
                .ok_or("HP1 character texture layer count overflowed")?;
            if next_layers > MAX_TEXTURE_LAYERS {
                return Err("HP1 character textures exceed the bounded layer count".into());
            }
            texture_rgba8.extend_from_slice(&preview.texture_rgba8);
            total_face_count = total_face_count
                .checked_add(preview.face_count)
                .ok_or("HP1 character face count overflowed")?;
            println!(
                "[hp1.characters.mesh] package={} mesh_ref={} mesh={} faces={} vertices={} layers={} layer_base={}",
                actor.mesh_package.display(),
                actor.mesh_reference,
                actor.mesh_object_path,
                preview.face_count,
                preview.vertices.len(),
                preview.texture_layer_count,
                texture_layer_count,
            );
            meshes.insert(
                key,
                LoadedMesh {
                    preview,
                    texture_layer_base: texture_layer_count,
                },
            );
            texture_layer_count = next_layers;
        }

        let estimated_vertices = actors.iter().try_fold(0_usize, |total, actor| {
            let mesh = &meshes[&(actor.mesh_package.clone(), actor.mesh_reference)];
            total
                .checked_add(mesh.preview.vertices.len())
                .ok_or("HP1 character vertex count overflowed")
        })?;
        let mut vertices = Vec::with_capacity(estimated_vertices);
        const ANIMATION_FRAME_COUNT: usize = 16;
        if meshes
            .values()
            .any(|mesh| mesh.preview.animation_frames.len() != ANIMATION_FRAME_COUNT)
        {
            return Err("HP1 character meshes disagree on animation frame count".into());
        }
        let mut animation_frames = (0..ANIMATION_FRAME_COUNT)
            .map(|_| Vec::with_capacity(estimated_vertices))
            .collect::<Vec<_>>();
        let mut staged_roles = BTreeSet::new();
        let mut staged_actor_count = 0_usize;
        let mut actor_visuals = Vec::with_capacity(actors.len());
        for actor in &actors {
            let mesh = &meshes[&(actor.mesh_package.clone(), actor.mesh_reference)];
            let yaw = actor.rotation_units[1] as f32 * std::f32::consts::TAU / 65_536.0;
            let rotation = Quat::from_rotation_y(yaw);
            let staged_offset = character_stage.and_then(|stage| {
                character_stage_offset(&actor.qualified_class_name, &mut staged_roles).map(
                    |offset| {
                        stage.player_start_position_m
                            + Quat::from_rotation_y(stage.player_start_yaw_radians).inverse()
                                * Vec3::new(offset.x, -stage.floor_drop_meters, offset.z)
                    },
                )
            });
            let actor_position = staged_offset.unwrap_or(actor.position_m);
            if staged_offset.is_some() {
                staged_actor_count += 1;
            }
            let actor_vertex_start = vertices.len();
            vertices.extend(mesh.preview.vertices.iter().map(|source| {
                let bind_local =
                    (source.position_m - mesh.preview.diagnostic_translation) * actor.draw_scale;
                CharacterVertex {
                    position_m: actor_position + rotation * bind_local,
                    texture_uv: source.texture_uv,
                    texture_layer: mesh.texture_layer_base + source.material_index,
                }
            }));
            for (frame_index, target) in animation_frames.iter_mut().enumerate() {
                let points = &mesh.preview.animation_frames[frame_index];
                target.extend(mesh.preview.vertices.iter().map(|source| {
                    actor_position + rotation * (points[source.point_index] * actor.draw_scale)
                }));
            }
            let actor_vertex_end = vertices.len();
            let (bind_min, bind_max) = point_bounds(
                vertices[actor_vertex_start..actor_vertex_end]
                    .iter()
                    .map(|vertex| vertex.position_m),
            )
            .ok_or("HP1 character bind bounds are empty")?;
            let (frame0_min, frame0_max) = point_bounds(
                animation_frames[0][actor_vertex_start..actor_vertex_end]
                    .iter()
                    .copied(),
            )
            .ok_or("HP1 character animation bounds are empty")?;
            actor_visuals.push(CharacterVisual {
                actor_reference: actor.actor_reference,
                object_name: actor.object_name.clone(),
                qualified_class_name: actor.qualified_class_name.clone(),
                vertex_start: actor_vertex_start,
                vertex_end: actor_vertex_end,
                bounds_min_m: bind_min.min(frame0_min),
                bounds_max_m: bind_max.max(frame0_max),
                staged: staged_offset.is_some(),
            });
            println!(
                "[hp1.characters.actor] actor_ref={} slot={} class={} object={} mesh={} position_m={:?} staged={} yaw_units={} draw_scale={} idle={} idle_seconds={} bind_min={:?} bind_max={:?} bind_height_m={:.3} frame0_min={:?} frame0_max={:?} frame0_height_m={:.3}",
                actor.actor_reference,
                actor.actor_slot_index,
                actor.qualified_class_name,
                actor.object_name,
                actor.mesh_object_path,
                actor_position,
                staged_offset.is_some(),
                actor.rotation_units[1],
                actor.draw_scale,
                mesh.preview.animation_sequence,
                mesh.preview.animation_duration_seconds,
                bind_min,
                bind_max,
                bind_max.y - bind_min.y,
                frame0_min,
                frame0_max,
                frame0_max.y - frame0_min.y,
            );
        }
        if vertices.iter().any(|vertex| {
            !vertex.position_m.is_finite()
                || vertex.texture_uv.iter().any(|value| !value.is_finite())
                || vertex.texture_layer >= texture_layer_count
        }) {
            return Err("HP1 character population produced invalid geometry".into());
        }
        if animation_frames.iter().any(|frame| {
            frame.len() != vertices.len() || frame.iter().any(|point| !point.is_finite())
        }) {
            return Err("HP1 character population produced invalid animation frames".into());
        }
        Ok(Self {
            vertices,
            animation_frames,
            texture_rgba8,
            texture_layer_count,
            actor_count: actors.len(),
            distinct_mesh_count: meshes.len(),
            total_face_count,
            inspected_actor_count: report.inspected_actor_count,
            excluded_actor_count: report.excluded_actor_count,
            unresolved_class_count: report.unresolved_class_count,
            staged_actor_count,
            actor_visuals,
        })
    }
}

#[cfg(feature = "gesture-projection")]
fn character_stage_offset(
    class_name: &str,
    staged_roles: &mut BTreeSet<&'static str>,
) -> Option<Vec3> {
    let (role, offset) = match class_name {
        "Tut1.Tut1Dumbledore" => ("dumbledore", Vec3::new(0.0, 0.0, -7.0)),
        "Tut1.Tut1McGonagall" => ("mcgonagall", Vec3::new(-3.0, 0.0, -8.5)),
        "Tut1.Tut1Quirrell" => ("quirrell", Vec3::new(3.0, 0.0, -8.5)),
        "Tut1.Tut1Hermione" => ("hermione", Vec3::new(2.0, 0.0, -5.0)),
        "Tut1.Tut1Ron" => ("ron", Vec3::new(-2.0, 0.0, -5.0)),
        "Tut1.Tut1Fred" => ("fred", Vec3::new(-4.0, 0.0, -6.0)),
        "Tut1.Tut1George" => ("george", Vec3::new(4.0, 0.0, -6.0)),
        _ => return None,
    };
    staged_roles.insert(role).then_some(offset)
}

#[cfg(feature = "gesture-projection")]
fn point_bounds(points: impl IntoIterator<Item = Vec3>) -> Option<(Vec3, Vec3)> {
    let mut points = points.into_iter();
    let first = points.next()?;
    let mut minimum = first;
    let mut maximum = first;
    for point in points {
        minimum = minimum.min(point);
        maximum = maximum.max(point);
    }
    Some((minimum, maximum))
}

#[cfg(feature = "gesture-projection")]
fn path_c_string(path: &Path) -> Result<CString, Box<dyn Error>> {
    let text = path
        .to_str()
        .ok_or_else(|| format!("path is not valid Unicode: {}", path.display()))?;
    Ok(CString::new(text.as_bytes())?)
}

#[cfg(feature = "gesture-projection")]
fn ffi_text<const N: usize>(bytes: &[u8; N], field: &str) -> Result<String, Box<dyn Error>> {
    if !bytes.contains(&0) {
        return Err(format!("native HP1 {field} is not null terminated").into());
    }
    // SAFETY: the checked fixed array contains a terminator and remains live.
    let value = unsafe { CStr::from_ptr(bytes.as_ptr().cast()) };
    Ok(value
        .to_str()
        .map_err(|_| format!("native HP1 {field} is not UTF-8"))?
        .to_owned())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ffi_layout_matches_character_manifest_c_header() {
        assert_eq!(std::mem::size_of::<FfiCharacterActor>(), 940);
        assert_eq!(std::mem::align_of::<FfiCharacterActor>(), 4);
        assert_eq!(std::mem::offset_of!(FfiCharacterActor, position_m), 16);
        assert_eq!(std::mem::offset_of!(FfiCharacterActor, object_name), 44);
        assert_eq!(
            std::mem::offset_of!(FfiCharacterActor, mesh_package_utf8),
            300
        );
        assert_eq!(std::mem::size_of::<FfiCharacterManifestReport>(), 300);
        assert_eq!(std::mem::align_of::<FfiCharacterManifestReport>(), 4);
        assert_eq!(std::mem::offset_of!(FfiCharacterManifestReport, error), 44);
    }
}
