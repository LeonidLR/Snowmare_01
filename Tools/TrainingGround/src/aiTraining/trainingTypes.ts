import type { GenLayout } from '../levelgen/schema.ts';

export const X6 = 'L_Gen_Depot_1_X6';
export const TRAINING_MAPS = [
  { id: X6, name: 'Депо X6' },
  { id: 'L_Gen_StoryShowcase_Kit', name: 'Story Showcase Kit' },
  { id: 'L_Demo01_A_FrozenLake', name: 'Замёрзшее озеро' },
  { id: 'L_VehicleTest', name: 'Vehicle Test' },
  { id: 'L_MainMenu', name: 'Главное меню' },
] as const;

export type TrainingMapId = typeof TRAINING_MAPS[number]['id'];
export type Point2 = [number, number];
export interface TrainingSelection { map: TrainingMapId; sector: number }
export interface TrainingPosition { pos: [number, number, number]; yaw: number }

export interface TrainingSector {
  id: number;
  name: string;
  box_cm: { min: Point2; max: Point2 };
  starts: TrainingPosition[];
  pmc_posts: Array<TrainingPosition & { id: string; kind: string }>;
  pmc_squad: { count: number; posture: string; weights: string };
  contents: { enterable_buildings: number; covers: Record<string, number>; fences_m: number; ladders: number };
  size_m: Point2;
}

export interface TrainingRun {
  file: string;
  source: 'demo' | 'arena';
  time: string;
  map: string;
  sector: number | null;
}

export interface SectorFightRow {
  sector: number | null;
  label: string;
  sessions: number;
  rounds: number;
  wins: number;
  losses: number;
  draws: number;
  minutes: number;
  lastPlayed: string | null;
  fewest?: boolean;
}

export interface SectorFightStats {
  sectors: SectorFightRow[];
  noSector: SectorFightRow;
  total: SectorFightRow;
  recommendedSector: number | null;
}

export type TrainingBuildId = 'main' | 'play';
export interface TrainingBuildStatus {
  id: TrainingBuildId;
  available: boolean;
  built: boolean;
  fresh: boolean;
  head: string | null;
  builtHead: string | null;
  label: string;
}
export interface TrainingBuilds {
  main: TrainingBuildStatus;
  play: TrainingBuildStatus;
  defaultBuild: TrainingBuildId;
}

export interface TrainingData {
  maps: typeof TRAINING_MAPS;
  active: TrainingSelection;
  sectors: TrainingSector[];
  layout: GenLayout | null;
  runs: TrainingRun[];
  sectorStats?: SectorFightStats;
  howto?: string;
  editorCmdPath?: string;
  builds?: TrainingBuilds;
}
