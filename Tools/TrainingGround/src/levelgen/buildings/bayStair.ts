// Switchback for the fixed 1-464 section, in the same centimetre frame as bayPlan.ts.
import type { BayPlan } from './bayPlan.ts';
import { BAY_SECTIONS, type KitStairGeom } from './kitPlan.ts';

export function bayStairGeom(plan: BayPlan): KitStairGeom {
  const { walls_cm: walls, core } = BAY_SECTIONS;
  const uIn: [number, number] = [plan.stair.u0 + walls.bearing / 2, plan.stair.u1 - walls.bearing / 2];
  const uLaneWall = (uIn[0] + uIn[1]) / 2;
  const laneOffset = (core.flight_clear_cm + walls.partition) / 2;
  const run = Math.ceil((BAY_SECTIONS.storey_cm / 2) / Math.tan(core.slope_deg * Math.PI / 180));
  const vLanding = plan.landing.v0;
  return {
    lane: [uLaneWall - laneOffset, uLaneWall + laneOffset],
    uIn,
    uLaneWall,
    vLanding,
    vMid: vLanding - run,
    vFar: walls.exterior / 2,
    run,
  };
}
