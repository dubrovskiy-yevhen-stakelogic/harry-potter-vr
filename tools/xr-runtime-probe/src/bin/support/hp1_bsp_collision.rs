use glam::{Quat, Vec2, Vec3, Vec3Swizzles};

use super::hp1_bsp_ffi::MapSlice;

const POLY_NOT_SOLID: u32 = 0x0000_0008;
const WALKABLE_NORMAL_Y: f32 = 0.64;
const MAX_STEP_UP_METERS: f32 = 0.32;
const MAX_STEP_DOWN_METERS: f32 = 0.45;
const MAX_SUBSTEP_METERS: f32 = 0.08;
const CONTACT_EPSILON_METERS: f32 = 0.006;

#[derive(Clone, Copy, Debug, Default)]
pub(super) struct CollisionMove {
    pub displacement: Vec3,
    pub blocked_substeps: u32,
    pub grounded_substeps: u32,
}

#[derive(Clone, Copy, Debug)]
struct Triangle {
    vertices: [Vec3; 3],
    minimum: Vec3,
    maximum: Vec3,
    normal: Vec3,
}

pub(super) struct BspCollision {
    triangles: Vec<Triangle>,
    capsule_radius_meters: f32,
    capsule_half_height_meters: f32,
}

impl BspCollision {
    pub(super) fn new(
        slice: &MapSlice,
        map_rotation: Quat,
        map_translation: Vec3,
        capsule_radius_meters: f32,
        capsule_half_height_meters: f32,
    ) -> Result<Self, String> {
        if !capsule_radius_meters.is_finite()
            || !capsule_half_height_meters.is_finite()
            || capsule_radius_meters <= 0.0
            || capsule_half_height_meters <= capsule_radius_meters
            || slice.vertices.len() % 3 != 0
        {
            return Err("invalid HP1 BSP collision input".to_owned());
        }

        let mut triangles = Vec::with_capacity(slice.vertices.len() / 3);
        let mut not_solid = 0_usize;
        let mut degenerate = 0_usize;
        for source in slice.vertices.chunks_exact(3) {
            if source[0].polygon_flags & POLY_NOT_SOLID != 0 {
                not_solid += 1;
                continue;
            }
            let vertices =
                [0, 1, 2].map(|index| map_rotation * source[index].position_m + map_translation);
            let cross = (vertices[1] - vertices[0]).cross(vertices[2] - vertices[0]);
            let Some(normal) = cross.try_normalize() else {
                degenerate += 1;
                continue;
            };
            let minimum = vertices[0].min(vertices[1]).min(vertices[2]);
            let maximum = vertices[0].max(vertices[1]).max(vertices[2]);
            triangles.push(Triangle {
                vertices,
                minimum,
                maximum,
                normal,
            });
        }
        if triangles.is_empty() {
            return Err("HP1 BSP collision has no solid triangles".to_owned());
        }
        println!(
            "[hp1.collision.load] solid_triangles={} not_solid_triangles={} degenerate_triangles={} radius_m={capsule_radius_meters:.3} half_height_m={capsule_half_height_meters:.3} max_step_up_m={MAX_STEP_UP_METERS:.2} max_step_down_m={MAX_STEP_DOWN_METERS:.2}",
            triangles.len(),
            not_solid,
            degenerate,
        );
        Ok(Self {
            triangles,
            capsule_radius_meters,
            capsule_half_height_meters,
        })
    }

    pub(super) fn resolve_movement(
        &self,
        capsule_center: Vec3,
        requested_displacement: Vec3,
    ) -> CollisionMove {
        let horizontal = Vec3::new(requested_displacement.x, 0.0, requested_displacement.z);
        let length = horizontal.length();
        if length <= f32::EPSILON {
            return CollisionMove::default();
        }
        let substep_count = (length / MAX_SUBSTEP_METERS).ceil().max(1.0) as u32;
        let substep = horizontal / substep_count as f32;
        let mut position = capsule_center;
        let mut blocked_substeps = 0_u32;
        let mut grounded_substeps = 0_u32;

        for _ in 0..substep_count {
            if let Some((candidate, grounded)) = self.try_position(position, substep) {
                position = candidate;
                grounded_substeps += u32::from(grounded);
                continue;
            }

            blocked_substeps += 1;
            let mut moved = false;
            for slide in [
                Vec3::new(substep.x, 0.0, 0.0),
                Vec3::new(0.0, 0.0, substep.z),
            ] {
                if slide.length_squared() <= f32::EPSILON {
                    continue;
                }
                if let Some((candidate, grounded)) = self.try_position(position, slide) {
                    position = candidate;
                    grounded_substeps += u32::from(grounded);
                    moved = true;
                }
            }
            if !moved {
                continue;
            }
        }

        CollisionMove {
            displacement: position - capsule_center,
            blocked_substeps,
            grounded_substeps,
        }
    }

    pub(super) fn ground_adjustment(&self, capsule_center: Vec3) -> Option<f32> {
        let current_foot = capsule_center.y - self.capsule_half_height_meters;
        self.ground_height(capsule_center.xz(), current_foot)
            .map(|ground| ground - current_foot)
    }

    pub(super) fn raycast_distance(
        &self,
        origin: Vec3,
        direction: Vec3,
        maximum_distance: f32,
    ) -> Option<f32> {
        if !origin.is_finite()
            || !direction.is_finite()
            || !maximum_distance.is_finite()
            || maximum_distance <= 0.0
        {
            return None;
        }
        let direction = direction.try_normalize()?;
        self.triangles
            .iter()
            .filter_map(|triangle| ray_triangle_distance(origin, direction, triangle.vertices))
            .filter(|distance| *distance <= maximum_distance)
            .reduce(f32::min)
    }

    fn try_position(&self, current: Vec3, horizontal_step: Vec3) -> Option<(Vec3, bool)> {
        let mut candidate = current + horizontal_step;
        let current_foot = current.y - self.capsule_half_height_meters;
        let ground = self.ground_height(candidate.xz(), current_foot);
        if let Some(ground_height) = ground {
            candidate.y = ground_height + self.capsule_half_height_meters;
        }
        if self.overlaps_wall(candidate) {
            None
        } else {
            Some((candidate, ground.is_some()))
        }
    }

    fn ground_height(&self, horizontal: Vec2, current_foot: f32) -> Option<f32> {
        let minimum_y = current_foot - MAX_STEP_DOWN_METERS;
        let maximum_y = current_foot + MAX_STEP_UP_METERS;
        let mut best: Option<f32> = None;
        for triangle in &self.triangles {
            if triangle.normal.y.abs() < WALKABLE_NORMAL_Y
                || horizontal.x < triangle.minimum.x - CONTACT_EPSILON_METERS
                || horizontal.x > triangle.maximum.x + CONTACT_EPSILON_METERS
                || horizontal.y < triangle.minimum.z - CONTACT_EPSILON_METERS
                || horizontal.y > triangle.maximum.z + CONTACT_EPSILON_METERS
            {
                continue;
            }
            let Some(height) = triangle_height_at_xz(triangle.vertices, horizontal) else {
                continue;
            };
            if height < minimum_y || height > maximum_y {
                continue;
            }
            best = Some(best.map_or(height, |existing| existing.max(height)));
        }
        best
    }

    fn overlaps_wall(&self, center: Vec3) -> bool {
        let radius = self.capsule_radius_meters;
        let segment_half = self.capsule_half_height_meters - radius;
        let samples = [
            center - Vec3::Y * segment_half,
            center,
            center + Vec3::Y * segment_half,
        ];
        let radius_squared = (radius - CONTACT_EPSILON_METERS).powi(2);
        self.triangles.iter().any(|triangle| {
            if triangle.normal.y.abs() >= WALKABLE_NORMAL_Y
                || center.x + radius < triangle.minimum.x
                || center.x - radius > triangle.maximum.x
                || center.y + self.capsule_half_height_meters < triangle.minimum.y
                || center.y - self.capsule_half_height_meters > triangle.maximum.y
                || center.z + radius < triangle.minimum.z
                || center.z - radius > triangle.maximum.z
            {
                return false;
            }
            samples.iter().any(|sample| {
                let closest = closest_point_on_triangle(*sample, triangle.vertices);
                sample.distance_squared(closest) < radius_squared
            })
        })
    }

    #[cfg(test)]
    fn from_test_triangles(
        triangles: impl IntoIterator<Item = [Vec3; 3]>,
        radius: f32,
        half_height: f32,
    ) -> Self {
        let triangles = triangles
            .into_iter()
            .map(|vertices| {
                let normal = (vertices[1] - vertices[0])
                    .cross(vertices[2] - vertices[0])
                    .normalize();
                Triangle {
                    minimum: vertices[0].min(vertices[1]).min(vertices[2]),
                    maximum: vertices[0].max(vertices[1]).max(vertices[2]),
                    vertices,
                    normal,
                }
            })
            .collect();
        Self {
            triangles,
            capsule_radius_meters: radius,
            capsule_half_height_meters: half_height,
        }
    }
}

fn triangle_height_at_xz(vertices: [Vec3; 3], point: Vec2) -> Option<f32> {
    let a = vertices[0];
    let b = vertices[1];
    let c = vertices[2];
    let denominator = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
    if denominator.abs() <= 1.0e-7 {
        return None;
    }
    let u = ((b.z - c.z) * (point.x - c.x) + (c.x - b.x) * (point.y - c.z)) / denominator;
    let v = ((c.z - a.z) * (point.x - c.x) + (a.x - c.x) * (point.y - c.z)) / denominator;
    let w = 1.0 - u - v;
    const BARYCENTRIC_EPSILON: f32 = 1.0e-4;
    if u < -BARYCENTRIC_EPSILON || v < -BARYCENTRIC_EPSILON || w < -BARYCENTRIC_EPSILON {
        return None;
    }
    Some(u * a.y + v * b.y + w * c.y)
}

fn closest_point_on_triangle(point: Vec3, vertices: [Vec3; 3]) -> Vec3 {
    let [a, b, c] = vertices;
    let ab = b - a;
    let ac = c - a;
    let ap = point - a;
    let d1 = ab.dot(ap);
    let d2 = ac.dot(ap);
    if d1 <= 0.0 && d2 <= 0.0 {
        return a;
    }

    let bp = point - b;
    let d3 = ab.dot(bp);
    let d4 = ac.dot(bp);
    if d3 >= 0.0 && d4 <= d3 {
        return b;
    }

    let vc = d1 * d4 - d3 * d2;
    if vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0 {
        let amount = d1 / (d1 - d3);
        return a + amount * ab;
    }

    let cp = point - c;
    let d5 = ab.dot(cp);
    let d6 = ac.dot(cp);
    if d6 >= 0.0 && d5 <= d6 {
        return c;
    }

    let vb = d5 * d2 - d1 * d6;
    if vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0 {
        let amount = d2 / (d2 - d6);
        return a + amount * ac;
    }

    let va = d3 * d6 - d5 * d4;
    if va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0 {
        let amount = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + amount * (c - b);
    }

    let denominator = 1.0 / (va + vb + vc);
    let v = vb * denominator;
    let w = vc * denominator;
    a + ab * v + ac * w
}

fn ray_triangle_distance(origin: Vec3, direction: Vec3, vertices: [Vec3; 3]) -> Option<f32> {
    let [a, b, c] = vertices;
    let edge1 = b - a;
    let edge2 = c - a;
    let perpendicular = direction.cross(edge2);
    let determinant = edge1.dot(perpendicular);
    if determinant.abs() <= 1.0e-7 {
        return None;
    }
    let inverse = determinant.recip();
    let offset = origin - a;
    let u = offset.dot(perpendicular) * inverse;
    if !(0.0..=1.0).contains(&u) {
        return None;
    }
    let cross = offset.cross(edge1);
    let v = direction.dot(cross) * inverse;
    if v < 0.0 || u + v > 1.0 {
        return None;
    }
    let distance = edge2.dot(cross) * inverse;
    (distance >= 0.0).then_some(distance)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn floor() -> [[Vec3; 3]; 2] {
        [
            [
                Vec3::new(-10.0, 0.0, -10.0),
                Vec3::new(10.0, 0.0, -10.0),
                Vec3::new(10.0, 0.0, 10.0),
            ],
            [
                Vec3::new(-10.0, 0.0, -10.0),
                Vec3::new(10.0, 0.0, 10.0),
                Vec3::new(-10.0, 0.0, 10.0),
            ],
        ]
    }

    #[test]
    fn floor_height_is_winding_independent() {
        let triangle = floor()[0];
        assert_eq!(triangle_height_at_xz(triangle, Vec2::ZERO), Some(0.0));
        let reversed = [triangle[2], triangle[1], triangle[0]];
        assert_eq!(triangle_height_at_xz(reversed, Vec2::ZERO), Some(0.0));
    }

    #[test]
    fn capsule_follows_floor_and_stops_before_wall() {
        let mut triangles = floor().to_vec();
        triangles.extend([
            [
                Vec3::new(1.0, 0.0, -2.0),
                Vec3::new(1.0, 3.0, -2.0),
                Vec3::new(1.0, 3.0, 2.0),
            ],
            [
                Vec3::new(1.0, 0.0, -2.0),
                Vec3::new(1.0, 3.0, 2.0),
                Vec3::new(1.0, 0.0, 2.0),
            ],
        ]);
        let collision = BspCollision::from_test_triangles(triangles, 0.3, 0.84);
        let movement =
            collision.resolve_movement(Vec3::new(0.0, 0.84, 0.0), Vec3::new(2.0, 0.0, 0.0));
        assert!(movement.displacement.x > 0.5);
        assert!(movement.displacement.x < 0.71);
        assert_eq!(movement.displacement.y, 0.0);
        assert!(movement.blocked_substeps > 0);
        assert!(movement.grounded_substeps > 0);
    }

    #[test]
    fn capsule_climbs_a_bounded_step() {
        let collision = BspCollision::from_test_triangles(
            [[
                Vec3::new(-2.0, 0.2, -2.0),
                Vec3::new(2.0, 0.2, -2.0),
                Vec3::new(0.0, 0.2, 2.0),
            ]],
            0.3,
            0.84,
        );
        let movement =
            collision.resolve_movement(Vec3::new(0.0, 0.84, 0.0), Vec3::new(0.1, 0.0, 0.0));
        assert!((movement.displacement.y - 0.2).abs() < 1.0e-6);
    }

    #[test]
    fn raycast_returns_nearest_solid_triangle() {
        let collision = BspCollision::from_test_triangles(
            [[
                Vec3::new(-1.0, -1.0, -3.0),
                Vec3::new(1.0, -1.0, -3.0),
                Vec3::new(0.0, 1.0, -3.0),
            ]],
            0.3,
            0.84,
        );
        let distance = collision
            .raycast_distance(Vec3::ZERO, Vec3::NEG_Z, 10.0)
            .expect("wall hit");
        assert!((distance - 3.0).abs() < 1.0e-6);
        assert!(
            collision
                .raycast_distance(Vec3::ZERO, Vec3::X, 10.0)
                .is_none()
        );
    }
}
