export interface ChronosResult {
  success: boolean;
  data?: {
    columns: string[];
    rows: string[][];
  };
  row_count?: number;
  total_count?: number;
  truncated?: boolean;
  total_rows?: number;
  max_rows?: number;
  message?: string;
  error?: string;
}

export interface UserInfo {
  username: string;
  role: string;
  currentDb: string;
}

export interface AIArmStats {
  strategy: string;
  pulls: number;
  avg_reward: number;
  ucb_score: number;
}

export interface AIAnomaly {
  table: string;
  user: string;
  severity: 'LOW' | 'MEDIUM' | 'HIGH' | 'NONE';
  z_score: number;
  current_rate: number;
  mean_rate: number;
  timestamp_us: number;
  description: string;
}

export interface AIHotspot {
  center_us: number;
  range_start_us: number;
  range_end_us: number;
  access_count: number;
  density: number;
}

export interface AIScheduledTask {
  name: string;
  interval_ms: number;
  run_count: number;
  periodic: boolean;
}

export interface AIOptimizerDimensionArm {
  name: string;
  pulls: number;
}

export interface AIOptimizerDimension {
  name: string;
  arms: AIOptimizerDimensionArm[];
}

export interface AIOptimizerStats {
  total_optimizations: number;
  filter_reorders: number;
  early_terminations: number;
  dimensions: AIOptimizerDimension[];
}

export interface AIDetailedResponse {
  initialized: boolean;
  metrics_recorded?: number;
  scheduled_tasks?: AIScheduledTask[];
  learning_engine?: {
    active: boolean;
    total_queries?: number;
    min_samples?: number;
    ready?: boolean;
    arms?: AIArmStats[];
    summary?: string;
    optimizer?: AIOptimizerStats;
  };
  immune_system?: {
    active: boolean;
    total_anomalies?: number;
    check_interval_ms?: number;
    blocked_tables?: string[];
    blocked_users?: string[];
    monitored_tables?: number;
    thresholds?: { low: number; medium: number; high: number };
    recent_anomalies?: AIAnomaly[];
    summary?: string;
  };
  temporal_index?: {
    active: boolean;
    total_accesses?: number;
    total_snapshots?: number;
    analysis_interval_ms?: number;
    hotspots?: AIHotspot[];
    summary?: string;
  };
}

export type Page =
  | 'dashboard'
  | 'databases'
  | 'tables'
  | 'query'
  | 'users'
  | 'ai-status';
