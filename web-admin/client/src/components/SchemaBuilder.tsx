import { useState, useCallback, useEffect } from 'react';
import { api } from '../api';

interface ForeignKey {
  refTable: string;
  refColumn: string;
  onDelete: string;
  onUpdate: string;
}

interface Column {
  name: string;
  type: string;
  length: string;
  primaryKey: boolean;
  notNull: boolean;
  unique: boolean;
  autoIncrement: boolean;
  defaultValue: string;
  hasDefault: boolean;
  check: string;
  hasForeignKey: boolean;
  foreignKey: ForeignKey;
}

interface SchemaBuilderProps {
  currentDb: string;
  onTableCreated: () => void;
  onClose: () => void;
}

const DATA_TYPES = [
  { value: 'INTEGER', label: 'INTEGER (RAKAM)', hasLength: false },
  { value: 'RAKAM', label: 'RAKAM (INTEGER)', hasLength: false },
  { value: 'VARCHAR', label: 'VARCHAR (GOMLA)', hasLength: true },
  { value: 'GOMLA', label: 'GOMLA (VARCHAR)', hasLength: true },
  { value: 'TEXT', label: 'TEXT (STRING)', hasLength: false },
  { value: 'STRING', label: 'STRING (TEXT)', hasLength: false },
  { value: 'BOOLEAN', label: 'BOOLEAN (BOOL)', hasLength: false },
  { value: 'BOOL', label: 'BOOL (BOOLEAN)', hasLength: false },
  { value: 'DATE', label: 'DATE (TARE5)', hasLength: false },
  { value: 'TARE5', label: 'TARE5 (DATE)', hasLength: false },
  { value: 'DATETIME', label: 'DATETIME', hasLength: false },
  { value: 'DECIMAL', label: 'DECIMAL (KASR)', hasLength: false },
  { value: 'KASR', label: 'KASR (DECIMAL)', hasLength: false },
  { value: 'FLOAT', label: 'FLOAT (DOUBLE)', hasLength: false },
];

const FK_ACTIONS = ['NO ACTION', 'CASCADE', 'RESTRICT', 'SET NULL'];

function createEmptyColumn(): Column {
  return {
    name: '', type: 'INTEGER', length: '255',
    primaryKey: false, notNull: false, unique: false, autoIncrement: false,
    defaultValue: '', hasDefault: false, check: '',
    hasForeignKey: false,
    foreignKey: { refTable: '', refColumn: '', onDelete: 'NO ACTION', onUpdate: 'NO ACTION' },
  };
}

export default function SchemaBuilder({ currentDb, onTableCreated, onClose }: SchemaBuilderProps) {
  const [tableName, setTableName] = useState('');
  const [columns, setColumns] = useState<Column[]>([
    { ...createEmptyColumn(), name: 'id', primaryKey: true, notNull: true, autoIncrement: true },
  ]);
  const [creating, setCreating] = useState(false);
  const [error, setError] = useState('');
  const [createIndex, setCreateIndex] = useState<string[]>([]);
  const [expandedCol, setExpandedCol] = useState<number | null>(null);
  const [availableTables, setAvailableTables] = useState<string[]>([]);
  const [tableColumns, setTableColumns] = useState<Record<string, string[]>>({});

  // Load available tables for foreign key references
  useEffect(() => {
    (async () => {
      try {
        const result = await api.getTables();
        if (result.data) {
          const tables = result.data.rows.map(r => r[0]);
          setAvailableTables(tables);
        }
      } catch {}
    })();
  }, []);

  // Load columns for a referenced table
  const loadTableColumns = useCallback(async (table: string) => {
    if (tableColumns[table]) return;
    try {
      const result = await api.getTableSchema(table);
      if (result.data) {
        const cols = result.data.rows.map(r => r[0]);
        setTableColumns(prev => ({ ...prev, [table]: cols }));
      }
    } catch {}
  }, [tableColumns]);

  const addColumn = useCallback(() => {
    setColumns(prev => [...prev, createEmptyColumn()]);
  }, []);

  const removeColumn = useCallback((index: number) => {
    setColumns(prev => prev.filter((_, i) => i !== index));
    setCreateIndex(prev => prev.filter(col => col !== columns[index].name));
    if (expandedCol === index) setExpandedCol(null);
    else if (expandedCol !== null && expandedCol > index) setExpandedCol(expandedCol - 1);
  }, [columns, expandedCol]);

  const updateColumn = useCallback((index: number, updates: Partial<Column>) => {
    setColumns(prev => prev.map((col, i) => {
      if (i !== index) return col;
      const updated = { ...col, ...updates };
      if (updates.primaryKey && updates.primaryKey === true) {
        updated.notNull = true;
      }
      return updated;
    }));
  }, []);

  const updateForeignKey = useCallback((index: number, updates: Partial<ForeignKey>) => {
    setColumns(prev => prev.map((col, i) => {
      if (i !== index) return col;
      return { ...col, foreignKey: { ...col.foreignKey, ...updates } };
    }));
  }, []);

  const toggleIndex = useCallback((colName: string) => {
    setCreateIndex(prev =>
      prev.includes(colName) ? prev.filter(c => c !== colName) : [...prev, colName]
    );
  }, []);

  const typeHasLength = (type: string) => {
    return DATA_TYPES.find(t => t.value === type)?.hasLength ?? false;
  };

  // Generate SQL for preview (multi-line)
  const generateSQL = useCallback(() => {
    if (!tableName.trim()) return '';

    const colDefs: string[] = [];
    const tableFKs: string[] = [];

    for (const col of columns) {
      let def = col.name;
      if (typeHasLength(col.type) && col.length) {
        def += ` ${col.type}(${col.length})`;
      } else {
        def += ` ${col.type}`;
      }
      if (col.primaryKey) def += ' PRIMARY KEY';
      if (col.autoIncrement) def += ' AUTO_INCREMENT';
      if (col.notNull && !col.primaryKey) def += ' NOT NULL';
      if (col.unique && !col.primaryKey) def += ' UNIQUE';
      if (col.hasDefault && col.defaultValue !== '') {
        const needsQuotes = !['INTEGER', 'RAKAM', 'DECIMAL', 'KASR', 'FLOAT', 'BOOLEAN', 'BOOL'].includes(col.type)
          && col.defaultValue.toUpperCase() !== 'NULL'
          && col.defaultValue.toUpperCase() !== 'TRUE'
          && col.defaultValue.toUpperCase() !== 'FALSE'
          && isNaN(Number(col.defaultValue));
        def += ` DEFAULT ${needsQuotes ? `'${col.defaultValue}'` : col.defaultValue}`;
      }
      if (col.check) {
        def += ` CHECK(${col.check})`;
      }
      colDefs.push(def);

      if (col.hasForeignKey && col.foreignKey.refTable && col.foreignKey.refColumn) {
        let fk = `FOREIGN KEY (${col.name}) REFERENCES ${col.foreignKey.refTable}(${col.foreignKey.refColumn})`;
        if (col.foreignKey.onDelete !== 'NO ACTION') {
          fk += ` ON DELETE ${col.foreignKey.onDelete}`;
        }
        if (col.foreignKey.onUpdate !== 'NO ACTION') {
          fk += ` ON UPDATE ${col.foreignKey.onUpdate}`;
        }
        tableFKs.push(fk);
      }
    }

    const allDefs = [...colDefs, ...tableFKs];
    return `CREATE TABLE ${tableName} (\n  ${allDefs.join(',\n  ')}\n);`;
  }, [tableName, columns]);

  // Generate SQL for execution (single-line)
  const generateExecutableSQL = useCallback(() => {
    if (!tableName.trim()) return '';

    const colDefs: string[] = [];
    const tableFKs: string[] = [];

    for (const col of columns) {
      let def = col.name;
      if (typeHasLength(col.type) && col.length) {
        def += ` ${col.type}(${col.length})`;
      } else {
        def += ` ${col.type}`;
      }
      if (col.primaryKey) def += ' PRIMARY KEY';
      if (col.autoIncrement) def += ' AUTO_INCREMENT';
      if (col.notNull && !col.primaryKey) def += ' NOT NULL';
      if (col.unique && !col.primaryKey) def += ' UNIQUE';
      if (col.hasDefault && col.defaultValue !== '') {
        const needsQuotes = !['INTEGER', 'RAKAM', 'DECIMAL', 'KASR', 'FLOAT', 'BOOLEAN', 'BOOL'].includes(col.type)
          && col.defaultValue.toUpperCase() !== 'NULL'
          && col.defaultValue.toUpperCase() !== 'TRUE'
          && col.defaultValue.toUpperCase() !== 'FALSE'
          && isNaN(Number(col.defaultValue));
        def += ` DEFAULT ${needsQuotes ? `'${col.defaultValue}'` : col.defaultValue}`;
      }
      if (col.check) {
        def += ` CHECK(${col.check})`;
      }
      colDefs.push(def);

      if (col.hasForeignKey && col.foreignKey.refTable && col.foreignKey.refColumn) {
        let fk = `FOREIGN KEY (${col.name}) REFERENCES ${col.foreignKey.refTable}(${col.foreignKey.refColumn})`;
        if (col.foreignKey.onDelete !== 'NO ACTION') {
          fk += ` ON DELETE ${col.foreignKey.onDelete}`;
        }
        if (col.foreignKey.onUpdate !== 'NO ACTION') {
          fk += ` ON UPDATE ${col.foreignKey.onUpdate}`;
        }
        tableFKs.push(fk);
      }
    }

    const allDefs = [...colDefs, ...tableFKs];
    return `CREATE TABLE ${tableName} (${allDefs.join(', ')});`;
  }, [tableName, columns]);

  const handleCreate = async () => {
    if (!tableName.trim()) { setError('Table name is required'); return; }
    const invalidCols = columns.filter(c => !c.name.trim());
    if (invalidCols.length > 0) { setError('All columns must have a name'); return; }

    // Validate foreign keys
    for (const col of columns) {
      if (col.hasForeignKey) {
        if (!col.foreignKey.refTable || !col.foreignKey.refColumn) {
          setError(`Foreign key on "${col.name}" requires a reference table and column`);
          return;
        }
      }
    }

    setCreating(true);
    setError('');

    try {
      const sql = generateExecutableSQL();
      const result = await api.executeQuery(sql);
      if (result.error) { setError(result.error); setCreating(false); return; }

      for (const colName of createIndex) {
        const indexSql = `CREATE INDEX idx_${tableName}_${colName} ON ${tableName}(${colName});`;
        await api.executeQuery(indexSql);
      }

      onTableCreated();
      onClose();
    } catch (err: any) {
      setError(err.message || 'Failed to create table');
    } finally {
      setCreating(false);
    }
  };

  if (!currentDb) {
    return (
      <div className="modal-overlay">
        <div className="modal-content">
          <div className="modal-header">
            <h3>Create Table</h3>
            <button className="btn-close" onClick={onClose}>×</button>
          </div>
          <div className="modal-body">
            <p className="text-muted">Please select a database first</p>
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className="modal-overlay">
      <div className="modal-content schema-builder-modal">
        <div className="modal-header">
          <h3>Create New Table</h3>
          <button className="btn-close" onClick={onClose}>×</button>
        </div>

        <div className="modal-body">
          {error && <div className="error-banner">{error}</div>}

          <div className="form-group">
            <label>Table Name</label>
            <input
              type="text"
              value={tableName}
              onChange={(e) => setTableName(e.target.value.replace(/[^a-zA-Z0-9_]/g, ''))}
              placeholder="my_table"
              autoFocus
            />
          </div>

          <div className="schema-columns">
            <div className="schema-columns-header">
              <span>Columns</span>
              <button className="btn-sm btn-primary" onClick={addColumn}>+ Add Column</button>
            </div>

            {columns.map((col, idx) => (
              <div key={idx} className={`schema-col-card ${expandedCol === idx ? 'expanded' : ''}`}>
                {/* Row 1: Name + Type + Length + Remove */}
                <div className="schema-col-top">
                  <div className="schema-col-grip" onClick={() => setExpandedCol(expandedCol === idx ? null : idx)}>
                    <span className="schema-col-expand-icon">{expandedCol === idx ? '▾' : '▸'}</span>
                  </div>
                  <div className="schema-col-field">
                    <label>Name</label>
                    <input
                      type="text"
                      value={col.name}
                      onChange={(e) => updateColumn(idx, { name: e.target.value.replace(/[^a-zA-Z0-9_]/g, '') })}
                      placeholder="column_name"
                    />
                  </div>
                  <div className="schema-col-field">
                    <label>Type</label>
                    <select
                      value={col.type}
                      onChange={(e) => updateColumn(idx, { type: e.target.value })}
                    >
                      {DATA_TYPES.map(t => (
                        <option key={t.value} value={t.value}>{t.label}</option>
                      ))}
                    </select>
                  </div>
                  {typeHasLength(col.type) && (
                    <div className="schema-col-field schema-col-field-sm">
                      <label>Length</label>
                      <input
                        type="number"
                        value={col.length}
                        onChange={(e) => updateColumn(idx, { length: e.target.value })}
                        placeholder="255"
                        min="1"
                        max="65535"
                      />
                    </div>
                  )}
                  {columns.length > 1 && (
                    <button className="btn-sm btn-danger schema-col-remove" onClick={() => removeColumn(idx)}>×</button>
                  )}
                </div>

                {/* Row 2: Constraints as full-label checkboxes */}
                <div className="schema-col-constraints">
                  <label className={`schema-constraint ${col.primaryKey ? 'active pk' : ''}`}>
                    <input type="checkbox" checked={col.primaryKey}
                      onChange={(e) => updateColumn(idx, { primaryKey: e.target.checked })} />
                    Primary Key
                  </label>
                  <label className={`schema-constraint ${col.notNull || col.primaryKey ? 'active nn' : ''}`}>
                    <input type="checkbox" checked={col.notNull || col.primaryKey} disabled={col.primaryKey}
                      onChange={(e) => updateColumn(idx, { notNull: e.target.checked })} />
                    Not Null
                  </label>
                  <label className={`schema-constraint ${col.unique ? 'active uq' : ''}`}>
                    <input type="checkbox" checked={col.unique}
                      onChange={(e) => updateColumn(idx, { unique: e.target.checked })} />
                    Unique
                  </label>
                  <label className={`schema-constraint ${col.autoIncrement ? 'active ai' : ''}`}>
                    <input type="checkbox" checked={col.autoIncrement}
                      onChange={(e) => updateColumn(idx, { autoIncrement: e.target.checked })} />
                    Auto Increment
                  </label>
                  <label className={`schema-constraint ${createIndex.includes(col.name) ? 'active idx' : ''}`}>
                    <input type="checkbox" checked={createIndex.includes(col.name)}
                      onChange={() => toggleIndex(col.name)} />
                    Index
                  </label>
                </div>

                {/* Expanded: DEFAULT, CHECK, FOREIGN KEY */}
                {expandedCol === idx && (
                  <div className="schema-col-details">
                    <div className="schema-detail-row">
                      <div className="schema-detail-group">
                        <label>
                          <input type="checkbox" checked={col.hasDefault}
                            onChange={(e) => updateColumn(idx, { hasDefault: e.target.checked })} />
                          DEFAULT Value
                        </label>
                        {col.hasDefault && (
                          <input
                            type="text"
                            value={col.defaultValue}
                            onChange={(e) => updateColumn(idx, { defaultValue: e.target.value })}
                            placeholder="e.g. 0, 'unknown', NULL"
                            className="schema-detail-input"
                          />
                        )}
                      </div>
                      <div className="schema-detail-group">
                        <label>CHECK Constraint</label>
                        <input
                          type="text"
                          value={col.check}
                          onChange={(e) => updateColumn(idx, { check: e.target.value })}
                          placeholder="e.g. age > 0"
                          className="schema-detail-input"
                        />
                      </div>
                    </div>

                    <div className="schema-fk-section">
                      <label className="schema-fk-toggle">
                        <input type="checkbox" checked={col.hasForeignKey}
                          onChange={(e) => updateColumn(idx, { hasForeignKey: e.target.checked })} />
                        FOREIGN KEY
                      </label>
                      {col.hasForeignKey && (
                        <div className="schema-fk-config">
                          <div className="schema-fk-row">
                            <div className="schema-detail-group">
                              <label>References Table</label>
                              <select
                                value={col.foreignKey.refTable}
                                onChange={(e) => {
                                  updateForeignKey(idx, { refTable: e.target.value, refColumn: '' });
                                  if (e.target.value) loadTableColumns(e.target.value);
                                }}
                                className="schema-detail-input"
                              >
                                <option value="">-- Select Table --</option>
                                {availableTables.map(t => (
                                  <option key={t} value={t}>{t}</option>
                                ))}
                              </select>
                            </div>
                            <div className="schema-detail-group">
                              <label>References Column</label>
                              <select
                                value={col.foreignKey.refColumn}
                                onChange={(e) => updateForeignKey(idx, { refColumn: e.target.value })}
                                className="schema-detail-input"
                                disabled={!col.foreignKey.refTable}
                              >
                                <option value="">-- Select Column --</option>
                                {(tableColumns[col.foreignKey.refTable] || []).map(c => (
                                  <option key={c} value={c}>{c}</option>
                                ))}
                              </select>
                            </div>
                          </div>
                          <div className="schema-fk-row">
                            <div className="schema-detail-group">
                              <label>ON DELETE</label>
                              <select
                                value={col.foreignKey.onDelete}
                                onChange={(e) => updateForeignKey(idx, { onDelete: e.target.value })}
                                className="schema-detail-input"
                              >
                                {FK_ACTIONS.map(a => <option key={a} value={a}>{a}</option>)}
                              </select>
                            </div>
                            <div className="schema-detail-group">
                              <label>ON UPDATE</label>
                              <select
                                value={col.foreignKey.onUpdate}
                                onChange={(e) => updateForeignKey(idx, { onUpdate: e.target.value })}
                                className="schema-detail-input"
                              >
                                {FK_ACTIONS.map(a => <option key={a} value={a}>{a}</option>)}
                              </select>
                            </div>
                          </div>
                        </div>
                      )}
                    </div>
                  </div>
                )}
              </div>
            ))}
          </div>

          <div className="sql-preview">
            <div className="sql-preview-header">SQL Preview</div>
            <pre>{generateSQL() || '-- Enter table name and columns'}</pre>
            {createIndex.length > 0 && (
              <pre>
                {createIndex.map(col =>
                  `CREATE INDEX idx_${tableName}_${col} ON ${tableName}(${col});`
                ).join('\n')}
              </pre>
            )}
          </div>
        </div>

        <div className="modal-footer">
          <button className="btn-secondary" onClick={onClose}>Cancel</button>
          <button
            className="btn-primary"
            onClick={handleCreate}
            disabled={creating || !tableName.trim() || columns.some(c => !c.name.trim())}
          >
            {creating ? 'Creating...' : 'Create Table'}
          </button>
        </div>
      </div>
    </div>
  );
}
