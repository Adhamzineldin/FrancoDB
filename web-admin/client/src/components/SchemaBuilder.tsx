import { useState, useCallback } from 'react';
import { api } from '../api';

interface Column {
  name: string;
  type: string;
  primaryKey: boolean;
  notNull: boolean;
  autoIncrement: boolean;
}

interface SchemaBuilderProps {
  currentDb: string;
  onTableCreated: () => void;
  onClose: () => void;
}

const DATA_TYPES = [
  // Integer types
  { value: 'INTEGER', label: 'INTEGER (RAKAM)' },
  { value: 'RAKAM', label: 'RAKAM (INTEGER)' },
  // String types
  { value: 'GOMLA', label: 'GOMLA (VARCHAR)' },
  { value: 'VARCHAR', label: 'VARCHAR (GOMLA)' },
  { value: 'TEXT', label: 'TEXT (STRING)' },
  { value: 'STRING', label: 'STRING (TEXT)' },
  // Boolean types
  { value: 'BOOLEAN', label: 'BOOLEAN (BOOL)' },
  { value: 'BOOL', label: 'BOOL (BOOLEAN)' },
  // Date types
  { value: 'TARE5', label: 'TARE5 (DATE)' },
  { value: 'DATE', label: 'DATE (TARE5)' },
  { value: 'DATETIME', label: 'DATETIME' },
  // Decimal types
  { value: 'KASR', label: 'KASR (DECIMAL)' },
  { value: 'DECIMAL', label: 'DECIMAL (KASR)' },
  { value: 'FLOAT', label: 'FLOAT (DOUBLE)' },
];

export default function SchemaBuilder({ currentDb, onTableCreated, onClose }: SchemaBuilderProps) {
  const [tableName, setTableName] = useState('');
  const [columns, setColumns] = useState<Column[]>([
    { name: 'id', type: 'INTEGER', primaryKey: true, notNull: true, autoIncrement: true },
  ]);
  const [creating, setCreating] = useState(false);
  const [error, setError] = useState('');
  const [createIndex, setCreateIndex] = useState<string[]>([]);

  const addColumn = useCallback(() => {
    setColumns(prev => [
      ...prev,
      { name: '', type: 'INTEGER', primaryKey: false, notNull: false, autoIncrement: false },
    ]);
  }, []);

  const removeColumn = useCallback((index: number) => {
    setColumns(prev => prev.filter((_, i) => i !== index));
    setCreateIndex(prev => prev.filter(col => col !== columns[index].name));
  }, [columns]);

  const updateColumn = useCallback((index: number, field: keyof Column, value: any) => {
    setColumns(prev => prev.map((col, i) => {
      if (i !== index) return col;
      const updated = { ...col, [field]: value };
      // If setting primary key, ensure only one primary key
      if (field === 'primaryKey' && value) {
        return { ...updated, notNull: true };
      }
      return updated;
    }));
  }, []);

  const toggleIndex = useCallback((colName: string) => {
    setCreateIndex(prev =>
      prev.includes(colName)
        ? prev.filter(c => c !== colName)
        : [...prev, colName]
    );
  }, []);

  // Generate SQL for preview (multi-line for readability)
  const generateSQL = useCallback(() => {
    if (!tableName.trim()) return '';

    const colDefs = columns.map(col => {
      let def = `${col.name} ${col.type}`;
      // Order matters: PRIMARY KEY should come before AUTO_INCREMENT
      if (col.primaryKey) def += ' PRIMARY KEY';
      if (col.autoIncrement) def += ' AUTO_INCREMENT';
      // NOT NULL is implied by PRIMARY KEY, so only add if not PK
      if (col.notNull && !col.primaryKey) def += ' NOT NULL';
      return def;
    });

    return `CREATE TABLE ${tableName} (\n  ${colDefs.join(',\n  ')}\n);`;
  }, [tableName, columns]);

  // Generate SQL for execution (single-line to avoid parser issues)
  const generateExecutableSQL = useCallback(() => {
    if (!tableName.trim()) return '';

    const colDefs = columns.map(col => {
      let def = `${col.name} ${col.type}`;
      if (col.primaryKey) def += ' PRIMARY KEY';
      if (col.autoIncrement) def += ' AUTO_INCREMENT';
      if (col.notNull && !col.primaryKey) def += ' NOT NULL';
      return def;
    });

    return `CREATE TABLE ${tableName} (${colDefs.join(', ')});`;
  }, [tableName, columns]);

  const handleCreate = async () => {
    if (!tableName.trim()) {
      setError('Table name is required');
      return;
    }

    const invalidCols = columns.filter(c => !c.name.trim());
    if (invalidCols.length > 0) {
      setError('All columns must have a name');
      return;
    }

    setCreating(true);
    setError('');

    try {
      // Create the table - use single-line SQL to avoid parser issues with multi-line
      const sql = generateExecutableSQL();
      const result = await api.executeQuery(sql);

      if (result.error) {
        setError(result.error);
        setCreating(false);
        return;
      }

      // Create indexes if specified
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
      <div className="modal-content schema-builder">
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

            <table className="schema-table">
              <thead>
                <tr>
                  <th>Name</th>
                  <th>Type</th>
                  <th>PK</th>
                  <th>NOT NULL</th>
                  <th>AUTO_INC</th>
                  <th>Index</th>
                  <th></th>
                </tr>
              </thead>
              <tbody>
                {columns.map((col, idx) => (
                  <tr key={idx}>
                    <td>
                      <input
                        type="text"
                        value={col.name}
                        onChange={(e) => updateColumn(idx, 'name', e.target.value.replace(/[^a-zA-Z0-9_]/g, ''))}
                        placeholder="column_name"
                      />
                    </td>
                    <td>
                      <select
                        value={col.type}
                        onChange={(e) => updateColumn(idx, 'type', e.target.value)}
                      >
                        {DATA_TYPES.map(t => (
                          <option key={t.value} value={t.value}>{t.label}</option>
                        ))}
                      </select>
                    </td>
                    <td>
                      <input
                        type="checkbox"
                        checked={col.primaryKey}
                        onChange={(e) => updateColumn(idx, 'primaryKey', e.target.checked)}
                      />
                    </td>
                    <td>
                      <input
                        type="checkbox"
                        checked={col.notNull || col.primaryKey}
                        disabled={col.primaryKey}
                        onChange={(e) => updateColumn(idx, 'notNull', e.target.checked)}
                      />
                    </td>
                    <td>
                      <input
                        type="checkbox"
                        checked={col.autoIncrement}
                        onChange={(e) => updateColumn(idx, 'autoIncrement', e.target.checked)}
                      />
                    </td>
                    <td>
                      <input
                        type="checkbox"
                        checked={createIndex.includes(col.name)}
                        onChange={() => toggleIndex(col.name)}
                        title={col.primaryKey ? "Primary keys already have an implicit index" : "Create an index on this column"}
                      />
                    </td>
                    <td>
                      {columns.length > 1 && (
                        <button className="btn-sm btn-danger" onClick={() => removeColumn(idx)}>×</button>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
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

