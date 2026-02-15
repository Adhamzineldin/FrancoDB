import { useState, useEffect, useCallback } from 'react';
import { api } from './api';
import type { Page, UserInfo } from './types';
import Login from './components/Login';
import Layout from './components/Layout';
import Dashboard from './components/Dashboard';
import DatabaseBrowser from './components/DatabaseBrowser';
import TableViewer from './components/TableViewer';
import SQLEditor from './components/SQLEditor';
import UserManagement from './components/UserManagement';
import AIStatus from './components/AIStatus';
import TestingPage from './components/TestingPage';

export default function App() {
  const [user, setUser] = useState<UserInfo | null>(null);
  const [loading, setLoading] = useState(true);
  const [page, setPage] = useState<Page>('dashboard');
  const [selectedTable, setSelectedTable] = useState('');
  const [currentDb, setCurrentDb] = useState('');

  // Check existing session on mount
  useEffect(() => {
    api.me()
      .then((res) => {
        if (res.success) {
          setUser({ username: res.username, role: res.role, currentDb: res.currentDb });
          setCurrentDb(res.currentDb);
        }
      })
      .catch(() => {})
      .finally(() => setLoading(false));
  }, []);

  const handleLogin = useCallback((username: string, role: string) => {
    setUser({ username, role, currentDb: '' });
  }, []);

  const handleLogout = useCallback(async () => {
    await api.logout();
    setUser(null);
    setPage('dashboard');
    setCurrentDb('');
  }, []);

  const handleUseDatabase = useCallback(async (db: string) => {
    const result = await api.useDatabase(db);
    if (result.success || result.message) {
      setCurrentDb(db);
      if (user) setUser({ ...user, currentDb: db });
    }
    return result;
  }, [user]);

  const handleViewTable = useCallback((tableName: string) => {
    setSelectedTable(tableName);
    setPage('tables');
  }, []);

  if (loading) {
    return (
      <div className="loading-screen">
        <div className="spinner" />
        <p>Connecting to ChronosDB...</p>
      </div>
    );
  }

  if (!user) {
    return <Login onLogin={handleLogin} />;
  }

  let content: JSX.Element;
  switch (page) {
    case 'dashboard':
      content = <Dashboard onNavigate={setPage} />;
      break;
    case 'databases':
      content = (
        <DatabaseBrowser
          currentDb={currentDb}
          onUseDatabase={handleUseDatabase}
          onViewTable={handleViewTable}
        />
      );
      break;
    case 'tables':
      content = (
        <TableViewer
          currentDb={currentDb}
          selectedTable={selectedTable}
          onSelectTable={setSelectedTable}
        />
      );
      break;
    case 'query':
      content = <SQLEditor currentDb={currentDb} />;
      break;
    case 'users':
      content = <UserManagement />;
      break;
    case 'ai-status':
      content = <AIStatus />;
      break;
    case 'testing':
      content = <TestingPage currentDb={currentDb} onUseDatabase={handleUseDatabase} />;
      break;
    default:
      content = <Dashboard onNavigate={setPage} />;
  }

  return (
    <Layout
      user={user}
      currentDb={currentDb}
      page={page}
      onNavigate={setPage}
      onLogout={handleLogout}
      onUseDatabase={handleUseDatabase}
    >
      {content}
    </Layout>
  );
}
