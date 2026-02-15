import { useState, useEffect, useCallback, useMemo } from 'react';
import { api } from '../api';
import type { ChronosResult } from '../types';
import SchemaBuilder from './SchemaBuilder';

interface TableViewerProps {
  currentDb: string;
  selectedTable: string;
  onSelectTable: (table: string) => void;
}

interface EditingCell {
  row: number;
  col: number;
  value: string;
}

interface NewRow {
  [colName: string]: string;
}

interface AlterOp {
  type: 'ADD' | 'DROP' | 'MODIFY' | 'RENAME';
  columnName: string;
  newColumnName?: string;
  dataType?: string;
  length?: string;
  notNull?: boolean;
  unique?: boolean;
  defaultValue?: string;
  autoIncrement?: boolean;
}

export default function TableViewer({ currentDb, selectedTable, onSelectTable }: TableViewerProps) {
  const [tables, setTables] = useState<string[]>([]);
  const [schema, setSchema] = useState<ChronosResult | null>(null);
  const [data, setData] = useState<ChronosResult | null>(null);
  const [loading, setLoading] = useState(false);
  const [pageSize, setPageSize] = useState(50);
  const [page, setPage] = useState(0);
  const [tab, setTab] = useState<'data' | 'schema' | 'alter'>('data');
  const [showSchemaBuilder, setShowSchemaBuilder] = useState(false);

  // Row editing state
  const [editingCell, setEditingCell] = useState<EditingCell | null>(null);
  const [pendingEdits, setPendingEdits] = useState<Map<string, Map<string, string>>>(new Map());
  const [saving, setSaving] = useState(false);
  const [editError, setEditError] = useState('');
  const [editSuccess, setEditSuccess] = useState('');

  // New row insertion state
  const [showAddRow, setShowAddRow] = useState(false);
  const [newRow, setNewRow] = useState<NewRow>({});
  const [addingRow, setAddingRow] = useState(false);

  // Delete row state
  const [deletingRow, setDeletingRow] = useState<number | null>(null);

  // ALTER TABLE state
  const [alterOps, setAlterOps] = useState<AlterOp[]>([]);
  const [alterError, setAlterError] = useState('');
  const [alterSuccess, setAlterSuccess] = useState('');
  const [altering, setAltering] = useState(false);
  const [showCreateTable, setShowCreateTable] = useState('');

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
      setPendingEdits(new Map());
      setEditingCell(null);
      setEditError('');
      setEditSuccess('');
      setShowAddRow(false);
      setNewRow({});

      // Load SHOW CREATE TABLE for alter tab
      try {
        const createRes = await api.executeQuery(`SHOW CREATE TABLE ${selectedTable};`);
        if (createRes.data && createRes.data.rows.length > 0) {
          setShowCreateTable(createRes.data.rows[0][1] || createRes.data.rows[0][0] || '');
        }
      } catch {}
    } catch {} finally {
      setLoading(false);
    }
  }, [selectedTable]);

  useEffect(() => { loadTables(); }, [loadTables]);
  useEffect(() => { loadTableDetails(); }, [loadTableDetails]);

  // Client-side pagination
  const allRows = data?.data?.rows ?? [];
  const totalRows = data?.total_count ?? allRows.length;
  const totalPages = Math.max(1, Math.ceil(allRows.length / pageSize));
  const pageRows = useMemo(
    () => allRows.slice(page * pageSize, (page + 1) * pageSize),
    [allRows, page, pageSize]
  );

  const columns = data?.data?.columns ?? [];
  const schemaRows = schema?.data?.rows ?? [];
  const schemaColumns = schema?.data?.columns ?? [];

  // Find primary key column(s)
  const pkColumns = useMemo(() => {
    if (!schema?.data) return [];
    const keyIdx = schemaColumns.indexOf('Key');
    const nameIdx = schemaColumns.indexOf('Column');
    if (keyIdx < 0 || nameIdx < 0) return [];
    return schemaRows.filter(r => r[keyIdx] === 'PRI').map(r => r[nameIdx]);
  }, [schema, schemaColumns, schemaRows]);

  // Get row identifier (PK values)
  const getRowKey = useCallback((row: string[]) => {
    if (pkColumns.length === 0) return null;
    return pkColumns.map(pk => {
      const idx = columns.indexOf(pk);
      return idx >= 0 ? row[idx] : '';
    }).join('|');
  }, [pkColumns, columns]);

  // Handle cell click to edit
  const handleCellClick = useCallback((rowIndex: number, colIndex: number, value: string) => {
    if (pkColumns.length === 0) return; // Can't edit without PK
    setEditingCell({ row: rowIndex, col: colIndex, value });
    setEditError('');
    setEditSuccess('');
  }, [pkColumns]);

  // Handle cell edit save
  const handleCellSave = useCallback(async () => {
    if (!editingCell || !data?.data) return;
    const actualRowIdx = page * pageSize + editingCell.row;
    const row = allRows[actualRowIdx];
    if (!row) return;

    const colName = columns[editingCell.col];
    const oldValue = row[editingCell.col];
    if (editingCell.value === oldValue) {
      setEditingCell(null);
      return;
    }

    setSaving(true);
    setEditError('');

    // Build WHERE clause with PKs
    const whereClause = pkColumns.map(pk => {
      const idx = columns.indexOf(pk);
      const val = row[idx];
      return `${pk} = ${isNaN(Number(val)) ? `'${val}'` : val}`;
    }).join(' AND ');

    const newVal = editingCell.value === '' || editingCell.value.toUpperCase() === 'NULL'
      ? 'NULL'
      : (isNaN(Number(editingCell.value)) ? `'${editingCell.value}'` : editingCell.value);

    const sql = `UPDATE ${selectedTable} SET ${colName} = ${newVal} WHERE ${whereClause};`;

    try {
      const result = await api.executeQuery(sql);
      if (result.error) {
        setEditError(result.error);
      } else {
        setEditSuccess(`Updated ${colName} successfully`);
        setTimeout(() => setEditSuccess(''), 3000);
        // Reload data
        const dataRes = await api.getTableData(selectedTable);
        setData(dataRes);
      }
    } catch (err: any) {
      setEditError(err.message || 'Failed to update');
    } finally {
      setSaving(false);
      setEditingCell(null);
    }
  }, [editingCell, data, allRows, columns, pkColumns, selectedTable, page, pageSize]);

  // Handle add new row
  const handleAddRow = useCallback(async () => {
    if (!data?.data) return;
    setAddingRow(true);
    setEditError('');

    const cols: string[] = [];
    const vals: string[] = [];

    for (const col of columns) {
      const val = newRow[col];
      if (val !== undefined && val !== '') {
        cols.push(col);
        if (val.toUpperCase() === 'NULL') {
          vals.push('NULL');
        } else if (isNaN(Number(val))) {
          vals.push(`'${val}'`);
        } else {
          vals.push(val);
        }
      }
    }

    if (cols.length === 0) {
      setEditError('At least one column value is required');
      setAddingRow(false);
      return;
    }

    const sql = `INSERT INTO ${selectedTable} (${cols.join(', ')}) VALUES (${vals.join(', ')});`;

    try {
      const result = await api.executeQuery(sql);
      if (result.error) {
        setEditError(result.error);
      } else {
        setEditSuccess('Row inserted successfully');
        setTimeout(() => setEditSuccess(''), 3000);
        setShowAddRow(false);
        setNewRow({});
        const dataRes = await api.getTableData(selectedTable);
        setData(dataRes);
      }
    } catch (err: any) {
      setEditError(err.message || 'Failed to insert row');
    } finally {
      setAddingRow(false);
    }
  }, [data, columns, newRow, selectedTable]);

  // Handle delete row
  const handleDeleteRow = useCallback(async (rowIndex: number) => {
    const actualRowIdx = page * pageSize + rowIndex;
    const row = allRows[actualRowIdx];
    if (!row || pkColumns.length === 0) return;

    setDeletingRow(rowIndex);
    setEditError('');

    const whereClause = pkColumns.map(pk => {
      const idx = columns.indexOf(pk);
      const val = row[idx];
      return `${pk} = ${isNaN(Number(val)) ? `'${val}'` : val}`;
    }).join(' AND ');

    const sql = `DELETE FROM ${selectedTable} WHERE ${whereClause};`;

    try {
      const result = await api.executeQuery(sql);
      if (result.error) {
        setEditError(result.error);
      } else {
        setEditSuccess('Row deleted successfully');
        setTimeout(() => setEditSuccess(''), 3000);
        const dataRes = await api.getTableData(selectedTable);
        setData(dataRes);
      }
    } catch (err: any) {
      setEditError(err.message || 'Failed to delete row');
    } finally {
      setDeletingRow(null);
    }
  }, [allRows, columns, pkColumns, selectedTable, page, pageSize]);

  // ALTER TABLE operations
  const addAlterOp = useCallback((type: AlterOp['type']) => {
    setAlterOps(prev => [...prev, {
      type,
      columnName: '',
      newColumnName: '',
      dataType: 'INTEGER',
      length: '255',
      notNull: false,
      unique: false,
      defaultValue: '',
      autoIncrement: false,
    }]);
  }, []);

  const updateAlterOp = useCallback((index: number, updates: Partial<AlterOp>) => {
    setAlterOps(prev => prev.map((op, i) => i === index ? { ...op, ...updates } : op));
  }, []);

  const removeAlterOp = useCallback((index: number) => {
    setAlterOps(prev => prev.filter((_, i) => i !== index));
  }, []);

  const generateAlterSQL = useCallback(() => {
    if (!selectedTable || alterOps.length === 0) return '';
    return alterOps.map(op => {
      switch (op.type) {
        case 'ADD': {
          let sql = `ALTER TABLE ${selectedTable} ADD COLUMN ${op.columnName} ${op.dataType}`;
          if (['VARCHAR', 'GOMLA'].includes(op.dataType || '') && op.length) sql += `(${op.length})`;
          if (op.autoIncrement) sql += ' AUTO_INCREMENT';
          if (op.notNull) sql += ' NOT NULL';
          if (op.unique) sql += ' UNIQUE';
          if (op.defaultValue) {
            const needsQuotes = !['INTEGER', 'RAKAM', 'DECIMAL', 'KASR', 'FLOAT', 'BOOLEAN', 'BOOL'].includes(op.dataType || '')
              && op.defaultValue.toUpperCase() !== 'NULL' && isNaN(Number(op.defaultValue));
            sql += ` DEFAULT ${needsQuotes ? `'${op.defaultValue}'` : op.defaultValue}`;
          }
          return sql + ';';
        }
        case 'DROP':
          return `ALTER TABLE ${selectedTable} DROP COLUMN ${op.columnName};`;
        case 'MODIFY': {
          let sql = `ALTER TABLE ${selectedTable} MODIFY COLUMN ${op.columnName} ${op.dataType}`;
          if (['VARCHAR', 'GOMLA'].includes(op.dataType || '') && op.length) sql += `(${op.length})`;
          if (op.notNull) sql += ' NOT NULL';
          if (op.unique) sql += ' UNIQUE';
          if (op.defaultValue) {
            const needsQuotes = !['INTEGER', 'RAKAM', 'DECIMAL', 'KASR', 'FLOAT', 'BOOLEAN', 'BOOL'].includes(op.dataType || '')
              && op.defaultValue.toUpperCase() !== 'NULL' && isNaN(Number(op.defaultValue));
            sql += ` DEFAULT ${needsQuotes ? `'${op.defaultValue}'` : op.defaultValue}`;
          }
          return sql + ';';
        }
        case 'RENAME':
          return `ALTER TABLE ${selectedTable} RENAME COLUMN ${op.columnName} TO ${op.newColumnName};`;
        default:
          return '';
      }
    }).join('\n');
  }, [selectedTable, alterOps]);

  const executeAlterOps = useCallback(async () => {
    if (alterOps.length === 0) return;

    // Validate
    for (const op of alterOps) {
      if (!op.columnName) {
        setAlterError('All operations require a column name');
        return;
      }
      if (op.type === 'RENAME' && !op.newColumnName) {
        setAlterError('Rename requires a new column name');
        return;
      }
    }

    setAltering(true);
    setAlterError('');
    setAlterSuccess('');

    const sqls = generateAlterSQL().split('\n').filter(s => s.trim());

    try {
      for (const sql of sqls) {
        const result = await api.executeQuery(sql);
        if (result.error) {
          setAlterError(result.error);
          setAltering(false);
          return;
        }
      }
      setAlterSuccess('Schema updated successfully');
      setAlterOps([]);
      setTimeout(() => setAlterSuccess(''), 3000);
      // Reload
      await loadTableDetails();
      await loadTables();
    } catch (err: any) {
      setAlterError(err.message || 'Failed to alter table');
    } finally {
      setAltering(false);
    }
  }, [alterOps, generateAlterSQL, loadTableDetails, loadTables]);

  const DATA_TYPES_SIMPLE = [
    'INTEGER', 'RAKAM', 'VARCHAR', 'GOMLA', 'TEXT', 'STRING',
    'BOOLEAN', 'BOOL', 'DATE', 'TARE5', 'DATETIME', 'DECIMAL', 'KASR', 'FLOAT',
  ];

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
          <div className="tv-sidebar-header">
            <span>Tables</span>
            <button
              className="btn-sm btn-primary"
              onClick={() => setShowSchemaBuilder(true)}
              title="Create new table"
              style={{ padding: '0.25rem 0.5rem', fontSize: '0.7rem' }}
            >
              + New
            </button>
          </div>
          {tables.map((t) => (
            <button
              key={t}
              className={`tv-table-btn ${t === selectedTable ? 'active' : ''}`}
              onClick={() => { onSelectTable(t); setPage(0); setTab('data'); }}
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
                  <button className={`tab-btn ${tab === 'data' ? 'active' : ''}`} onClick={() => setTab('data')}>
                    Data
                  </button>
                  <button className={`tab-btn ${tab === 'schema' ? 'active' : ''}`} onClick={() => setTab('schema')}>
                    Schema
                  </button>
                  <button className={`tab-btn ${tab === 'alter' ? 'active' : ''}`} onClick={() => setTab('alter')}>
                    Alter Table
                  </button>
                </div>
              </div>

              {/* ========== DATA TAB ========== */}
              {tab === 'data' && data?.data && (
                <>
                  {editError && <div className="tv-banner error">{editError}</div>}
                  {editSuccess && <div className="tv-banner success">{editSuccess}</div>}
                  {data.truncated && (
                    <div className="tv-banner warn">
                      Showing first {allRows.length.toLocaleString()} of {totalRows.toLocaleString()} rows (table too large to load fully)
                    </div>
                  )}

                  {/* Toolbar */}
                  <div className="tv-data-toolbar">
                    <button className="btn-sm btn-primary" onClick={() => {
                      setShowAddRow(!showAddRow);
                      setNewRow({});
                    }}>
                      {showAddRow ? 'Cancel' : '+ Add Row'}
                    </button>
                    <button className="btn-sm" onClick={loadTableDetails}>Refresh</button>
                    {pkColumns.length === 0 && (
                      <span className="tv-toolbar-hint">
                        Editing disabled: table has no primary key
                      </span>
                    )}
                    {pkColumns.length > 0 && (
                      <span className="tv-toolbar-hint">
                        Click any cell to edit inline
                      </span>
                    )}
                  </div>

                  {/* New Row Form */}
                  {showAddRow && (
                    <div className="tv-add-row">
                      <div className="tv-add-row-header">Insert New Row</div>
                      <div className="tv-add-row-fields">
                        {columns.map((col, ci) => {
                          const schemaRow = schemaRows.find(r => r[0] === col);
                          const extra = schemaRow ? schemaRow[5] : '';
                          const isAuto = extra.toLowerCase().includes('auto_increment');
                          return (
                            <div key={ci} className="tv-add-row-field">
                              <label>
                                {col}
                                {isAuto && <span className="tv-auto-tag">AUTO</span>}
                              </label>
                              <input
                                type="text"
                                value={newRow[col] || ''}
                                onChange={(e) => setNewRow(prev => ({ ...prev, [col]: e.target.value }))}
                                placeholder={isAuto ? '(auto)' : `Value for ${col}`}
                                disabled={isAuto}
                              />
                            </div>
                          );
                        })}
                      </div>
                      <div className="tv-add-row-actions">
                        <button className="btn-primary" onClick={handleAddRow} disabled={addingRow}>
                          {addingRow ? 'Inserting...' : 'Insert Row'}
                        </button>
                        <button className="btn-secondary" onClick={() => { setShowAddRow(false); setNewRow({}); }}>
                          Cancel
                        </button>
                      </div>
                    </div>
                  )}

                  <div className="data-table-wrapper">
                    <table className="data-table">
                      <thead>
                        <tr>
                          {columns.map((col, i) => (
                            <th key={i}>{col}</th>
                          ))}
                          {pkColumns.length > 0 && <th className="tv-actions-col">Actions</th>}
                        </tr>
                      </thead>
                      <tbody>
                        {pageRows.map((row, ri) => (
                          <tr key={page + '-' + ri}>
                            {row.map((cell, ci) => (
                              <td
                                key={ci}
                                className={`${editingCell?.row === ri && editingCell?.col === ci ? 'editing' : ''} ${pkColumns.length > 0 ? 'editable' : ''}`}
                                onClick={() => {
                                  if (!(editingCell?.row === ri && editingCell?.col === ci)) {
                                    handleCellClick(ri, ci, cell);
                                  }
                                }}
                              >
                                {editingCell?.row === ri && editingCell?.col === ci ? (
                                  <input
                                    type="text"
                                    className="tv-cell-edit"
                                    value={editingCell.value}
                                    onChange={(e) => setEditingCell({ ...editingCell, value: e.target.value })}
                                    onKeyDown={(e) => {
                                      if (e.key === 'Enter') handleCellSave();
                                      if (e.key === 'Escape') setEditingCell(null);
                                    }}
                                    onBlur={handleCellSave}
                                    autoFocus
                                    disabled={saving}
                                  />
                                ) : (
                                  <span className="tv-cell-value">{cell}</span>
                                )}
                              </td>
                            ))}
                            {pkColumns.length > 0 && (
                              <td className="tv-actions-cell">
                                <button
                                  className="btn-sm btn-danger"
                                  onClick={() => handleDeleteRow(ri)}
                                  disabled={deletingRow === ri}
                                  title="Delete this row"
                                >
                                  {deletingRow === ri ? '...' : 'Del'}
                                </button>
                              </td>
                            )}
                          </tr>
                        ))}
                        {pageRows.length === 0 && (
                          <tr>
                            <td colSpan={columns.length + (pkColumns.length > 0 ? 1 : 0)} className="text-muted" style={{ textAlign: 'center' }}>
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
                      <button className="btn-sm" disabled={page === 0} onClick={() => setPage(p => p - 1)}>Previous</button>
                      <span style={{ color: 'var(--text-secondary)', fontSize: '0.8rem' }}>Page {page + 1} of {totalPages}</span>
                      <select value={pageSize} onChange={(e) => { setPageSize(+e.target.value); setPage(0); }}>
                        <option value={25}>25 rows</option>
                        <option value={50}>50 rows</option>
                        <option value={100}>100 rows</option>
                        <option value={500}>500 rows</option>
                      </select>
                      <button className="btn-sm" disabled={page >= totalPages - 1} onClick={() => setPage(p => p + 1)}>Next</button>
                    </div>
                  </div>
                </>
              )}

              {tab === 'data' && !data?.data && data?.error && (
                <div className="error-banner" style={{ margin: '1rem' }}>{data.error}</div>
              )}

              {/* ========== SCHEMA TAB ========== */}
              {tab === 'schema' && schema?.data && (
                <div className="tv-schema-view">
                  <div className="tv-schema-cards">
                    {schemaRows.map((row, ri) => {
                      const name = row[schemaColumns.indexOf('Column')] || row[0];
                      const type = row[schemaColumns.indexOf('Type')] || row[1];
                      const nullable = row[schemaColumns.indexOf('Nullable')] || row[2];
                      const key = row[schemaColumns.indexOf('Key')] || row[3];
                      const defaultVal = row[schemaColumns.indexOf('Default')] || row[4];
                      const extra = row[schemaColumns.indexOf('Extra')] || row[5];

                      return (
                        <div key={ri} className="tv-schema-card">
                          <div className="tv-schema-card-header">
                            <span className="tv-schema-col-name">{name}</span>
                            <span className="tv-schema-col-type">{type}</span>
                          </div>
                          <div className="tv-schema-card-badges">
                            {key === 'PRI' && <span className="schema-badge-tag pk">PRIMARY KEY</span>}
                            {key === 'UNI' && <span className="schema-badge-tag uq">UNIQUE</span>}
                            {nullable === 'NO' && <span className="schema-badge-tag nn">NOT NULL</span>}
                            {nullable === 'YES' && <span className="schema-badge-tag nullable">NULLABLE</span>}
                            {extra && extra.includes('auto_increment') && <span className="schema-badge-tag ai">AUTO_INCREMENT</span>}
                          </div>
                          <div className="tv-schema-card-details">
                            {defaultVal && (
                              <div className="tv-schema-detail">
                                <span className="tv-schema-detail-label">Default:</span>
                                <span className="tv-schema-detail-value">{defaultVal}</span>
                              </div>
                            )}
                            {extra && !extra.includes('auto_increment') && (
                              <div className="tv-schema-detail">
                                <span className="tv-schema-detail-label">Extra:</span>
                                <span className="tv-schema-detail-value">{extra}</span>
                              </div>
                            )}
                          </div>
                        </div>
                      );
                    })}
                  </div>

                  {/* Raw schema table */}
                  <div className="tv-schema-raw">
                    <div className="tv-schema-raw-header">Raw Schema Details</div>
                    <div className="data-table-wrapper" style={{ maxHeight: '300px' }}>
                      <table className="data-table">
                        <thead>
                          <tr>
                            {schemaColumns.map((col, i) => (
                              <th key={i}>{col}</th>
                            ))}
                          </tr>
                        </thead>
                        <tbody>
                          {schemaRows.map((row, ri) => (
                            <tr key={ri}>
                              {row.map((cell, ci) => (
                                <td key={ci}>{cell || '-'}</td>
                              ))}
                            </tr>
                          ))}
                        </tbody>
                      </table>
                    </div>
                  </div>

                  {/* CREATE TABLE statement */}
                  {showCreateTable && (
                    <div className="sql-preview" style={{ margin: '1rem' }}>
                      <div className="sql-preview-header">CREATE TABLE Statement</div>
                      <pre>{showCreateTable}</pre>
                    </div>
                  )}
                </div>
              )}

              {/* ========== ALTER TABLE TAB ========== */}
              {tab === 'alter' && (
                <div className="tv-alter-view">
                  {alterError && <div className="tv-banner error">{alterError}</div>}
                  {alterSuccess && <div className="tv-banner success">{alterSuccess}</div>}

                  <div className="tv-alter-header">
                    <span>Modify Schema: {selectedTable}</span>
                    <div className="tv-alter-actions">
                      <button className="btn-sm btn-primary" onClick={() => addAlterOp('ADD')}>+ Add Column</button>
                      <button className="btn-sm" onClick={() => addAlterOp('DROP')}>Drop Column</button>
                      <button className="btn-sm" onClick={() => addAlterOp('MODIFY')}>Modify Column</button>
                      <button className="btn-sm" onClick={() => addAlterOp('RENAME')}>Rename Column</button>
                    </div>
                  </div>

                  {/* Current schema reference */}
                  {schema?.data && (
                    <div className="tv-alter-current">
                      <div className="tv-alter-current-header">Current Columns</div>
                      <div className="tv-alter-current-cols">
                        {schemaRows.map((row, i) => (
                          <span key={i} className="tv-alter-col-tag">
                            {row[0]} <span className="tv-alter-col-type">{row[1]}</span>
                          </span>
                        ))}
                      </div>
                    </div>
                  )}

                  {/* Operations */}
                  {alterOps.length === 0 && (
                    <div className="tv-alter-empty">
                      <p>No operations queued. Use the buttons above to add schema modifications.</p>
                    </div>
                  )}

                  {alterOps.map((op, idx) => (
                    <div key={idx} className={`tv-alter-op tv-alter-op-${op.type.toLowerCase()}`}>
                      <div className="tv-alter-op-header">
                        <span className={`tv-alter-op-badge ${op.type.toLowerCase()}`}>{op.type}</span>
                        <button className="btn-sm btn-danger" onClick={() => removeAlterOp(idx)}>Remove</button>
                      </div>
                      <div className="tv-alter-op-body">
                        {(op.type === 'DROP' || op.type === 'RENAME') && (
                          <div className="tv-alter-op-field">
                            <label>Column</label>
                            <select
                              value={op.columnName}
                              onChange={(e) => updateAlterOp(idx, { columnName: e.target.value })}
                            >
                              <option value="">-- Select Column --</option>
                              {schemaRows.map((r, i) => (
                                <option key={i} value={r[0]}>{r[0]} ({r[1]})</option>
                              ))}
                            </select>
                          </div>
                        )}

                        {op.type === 'RENAME' && (
                          <div className="tv-alter-op-field">
                            <label>New Name</label>
                            <input
                              type="text"
                              value={op.newColumnName || ''}
                              onChange={(e) => updateAlterOp(idx, { newColumnName: e.target.value.replace(/[^a-zA-Z0-9_]/g, '') })}
                              placeholder="new_column_name"
                            />
                          </div>
                        )}

                        {op.type === 'MODIFY' && (
                          <div className="tv-alter-op-field">
                            <label>Column</label>
                            <select
                              value={op.columnName}
                              onChange={(e) => updateAlterOp(idx, { columnName: e.target.value })}
                            >
                              <option value="">-- Select Column --</option>
                              {schemaRows.map((r, i) => (
                                <option key={i} value={r[0]}>{r[0]} ({r[1]})</option>
                              ))}
                            </select>
                          </div>
                        )}

                        {op.type === 'ADD' && (
                          <div className="tv-alter-op-field">
                            <label>Column Name</label>
                            <input
                              type="text"
                              value={op.columnName}
                              onChange={(e) => updateAlterOp(idx, { columnName: e.target.value.replace(/[^a-zA-Z0-9_]/g, '') })}
                              placeholder="new_column"
                            />
                          </div>
                        )}

                        {(op.type === 'ADD' || op.type === 'MODIFY') && (
                          <>
                            <div className="tv-alter-op-field">
                              <label>Data Type</label>
                              <select
                                value={op.dataType}
                                onChange={(e) => updateAlterOp(idx, { dataType: e.target.value })}
                              >
                                {DATA_TYPES_SIMPLE.map(t => <option key={t} value={t}>{t}</option>)}
                              </select>
                            </div>
                            {['VARCHAR', 'GOMLA'].includes(op.dataType || '') && (
                              <div className="tv-alter-op-field">
                                <label>Length</label>
                                <input type="number" value={op.length || '255'}
                                  onChange={(e) => updateAlterOp(idx, { length: e.target.value })}
                                  min="1" max="65535" />
                              </div>
                            )}
                            <div className="tv-alter-op-checks">
                              <label><input type="checkbox" checked={op.notNull || false}
                                onChange={(e) => updateAlterOp(idx, { notNull: e.target.checked })} /> NOT NULL</label>
                              <label><input type="checkbox" checked={op.unique || false}
                                onChange={(e) => updateAlterOp(idx, { unique: e.target.checked })} /> UNIQUE</label>
                              {op.type === 'ADD' && (
                                <label><input type="checkbox" checked={op.autoIncrement || false}
                                  onChange={(e) => updateAlterOp(idx, { autoIncrement: e.target.checked })} /> AUTO_INCREMENT</label>
                              )}
                            </div>
                            <div className="tv-alter-op-field">
                              <label>Default Value</label>
                              <input type="text" value={op.defaultValue || ''}
                                onChange={(e) => updateAlterOp(idx, { defaultValue: e.target.value })}
                                placeholder="(optional)" />
                            </div>
                          </>
                        )}
                      </div>
                    </div>
                  ))}

                  {/* SQL Preview + Execute */}
                  {alterOps.length > 0 && (
                    <>
                      <div className="sql-preview" style={{ margin: '1rem' }}>
                        <div className="sql-preview-header">SQL Preview</div>
                        <pre>{generateAlterSQL()}</pre>
                      </div>
                      <div className="tv-alter-execute">
                        <button
                          className="btn-primary"
                          onClick={executeAlterOps}
                          disabled={altering || alterOps.some(op => !op.columnName)}
                        >
                          {altering ? 'Executing...' : 'Execute Changes'}
                        </button>
                        <button className="btn-secondary" onClick={() => { setAlterOps([]); setAlterError(''); }}>
                          Clear All
                        </button>
                      </div>
                    </>
                  )}
                </div>
              )}
            </>
          )}
        </div>
      </div>

      {/* Schema Builder Modal */}
      {showSchemaBuilder && (
        <SchemaBuilder
          currentDb={currentDb}
          onTableCreated={loadTables}
          onClose={() => setShowSchemaBuilder(false)}
        />
      )}
    </div>
  );
}
