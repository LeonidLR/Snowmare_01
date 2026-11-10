// Training Ground / movement: formation shapes coverage and live validation.
// Docs: docs/port/gemini/inbox/008-formation-editor.md.
//
// Calculates 360-degree sector coverage, blind spots, duplicated looks,
// spacing violations (< 120 cm) and radius bounds (> 4000 cm).
// Also provides formation mirroring and default shape computation.

import { normYaw } from './geometry.ts';
import type { FormationConfig } from './config.ts';
import { FormationShapeRules, FormationSectorRules, rolesFor, type Role } from './rules.ts';
import { CUSTOM_MAX_RADIUS } from './custom.ts';
import {
  type FormationMemberShape,
  type FormationShape,
  type CustomShapeDef,
  type CustomMemberShape,
  type FormationShapesFile,
  type EditableFormation,
  ROLE_LETTER_TO_NAME,
  DEFAULT_ARC_DEG,
} from './shapesSchema.ts';

export const MIN_MEMBER_SPACING_CM = 120; // slot.min_spacing

export interface AngularSpan {
  from: number; // degrees [-180, 180]
  to: number;   // degrees [-180, 180]
}

export interface CoverageReport {
  coveragePercent: number; // 0..100
  uncoveredPercent: number; // 0..100
  coveredSpans: AngularSpan[];
  uncoveredSpans: AngularSpan[];
  overlappingSpans: AngularSpan[];
  degreeCounts: number[]; // 360 elements for degrees -179..180
}

export interface SpacingViolation {
  memberA: number;
  memberB: number;
  roleA?: string;
  roleB?: string;
  distCm: number;
  minSpacingCm: number;
}

export interface RadiusViolation {
  memberIndex: number;
  role?: string;
  radiusCm: number;
  maxRadiusCm: number;
}

export interface LiveValidationResult {
  valid: boolean;
  spacingViolations: SpacingViolation[];
  radiusViolations: RadiusViolation[];
  coverage: CoverageReport;
}

/** Returns the angular difference in degrees (-180, 180] between target angle and look center. */
export function angleDeltaDeg(angle: number, center: number): number {
  return normYaw(angle - center);
}

/** Tests if an angle (in degrees) is covered by a sector defined by look center and arc width. */
export function isAngleCovered(angleDeg: number, lookDeg: number, arcDeg: number): boolean {
  const halfArc = arcDeg / 2;
  const delta = Math.abs(angleDeltaDeg(angleDeg, lookDeg));
  return delta <= halfArc + 1e-6;
}

/** Computes 360-degree coverage report for a set of members with look and arc. */
export function computeCoverage(
  members: readonly { look: number; arc?: number }[]
): CoverageReport {
  const counts = new Array<number>(360).fill(0);

  for (let i = 0; i < 360; i++) {
    // Map i=0..359 to angle -179..180
    const angle = i - 179;
    let c = 0;
    for (const m of members) {
      const arc = m.arc ?? DEFAULT_ARC_DEG;
      if (isAngleCovered(angle, m.look, arc)) {
        c++;
      }
    }
    counts[i] = c;
  }

  let coveredCount = 0;
  for (let i = 0; i < 360; i++) {
    if (counts[i] > 0) coveredCount++;
  }

  const coveredPercent = Math.round((coveredCount / 360) * 1000) / 10;
  const uncoveredPercent = Math.round(((360 - coveredCount) / 360) * 1000) / 10;

  const buildSpans = (predicate: (count: number) => boolean): AngularSpan[] => {
    const spans: AngularSpan[] = [];
    let startIdx: number | null = null;

    for (let i = 0; i < 360; i++) {
      const matches = predicate(counts[i]);
      if (matches && startIdx === null) {
        startIdx = i;
      } else if (!matches && startIdx !== null) {
        spans.push({
          from: startIdx - 179,
          to: i - 1 - 179,
        });
        startIdx = null;
      }
    }
    if (startIdx !== null) {
      spans.push({
        from: startIdx - 179,
        to: 180,
      });
    }

    // Merge circular wrap-around if first and last spans touch (-179 and 180)
    if (spans.length > 1 && spans[0].from === -179 && spans[spans.length - 1].to === 180) {
      const last = spans.pop()!;
      spans[0].from = last.from;
    }

    return spans;
  };

  return {
    coveragePercent: coveredPercent,
    uncoveredPercent: uncoveredPercent,
    coveredSpans: buildSpans((c) => c > 0),
    uncoveredSpans: buildSpans((c) => c === 0),
    overlappingSpans: buildSpans((c) => c >= 2),
    degreeCounts: counts,
  };
}

/** Performs live validation of member positions, spacing, radius bounds and coverage. */
export function validateLiveShape(
  members: readonly (FormationMemberShape | CustomMemberShape)[],
  minSpacingCm = MIN_MEMBER_SPACING_CM,
  maxRadiusCm = CUSTOM_MAX_RADIUS
): LiveValidationResult {
  const spacingViolations: SpacingViolation[] = [];
  const radiusViolations: RadiusViolation[] = [];

  for (let i = 0; i < members.length; i++) {
    const m = members[i];
    const r = Math.hypot(m.fwd, m.right);
    if (r > maxRadiusCm) {
      radiusViolations.push({
        memberIndex: i,
        role: 'role' in m ? String((m as FormationMemberShape).role) : undefined,
        radiusCm: Math.round(r),
        maxRadiusCm,
      });
    }

    for (let j = i + 1; j < members.length; j++) {
      const m2 = members[j];
      const d = Math.hypot(m.fwd - m2.fwd, m.right - m2.right);
      if (d < minSpacingCm) {
        spacingViolations.push({
          memberA: i,
          memberB: j,
          roleA: 'role' in m ? String((m as FormationMemberShape).role) : undefined,
          roleB: 'role' in m2 ? String((m2 as FormationMemberShape).role) : undefined,
          distCm: Math.round(d),
          minSpacingCm,
        });
      }
    }
  }

  const coverage = computeCoverage(members);
  const valid = spacingViolations.length === 0 && radiusViolations.length === 0;

  return {
    valid,
    spacingViolations,
    radiusViolations,
    coverage,
  };
}

/** Mirrors a formation shape across the heading axis. Inverts right, inverts look, swaps roles left<->right. */
export function mirrorFormationShape(
  formation: EditableFormation,
  shape: FormationShape
): { formation: EditableFormation; shape: FormationShape } {
  let targetFormation: EditableFormation = formation;
  if (formation === 'echelon_left') targetFormation = 'echelon_right';
  else if (formation === 'echelon_right') targetFormation = 'echelon_left';

  const mirroredMembers: FormationMemberShape[] = shape.members.map((m) => {
    let newRole = m.role;
    if (m.role === 'left') newRole = 'right';
    else if (m.role === 'right') newRole = 'left';

    const invertedLook = normYaw(-m.look);
    const normalizedLook = invertedLook === -180 ? 180 : invertedLook;

    const res: FormationMemberShape = {
      role: newRole,
      fwd: m.fwd,
      right: m.right === 0 ? 0 : -m.right,
      look: normalizedLook,
    };
    if (m.arc !== undefined) {
      res.arc = m.arc;
    }
    return res;
  });

  return {
    formation: targetFormation,
    shape: { members: mirroredMembers },
  };
}

/** Mirrors a custom formation shape across the heading axis. Inverts right and look. */
export function mirrorCustomShape(shape: CustomShapeDef): CustomShapeDef {
  return {
    members: shape.members.map((m) => {
      const invertedLook = normYaw(-m.look);
      const normalizedLook = invertedLook === -180 ? 180 : invertedLook;
      return {
        squad: m.squad,
        fwd: m.fwd,
        right: m.right === 0 ? 0 : -m.right,
        look: normalizedLook,
      };
    }),
  };
}

/** Computes the default procedural formation shape for a formation and count from rules.ts. */
export function computeDefaultFormationShape(
  formation: EditableFormation,
  memberCount: number,
  config: FormationConfig
): FormationShape {
  const roles = rolesFor(memberCount);
  const members: FormationMemberShape[] = [];

  // For pair formation, formation in rules is 'column' or 'wedge' with memberCount 2
  const ruleFormation = formation === 'pair' ? 'column' : formation;
  const slots = FormationShapeRules.Slots(ruleFormation, memberCount, [0, 0], 0, config);

  for (let i = 0; i < roles.length; i++) {
    const role: Role = roles[i];
    const slot = slots.find((s) => s.role === role);
    const fwd = slot ? Math.round(slot.pos[0]) : 0;
    const right = slot ? Math.round(slot.pos[1]) : 0;

    const look = Math.round(
      FormationSectorRules.LookYaw(role, ruleFormation, 'halt', memberCount, 0, 0, null, config)
    );

    members.push({
      role: ROLE_LETTER_TO_NAME[role],
      fwd: role === 'K' ? 0 : fwd,
      right: role === 'K' ? 0 : right,
      look: normYaw(look) === -180 ? 180 : normYaw(look),
      arc: DEFAULT_ARC_DEG,
    });
  }

  return { members };
}

/** Resets a formation shape entry by removing it from the document, so game falls back to procedural rules. */
export function resetFormationShape(
  doc: FormationShapesFile,
  formation: EditableFormation,
  memberCount: number
): boolean {
  if (!doc.formations || !doc.formations[formation]) {
    return false;
  }
  const countKey = String(memberCount);
  if (!(countKey in doc.formations[formation]!)) {
    return false;
  }
  delete doc.formations[formation]![countKey];
  return true;
}

/** Resets a custom shape entry by removing it from the document. */
export function resetCustomShape(
  doc: FormationShapesFile,
  customKey: 'custom_1' | 'custom_2' | 'custom_3'
): boolean {
  if (!doc.custom || !(customKey in doc.custom)) {
    return false;
  }
  delete doc.custom[customKey];
  return true;
}
