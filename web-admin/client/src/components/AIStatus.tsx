import { useState, useEffect, useCallback } from 'react';
import { api } from '../api';
import type { AIDetailedResponse, AIAnomaly } from '../types';

function formatTimestamp(us: number): string {
  if (!us) return 'N/A';
  const ms = us / 1000;
  const d = new Date(ms);
  if (isNaN(d.getTime())) return String(us);
  return d.toLocaleString();
}

function severityClass(severity: string): string {
  switch (severity?.toUpperCase()) {
    case 'HIGH': return 'severity-high';
    case 'MEDIUM': return 'severity-medium';
    case 'LOW': return 'severity-low';
    default: return '';
  }
}

function rewardPercent(r: number): string {
  return (r * 100).toFixed(1) + '%';
}

export default function AIStatus() {
  const [data, setData] = useState<AIDetailedResponse | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [autoRefresh, setAutoRefresh] = useState(false);

  const loadData = useCallback(async () => {
    try {
      const result = await api.getAIDetailed();
      setData(result);
      setError(null);
    } catch (err: any) {
      setError(err.message || 'Failed to load AI status');
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => { loadData(); }, [loadData]);

  useEffect(() => {
    if (!autoRefresh) return;
    const interval = setInterval(loadData, 3000);
    return () => clearInterval(interval);
  }, [autoRefresh, loadData]);

  if (loading) {
    return <div className="loading"><div className="spinner" /> Loading AI status...</div>;
  }

  if (error) {
    return (
      <div className="panel">
        <div className="panel-body">
          <div className="error-text">{error}</div>
          <button className="btn-sm" onClick={loadData} style={{ marginTop: '0.5rem' }}>Retry</button>
        </div>
      </div>
    );
  }

  if (!data || !data.initialized) {
    return (
      <div className="panel">
        <div className="panel-body">
          <p className="text-muted">AI Layer is not initialized. Start the ChronosDB server to enable AI features.</p>
        </div>
      </div>
    );
  }

  const le = data.learning_engine;
  const im = data.immune_system;
  const ti = data.temporal_index;
  const tasks = data.scheduled_tasks ?? [];

  // Determine preferred strategy
  const arms = le?.arms ?? [];
  const seqArm = arms.find(a => a.strategy === 'Sequential Scan');
  const idxArm = arms.find(a => a.strategy === 'Index Scan');
  const totalPulls = (seqArm?.pulls ?? 0) + (idxArm?.pulls ?? 0);
  const preferredStrategy = totalPulls > 0
    ? ((idxArm?.avg_reward ?? 0) > (seqArm?.avg_reward ?? 0) ? 'Index Scan' : 'Sequential Scan')
    : null;

  // Anomaly severity counts
  const anomalies = im?.recent_anomalies ?? [];
  const highCount = anomalies.filter(a => a.severity === 'HIGH').length;
  const medCount = anomalies.filter(a => a.severity === 'MEDIUM').length;

  return (
    <div className="ai-dashboard">
      {/* Header */}
      <div className="ai-dash-header">
        <div className="ai-dash-title">
          <h2>AI Intelligence Layer</h2>
          <span className="text-muted">{data.metrics_recorded?.toLocaleString() ?? 0} events recorded</span>
        </div>
        <div className="ai-dash-actions">
          <label className="auto-refresh-toggle">
            <input type="checkbox" checked={autoRefresh} onChange={(e) => setAutoRefresh(e.target.checked)} />
            Auto-refresh
          </label>
          <button className="btn-sm" onClick={loadData}>Refresh</button>
        </div>
      </div>

      {/* System Status Cards */}
      <div className="ai-system-cards">
        <div className={`ai-sys-card ${le?.active ? 'active' : 'inactive'}`}>
          <div className="ai-sys-icon">&#x1F9E0;</div>
          <div className="ai-sys-info">
            <div className="ai-sys-name">Learning Engine</div>
            <div className={`ai-sys-status ${le?.active ? 'active' : 'inactive'}`}>
              {le?.active ? 'ACTIVE' : 'INACTIVE'}
            </div>
          </div>
          <div className="ai-sys-metric">
            <span className="metric-value">{le?.total_queries?.toLocaleString() ?? 0}</span>
            <span className="metric-label">queries observed</span>
          </div>
        </div>

        <div className={`ai-sys-card ${im?.active ? 'active' : 'inactive'}`}>
          <div className="ai-sys-icon">&#x1F6E1;</div>
          <div className="ai-sys-info">
            <div className="ai-sys-name">Immune System</div>
            <div className={`ai-sys-status ${im?.active ? 'active' : 'inactive'}`}>
              {im?.active ? 'ACTIVE' : 'INACTIVE'}
            </div>
          </div>
          <div className="ai-sys-metric">
            <span className="metric-value">{im?.total_anomalies ?? 0}</span>
            <span className="metric-label">anomalies detected</span>
          </div>
        </div>

        <div className={`ai-sys-card ${ti?.active ? 'active' : 'inactive'}`}>
          <div className="ai-sys-icon">&#x23F1;</div>
          <div className="ai-sys-info">
            <div className="ai-sys-name">Temporal Index</div>
            <div className={`ai-sys-status ${ti?.active ? 'active' : 'inactive'}`}>
              {ti?.active ? 'ACTIVE' : 'INACTIVE'}
            </div>
          </div>
          <div className="ai-sys-metric">
            <span className="metric-value">{ti?.total_accesses ?? 0}</span>
            <span className="metric-label">time-travel queries</span>
          </div>
        </div>
      </div>

      {/* Learning Engine Detail */}
      {le?.active && (
        <div className="panel ai-section">
          <div className="panel-header">
            <h3>Self-Learning Execution Engine (UCB1 Bandit)</h3>
            <span className={`ai-status-badge ${le.ready ? 'active' : 'info'}`}>
              {le.ready ? 'RECOMMENDING' : `LEARNING (${le.total_queries}/${le.min_samples})`}
            </span>
          </div>
          <div className="panel-body">
            {/* Progress bar for learning readiness */}
            {!le.ready && (
              <div className="learning-progress">
                <div className="progress-label">
                  Learning progress: {le.total_queries} / {le.min_samples} queries needed
                </div>
                <div className="progress-bar-track">
                  <div
                    className="progress-bar-fill"
                    style={{ width: `${Math.min(100, ((le.total_queries ?? 0) / (le.min_samples ?? 30)) * 100)}%` }}
                  />
                </div>
              </div>
            )}

            {/* Strategy Comparison */}
            <div className="strategy-comparison">
              <div className="strategy-card">
                <div className="strategy-header">
                  <span className="strategy-name">Sequential Scan</span>
                  {preferredStrategy === 'Sequential Scan' && (
                    <span className="preferred-badge">PREFERRED</span>
                  )}
                </div>
                <div className="strategy-stats">
                  <div className="stat-row">
                    <span className="stat-label">Times Selected</span>
                    <span className="stat-value">{seqArm?.pulls ?? 0}</span>
                  </div>
                  <div className="stat-row">
                    <span className="stat-label">Avg Reward</span>
                    <span className="stat-value">{rewardPercent(seqArm?.avg_reward ?? 0)}</span>
                  </div>
                  <div className="stat-row">
                    <span className="stat-label">UCB Score</span>
                    <span className="stat-value">{(seqArm?.ucb_score ?? 0).toFixed(4)}</span>
                  </div>
                  {totalPulls > 0 && (
                    <div className="stat-bar">
                      <div className="stat-bar-fill seq" style={{ width: `${((seqArm?.pulls ?? 0) / totalPulls) * 100}%` }} />
                    </div>
                  )}
                </div>
              </div>

              <div className="strategy-vs">VS</div>

              <div className="strategy-card">
                <div className="strategy-header">
                  <span className="strategy-name">Index Scan</span>
                  {preferredStrategy === 'Index Scan' && (
                    <span className="preferred-badge">PREFERRED</span>
                  )}
                </div>
                <div className="strategy-stats">
                  <div className="stat-row">
                    <span className="stat-label">Times Selected</span>
                    <span className="stat-value">{idxArm?.pulls ?? 0}</span>
                  </div>
                  <div className="stat-row">
                    <span className="stat-label">Avg Reward</span>
                    <span className="stat-value">{rewardPercent(idxArm?.avg_reward ?? 0)}</span>
                  </div>
                  <div className="stat-row">
                    <span className="stat-label">UCB Score</span>
                    <span className="stat-value">{(idxArm?.ucb_score ?? 0).toFixed(4)}</span>
                  </div>
                  {totalPulls > 0 && (
                    <div className="stat-bar">
                      <div className="stat-bar-fill idx" style={{ width: `${((idxArm?.pulls ?? 0) / totalPulls) * 100}%` }} />
                    </div>
                  )}
                </div>
              </div>
            </div>

            <div className="ai-formula">
              <code>UCB1: Score(a) = Q(a) + {'\u221A'}2 * {'\u221A'}(ln(N) / N_a) &nbsp;|&nbsp; Reward = 1 / (1 + time_ms / 100)</code>
            </div>

            {/* Multi-Dimensional Query Plan Optimizer */}
            {le.optimizer && (
              <div className="optimizer-section">
                <h4>Query Plan Optimizer (Multi-Dimensional Learning)</h4>
                <div className="optimizer-summary">
                  <div className="immune-metrics">
                    <div className="immune-metric-card">
                      <div className="immune-metric-value">{le.optimizer.total_optimizations.toLocaleString()}</div>
                      <div className="immune-metric-label">Plans Generated</div>
                    </div>
                    <div className="immune-metric-card">
                      <div className="immune-metric-value">{le.optimizer.filter_reorders.toLocaleString()}</div>
                      <div className="immune-metric-label">Filter Reorders</div>
                    </div>
                    <div className="immune-metric-card">
                      <div className="immune-metric-value">{le.optimizer.early_terminations.toLocaleString()}</div>
                      <div className="immune-metric-label">Early Terminations</div>
                    </div>
                  </div>
                  {le.optimizer.dimensions.map((dim, di) => {
                    const totalPulls = dim.arms.reduce((s, a) => s + a.pulls, 0);
                    return (
                      <div key={di} className="optimizer-dimension">
                        <div className="dimension-header">{dim.name}</div>
                        <div className="dimension-arms">
                          {dim.arms.map((arm, ai) => {
                            const pct = totalPulls > 0 ? (arm.pulls / totalPulls) * 100 : 0;
                            return (
                              <div key={ai} className="dimension-arm">
                                <div className="arm-name-row">
                                  <span className="arm-name">{arm.name}</span>
                                  <span className="arm-pulls">{arm.pulls} pulls ({pct.toFixed(0)}%)</span>
                                </div>
                                <div className="stat-bar">
                                  <div className={`stat-bar-fill dim-${ai}`} style={{ width: `${pct}%` }} />
                                </div>
                              </div>
                            );
                          })}
                        </div>
                      </div>
                    );
                  })}
                </div>
                <div className="ai-formula">
                  <code>Dimensions: Scan Strategy | Filter Ordering (selectivity/cost) | Limit (full scan/early termination)</code>
                </div>
              </div>
            )}
          </div>
        </div>
      )}

      {/* Immune System Detail */}
      {im?.active && (
        <div className="panel ai-section">
          <div className="panel-header">
            <h3>Immune System (Anomaly Detection)</h3>
            <span className={`ai-status-badge ${highCount > 0 ? 'high' : medCount > 0 ? 'medium' : 'active'}`}>
              {highCount > 0 ? `${highCount} HIGH` : medCount > 0 ? `${medCount} MEDIUM` : 'HEALTHY'}
            </span>
          </div>
          <div className="panel-body">
            {/* Immune metrics row */}
            <div className="immune-metrics">
              <div className="immune-metric-card">
                <div className="immune-metric-value">{im.monitored_tables ?? 0}</div>
                <div className="immune-metric-label">Tables Monitored</div>
              </div>
              <div className="immune-metric-card">
                <div className="immune-metric-value">{im.total_anomalies ?? 0}</div>
                <div className="immune-metric-label">Total Anomalies</div>
              </div>
              <div className="immune-metric-card warn">
                <div className="immune-metric-value">{(im.blocked_tables?.length ?? 0) + (im.blocked_users?.length ?? 0)}</div>
                <div className="immune-metric-label">Active Blocks</div>
              </div>
              <div className="immune-metric-card">
                <div className="immune-metric-value">{im.check_interval_ms ?? 0}ms</div>
                <div className="immune-metric-label">Check Interval</div>
              </div>
            </div>

            {/* Blocked resources */}
            {((im.blocked_tables?.length ?? 0) > 0 || (im.blocked_users?.length ?? 0) > 0) && (
              <div className="blocked-section">
                <h4>Active Blocks</h4>
                <div className="blocked-items">
                  {im.blocked_tables?.map(t => (
                    <span key={t} className="blocked-tag table-tag">Table: {t}</span>
                  ))}
                  {im.blocked_users?.map(u => (
                    <span key={u} className="blocked-tag user-tag">User: {u}</span>
                  ))}
                </div>
              </div>
            )}

            {/* Z-Score thresholds */}
            <div className="threshold-bar">
              <div className="threshold-section normal">
                <span>Normal</span>
                <span>z &lt; {im.thresholds?.low}</span>
              </div>
              <div className="threshold-section low">
                <span>LOW (Log)</span>
                <span>z &ge; {im.thresholds?.low}</span>
              </div>
              <div className="threshold-section medium">
                <span>MEDIUM (Block)</span>
                <span>z &ge; {im.thresholds?.medium}</span>
              </div>
              <div className="threshold-section high">
                <span>HIGH (Recover)</span>
                <span>z &ge; {im.thresholds?.high}</span>
              </div>
            </div>

            {/* Recent anomalies */}
            {anomalies.length > 0 && (
              <div className="anomaly-list">
                <h4>Recent Anomalies ({anomalies.length})</h4>
                <div className="anomaly-table-wrap">
                  <table className="data-table">
                    <thead>
                      <tr>
                        <th>Severity</th>
                        <th>Table</th>
                        <th>User</th>
                        <th>Z-Score</th>
                        <th>Rate</th>
                        <th>Baseline</th>
                        <th>Time</th>
                      </tr>
                    </thead>
                    <tbody>
                      {anomalies.map((a: AIAnomaly, i: number) => (
                        <tr key={i} className={severityClass(a.severity)}>
                          <td><span className={`severity-pill ${a.severity.toLowerCase()}`}>{a.severity}</span></td>
                          <td>{a.table}</td>
                          <td>{a.user || '-'}</td>
                          <td>{a.z_score.toFixed(2)}</td>
                          <td>{a.current_rate.toFixed(2)}/s</td>
                          <td>{a.mean_rate.toFixed(2)}/s</td>
                          <td>{formatTimestamp(a.timestamp_us)}</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </div>
            )}

            {anomalies.length === 0 && (
              <div className="no-anomalies">
                No anomalies detected - all systems normal
              </div>
            )}

            <div className="ai-formula">
              <code>Z-Score: z = (x - {'\u03BC'}) / {'\u03C3'} &nbsp;|&nbsp; Window: {im.check_interval_ms}ms &nbsp;|&nbsp; History: 100 intervals</code>
            </div>
          </div>
        </div>
      )}

      {/* Temporal Index Detail */}
      {ti?.active && (
        <div className="panel ai-section">
          <div className="panel-header">
            <h3>Temporal Index Manager</h3>
            <span className="ai-status-badge info">
              {(ti.hotspots?.length ?? 0)} hotspot{(ti.hotspots?.length ?? 0) !== 1 ? 's' : ''}
            </span>
          </div>
          <div className="panel-body">
            <div className="temporal-metrics">
              <div className="immune-metric-card">
                <div className="immune-metric-value">{ti.total_accesses ?? 0}</div>
                <div className="immune-metric-label">Time-Travel Queries</div>
              </div>
              <div className="immune-metric-card">
                <div className="immune-metric-value">{ti.hotspots?.length ?? 0}</div>
                <div className="immune-metric-label">Active Hotspots</div>
              </div>
              <div className="immune-metric-card">
                <div className="immune-metric-value">{ti.total_snapshots ?? 0}</div>
                <div className="immune-metric-label">Smart Snapshots</div>
              </div>
              <div className="immune-metric-card">
                <div className="immune-metric-value">{((ti.analysis_interval_ms ?? 0) / 1000)}s</div>
                <div className="immune-metric-label">Analysis Interval</div>
              </div>
            </div>

            {(ti.hotspots?.length ?? 0) > 0 && (
              <div className="hotspot-list">
                <h4>Detected Temporal Hotspots</h4>
                <div className="anomaly-table-wrap">
                  <table className="data-table">
                    <thead>
                      <tr>
                        <th>Center Time</th>
                        <th>Range Start</th>
                        <th>Range End</th>
                        <th>Accesses</th>
                        <th>Density</th>
                      </tr>
                    </thead>
                    <tbody>
                      {ti.hotspots!.map((h, i) => (
                        <tr key={i}>
                          <td>{formatTimestamp(h.center_us)}</td>
                          <td>{formatTimestamp(h.range_start_us)}</td>
                          <td>{formatTimestamp(h.range_end_us)}</td>
                          <td>{h.access_count}</td>
                          <td>{h.density.toFixed(2)}/s</td>
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              </div>
            )}

            {(ti.hotspots?.length ?? 0) === 0 && (
              <div className="no-anomalies">
                No temporal hotspots detected yet. Run some time-travel queries (RECOVER TO) to see clustering.
              </div>
            )}

            <div className="ai-formula">
              <code>DBSCAN(eps=60s, minPts=5) &nbsp;|&nbsp; CUSUM change-point: threshold=4{'\u03C3'}</code>
            </div>
          </div>
        </div>
      )}

      {/* Scheduled Tasks */}
      {tasks.length > 0 && (
        <div className="panel ai-section">
          <div className="panel-header">
            <h3>Background Tasks</h3>
            <span className="ai-status-badge info">{tasks.length} active</span>
          </div>
          <div className="panel-body">
            <div className="tasks-grid">
              {tasks.map((t, i) => (
                <div key={i} className="task-card">
                  <div className="task-name">{t.name}</div>
                  <div className="task-details">
                    <span>{t.periodic ? 'Periodic' : 'One-shot'}</span>
                    <span>Every {t.interval_ms >= 1000 ? `${(t.interval_ms / 1000)}s` : `${t.interval_ms}ms`}</span>
                    <span>{t.run_count.toLocaleString()} runs</span>
                  </div>
                </div>
              ))}
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
