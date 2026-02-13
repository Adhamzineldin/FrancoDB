import { useState } from 'react';
import { api } from '../api';

interface LoginProps {
  onLogin: (username: string, role: string) => void;
}

export default function Login({ onLogin }: LoginProps) {
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setError('');
    setLoading(true);

    try {
      const result = await api.login(username, password);
      if (result.success) {
        onLogin(result.username!, result.role || 'USER');
      } else {
        setError(result.error || 'Authentication failed');
      }
    } catch (err: any) {
      setError(err.message || 'Connection failed. Is ChronosDB running?');
    } finally {
      setLoading(false);
    }
  };

  return (
    <div className="login-page">
      <div className="login-card">
        <div className="login-header">
          <div className="login-logo">
            <svg width="48" height="48" viewBox="0 0 48 48" fill="none">
              <circle cx="24" cy="24" r="22" stroke="#6366f1" strokeWidth="3" />
              <circle cx="24" cy="24" r="4" fill="#6366f1" />
              <line x1="24" y1="24" x2="24" y2="10" stroke="#6366f1" strokeWidth="3" strokeLinecap="round" />
              <line x1="24" y1="24" x2="34" y2="28" stroke="#6366f1" strokeWidth="2" strokeLinecap="round" />
            </svg>
          </div>
          <h1>ChronosDB</h1>
          <p className="login-subtitle">Web Administration Panel</p>
        </div>

        <form onSubmit={handleSubmit}>
          {error && <div className="error-banner">{error}</div>}

          <div className="form-group">
            <label htmlFor="username">Username</label>
            <input
              id="username"
              type="text"
              value={username}
              onChange={(e) => setUsername(e.target.value)}
              placeholder="chronos"
              autoFocus
              required
            />
          </div>

          <div className="form-group">
            <label htmlFor="password">Password</label>
            <input
              id="password"
              type="password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              placeholder="Enter password"
              required
            />
          </div>

          <button type="submit" className="btn-primary btn-full" disabled={loading}>
            {loading ? 'Connecting...' : 'Sign In'}
          </button>
        </form>

        <div className="login-footer">
          <p>Default: <code>chronos</code> / <code>root</code></p>
        </div>
      </div>
    </div>
  );
}
