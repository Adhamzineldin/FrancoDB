import { useState, useEffect, useCallback } from 'react';
import { api } from '../api';
import type { ChronosResult } from '../types';

interface TableViewerProps {
  currentDb: string;
  selectedTable: string;
  onSelectTable: (table: string) => void;
}

export default function TableViewer({ currentDb, selectedTable, onSelectTable }: TableViewerProps) {
  const [tables, setTables] = useState<string[]>([]);
  const [schema, setSchema] = useState<ChronosResult | null>(null);
  const [data, setData] = useState<ChronosResult | null>(null);
  const [loading, setLoading] = useState(false);
  const [limit, setLimit] = useState(50);
  const [offset, setOffset] = useState(0);
  const [tab, setTab] = useState<'data' | 'schema'>('data');

  const loadTables = useCallback(async () => {
    try {
      const result = await api.getTables();
      if (result.data) {
        setTables(result.data.rows.map(r => r[0]));
      }
    } catch {}
  }, []);

  const loadTableDetails = useCallback(async () => {
    if (!selectedTable) return;
    setLoading(true);
    try {
      const [schemaRes, dataRes] = await Promise.all([
        api.getTableSchema(selectedTable),
        api.getTableData(selectedTable, limit, offset),
      ]);
      setSchema(schemaRes);
      setData(dataRes);
    } catch {} finally {
      setLoading(false);
    }
  }, [selectedTable, limit, offset]);

  useEffect(() => { loadTables(); }, [loadTables]);
  useEffect(() => { loadTableDetails(); }, [loadTableDetails]);

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
    <div className="table-viewer">
      <div className="tv-layout">
        {/* Table List Sidebar */}
        <div className="tv-sidebar">
          <div className="tv-sidebar-header">Tables</div>
          {tables.map((t) => (
            <button
              key={t}
              className={`tv-table-btn ${t === selectedTable ? 'active' : ''}`}
              onClick={() => { onSelectTable(t); setOffset(0); }}
            >
              {t}
            </button>
          ))}
          {tables.length === 0 && (
            <p className="text-muted" style={{ padding: '1rem' }}>No tables</p>
          )}
        </div>

        {/* Table Content */}
        <div className="tv-content">
          {!selectedTable ? (
            <div className="tv-placeholder">
              <p>Select a table from the sidebar</p>
            </div>
          ) : loading ? (
            <div className="loading"><div className="spinner" /> Loading table data...</div>
          ) : (
            <>
              <div className="tv-header">
                <h3>{selectedTable}</h3>
                <div className="tv-tabs">
                  <button
                    className={`tab-btn ${tab === 'data' ? 'active' : ''}`}
                    onClick={() => setTab('data')}
                  >
                    Data
                  </button>
                  <button
                    className={`tab-btn ${tab === 'schema' ? 'active' : ''}`}
                    onClick={() => setTab('schema')}
                  >
                    Schema
                  </button>
                </div>
              </div>

              {tab === 'data' && data?.data && (
                <>
                  <div className="data-table-wrapper">
                    <table className="data-table">
                      <thead>
                        <tr>
                          {data.data.columns.map((col, i) => (
                            <th key={i}>{col}</th>
                          ))}
                        </tr>
                      </thead>
                      <tbody>
                        {data.data.rows.map((row, ri) => (
                          <tr key={ri}>
                            {row.map((cell, ci) => (
                              <td key={ci}>{cell}</td>
                            ))}
                          </tr>
                        ))}
                        {data.data.rows.length === 0 && (
                          <tr>
                            <td colSpan={data.data.columns.length} className="text-muted" style={{ textAlign: 'center' }}>
                              No data
                            </td>
                          </tr>
                        )}
                      </tbody>
                    </table>
                  </div>
                  <div className="pagination">
                    <span>{data.row_count ?? data.data.rows.length} rows</span>
                    <div className="pagination-controls">
                      <button
                        className="btn-sm"
                        disabled={offset === 0}
                        onClick={() => setOffset(Math.max(0, offset - limit))}
                      >
                        ← Previous
                      </button>
                      <select value={limit} onChange={(e) => { setLimit(+e.target.value); setOffset(0); }}>
                        <option value={25}>25 rows</option>
                        <option value={50}>50 rows</option>
                        <option value={100}>100 rows</option>
                        <option value={500}>500 rows</option>
                      </select>
                      <button
                        className="btn-sm"
                        disabled={!data.data.rows.length || data.data.rows.length < limit}
                        onClick={() => setOffset(offset + limit)}
                      >
                        Next →
                      </button>
                    </div>
                  </div>
                </>
              )}

              {tab === 'data' && !data?.data && data?.error && (
                <div className="error-banner">{data.error}</div>
              )}

              {tab === 'schema' && schema?.data && (
                <div className="data-table-wrapper">
                  <table className="data-table">
                    <thead>
                      <tr>
                        {schema.data.columns.map((col, i) => (
                          <th key={i}>{col}</th>
                        ))}
                      </tr>
                    </thead>
                    <tbody>
                      {schema.data.rows.map((row, ri) => (
                        <tr key={ri}>
                          {row.map((cell, ci) => (
                            <td key={ci}>{cell}</td>
                          ))}
                        </tr>
                      ))}
                    </tbody>
                  </table>
                </div>
              )}
            </>
          )}
        </div>
      </div>
    </div>
  );
}
