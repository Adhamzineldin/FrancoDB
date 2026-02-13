import { useState, useEffect } from 'react';
import { api } from '../api';
import type { ChronosResult, Page } from '../types';

interface DashboardProps {
  onNavigate: (page: Page) => void;
}

export default function Dashboard({ onNavigate }: DashboardProps) {
  const [status, setStatus] = useState<ChronosResult | null>(null);
  const [aiStatus, setAiStatus] = useState<ChronosResult | null>(null);
  const [databases, setDatabases] = useState<ChronosResult | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    Promise.all([
      api.getStatus().catch(() => null),
      api.getAIStatus().catch(() => null),
      api.getDatabases().catch(() => null),
    ]).then(([s, ai, db]) => {
      setStatus(s);
      setAiStatus(ai);
      setDatabases(db);
      setLoading(false);
    });
  }, []);

  if (loading) {
    return <div className="loading"><div className="spinner" /> Loading dashboard...</div>;
  }

  const statusRows = status?.data?.rows || [];
  const aiRows = aiStatus?.data?.rows || [];
  const dbCount = databases?.data?.rows?.length || 0;

  return (
    <div className="dashboard">
      {/* Stat Cards */}
      <div className="stat-grid">
        <div className="stat-card" onClick={() => onNavigate('databases')}>
          <div className="stat-icon">⛁</div>
          <div className="stat-body">
            <div className="stat-value">{dbCount}</div>
            <div className="stat-label">Databases</div>
          </div>
        </div>

        {statusRows.map((row, i) => (
          <div key={i} className="stat-card">
            <div className="stat-icon">
              {row[0] === 'Current User' ? '⊕' :
               row[0] === 'Current Role' ? '🔑' :
               row[0] === 'Authenticated' ? '✓' : '•'}
            </div>
            <div className="stat-body">
              <div className="stat-value">{row[1] || 'N/A'}</div>
              <div className="stat-label">{row[0]}</div>
            </div>
          </div>
        ))}
      </div>

      {/* AI Status Panel */}
      <div className="panel">
        <div className="panel-header">
          <h3>AI Layer Status</h3>
          <button className="btn-sm" onClick={() => onNavigate('ai-status')}>
            View Details →
          </button>
        </div>
        <div className="panel-body">
          {aiRows.length > 0 ? (
            <div className="ai-grid">
              {aiRows.map((row, i) => (
                <div key={i} className={`ai-card ${row[1]?.toLowerCase()}`}>
                  <div className="ai-name">{row[0]}</div>
                  <div className={`ai-badge ${row[1]?.toLowerCase()}`}>
                    {row[1]}
                  </div>
                  <div className="ai-detail">{row[2]}</div>
                </div>
              ))}
            </div>
          ) : (
            <p className="text-muted">AI Layer not initialized</p>
          )}
        </div>
      </div>

      {/* Quick Actions */}
      <div className="panel">
        <div className="panel-header">
          <h3>Quick Actions</h3>
        </div>
        <div className="panel-body">
          <div className="action-grid">
            <button className="action-card" onClick={() => onNavigate('query')}>
              <span className="action-icon">⟩_</span>
              <span>SQL Editor</span>
            </button>
            <button className="action-card" onClick={() => onNavigate('databases')}>
              <span className="action-icon">⛁</span>
              <span>Browse Databases</span>
            </button>
            <button className="action-card" onClick={() => onNavigate('tables')}>
              <span className="action-icon">☰</span>
              <span>View Tables</span>
            </button>
            <button className="action-card" onClick={() => onNavigate('users')}>
              <span className="action-icon">⊕</span>
              <span>Manage Users</span>
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
