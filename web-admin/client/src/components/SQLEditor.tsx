import { useState, useRef, useEffect } from 'react';
import { api } from '../api';
import type { ChronosResult } from '../types';

interface SQLEditorProps {
  currentDb: string;
}

interface QueryHistoryEntry {
  sql: string;
  result: ChronosResult;
  timestamp: number;
  duration: number;
}

export default function SQLEditor({ currentDb }: SQLEditorProps) {
  const [sql, setSql] = useState('');
  const [executing, setExecuting] = useState(false);
  const [history, setHistory] = useState<QueryHistoryEntry[]>([]);
  const textareaRef = useRef<HTMLTextAreaElement>(null);

  // Auto-resize textarea
  useEffect(() => {
    if (textareaRef.current) {
      textareaRef.current.style.height = 'auto';
      textareaRef.current.style.height = textareaRef.current.scrollHeight + 'px';
    }
  }, [sql]);

  const executeQuery = async () => {
    if (!sql.trim()) return;
    setExecuting(true);
    const start = performance.now();

    try {
      const result = await api.executeQuery(sql.trim());
      const duration = performance.now() - start;
      setHistory(prev => [{
        sql: sql.trim(),
        result,
        timestamp: Date.now(),
        duration,
      }, ...prev].slice(0, 50)); // Keep last 50
    } catch (err: any) {
      const duration = performance.now() - start;
      setHistory(prev => [{
        sql: sql.trim(),
        result: { success: false, error: err.message },
        timestamp: Date.now(),
        duration,
      }, ...prev].slice(0, 50));
    } finally {
      setExecuting(false);
    }
  };

  const handleKeyDown = (e: React.KeyboardEvent) => {
    if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
      e.preventDefault();
      executeQuery();
    }
  };

  const loadExample = (query: string) => {
    setSql(query);
    textareaRef.current?.focus();
  };

  if (!currentDb) {
    return (
      <div className="panel">
        <div className="panel-body">
          <p className="text-muted">Please select a database first (go to Databases page)</p>
        </div>
      </div>
    );
  }

  return (
    <div className="sql-editor">
      {/* Editor */}
      <div className="panel">
        <div className="panel-header">
          <h3>Query Editor</h3>
          <span className="text-muted">Database: {currentDb}</span>
        </div>
        <div className="panel-body">
          <textarea
            ref={textareaRef}
            className="sql-textarea"
            value={sql}
            onChange={(e) => setSql(e.target.value)}
            onKeyDown={handleKeyDown}
            placeholder="Enter SQL query... (Ctrl+Enter to execute)"
            rows={5}
            spellCheck={false}
          />
          <div className="editor-toolbar">
            <button
              className="btn-primary"
              onClick={executeQuery}
              disabled={executing || !sql.trim()}
            >
              {executing ? 'Executing...' : '▶ Execute (Ctrl+Enter)'}
            </button>
            <button className="btn-sm" onClick={() => setSql('')}>Clear</button>
            <div className="example-queries">
              <span className="text-muted">Examples:</span>
              <button className="btn-link" onClick={() => loadExample('SHOW TABLES')}>SHOW TABLES</button>
              <button className="btn-link" onClick={() => loadExample('SHOW STATUS')}>SHOW STATUS</button>
              <button className="btn-link" onClick={() => loadExample('SHOW AI STATUS')}>AI STATUS</button>
              <button className="btn-link" onClick={() => loadExample('SHOW ANOMALIES')}>ANOMALIES</button>
            </div>
          </div>
        </div>
      </div>

      {/* Results */}
      {history.map((entry, idx) => (
        <div key={entry.timestamp} className={`panel result-panel ${entry.result.success ? '' : 'error'}`}>
          <div className="panel-header">
            <div className="result-header-info">
              <code className="result-sql">{entry.sql}</code>
              <span className="result-meta">
                {entry.result.success ? '✓' : '✗'} {entry.duration.toFixed(0)}ms
              </span>
            </div>
            {idx > 0 && (
              <button
                className="btn-sm"
                onClick={() => setHistory(prev => prev.filter((_, i) => i !== idx))}
              >
                Dismiss
              </button>
            )}
          </div>
          <div className="panel-body">
            {entry.result.error && (
              <div className="error-text">{entry.result.error}</div>
            )}
            {entry.result.message && !entry.result.data && (
              <div className="result-message">{entry.result.message}</div>
            )}
            {entry.result.data && (
              <div className="data-table-wrapper">
                {entry.result.truncated && (
                  <div className="warning-banner" style={{ padding: '0.5rem 1rem', background: '#2d2300', color: '#f0c000', borderBottom: '1px solid #4a3d00', fontSize: '0.85rem' }}>
                    Result truncated: showing {entry.result.max_rows?.toLocaleString()} of {entry.result.total_rows?.toLocaleString()} rows. Add a LIMIT clause to your query.
                  </div>
                )}
                <table className="data-table">
                  <thead>
                    <tr>
                      {entry.result.data.columns.map((col, i) => (
                        <th key={i}>{col}</th>
                      ))}
                    </tr>
                  </thead>
                  <tbody>
                    {entry.result.data.rows.map((row, ri) => (
                      <tr key={ri}>
                        {row.map((cell, ci) => (
                          <td key={ci}>{cell}</td>
                        ))}
                      </tr>
                    ))}
                    {entry.result.data.rows.length === 0 && (
                      <tr>
                        <td colSpan={entry.result.data.columns.length} className="text-muted" style={{ textAlign: 'center' }}>
                          Empty result set
                        </td>
                      </tr>
                    )}
                  </tbody>
                </table>
                <div className="result-row-count">
                  {entry.result.truncated
                    ? `${entry.result.max_rows?.toLocaleString()} of ${entry.result.total_rows?.toLocaleString()} row(s)`
                    : `${(entry.result.row_count ?? entry.result.data.rows.length).toLocaleString()} row(s)`}
                </div>
              </div>
            )}
          </div>
        </div>
      ))}

      {history.length === 0 && (
        <div className="panel">
          <div className="panel-body text-center">
            <p className="text-muted">Query results will appear here</p>
          </div>
        </div>
      )}
    </div>
  );
}
