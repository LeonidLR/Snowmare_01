// Training Ground: unit tests for Project Health schema & SLA calculation (Task 080).
import test from 'node:test';
import assert from 'node:assert/strict';
import type { ProjectHealthData, KnownFailItem } from './ProjectHealthDepartment.tsx';

test('ProjectHealth: SLA 7-day expiry calculation', () => {
  const today = new Date('2026-11-24T00:00:00Z');

  const checkItem = (firstSeenStr: string): boolean => {
    const fs = new Date(`${firstSeenStr}T00:00:00Z`);
    const diffDays = Math.floor((today.getTime() - fs.getTime()) / (1000 * 60 * 60 * 24));
    return diffDays > 7;
  };

  // 1 day old -> active (not expired)
  assert.equal(checkItem('2026-11-23'), false);
  // 6 days old -> active
  assert.equal(checkItem('2026-11-18'), false);
  // 7 days old -> active (border)
  assert.equal(checkItem('2026-11-17'), false);
  // 8 days old -> expired!
  assert.equal(checkItem('2026-11-16'), true);
  // 14 days old -> expired!
  assert.equal(checkItem('2026-11-10'), true);
});

test('ProjectHealth: Schema validation and default fallback', () => {
  const sampleData: ProjectHealthData = {
    $schema: 'codex.project_health/1',
    generated_at: '2026-11-24T08:00:00Z',
    date: '2026-11-24',
    head: 'ba8c544c',
    summary: {
      status: 'WARNING',
      unit_tests: { total: 420, passed: 420, failed: 0, duration_s: 92.4 },
      smokes: { total: 41, pass: 40, known_fail: 1, fail: 0, duration_s: 480 },
      known_fails_count: 1,
      known_fails_expired: 0,
      disabled_this_week_count: 0,
      todo_markers_count: 3,
      last_build_time_s: 241.5,
      last_merge_gate_status: 'PASS',
    },
    known_fails: [
      {
        name: 'GeneratedPropsX6Smoke',
        type: 'smoke',
        reason: '078: 0 safe crates on X6',
        owner: 'gemini',
        first_seen: '2026-11-24',
        days_active: 0,
        expired: false,
      },
    ],
    disabled_this_week: [],
    todo_markers: {
      total: 3,
      growth: 0,
      by_directory: { 'Source/CodexTacticsDev': 1, 'Scripts': 2 },
      sample: [{ file: 'Scripts/Editor/test.py', line: 10, text: 'TODO: check' }],
    },
    build_trend: [
      {
        timestamp: '2026-11-23 20:31',
        label: 'lead_build_053',
        duration_s: 241.76,
        actions: 54,
        status: 'SUCCESS',
        log_file: 'lead_build_053.log',
      },
    ],
    merge_gate_runs: [
      {
        timestamp: '20261123-203140-096',
        branch: 'ai/x6-arena',
        status: 'PASS',
        details: '41 smokes, 0 unexpected fails',
      },
    ],
  };

  assert.equal(sampleData.summary.status, 'WARNING');
  assert.equal(sampleData.summary.smokes.total, 41);
  assert.equal(sampleData.known_fails.length, 1);
  assert.equal(sampleData.known_fails[0].expired, false);
  assert.equal(sampleData.todo_markers.total, 3);
  assert.equal(sampleData.build_trend.length, 1);
  assert.equal(sampleData.merge_gate_runs[0].status, 'PASS');
});
