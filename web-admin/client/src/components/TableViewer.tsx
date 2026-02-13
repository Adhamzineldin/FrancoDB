import { useState, useEffect, useCallback, useMemo } from 'react';
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
  const [pageSize, setPageSize] = useState(50);
  const [page, setPage] = useState(0);
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
        api.getTableData(selectedTable),
      ]);
      setSchema(schemaRes);
      setData(dataRes);
      setPage(0);
    } catch {} finally {
      setLoading(false);
    }
  }, [selectedTable]);

  useEffect(() => { loadTables(); }, [loadTables]);
  useEffect(() => { loadTableDetails(); }, [loadTableDetails]);

  // Client-side pagination: slice from the full fetched rows
  const allRows = data?.data?.rows ?? [];
  const totalRows = data?.total_count ?? allRows.length;
  const totalPages = Math.max(1, Math.ceil(allRows.length / pageSize));
  const pageRows = useMemo(
    () => allRows.slice(page * pageSize, (page + 1) * pageSize),
    [allRows, page, pageSize]
  );

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
              onClick={() => { onSelectTable(t); setPage(0); }}
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
                  {data.truncated && (
                    <div style={{ padding: '0.5rem 1rem', background: '#2d2300', color: '#f0c000', fontSize: '0.85rem', flexShrink: 0 }}>
                      Showing first {allRows.length.toLocaleString()} of {totalRows.toLocaleString()} rows (table too large to load fully)
                    </div>
                  )}
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
                        {pageRows.map((row, ri) => (
                          <tr key={page + '-' + ri}>
                            {row.map((cell, ci) => (
                              <td key={ci}>{cell}</td>
                            ))}
                          </tr>
                        ))}
                        {pageRows.length === 0 && (
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
                    <span>
                      {allRows.length > 0
                        ? `${(page * pageSize + 1).toLocaleString()}\u2013${Math.min((page + 1) * pageSize, allRows.length).toLocaleString()} of ${totalRows.toLocaleString()} rows`
                        : '0 rows'}
                    </span>
                    <div className="pagination-controls">
                      <button
                        className="btn-sm"
                        disabled={page === 0}
                        onClick={() => setPage(p => p - 1)}
                      >
                        Previous
                      </button>
                      <span style={{ color: 'var(--text-secondary)', fontSize: '0.8rem' }}>
                        Page {page + 1} of {totalPages}
                      </span>
                      <select value={pageSize} onChange={(e) => { setPageSize(+e.target.value); setPage(0); }}>
                        <option value={25}>25 rows</option>
                        <option value={50}>50 rows</option>
                        <option value={100}>100 rows</option>
                        <option value={500}>500 rows</option>
                      </select>
                      <button
                        className="btn-sm"
                        disabled={page >= totalPages - 1}
                        onClick={() => setPage(p => p + 1)}
                      >
                        Next
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
