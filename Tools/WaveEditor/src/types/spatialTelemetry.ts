export interface LevelBounds {
  min_x: number;
  max_x: number;
  min_z: number;
  max_z: number;
}

export interface CoverLayout {
  id: string;
  type: string;
  position: [number, number, number];
  rotation_y?: number;
  size?: [number, number, number];
  max_health?: number;
  usage_summary?: {
    time_sec: number;
    hits_absorbed: number;
    max_hp: number;
    final_hp: number;
    destroyed?: boolean;
    world_pos?: [number, number];
  };
}

export interface ElevationLayout {
  id: string;
  height: number;
  polygon: [number, number][];
}

export interface SpawnPointLayout {
  id: string;
  position: [number, number, number];
  lane?: string;
}

export interface DefendPointLayout {
  id: string;
  position: [number, number, number];
  radius?: number;
}

export interface ObstacleLayout {
  id: string;
  type: string;
  shape?: string; // 'box' | 'cylinder'
  position: [number, number, number];
  size: [number, number, number];
  rotation_y?: number;
}

export interface LevelLayout {
  bounds: LevelBounds;
  obstacles?: ObstacleLayout[];
  covers: CoverLayout[];
  elevations: ElevationLayout[];
  spawn_points: SpawnPointLayout[];
  defend_points?: DefendPointLayout[];
}

export interface SquadMemberFrame {
  id: string;
  x: number;
  y: number;
  z: number;
  hp: number;
  stance: number; // 0 = STANDING, 1 = CROUCHING, 2 = PRONE
  in_cover: boolean;
  cover_id?: string | null;
  weapon: string;
  cold?: number;
}

export interface EnemyFrame {
  id: string;
  type: string;
  x: number;
  y: number;
  z: number;
  hp: number;
  vx: number;
  vz: number;
  target_id?: string | null;
}

export interface TelemetryFrame {
  t: number;
  squad: SquadMemberFrame[];
  enemies: EnemyFrame[];
}

export interface TelemetryEvent {
  t: number;
  type: 'SHOT' | 'GRENADE_THROWN' | 'EXPLOSION' | 'HIT' | 'SQUAD_DEATH' | 'ENEMY_DEATH' | 'COVER_ENTER' | 'COVER_LEAVE' | 'COVER_DAMAGED' | 'COVER_DESTROYED' | 'CHOKE_CONGESTION' | string;
  actor_id?: string;
  target_id?: string;
  position?: [number, number, number];
  details?: Record<string, any>;
}

export interface ChokePointSummary {
  position: [number, number];
  congestion_score: number;
  enemy_count_peak: number;
  duration_sec?: number;
  stuck_type?: 'MASS' | 'SINGLE';
}

export interface SpatialTelemetrySummary {
  result: 'VICTORY' | 'DEFEAT' | 'ABORTED' | string;
  waves_cleared: number;
  total_waves: number;
  cover_metrics?: Record<string, {
    time_sec: number;
    hits_absorbed: number;
    max_hp: number;
    final_hp: number;
    destroyed?: boolean;
    world_pos?: [number, number];
  }>;
  choke_points_detected?: ChokePointSummary[];
  height_metrics?: {
    elevation_usage_time_sec?: number;
    kills_from_height?: number;
  };
}

export interface SpatialTelemetryRun {
  session_id: string;
  timestamp_utc: string;
  level_id: string;
  tester_profile: string;
  duration_sec: number;
  tick_rate_hz: number;
  level_layout: LevelLayout;
  frames: TelemetryFrame[];
  events: TelemetryEvent[];
  summary: SpatialTelemetrySummary;
}

export interface SpatialRunHeader {
  fileName: string;
  sessionId: string;
  timestamp: string;
  levelId: string;
  profile: string;
  durationSec: number;
  result: string;
  wavesCleared: number;
  framesCount: number;
  chokePointsCount: number;
}
