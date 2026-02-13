import type { ChronosResult, UserInfo, AIDetailedResponse } from './types';

const BASE = '/api';
const TOKEN_KEY = 'chronos_token';

function getToken(): string | null {
  return localStorage.getItem(TOKEN_KEY);
}

async function request<T = ChronosResult>(
  path: string,
  options?: RequestInit
): Promise<T> {
  const token = getToken();
  const headers: Record<string, string> = {
    'Content-Type': 'application/json',
  };
  if (token) {
    headers['Authorization'] = `Bearer ${token}`;
  }

  const res = await fetch(`${BASE}${path}`, {
    ...options,
    credentials: 'include',
    headers,
  });
  if (res.status === 401) {
    localStorage.removeItem(TOKEN_KEY);
    throw new Error('UNAUTHORIZED');
  }
  return res.json();
}

export const api = {
  // Auth
  login: async (username: string, password: string) => {
    const result = await request<{
      success: boolean; username?: string; role?: string;
      token?: string; error?: string;
    }>('/login', { method: 'POST', body: JSON.stringify({ username, password }) });

    if (result.success && result.token) {
      localStorage.setItem(TOKEN_KEY, result.token);
    }
    return result;
  },

  logout: async () => {
    const result = await request('/logout', { method: 'POST' });
    localStorage.removeItem(TOKEN_KEY);
    return result;
  },

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

  getTableData: (name: string) =>
    request(`/tables/${name}/data`),

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
  getAIDetailed: () => request<AIDetailedResponse>('/ai/detailed'),
};
