use std::{error::Error, ops::Range};

use glam::{Quat, Vec3};

use super::{
    gesture_projection::{SpellCastEvent, SpellKind},
    hp1_bsp_collision::BspCollision,
    hp1_character_population::CharacterPopulation,
};

const MAX_TARGET_DISTANCE_METERS: f32 = 18.0;
const MIN_TARGET_DISTANCE_METERS: f32 = 0.15;
const BSP_OCCLUSION_EPSILON_METERS: f32 = 0.03;
const REACTION_DURATION_SECONDS: f32 = 1.20;
const REACTION_PUSH_METERS: f32 = 0.65;
const REACTION_LIFT_METERS: f32 = 0.24;
const SPELL_BEAM_SECONDS: f32 = 0.32;

#[derive(Clone, Copy, Debug)]
struct Reaction {
    elapsed_seconds: f32,
    direction: Vec3,
}

#[derive(Clone, Debug)]
struct Target {
    actor_reference: i32,
    object_name: String,
    qualified_class_name: String,
    vertices: Range<usize>,
    bounds_min: Vec3,
    bounds_max: Vec3,
    staged: bool,
    reaction: Option<Reaction>,
}

#[derive(Default)]
struct Telemetry {
    events_received: u32,
    hits: u32,
    misses: u32,
    bsp_occlusions: u32,
    reactions_completed: u32,
    duplicate_or_reordered: u32,
}

pub(super) struct NpcSpellInteraction {
    targets: Vec<Target>,
    last_event_serial: Option<u64>,
    beam_start: Vec3,
    beam_end: Vec3,
    beam_remaining_seconds: f32,
    beam_hit: bool,
    telemetry: Telemetry,
}

impl NpcSpellInteraction {
    pub(super) fn new(
        population: &CharacterPopulation,
        map_rotation: Quat,
        map_translation: Vec3,
    ) -> Result<Self, Box<dyn Error>> {
        let targets = population
            .actor_visuals
            .iter()
            .map(|actor| {
                let (bounds_min, bounds_max) = transform_bounds(
                    actor.bounds_min_m,
                    actor.bounds_max_m,
                    map_rotation,
                    map_translation,
                );
                if actor.vertex_start >= actor.vertex_end
                    || actor.vertex_end > population.vertices.len()
                    || !bounds_min.is_finite()
                    || !bounds_max.is_finite()
                    || bounds_min.cmpgt(bounds_max).any()
                {
                    return Err("HP1 NPC interaction target has invalid bounds or vertex range");
                }
                Ok(Target {
                    actor_reference: actor.actor_reference,
                    object_name: actor.object_name.clone(),
                    qualified_class_name: actor.qualified_class_name.clone(),
                    vertices: actor.vertex_start..actor.vertex_end,
                    bounds_min,
                    bounds_max,
                    staged: actor.staged,
                    reaction: None,
                })
            })
            .collect::<Result<Vec<_>, _>>()?;
        if targets.is_empty() {
            return Err("HP1 NPC interaction requires at least one target".into());
        }
        println!(
            "[hp1.npc.spell] targets={} staged_targets={} max_distance_m={MAX_TARGET_DISTANCE_METERS:.1} reaction_seconds={REACTION_DURATION_SECONDS:.2} status=READY",
            targets.len(),
            targets.iter().filter(|target| target.staged).count(),
        );
        Ok(Self {
            targets,
            last_event_serial: None,
            beam_start: Vec3::ZERO,
            beam_end: Vec3::ZERO,
            beam_remaining_seconds: 0.0,
            beam_hit: false,
            telemetry: Telemetry::default(),
        })
    }

    pub(super) fn reset_visuals(&mut self) {
        for target in &mut self.targets {
            target.reaction = None;
        }
        self.beam_remaining_seconds = 0.0;
    }

    pub(super) fn offline_validate(
        &self,
        bsp_collision: &BspCollision,
        aim_origin: Vec3,
    ) -> Result<(), Box<dyn Error>> {
        let (expected_index, expected) = self
            .targets
            .iter()
            .enumerate()
            .find(|(_, target)| target.staged)
            .ok_or("NPC spell validation requires a staged target")?;
        let aim_point = (expected.bounds_min + expected.bounds_max) * 0.5;
        let direction = (aim_point - aim_origin)
            .try_normalize()
            .ok_or("NPC spell validation aim is degenerate")?;
        let (selected_index, target_distance) =
            nearest_target(&self.targets, aim_origin, direction)
                .ok_or("NPC spell validation ray missed all targets")?;
        if selected_index != expected_index {
            return Err("NPC spell validation selected an unexpected nearer target".into());
        }
        let bsp_distance =
            bsp_collision.raycast_distance(aim_origin, direction, MAX_TARGET_DISTANCE_METERS);
        if bsp_distance.is_some_and(|wall| wall + BSP_OCCLUSION_EPSILON_METERS < target_distance) {
            return Err("NPC spell validation target is occluded by BSP".into());
        }
        println!(
            "[hp1.npc.spell.validate] actor_ref={} class={} object={} target_distance_m={target_distance:.3} bsp_distance_m={bsp_distance:?} PASS",
            expected.actor_reference, expected.qualified_class_name, expected.object_name,
        );
        Ok(())
    }

    pub(super) fn consume(
        &mut self,
        event: SpellCastEvent,
        bsp_collision: Option<&BspCollision>,
    ) -> Result<(), Box<dyn Error>> {
        if self
            .last_event_serial
            .is_some_and(|last| event.serial <= last)
        {
            self.telemetry.duplicate_or_reordered += 1;
            return Err("NPC spell interaction received duplicate/reordered event".into());
        }
        if event.spell != SpellKind::Flipendo
            || !event.tip.is_finite()
            || !event.aim_origin.is_finite()
            || !event.aim_direction.is_finite()
            || (event.aim_direction.length() - 1.0).abs() > 1.0e-3
            || !event.score.is_finite()
            || !event.threshold.is_finite()
            || event.score < event.threshold
        {
            return Err("NPC spell interaction rejected an invalid accepted-spell event".into());
        }

        self.telemetry.events_received += 1;
        self.last_event_serial = Some(event.serial);
        let direction = event.aim_direction.normalize();
        let selected = nearest_target(&self.targets, event.aim_origin, direction);
        let target_distance = selected.map(|(_, distance)| distance);
        let bsp_distance = bsp_collision.and_then(|collision| {
            collision.raycast_distance(event.aim_origin, direction, MAX_TARGET_DISTANCE_METERS)
        });
        let occluded = matches!((target_distance, bsp_distance), (Some(target), Some(wall)) if wall + BSP_OCCLUSION_EPSILON_METERS < target);

        self.beam_start = event.tip;
        self.beam_remaining_seconds = SPELL_BEAM_SECONDS;
        if let Some((target_index, distance)) = selected.filter(|_| !occluded) {
            let target = &mut self.targets[target_index];
            let push_direction = Vec3::new(direction.x, 0.0, direction.z)
                .try_normalize()
                .unwrap_or(Vec3::NEG_Z);
            target.reaction = Some(Reaction {
                elapsed_seconds: 0.0,
                direction: push_direction,
            });
            self.beam_end = event.aim_origin + direction * distance;
            self.beam_hit = true;
            self.telemetry.hits += 1;
            println!(
                "[hp1.npc.hit] serial={} spell={} outcome=HIT actor_ref={} class={} object={} staged={} distance_m={distance:.3} score={:.6}",
                event.serial,
                event.spell.label(),
                target.actor_reference,
                target.qualified_class_name,
                target.object_name,
                target.staged,
                event.score,
            );
        } else {
            let endpoint_distance = bsp_distance.unwrap_or(MAX_TARGET_DISTANCE_METERS);
            self.beam_end = event.aim_origin + direction * endpoint_distance;
            self.beam_hit = false;
            self.telemetry.misses += 1;
            if occluded {
                self.telemetry.bsp_occlusions += 1;
            }
            println!(
                "[hp1.npc.hit] serial={} spell={} outcome={} target_distance_m={:?} bsp_distance_m={:?} score={:.6}",
                event.serial,
                event.spell.label(),
                if occluded { "BLOCKED_BY_BSP" } else { "MISS" },
                target_distance,
                bsp_distance,
                event.score,
            );
        }
        Ok(())
    }

    pub(super) fn advance(&mut self, delta_seconds: f32) {
        self.beam_remaining_seconds =
            (self.beam_remaining_seconds - delta_seconds.max(0.0)).max(0.0);
        for target in &mut self.targets {
            let Some(mut reaction) = target.reaction else {
                continue;
            };
            reaction.elapsed_seconds += delta_seconds.max(0.0);
            if reaction.elapsed_seconds >= REACTION_DURATION_SECONDS {
                target.reaction = None;
                self.telemetry.reactions_completed += 1;
            } else {
                target.reaction = Some(reaction);
            }
        }
    }

    pub(super) fn active_offsets(&self) -> impl Iterator<Item = (Range<usize>, Vec3)> + '_ {
        self.targets.iter().filter_map(|target| {
            target
                .reaction
                .map(|reaction| (target.vertices.clone(), reaction_offset(reaction)))
        })
    }

    pub(super) fn beam(&self) -> Option<(Vec3, Vec3, bool)> {
        (self.beam_remaining_seconds > 0.0).then_some((
            self.beam_start,
            self.beam_end,
            self.beam_hit,
        ))
    }

    pub(super) fn verify_and_report(&self) -> Result<(), Box<dyn Error>> {
        if self.telemetry.events_received != self.telemetry.hits + self.telemetry.misses
            || self.telemetry.bsp_occlusions > self.telemetry.misses
            || self.telemetry.duplicate_or_reordered != 0
        {
            return Err("NPC spell interaction telemetry invariant failed".into());
        }
        println!(
            "[hp1.npc.spell.verify] targets={} events={} hits={} misses={} bsp_occlusions={} reactions_completed={} active_reactions={} last_serial={} PASS",
            self.targets.len(),
            self.telemetry.events_received,
            self.telemetry.hits,
            self.telemetry.misses,
            self.telemetry.bsp_occlusions,
            self.telemetry.reactions_completed,
            self.targets
                .iter()
                .filter(|target| target.reaction.is_some())
                .count(),
            self.last_event_serial.unwrap_or(0),
        );
        Ok(())
    }
}

fn nearest_target(targets: &[Target], origin: Vec3, direction: Vec3) -> Option<(usize, f32)> {
    targets
        .iter()
        .enumerate()
        .filter_map(|(index, target)| {
            let offset = target.reaction.map(reaction_offset).unwrap_or(Vec3::ZERO);
            ray_aabb_distance(
                origin,
                direction,
                target.bounds_min + offset,
                target.bounds_max + offset,
                MAX_TARGET_DISTANCE_METERS,
            )
            .filter(|distance| *distance >= MIN_TARGET_DISTANCE_METERS)
            .map(|distance| (index, distance))
        })
        .min_by(|left, right| left.1.total_cmp(&right.1))
}

fn reaction_offset(reaction: Reaction) -> Vec3 {
    let phase = (reaction.elapsed_seconds / REACTION_DURATION_SECONDS).clamp(0.0, 1.0);
    let envelope = (std::f32::consts::PI * phase).sin();
    reaction.direction * (REACTION_PUSH_METERS * envelope)
        + Vec3::Y * (REACTION_LIFT_METERS * envelope)
}

fn transform_bounds(
    minimum: Vec3,
    maximum: Vec3,
    rotation: Quat,
    translation: Vec3,
) -> (Vec3, Vec3) {
    let mut transformed_minimum = Vec3::splat(f32::INFINITY);
    let mut transformed_maximum = Vec3::splat(f32::NEG_INFINITY);
    for x in [minimum.x, maximum.x] {
        for y in [minimum.y, maximum.y] {
            for z in [minimum.z, maximum.z] {
                let point = rotation * Vec3::new(x, y, z) + translation;
                transformed_minimum = transformed_minimum.min(point);
                transformed_maximum = transformed_maximum.max(point);
            }
        }
    }
    (transformed_minimum, transformed_maximum)
}

fn ray_aabb_distance(
    origin: Vec3,
    direction: Vec3,
    minimum: Vec3,
    maximum: Vec3,
    maximum_distance: f32,
) -> Option<f32> {
    let mut near = 0.0_f32;
    let mut far = maximum_distance;
    for axis in 0..3 {
        if direction[axis].abs() <= 1.0e-7 {
            if origin[axis] < minimum[axis] || origin[axis] > maximum[axis] {
                return None;
            }
            continue;
        }
        let inverse = direction[axis].recip();
        let mut first = (minimum[axis] - origin[axis]) * inverse;
        let mut second = (maximum[axis] - origin[axis]) * inverse;
        if first > second {
            std::mem::swap(&mut first, &mut second);
        }
        near = near.max(first);
        far = far.min(second);
        if near > far {
            return None;
        }
    }
    (near <= maximum_distance && far >= 0.0).then_some(near.max(0.0))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ray_selects_aabb_and_rejects_parallel_miss() {
        let distance = ray_aabb_distance(
            Vec3::ZERO,
            Vec3::NEG_Z,
            Vec3::new(-0.5, -1.0, -5.0),
            Vec3::new(0.5, 1.0, -4.0),
            18.0,
        )
        .expect("target hit");
        assert!((distance - 4.0).abs() < 1.0e-6);
        assert!(
            ray_aabb_distance(
                Vec3::ZERO,
                Vec3::X,
                Vec3::new(-0.5, -1.0, -5.0),
                Vec3::new(0.5, 1.0, -4.0),
                18.0,
            )
            .is_none()
        );
    }

    #[test]
    fn flipendo_reaction_returns_to_origin() {
        let start = reaction_offset(Reaction {
            elapsed_seconds: 0.0,
            direction: Vec3::NEG_Z,
        });
        let middle = reaction_offset(Reaction {
            elapsed_seconds: REACTION_DURATION_SECONDS * 0.5,
            direction: Vec3::NEG_Z,
        });
        let end = reaction_offset(Reaction {
            elapsed_seconds: REACTION_DURATION_SECONDS,
            direction: Vec3::NEG_Z,
        });
        assert_eq!(start, Vec3::ZERO);
        assert!(middle.y > 0.2 && middle.z < -0.6);
        assert!(end.length() < 1.0e-5);
    }

    #[test]
    fn accepted_flipendo_hits_nearest_actor_once() {
        let mut interaction = NpcSpellInteraction {
            targets: vec![
                Target {
                    actor_reference: 1,
                    object_name: "near".to_owned(),
                    qualified_class_name: "Test.Near".to_owned(),
                    vertices: 0..3,
                    bounds_min: Vec3::new(-0.5, -1.0, -5.0),
                    bounds_max: Vec3::new(0.5, 1.0, -4.0),
                    staged: true,
                    reaction: None,
                },
                Target {
                    actor_reference: 2,
                    object_name: "far".to_owned(),
                    qualified_class_name: "Test.Far".to_owned(),
                    vertices: 3..6,
                    bounds_min: Vec3::new(-0.5, -1.0, -9.0),
                    bounds_max: Vec3::new(0.5, 1.0, -8.0),
                    staged: true,
                    reaction: None,
                },
            ],
            last_event_serial: None,
            beam_start: Vec3::ZERO,
            beam_end: Vec3::ZERO,
            beam_remaining_seconds: 0.0,
            beam_hit: false,
            telemetry: Telemetry::default(),
        };
        interaction
            .consume(
                SpellCastEvent {
                    serial: 1,
                    spell: SpellKind::Flipendo,
                    predicted_display_time_ns: 1,
                    tip: Vec3::ZERO,
                    aim_origin: Vec3::ZERO,
                    aim_direction: Vec3::NEG_Z,
                    score: 1.0,
                    threshold: 0.5,
                },
                None,
            )
            .unwrap();
        assert!(interaction.targets[0].reaction.is_some());
        assert!(interaction.targets[1].reaction.is_none());
        assert_eq!(interaction.telemetry.hits, 1);
        assert!(interaction.beam_hit);
        interaction.verify_and_report().unwrap();
    }
}
