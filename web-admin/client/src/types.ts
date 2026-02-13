export interface ChronosResult {
  success: boolean;
  data?: {
    columns: string[];
    rows: string[][];
  };
  row_count?: number;
  message?: string;
  error?: string;
}

export interface UserInfo {
  username: string;
  role: string;
  currentDb: string;
}

export type Page =
  | 'dashboard'
  | 'databases'
  | 'tables'
  | 'query'
  | 'users'
  | 'ai-status';
