import type { ChronosResult, UserInfo } from './types';

const BASE = '/api';

async function request<T = ChronosResult>(
  path: string,
  options?: RequestInit
): Promise<T> {
  const res = await fetch(`${BASE}${path}`, {
    credentials: 'include',
    headers: { 'Content-Type': 'application/json' },
    ...options,
  });
  if (res.status === 401) {
    throw new Error('UNAUTHORIZED');
  }
  return res.json();
}

export const api = {
  // Auth
  login: (username: string, password: string) =>
    request<{ success: boolean; username?: string; role?: string; error?: string }>(
      '/login',
      { method: 'POST', body: JSON.stringify({ username, password }) }
    ),

  logout: () => request('/logout', { method: 'POST' }),

  me: () =>
    request<{ success: boolean } & UserInfo>('/me'),

  // Databases
  getDatabases: () => request('/databases'),

  useDatabase: (database: string) =>
    request('/databases/use', {
      method: 'POST',
      body: JSON.stringify({ database }),
    }),

  createDatabase: (name: string) =>
    request('/databases/create', {
      method: 'POST',
      body: JSON.stringify({ name }),
    }),

  dropDatabase: (name: string) =>
    request(`/databases/${name}`, { method: 'DELETE' }),

  // Tables
  getTables: () => request('/tables'),

  getTableSchema: (name: string) => request(`/tables/${name}/schema`),

  getTableData: (name: string, limit = 100, offset = 0) =>
    request(`/tables/${name}/data?limit=${limit}&offset=${offset}`),

  // Query
  executeQuery: (sql: string) =>
    request('/query', {
      method: 'POST',
      body: JSON.stringify({ sql }),
    }),

  // Users
  getUsers: () => request('/users'),

  createUser: (username: string, password: string, role: string) =>
    request('/users', {
      method: 'POST',
      body: JSON.stringify({ username, password, role }),
    }),

  deleteUser: (username: string) =>
    request(`/users/${username}`, { method: 'DELETE' }),

  changeRole: (username: string, role: string, database?: string) =>
    request(`/users/${username}/role`, {
      method: 'PUT',
      body: JSON.stringify({ role, database }),
    }),

  // Status & AI
  getStatus: () => request('/status'),
  getAIStatus: () => request('/ai/status'),
  getAnomalies: () => request('/ai/anomalies'),
  getExecStats: () => request('/ai/stats'),
};
