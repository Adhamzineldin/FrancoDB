import { useState, useEffect, useCallback } from 'react';
import { api } from '../api';
import type { ChronosResult } from '../types';

export default function AIStatus() {
  const [aiStatus, setAiStatus] = useState<ChronosResult | null>(null);
  const [anomalies, setAnomalies] = useState<ChronosResult | null>(null);
  const [execStats, setExecStats] = useState<ChronosResult | null>(null);
  const [loading, setLoading] = useState(true);
  const [activeTab, setActiveTab] = useState<'overview' | 'anomalies' | 'stats'>('overview');

  const loadData = useCallback(async () => {
    setLoading(true);
    try {
      const [ai, anom, stats] = await Promise.all([
        api.getAIStatus().catch(() => null),
        api.getAnomalies().catch(() => null),
        api.getExecStats().catch(() => null),
      ]);
      setAiStatus(ai);
      setAnomalies(anom);
      setExecStats(stats);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => { loadData(); }, [loadData]);

  if (loading) {
    return <div className="loading"><div className="spinner" /> Loading AI status...</div>;
  }

  const aiRows = aiStatus?.data?.rows || [];
  const anomRows = anomalies?.data?.rows || [];
  const statRows = execStats?.data?.rows || [];

  return (
    <div className="ai-status">
      {/* Tab Navigation */}
      <div className="tab-bar">
        <button
          className={`tab-btn ${activeTab === 'overview' ? 'active' : ''}`}
          onClick={() => setActiveTab('overview')}
        >
          Overview
        </button>
        <button
          className={`tab-btn ${activeTab === 'anomalies' ? 'active' : ''}`}
          onClick={() => setActiveTab('anomalies')}
        >
          Anomalies ({anomRows.length})
        </button>
        <button
          className={`tab-btn ${activeTab === 'stats' ? 'active' : ''}`}
          onClick={() => setActiveTab('stats')}
        >
          Execution Stats
        </button>
        <button className="btn-sm" onClick={loadData} style={{ marginLeft: 'auto' }}>
          Refresh
        </button>
      </div>

      {/* Overview Tab */}
      {activeTab === 'overview' && (
        <div className="ai-overview">
          {aiRows.length > 0 ? (
            <div className="ai-cards">
              {aiRows.map((row, i) => {
                const status = row[1]?.toUpperCase();
                return (
                  <div key={i} className={`ai-status-card ${status === 'ACTIVE' ? 'active' : status === 'INFO' ? 'info' : 'inactive'}`}>
                    <div className="ai-card-header">
                      <span className="ai-card-title">{row[0]}</span>
                      <span className={`ai-status-badge ${status?.toLowerCase()}`}>
                        {status}
                      </span>
                    </div>
                    <div className="ai-card-detail">{row[2]}</div>
                  </div>
                );
              })}
            </div>
          ) : (
            <div className="panel">
              <div className="panel-body">
                <p className="text-muted">AI Layer is not active. Start the ChronosDB server to enable AI features.</p>
              </div>
            </div>
          )}

          {/* Architecture Info */}
          <div className="panel" style={{ marginTop: '1.5rem' }}>
            <div className="panel-header"><h3>AI Architecture</h3></div>
            <div className="panel-body">
              <div className="arch-grid">
                <div className="arch-card">
                  <h4>Self-Learning Engine</h4>
                  <p>UCB1 multi-armed bandit that learns optimal scan strategies (Sequential vs Index) per table based on query performance feedback.</p>
                  <code>Score(a) = Q(a) + c * sqrt(ln(N) / N_a)</code>
                </div>
                <div className="arch-card">
                  <h4>Immune System</h4>
                  <p>Z-score anomaly detection on mutation rates with automated responses: LOG (z≥2), BLOCK (z≥3), AUTO-RECOVER (z≥4) using time travel.</p>
                  <code>z = (x - mu) / sigma</code>
                </div>
                <div className="arch-card">
                  <h4>Temporal Index Manager</h4>
                  <p>DBSCAN clustering on time-travel queries to detect temporal hotspots. CUSUM change-point detection for optimal snapshot scheduling.</p>
                  <code>DBSCAN(eps=60s, minPts=5)</code>
                </div>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* Anomalies Tab */}
      {activeTab === 'anomalies' && (
        <div className="panel">
          <div className="panel-header">
            <h3>Recent Anomalies</h3>
          </div>
          <div className="panel-body">
            {anomalies?.data ? (
              <div className="data-table-wrapper">
                <table className="data-table">
                  <thead>
                    <tr>
                      {anomalies.data.columns.map((col, i) => (
                        <th key={i}>{col}</th>
                      ))}
                    </tr>
                  </thead>
                  <tbody>
                    {anomRows.map((row, ri) => (
                      <tr key={ri} className={
                        row[1]?.toLowerCase() === 'high' ? 'severity-high' :
                        row[1]?.toLowerCase() === 'medium' ? 'severity-medium' :
                        row[1]?.toLowerCase() === 'low' ? 'severity-low' : ''
                      }>
                        {row.map((cell, ci) => (
                          <td key={ci}>{cell}</td>
                        ))}
                      </tr>
                    ))}
                    {anomRows.length === 0 && (
                      <tr>
                        <td colSpan={anomalies.data.columns.length} className="text-muted" style={{ textAlign: 'center' }}>
                          No anomalies detected
                        </td>
                      </tr>
                    )}
                  </tbody>
                </table>
              </div>
            ) : (
              <p className="text-muted">
                {anomalies?.error || 'Immune System not active'}
              </p>
            )}
          </div>
        </div>
      )}

      {/* Execution Stats Tab */}
      {activeTab === 'stats' && (
        <div className="panel">
          <div className="panel-header">
            <h3>Execution Strategy Statistics</h3>
          </div>
          <div className="panel-body">
            {execStats?.data ? (
              <div className="data-table-wrapper">
                <table className="data-table">
                  <thead>
                    <tr>
                      {execStats.data.columns.map((col, i) => (
                        <th key={i}>{col}</th>
                      ))}
                    </tr>
                  </thead>
                  <tbody>
                    {statRows.map((row, ri) => (
                      <tr key={ri}>
                        {row.map((cell, ci) => (
                          <td key={ci}>{cell}</td>
                        ))}
                      </tr>
                    ))}
                    {statRows.length === 0 && (
                      <tr>
                        <td colSpan={execStats.data.columns.length} className="text-muted" style={{ textAlign: 'center' }}>
                          No execution data yet
                        </td>
                      </tr>
                    )}
                  </tbody>
                </table>
              </div>
            ) : (
              <p className="text-muted">
                {execStats?.error || 'Learning Engine not active'}
              </p>
            )}
          </div>
        </div>
      )}
    </div>
  );
}
