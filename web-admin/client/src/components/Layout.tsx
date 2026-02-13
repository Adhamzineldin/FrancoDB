import type { ReactNode } from 'react';
import type { Page, UserInfo } from '../types';

interface LayoutProps {
  user: UserInfo;
  currentDb: string;
  page: Page;
  onNavigate: (page: Page) => void;
  onLogout: () => void;
  children: ReactNode;
}

const NAV_ITEMS: { id: Page; label: string; icon: string }[] = [
  { id: 'dashboard', label: 'Dashboard', icon: '⊞' },
  { id: 'databases', label: 'Databases', icon: '⛁' },
  { id: 'tables', label: 'Tables', icon: '☰' },
  { id: 'query', label: 'SQL Editor', icon: '⟩_' },
  { id: 'users', label: 'Users', icon: '⊕' },
  { id: 'ai-status', label: 'AI Layer', icon: '⚡' },
];

export default function Layout({ user, currentDb, page, onNavigate, onLogout, children }: LayoutProps) {
  const isAdmin = user.role.includes('SUPER') || user.role.includes('ADMIN');

  return (
    <div className="app-layout">
      {/* Sidebar */}
      <aside className="sidebar">
        <div className="sidebar-brand">
          <svg width="32" height="32" viewBox="0 0 48 48" fill="none">
            <circle cx="24" cy="24" r="22" stroke="#6366f1" strokeWidth="3" />
            <circle cx="24" cy="24" r="4" fill="#6366f1" />
            <line x1="24" y1="24" x2="24" y2="10" stroke="#6366f1" strokeWidth="3" strokeLinecap="round" />
            <line x1="24" y1="24" x2="34" y2="28" stroke="#6366f1" strokeWidth="2" strokeLinecap="round" />
          </svg>
          <span>ChronosDB</span>
        </div>

        <nav className="sidebar-nav">
          {NAV_ITEMS.map((item) => {
            // Hide Users tab for non-admin users
            if (item.id === 'users' && !isAdmin) return null;
            return (
              <button
                key={item.id}
                className={`nav-item ${page === item.id ? 'active' : ''}`}
                onClick={() => onNavigate(item.id)}
              >
                <span className="nav-icon">{item.icon}</span>
                <span className="nav-label">{item.label}</span>
              </button>
            );
          })}
        </nav>

        <div className="sidebar-footer">
          <div className="user-info">
            <div className="user-avatar">
              {user.username.charAt(0).toUpperCase()}
            </div>
            <div className="user-details">
              <div className="user-name">{user.username}</div>
              <div className="user-role">{user.role}</div>
            </div>
          </div>
          <button className="btn-logout" onClick={onLogout} title="Logout">
            ⏻
          </button>
        </div>
      </aside>

      {/* Main Content */}
      <main className="main-content">
        <header className="topbar">
          <div className="topbar-left">
            <h2 className="page-title">
              {NAV_ITEMS.find((n) => n.id === page)?.label || 'Dashboard'}
            </h2>
          </div>
          <div className="topbar-right">
            {currentDb && (
              <div className="db-badge">
                <span className="db-dot" />
                {currentDb}
              </div>
            )}
          </div>
        </header>

        <div className="content-area">
          {children}
        </div>
      </main>
    </div>
  );
}
