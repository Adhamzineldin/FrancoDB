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

/** Info tooltip component */
function InfoTip({ text }: { text: string }) {
  const [show, setShow] = useState(false);
  return (
    <span
      className="info-tip"
      onMouseEnter={() => setShow(true)}
      onMouseLeave={() => setShow(false)}
      onClick={() => setShow(!show)}
    >
      <span className="info-tip-icon">i</span>
      {show && <span className="info-tip-popup">{text}</span>}
    </span>
  );
}

/** Map severity names to user-friendly labels */
function severityLabel(severity: string): string {
  switch (severity?.toUpperCase()) {
    case 'HIGH': return 'CRITICAL';
    case 'MEDIUM': return 'WARNING';
    case 'LOW': return 'SUSPICIOUS';
    default: return severity;
  }
}

/** Detect attack type from anomaly description */
function attackTypeFromDescription(desc: string): string | null {
  if (!desc) return null;
  if (desc.includes('[SQL_INJECTION]')) return 'SQL Injection';
  if (desc.includes('[XSS]')) return 'XSS Attack';
  if (desc.includes('Mass INSERT') || desc.includes('Mass UPDATE') || desc.includes('Mass DELETE')) return 'Mass Operation';
  return null;
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
  const td = im?.threat_detection;

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

      {/* ======================================================================
          LEARNING ENGINE
          ====================================================================== */}
      {le?.active && (
        <div className="panel ai-section">
          <div className="panel-header">
            <h3>
              Self-Learning Execution Engine
              <InfoTip text="Uses UCB1 (Upper Confidence Bound) multi-armed bandit algorithm to learn which query execution strategy is fastest. It balances exploration (trying new strategies) with exploitation (using the best known strategy). Higher reward = faster query execution." />
            </h3>
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
                  <InfoTip text="The AI needs to observe enough queries before making recommendations. During this phase, it collects baseline performance data for each strategy." />
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
                  <InfoTip text="Reads every row in the table and checks if it matches the WHERE condition. Simple but slow for large tables. Best when most rows match or the table is small." />
                </div>
                <div className="strategy-stats">
                  <div className="stat-row">
                    <span className="stat-label">Times Selected</span>
                    <span className="stat-value">{seqArm?.pulls ?? 0}</span>
                  </div>
                  <div className="stat-row">
                    <span className="stat-label">
                      Avg Reward
                      <InfoTip text="How well this strategy performed on average. 100% = instant, 0% = very slow. Calculated as 1/(1 + query_time_ms/100). Higher is better." />
                    </span>
                    <span className="stat-value">{rewardPercent(seqArm?.avg_reward ?? 0)}</span>
                  </div>
                  <div className="stat-row">
                    <span className="stat-label">
                      UCB Score
                      <InfoTip text="UCB1 score combines average reward with an exploration bonus. Higher score = more likely to be chosen next. The bonus decreases as an arm is pulled more, encouraging trying less-used strategies." />
                    </span>
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
                  <InfoTip text="Uses a B+ tree index to jump directly to matching rows instead of scanning the whole table. Much faster for selective queries (WHERE column = value) on indexed columns." />
                </div>
                <div className="strategy-stats">
                  <div className="stat-row">
                    <span className="stat-label">Times Selected</span>
                    <span className="stat-value">{idxArm?.pulls ?? 0}</span>
                  </div>
                  <div className="stat-row">
                    <span className="stat-label">
                      Avg Reward
                      <InfoTip text="How well this strategy performed on average. 100% = instant, 0% = very slow. Higher is better." />
                    </span>
                    <span className="stat-value">{rewardPercent(idxArm?.avg_reward ?? 0)}</span>
                  </div>
                  <div className="stat-row">
                    <span className="stat-label">
                      UCB Score
                      <InfoTip text="UCB1 score combines average reward with an exploration bonus. The strategy with the highest UCB score gets chosen for the next query." />
                    </span>
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
                <h4>
                  Query Plan Optimizer
                  <InfoTip text="Beyond scan strategy, the optimizer also learns how to order WHERE clause predicates (most selective first?) and whether to use early termination for LIMIT queries. Each decision is a separate UCB1 bandit learning independently." />
                </h4>
                <div className="optimizer-summary">
                  <div className="immune-metrics">
                    <div className="immune-metric-card">
                      <div className="immune-metric-value">{le.optimizer.total_optimizations.toLocaleString()}</div>
                      <div className="immune-metric-label">
                        Plans Generated
                        <InfoTip text="Total number of query execution plans the AI has generated. Each SELECT query gets an AI-optimized plan after the learning threshold is reached." />
                      </div>
                    </div>
                    <div className="immune-metric-card">
                      <div className="immune-metric-value">{le.optimizer.filter_reorders.toLocaleString()}</div>
                      <div className="immune-metric-label">
                        Filter Reorders
                        <InfoTip text="Number of times the AI reordered WHERE clause predicates for better performance. The AI learns which column filters are most selective and evaluates them first to skip rows faster." />
                      </div>
                    </div>
                    <div className="immune-metric-card">
                      <div className="immune-metric-value">{le.optimizer.early_terminations.toLocaleString()}</div>
                      <div className="immune-metric-label">
                        Early Terminations
                        <InfoTip text="Number of times the AI stopped scanning early for LIMIT queries (without ORDER BY). Instead of scanning all rows then cutting, it stops as soon as enough rows are found." />
                      </div>
                    </div>
                  </div>
                  {le.optimizer.dimensions.map((dim, di) => {
                    const dimTotalPulls = dim.arms.reduce((s, a) => s + a.pulls, 0);
                    return (
                      <div key={di} className="optimizer-dimension">
                        <div className="dimension-header">
                          {dim.name}
                          {dim.name === 'Filter Strategy' && (
                            <InfoTip text="How to order WHERE clause predicates. ORIGINAL keeps them as written. SELECTIVITY puts the most filtering predicate first. COST puts the cheapest-to-evaluate predicate first." />
                          )}
                          {dim.name === 'Limit Strategy' && (
                            <InfoTip text="How to handle LIMIT queries. FULL SCAN reads all rows then cuts to the limit. EARLY TERMINATION stops scanning as soon as enough rows are found (only when no ORDER BY)." />
                          )}
                        </div>
                        <div className="dimension-arms">
                          {dim.arms.map((arm, ai) => {
                            const pct = dimTotalPulls > 0 ? (arm.pulls / dimTotalPulls) * 100 : 0;
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

      {/* ======================================================================
          IMMUNE SYSTEM
          ====================================================================== */}
      {im?.active && (
        <div className="panel ai-section">
          <div className="panel-header">
            <h3>
              Immune System
              <InfoTip text="Monitors all database operations for anomalies and threats. Uses Z-score statistical analysis for mutation rate anomalies, and pattern matching for SQL injection and XSS attacks. Can automatically block suspicious operations and recover from attacks." />
            </h3>
            <span className={`ai-status-badge ${highCount > 0 ? 'high' : medCount > 0 ? 'medium' : 'active'}`}>
              {highCount > 0 ? `${highCount} CRITICAL` : medCount > 0 ? `${medCount} WARNING` : 'HEALTHY'}
            </span>
          </div>
          <div className="panel-body">
            {/* Threat Detection Stats */}
            {td && (td.total_threats > 0 || true) && (
              <div className="threat-detection-section">
                <h4>
                  Threat Detection
                  <InfoTip text="Real-time detection of SQL injection and XSS (Cross-Site Scripting) attacks embedded in query values. Pattern-matching engine analyzes all INSERT, UPDATE, and DELETE operations before execution." />
                </h4>
                <div className="immune-metrics">
                  <div className={`immune-metric-card ${(td?.total_threats ?? 0) > 0 ? 'warn' : ''}`}>
                    <div className="immune-metric-value">{td?.total_threats ?? 0}</div>
                    <div className="immune-metric-label">
                      Total Threats
                      <InfoTip text="Total number of malicious patterns detected across all operations. Each detected threat is classified by type and severity." />
                    </div>
                  </div>
                  <div className={`immune-metric-card ${(td?.sql_injection_count ?? 0) > 0 ? 'warn' : ''}`}>
                    <div className="immune-metric-value">{td?.sql_injection_count ?? 0}</div>
                    <div className="immune-metric-label">
                      SQL Injections
                      <InfoTip text="SQL injection attempts detected. These include UNION SELECT data exfiltration, OR 1=1 authentication bypass, semicolon-based command injection, and timing attacks (SLEEP, BENCHMARK)." />
                    </div>
                  </div>
                  <div className={`immune-metric-card ${(td?.xss_count ?? 0) > 0 ? 'warn' : ''}`}>
                    <div className="immune-metric-value">{td?.xss_count ?? 0}</div>
                    <div className="immune-metric-label">
                      XSS Attacks
                      <InfoTip text="Cross-Site Scripting attempts detected. These include script tag injection, JavaScript event handlers (onerror, onload), eval() execution, and document.cookie theft attempts." />
                    </div>
                  </div>
                </div>
              </div>
            )}

            {/* Anomaly Detection Metrics */}
            <h4>
              Anomaly Detection
              <InfoTip text="Statistical monitoring of mutation rates (INSERT/UPDATE/DELETE per second). Uses Z-score analysis: if the current rate deviates significantly from the historical average, it's flagged as an anomaly." />
            </h4>
            <div className="immune-metrics">
              <div className="immune-metric-card">
                <div className="immune-metric-value">{im.monitored_tables ?? 0}</div>
                <div className="immune-metric-label">
                  Tables Monitored
                  <InfoTip text="Number of tables the immune system is actively tracking mutation rates for. System tables (chronos_users, sys_*) are excluded." />
                </div>
              </div>
              <div className="immune-metric-card">
                <div className="immune-metric-value">{im.total_anomalies ?? 0}</div>
                <div className="immune-metric-label">
                  Total Anomalies
                  <InfoTip text="Total anomalies detected since server start. Includes both statistical anomalies (unusual mutation rates) and threat detections (SQL injection, XSS)." />
                </div>
              </div>
              <div className="immune-metric-card warn">
                <div className="immune-metric-value">{(im.blocked_tables?.length ?? 0) + (im.blocked_users?.length ?? 0)}</div>
                <div className="immune-metric-label">
                  Active Blocks
                  <InfoTip text="Tables or users currently blocked from performing write operations. Blocks are applied for WARNING-level threats and automatically expire after a cooldown period." />
                </div>
              </div>
              <div className="immune-metric-card">
                <div className="immune-metric-value">{im.check_interval_ms ?? 0}ms</div>
                <div className="immune-metric-label">
                  Check Interval
                  <InfoTip text="How often the immune system runs its periodic analysis to detect statistical anomalies in mutation rates. Threat detection (SQL injection, XSS) happens in real-time on every query." />
                </div>
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

            {/* Severity scale */}
            <div className="threshold-bar">
              <div className="threshold-section normal">
                <span>Normal</span>
                <span>z &lt; {im.thresholds?.low}</span>
              </div>
              <div className="threshold-section low">
                <span>SUSPICIOUS (Log)</span>
                <span>z &ge; {im.thresholds?.low}</span>
              </div>
              <div className="threshold-section medium">
                <span>WARNING (Block)</span>
                <span>z &ge; {im.thresholds?.medium}</span>
              </div>
              <div className="threshold-section high">
                <span>CRITICAL (Recover)</span>
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
                        <th>Type</th>
                        <th>Table</th>
                        <th>User</th>
                        <th>Z-Score</th>
                        <th>Rate</th>
                        <th>Baseline</th>
                        <th>Time</th>
                      </tr>
                    </thead>
                    <tbody>
                      {anomalies.map((a: AIAnomaly, i: number) => {
                        const attackType = attackTypeFromDescription(a.description);
                        return (
                          <tr key={i} className={severityClass(a.severity)}>
                            <td><span className={`severity-pill ${a.severity.toLowerCase()}`}>{severityLabel(a.severity)}</span></td>
                            <td>{attackType || 'Statistical'}</td>
                            <td>{a.table}</td>
                            <td>{a.user || '-'}</td>
                            <td>{a.z_score.toFixed(2)}</td>
                            <td>{a.current_rate.toFixed(2)}/s</td>
                            <td>{a.mean_rate.toFixed(2)}/s</td>
                            <td>{formatTimestamp(a.timestamp_us)}</td>
                          </tr>
                        );
                      })}
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
              <code>Z-Score: z = (x - {'\u03BC'}) / {'\u03C3'} &nbsp;|&nbsp; Window: {im.check_interval_ms}ms &nbsp;|&nbsp; Threats: 32 SQL injection + 24 XSS patterns</code>
            </div>
          </div>
        </div>
      )}

      {/* ======================================================================
          TEMPORAL INDEX
          ====================================================================== */}
      {ti?.active && (
        <div className="panel ai-section">
          <div className="panel-header">
            <h3>
              Temporal Index Manager
              <InfoTip text="Tracks time-travel query patterns and uses DBSCAN clustering to detect temporal hotspots - time periods that are queried frequently. Can pre-compute snapshots for hot time ranges to speed up future queries." />
            </h3>
            <span className="ai-status-badge info">
              {(ti.hotspots?.length ?? 0)} hotspot{(ti.hotspots?.length ?? 0) !== 1 ? 's' : ''}
            </span>
          </div>
          <div className="panel-body">
            <div className="temporal-metrics">
              <div className="immune-metric-card">
                <div className="immune-metric-value">{ti.total_accesses ?? 0}</div>
                <div className="immune-metric-label">
                  Time-Travel Queries
                  <InfoTip text="Total AS OF queries executed. Each query reconstructs a table's state at a past point in time by replaying or undoing WAL log records." />
                </div>
              </div>
              <div className="immune-metric-card">
                <div className="immune-metric-value">{ti.hotspots?.length ?? 0}</div>
                <div className="immune-metric-label">
                  Active Hotspots
                  <InfoTip text="Time periods detected by DBSCAN clustering as frequently queried. A hotspot forms when 5+ queries target timestamps within 60 seconds of each other." />
                </div>
              </div>
              <div className="immune-metric-card">
                <div className="immune-metric-value">{ti.total_snapshots ?? 0}</div>
                <div className="immune-metric-label">
                  Smart Snapshots
                  <InfoTip text="Pre-computed table snapshots at hotspot times. These cache the result of time-travel reconstruction so future queries to the same time range return instantly." />
                </div>
              </div>
              <div className="immune-metric-card">
                <div className="immune-metric-value">{((ti.analysis_interval_ms ?? 0) / 1000)}s</div>
                <div className="immune-metric-label">
                  Analysis Interval
                  <InfoTip text="How often DBSCAN clustering runs to discover new hotspots from accumulated time-travel query patterns." />
                </div>
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
                        <th>
                          Density
                          <InfoTip text="Query frequency within this hotspot cluster (queries per second). Higher density = more frequently accessed time period." />
                        </th>
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
                No temporal hotspots detected yet. Run some time-travel queries (SELECT ... AS OF) to see clustering.
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
            <h3>
              Background Tasks
              <InfoTip text="AI subsystem background workers. These run periodically to analyze patterns, detect anomalies, cluster hotspots, and save learned state to disk." />
            </h3>
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
