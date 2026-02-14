import { useState, useEffect, useCallback } from 'react';
import { api } from '../api';
import type { ChronosResult } from '../types';
import SchemaBuilder from './SchemaBuilder';

interface DatabaseBrowserProps {
  currentDb: string;
  onUseDatabase: (db: string) => Promise<ChronosResult>;
  onViewTable: (table: string) => void;
}

export default function DatabaseBrowser({ currentDb, onUseDatabase, onViewTable }: DatabaseBrowserProps) {
  const [databases, setDatabases] = useState<string[]>([]);
  const [tables, setTables] = useState<string[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [newDbName, setNewDbName] = useState('');
  const [creating, setCreating] = useState(false);
  const [showSchemaBuilder, setShowSchemaBuilder] = useState(false);

  const loadDatabases = useCallback(async () => {
    try {
      const result = await api.getDatabases();
      if (result.data) {
        setDatabases(result.data.rows.map(r => r[0]));
      }
    } catch (err: any) {
      setError(err.message);
    }
  }, []);

  const loadTables = useCallback(async () => {
    if (!currentDb) return;
    try {
      const result = await api.getTables();
      if (result.data) {
        setTables(result.data.rows.map(r => r[0]));
      }
    } catch {
      setTables([]);
    }
  }, [currentDb]);

  useEffect(() => {
    loadDatabases().then(() => setLoading(false));
  }, [loadDatabases]);

  useEffect(() => {
    loadTables();
  }, [loadTables]);

  const handleUse = async (db: string) => {
    setError('');
    const result = await onUseDatabase(db);
    if (result.error) {
      setError(result.error);
    } else {
      await loadTables();
    }
  };

  const handleCreate = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!newDbName.trim()) return;
    setCreating(true);
    setError('');
    try {
      const result = await api.createDatabase(newDbName.trim());
      if (result.error) {
        setError(result.error);
      } else {
        setNewDbName('');
        await loadDatabases();
      }
    } catch (err: any) {
      setError(err.message);
    } finally {
      setCreating(false);
    }
  };

  const handleDrop = async (db: string) => {
    if (!confirm(`Are you sure you want to drop database "${db}"? This cannot be undone.`)) return;
    try {
      const result = await api.dropDatabase(db);
      if (result.error) {
        setError(result.error);
      } else {
        await loadDatabases();
      }
    } catch (err: any) {
      setError(err.message);
    }
  };

  if (loading) {
    return <div className="loading"><div className="spinner" /> Loading databases...</div>;
  }

  return (
    <div className="database-browser">
      {error && <div className="error-banner">{error}</div>}

      {/* Create Database */}
      <div className="panel">
        <div className="panel-header"><h3>Create Database</h3></div>
        <div className="panel-body">
          <form onSubmit={handleCreate} className="inline-form">
            <input
              type="text"
              placeholder="Database name..."
              value={newDbName}
              onChange={(e) => setNewDbName(e.target.value)}
            />
            <button type="submit" className="btn-primary" disabled={creating}>
              {creating ? 'Creating...' : 'Create'}
            </button>
          </form>
        </div>
      </div>

      {/* Database List */}
      <div className="panel">
        <div className="panel-header">
          <h3>Databases ({databases.length})</h3>
        </div>
        <div className="panel-body">
          <div className="db-list">
            {databases.map((db) => (
              <div key={db} className={`db-item ${db === currentDb ? 'active' : ''}`}>
                <div className="db-item-info">
                  <span className="db-icon">⛁</span>
                  <span className="db-name">{db}</span>
                  {db === currentDb && <span className="badge badge-green">Active</span>}
                </div>
                <div className="db-item-actions">
                  {db !== currentDb && (
                    <button className="btn-sm btn-primary" onClick={() => handleUse(db)}>
                      USE
                    </button>
                  )}
                  <button className="btn-sm btn-danger" onClick={() => handleDrop(db)}>
                    DROP
                  </button>
                </div>
              </div>
            ))}
            {databases.length === 0 && (
              <p className="text-muted">No databases found</p>
            )}
          </div>
        </div>
      </div>

      {/* Tables in Current Database */}
      {currentDb && (
        <div className="panel">
          <div className="panel-header">
            <h3>Tables in "{currentDb}"</h3>
            <button className="btn-sm btn-primary" onClick={() => setShowSchemaBuilder(true)}>
              + Create Table
            </button>
          </div>
          <div className="panel-body">
            <div className="table-list">
              {tables.map((table) => (
                <div key={table} className="table-item" onClick={() => onViewTable(table)}>
                  <span className="table-icon">☰</span>
                  <span>{table}</span>
                  <span className="table-arrow">→</span>
                </div>
              ))}
              {tables.length === 0 && (
                <p className="text-muted">No tables in this database</p>
              )}
            </div>
          </div>
        </div>
      )}

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
