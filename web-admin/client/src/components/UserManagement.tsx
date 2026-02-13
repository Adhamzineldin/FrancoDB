import { useState, useEffect, useCallback } from 'react';
import { api } from '../api';

export default function UserManagement() {
  const [users, setUsers] = useState<string[][]>([]);
  const [columns, setColumns] = useState<string[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [success, setSuccess] = useState('');

  // Create user form
  const [newUsername, setNewUsername] = useState('');
  const [newPassword, setNewPassword] = useState('');
  const [newRole, setNewRole] = useState('READONLY');

  // Change role form
  const [editUser, setEditUser] = useState('');
  const [editRole, setEditRole] = useState('');
  const [editDb, setEditDb] = useState('');

  const loadUsers = useCallback(async () => {
    try {
      const result = await api.getUsers();
      if (result.data) {
        setColumns(result.data.columns);
        setUsers(result.data.rows);
      }
      if (result.error) {
        setError(result.error);
      }
    } catch (err: any) {
      setError(err.message);
    } finally {
      setLoading(false);
    }
  }, []);

  useEffect(() => { loadUsers(); }, [loadUsers]);

  const handleCreate = async (e: React.FormEvent) => {
    e.preventDefault();
    setError('');
    setSuccess('');
    try {
      const result = await api.createUser(newUsername, newPassword, newRole);
      if (result.error) {
        setError(result.error);
      } else {
        setSuccess(`User "${newUsername}" created successfully`);
        setNewUsername('');
        setNewPassword('');
        setNewRole('READONLY');
        await loadUsers();
      }
    } catch (err: any) {
      setError(err.message);
    }
  };

  const handleDelete = async (username: string) => {
    if (!confirm(`Delete user "${username}"?`)) return;
    setError('');
    setSuccess('');
    try {
      const result = await api.deleteUser(username);
      if (result.error) {
        setError(result.error);
      } else {
        setSuccess(`User "${username}" deleted`);
        await loadUsers();
      }
    } catch (err: any) {
      setError(err.message);
    }
  };

  const handleChangeRole = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!editUser || !editRole) return;
    setError('');
    setSuccess('');
    try {
      const result = await api.changeRole(editUser, editRole, editDb || undefined);
      if (result.error) {
        setError(result.error);
      } else {
        setSuccess(`Role updated for "${editUser}"`);
        setEditUser('');
        setEditRole('');
        setEditDb('');
        await loadUsers();
      }
    } catch (err: any) {
      setError(err.message);
    }
  };

  if (loading) {
    return <div className="loading"><div className="spinner" /> Loading users...</div>;
  }

  return (
    <div className="user-management">
      {error && <div className="error-banner">{error}</div>}
      {success && <div className="success-banner">{success}</div>}

      {/* Create User */}
      <div className="panel">
        <div className="panel-header"><h3>Create User</h3></div>
        <div className="panel-body">
          <form onSubmit={handleCreate} className="form-grid">
            <div className="form-group">
              <label>Username</label>
              <input
                type="text"
                value={newUsername}
                onChange={(e) => setNewUsername(e.target.value)}
                placeholder="username"
                required
              />
            </div>
            <div className="form-group">
              <label>Password</label>
              <input
                type="password"
                value={newPassword}
                onChange={(e) => setNewPassword(e.target.value)}
                placeholder="password"
                required
              />
            </div>
            <div className="form-group">
              <label>Role</label>
              <select value={newRole} onChange={(e) => setNewRole(e.target.value)}>
                <option value="READONLY">READONLY</option>
                <option value="NORMAL">NORMAL</option>
                <option value="ADMIN">ADMIN</option>
                <option value="SUPERADMIN">SUPERADMIN</option>
              </select>
            </div>
            <div className="form-group form-submit">
              <button type="submit" className="btn-primary">Create User</button>
            </div>
          </form>
        </div>
      </div>

      {/* Change Role */}
      <div className="panel">
        <div className="panel-header"><h3>Change User Role</h3></div>
        <div className="panel-body">
          <form onSubmit={handleChangeRole} className="form-grid">
            <div className="form-group">
              <label>Username</label>
              <input
                type="text"
                value={editUser}
                onChange={(e) => setEditUser(e.target.value)}
                placeholder="username"
                required
              />
            </div>
            <div className="form-group">
              <label>New Role</label>
              <select value={editRole} onChange={(e) => setEditRole(e.target.value)} required>
                <option value="">Select...</option>
                <option value="READONLY">READONLY</option>
                <option value="NORMAL">NORMAL</option>
                <option value="ADMIN">ADMIN</option>
                <option value="SUPERADMIN">SUPERADMIN</option>
              </select>
            </div>
            <div className="form-group">
              <label>Database (optional)</label>
              <input
                type="text"
                value={editDb}
                onChange={(e) => setEditDb(e.target.value)}
                placeholder="database name"
              />
            </div>
            <div className="form-group form-submit">
              <button type="submit" className="btn-primary">Update Role</button>
            </div>
          </form>
        </div>
      </div>

      {/* User List */}
      <div className="panel">
        <div className="panel-header">
          <h3>All Users ({users.length})</h3>
          <button className="btn-sm" onClick={loadUsers}>Refresh</button>
        </div>
        <div className="panel-body">
          <div className="data-table-wrapper">
            <table className="data-table">
              <thead>
                <tr>
                  {columns.map((col, i) => (
                    <th key={i}>{col}</th>
                  ))}
                  <th>Actions</th>
                </tr>
              </thead>
              <tbody>
                {users.map((row, ri) => (
                  <tr key={ri}>
                    {row.map((cell, ci) => (
                      <td key={ci}>{cell}</td>
                    ))}
                    <td>
                      <button
                        className="btn-sm btn-danger"
                        onClick={() => handleDelete(row[0])}
                      >
                        Delete
                      </button>
                    </td>
                  </tr>
                ))}
                {users.length === 0 && (
                  <tr>
                    <td colSpan={columns.length + 1} className="text-muted" style={{ textAlign: 'center' }}>
                      No users found
                    </td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        </div>
      </div>
    </div>
  );
}
