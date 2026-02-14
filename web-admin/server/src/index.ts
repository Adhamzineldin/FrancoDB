/**
 * ChronosDB Web Admin - Express Backend Server
 *
 * Provides REST API endpoints for the React frontend to interact
 * with ChronosDB via its native TCP protocol.
 */

import express from 'express';
import cors from 'cors';
import cookieParser from 'cookie-parser';
import path from 'path';
import { v4 as uuidv4 } from 'uuid';
import { ChronosConnectionPool, ChronosResult } from './chronos-client';

const app = express();
const PORT = process.env.PORT ? parseInt(process.env.PORT) : 3001;
const CHRONOS_HOST = process.env.CHRONOS_HOST || 'localhost';
const CHRONOS_PORT = process.env.CHRONOS_PORT ? parseInt(process.env.CHRONOS_PORT) : 2501;

// Connection pool
const pool = new ChronosConnectionPool(CHRONOS_HOST, CHRONOS_PORT);

// Session store: sessionId -> { username, password, currentDb, role }
interface SessionData {
  username: string;
  password: string;
  currentDb: string;
  role: string;
  createdAt: number;
}
const sessions = new Map<string, SessionData>();

// Middleware
app.use(cors({ origin: true, credentials: true }));
app.use(express.json());
app.use(cookieParser());

// Serve static files in production
const publicDir = path.join(__dirname, '../public');
app.use(express.static(publicDir));

// ────────────────────────────────────────────
// Auth middleware
// ────────────────────────────────────────────
function requireAuth(req: express.Request, res: express.Response, next: express.NextFunction): void {
  const sessionId = req.cookies?.chronos_session;
  if (!sessionId || !sessions.has(sessionId)) {
    res.status(401).json({ success: false, error: 'Not authenticated' });
    return;
  }
  (req as any).sessionId = sessionId;
  (req as any).sessionData = sessions.get(sessionId)!;
  next();
}

async function getClient(req: express.Request): Promise<ReturnType<ChronosConnectionPool['getConnection']>> {
  const sessionId = (req as any).sessionId as string;
  const session = (req as any).sessionData as SessionData;
  return pool.getConnection(sessionId, session.username, session.password);
}

// ────────────────────────────────────────────
// AUTH ROUTES
// ────────────────────────────────────────────
app.post('/api/login', async (req, res) => {
  try {
    const { username, password } = req.body;
    if (!username || !password) {
      res.status(400).json({ success: false, error: 'Username and password required' });
      return;
    }

    const sessionId = uuidv4();
    const client = await pool.getConnection(sessionId, username, password);

    // Extract role from login response
    const statusResult = await client.query('SHOW STATUS');
    let role = 'USER';
    if (statusResult.data) {
      const roleRow = statusResult.data.rows.find(r => r[0] === 'Current Role');
      if (roleRow) role = roleRow[1];
    }

    sessions.set(sessionId, {
      username,
      password,
      currentDb: '',
      role,
      createdAt: Date.now(),
    });

    res.cookie('chronos_session', sessionId, {
      httpOnly: true,
      maxAge: 24 * 60 * 60 * 1000, // 24 hours
      sameSite: 'lax',
    });

    res.json({ success: true, username, role });
  } catch (err: any) {
    res.status(401).json({ success: false, error: err.message || 'Authentication failed' });
  }
});

app.post('/api/logout', (req, res) => {
  const sessionId = req.cookies?.chronos_session;
  if (sessionId) {
    pool.removeConnection(sessionId);
    sessions.delete(sessionId);
  }
  res.clearCookie('chronos_session');
  res.json({ success: true });
});

app.get('/api/me', requireAuth, (req, res) => {
  const session = (req as any).sessionData as SessionData;
  res.json({
    success: true,
    username: session.username,
    role: session.role,
    currentDb: session.currentDb,
  });
});

// ────────────────────────────────────────────
// DATABASE ROUTES
// ────────────────────────────────────────────
app.get('/api/databases', requireAuth, async (req, res) => {
  try {
    const client = await getClient(req);
    const result = await client.query('SHOW DATABASES');
    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.post('/api/databases/use', requireAuth, async (req, res) => {
  try {
    const { database } = req.body;
    if (!database) {
      res.status(400).json({ success: false, error: 'Database name required' });
      return;
    }

    const client = await getClient(req);
    const result = await client.query(`USE ${database}`);

    if (result.success || (result.message && !result.error)) {
      const session = (req as any).sessionData as SessionData;
      session.currentDb = database;

      // Refresh role for this database
      const statusResult = await client.query('SHOW STATUS');
      if (statusResult.data) {
        const roleRow = statusResult.data.rows.find(r => r[0] === 'Current Role');
        if (roleRow) session.role = roleRow[1];
      }
    }

    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.post('/api/databases/create', requireAuth, async (req, res) => {
  try {
    const { name } = req.body;
    if (!name) {
      res.status(400).json({ success: false, error: 'Database name required' });
      return;
    }
    const client = await getClient(req);
    const result = await client.query(`CREATE DATABASE ${name}`);
    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.delete('/api/databases/:name', requireAuth, async (req, res) => {
  try {
    const client = await getClient(req);
    const result = await client.query(`DROP DATABASE ${req.params.name}`);
    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

// ────────────────────────────────────────────
// TABLE ROUTES
// ────────────────────────────────────────────
app.get('/api/tables', requireAuth, async (req, res) => {
  try {
    const client = await getClient(req);
    const result = await client.query('SHOW TABLES');
    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.get('/api/tables/:name/schema', requireAuth, async (req, res) => {
  try {
    const client = await getClient(req);
    const result = await client.query(`DESCRIBE ${req.params.name}`);
    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.get('/api/tables/:name/data', requireAuth, async (req, res) => {
  try {
    const client = await getClient(req);
    const limit = parseInt(req.query.limit as string) || 100;
    const offset = parseInt(req.query.offset as string) || 0;
    let sql = `SELECT * FROM ${req.params.name}`;
    if (limit > 0) sql += ` LIMIT ${limit}`;
    if (offset > 0) sql += ` OFFSET ${offset}`;
    const result = await client.query(sql);
    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

// ────────────────────────────────────────────
// QUERY ROUTE
// ────────────────────────────────────────────
app.post('/api/query', requireAuth, async (req, res) => {
  try {
    const { sql } = req.body;
    if (!sql) {
      res.status(400).json({ success: false, error: 'SQL query required' });
      return;
    }
    const client = await getClient(req);
    const result = await client.query(sql);
    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

// ────────────────────────────────────────────
// USER MANAGEMENT ROUTES
// ────────────────────────────────────────────
app.get('/api/users', requireAuth, async (req, res) => {
  try {
    const client = await getClient(req);
    const result = await client.query('SHOW USERS');
    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.post('/api/users', requireAuth, async (req, res) => {
  try {
    const { username, password, role } = req.body;
    if (!username || !password) {
      res.status(400).json({ success: false, error: 'Username and password required' });
      return;
    }
    const client = await getClient(req);
    const roleStr = role || 'READONLY';
    const result = await client.query(`CREATE USER '${username}' '${password}' ${roleStr}`);
    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.delete('/api/users/:username', requireAuth, async (req, res) => {
  try {
    const client = await getClient(req);
    const result = await client.query(`DELETE USER '${req.params.username}'`);
    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.put('/api/users/:username/role', requireAuth, async (req, res) => {
  try {
    const { role, database } = req.body;
    if (!role) {
      res.status(400).json({ success: false, error: 'Role required' });
      return;
    }
    const client = await getClient(req);
    let sql = `ALTER USER '${req.params.username}' ROLE ${role}`;
    if (database) sql += ` IN ${database}`;
    const result = await client.query(sql);
    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

// ────────────────────────────────────────────
// STATUS & AI ROUTES
// ────────────────────────────────────────────
app.get('/api/status', requireAuth, async (req, res) => {
  try {
    const client = await getClient(req);
    const result = await client.query('SHOW STATUS');
    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.get('/api/ai/status', requireAuth, async (req, res) => {
  try {
    const client = await getClient(req);
    const result = await client.query('SHOW AI STATUS');
    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.get('/api/ai/anomalies', requireAuth, async (req, res) => {
  try {
    const client = await getClient(req);
    const result = await client.query('SHOW ANOMALIES');
    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

app.get('/api/ai/stats', requireAuth, async (req, res) => {
  try {
    const client = await getClient(req);
    const result = await client.query('SHOW EXECUTION STATS');
    res.json(result);
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

// ────────────────────────────────────────────
// TESTING ROUTES
// ────────────────────────────────────────────

/**
 * Bulk data insert endpoint for testing
 * Supports time manipulation and optional index creation
 */
app.post('/api/test/bulk-insert', requireAuth, async (req, res) => {
  try {
    const {
      tableName = 'test_bulk_data',
      rowCount = 10000,
      withIndex = true,
      indexColumn = 'value',
      useTimeManipulation = false,
      startTimestamp = Date.now(),
      timeIncrementMs = 1000,
      batchSize = 1000,
    } = req.body;

    const client = await getClient(req);
    const results: any[] = [];
    const startTime = Date.now();

    // Drop existing table
    await client.query(`DROP TABLE IF EXISTS ${tableName}`);
    results.push({ step: 'drop_table', success: true });

    // Create table
    const createResult = await client.query(`
      CREATE TABLE ${tableName} (
        id INTEGER PRIMARY KEY AUTO_INCREMENT,
        value INTEGER NOT NULL,
        data VARCHAR(255),
        created_at TIMESTAMP
      )
    `);
    results.push({ step: 'create_table', success: !createResult.error, error: createResult.error });

    if (createResult.error) {
      res.status(500).json({ success: false, error: createResult.error, results });
      return;
    }

    // Create index if requested
    if (withIndex) {
      const indexResult = await client.query(
        `CREATE INDEX idx_${tableName}_${indexColumn} ON ${tableName}(${indexColumn})`
      );
      results.push({ step: 'create_index', success: !indexResult.error, error: indexResult.error });
    }

    // Insert data in batches
    let insertedRows = 0;
    const batches = Math.ceil(rowCount / batchSize);

    for (let batch = 0; batch < batches; batch++) {
      const batchStart = batch * batchSize;
      const batchEnd = Math.min(batchStart + batchSize, rowCount);
      const values: string[] = [];

      for (let i = batchStart; i < batchEnd; i++) {
        const value = Math.floor(Math.random() * 1000000);
        const data = `test_data_${i}_${Math.random().toString(36).substring(7)}`;

        if (useTimeManipulation) {
          const timestamp = startTimestamp + (i * timeIncrementMs);
          const tsStr = new Date(timestamp).toISOString().replace('T', ' ').substring(0, 19);
          values.push(`(${value}, '${data}', '${tsStr}')`);
        } else {
          values.push(`(${value}, '${data}', CURRENT_TIMESTAMP)`);
        }
      }

      const insertSql = `INSERT INTO ${tableName} (value, data, created_at) VALUES ${values.join(', ')}`;
      const insertResult = await client.query(insertSql);

      if (insertResult.error) {
        results.push({ step: `batch_${batch}`, success: false, error: insertResult.error });
      } else {
        insertedRows += (batchEnd - batchStart);
      }
    }

    // Verify count
    const countResult = await client.query(`SELECT COUNT(*) FROM ${tableName}`);
    const actualCount = countResult.data?.rows?.[0]?.[0] || '0';

    const duration = Date.now() - startTime;

    res.json({
      success: true,
      tableName,
      requestedRows: rowCount,
      insertedRows,
      actualCount: parseInt(actualCount),
      withIndex,
      useTimeManipulation,
      durationMs: duration,
      rowsPerSecond: (insertedRows / (duration / 1000)).toFixed(2),
      results,
    });
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

/**
 * Run performance comparison test
 * Compares index scan vs sequential scan
 */
app.post('/api/test/performance-compare', requireAuth, async (req, res) => {
  try {
    const { rowCount = 10000, queryCount = 50 } = req.body;
    const client = await getClient(req);
    const results: any[] = [];

    // Create test tables
    await client.query('DROP TABLE IF EXISTS perf_with_index');
    await client.query('DROP TABLE IF EXISTS perf_no_index');

    await client.query(`
      CREATE TABLE perf_with_index (
        id INTEGER PRIMARY KEY,
        value INTEGER,
        data VARCHAR(100)
      )
    `);
    await client.query(`
      CREATE TABLE perf_no_index (
        id INTEGER PRIMARY KEY,
        value INTEGER,
        data VARCHAR(100)
      )
    `);
    await client.query('CREATE INDEX idx_perf_value ON perf_with_index(value)');

    // Insert data
    const batchSize = 1000;
    const batches = Math.ceil(rowCount / batchSize);

    for (let batch = 0; batch < batches; batch++) {
      const values: string[] = [];
      for (let i = 0; i < batchSize && (batch * batchSize + i) < rowCount; i++) {
        const id = batch * batchSize + i;
        const value = Math.floor(Math.random() * 1000);
        values.push(`(${id}, ${value}, 'data_${id}')`);
      }
      await client.query(`INSERT INTO perf_with_index (id, value, data) VALUES ${values.join(', ')}`);
      await client.query(`INSERT INTO perf_no_index (id, value, data) VALUES ${values.join(', ')}`);
    }

    // Test index scan
    const indexStart = Date.now();
    for (let i = 0; i < queryCount; i++) {
      await client.query(`SELECT * FROM perf_with_index WHERE value = ${Math.floor(Math.random() * 1000)}`);
    }
    const indexDuration = Date.now() - indexStart;

    results.push({
      test: 'index_scan',
      totalDurationMs: indexDuration,
      queryCount,
      avgQueryMs: (indexDuration / queryCount).toFixed(2),
    });

    // Test sequential scan
    const seqStart = Date.now();
    for (let i = 0; i < queryCount; i++) {
      await client.query(`SELECT * FROM perf_no_index WHERE value = ${Math.floor(Math.random() * 1000)}`);
    }
    const seqDuration = Date.now() - seqStart;

    results.push({
      test: 'sequential_scan',
      totalDurationMs: seqDuration,
      queryCount,
      avgQueryMs: (seqDuration / queryCount).toFixed(2),
    });

    // Cleanup
    await client.query('DROP TABLE IF EXISTS perf_with_index');
    await client.query('DROP TABLE IF EXISTS perf_no_index');

    const improvement = ((seqDuration - indexDuration) / seqDuration * 100);

    res.json({
      success: true,
      rowCount,
      queryCount,
      indexScanMs: indexDuration,
      sequentialScanMs: seqDuration,
      improvementPercent: improvement.toFixed(2),
      results,
    });
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

/**
 * AI system health check endpoint
 */
app.get('/api/test/ai-health', requireAuth, async (req, res) => {
  try {
    const client = await getClient(req);
    const checks: any[] = [];

    // Check AI status
    const aiStatus = await client.query('SHOW AI STATUS');
    checks.push({
      name: 'AI Layer Status',
      success: aiStatus.data !== undefined,
      data: aiStatus.data,
    });

    // Check execution stats
    const execStats = await client.query('SHOW EXECUTION STATS');
    checks.push({
      name: 'Execution Stats',
      success: execStats.data !== undefined,
      data: execStats.data,
    });

    // Check anomalies
    const anomalies = await client.query('SHOW ANOMALIES');
    checks.push({
      name: 'Anomaly Detection',
      success: anomalies.data !== undefined,
      data: anomalies.data,
    });

    const allPassed = checks.every(c => c.success);

    res.json({
      success: allPassed,
      healthChecks: checks,
    });
  } catch (err: any) {
    res.status(500).json({ success: false, error: err.message });
  }
});

// ────────────────────────────────────────────
// SPA FALLBACK
// ────────────────────────────────────────────
app.get('*', (req, res) => {
  const indexPath = path.join(publicDir, 'index.html');
  res.sendFile(indexPath, (err) => {
    if (err) {
      res.status(200).send(`
        <html><body style="font-family:sans-serif;text-align:center;padding:4em">
          <h1>ChronosDB Web Admin</h1>
          <p>Run <code>npm run dev</code> from <code>web-admin/</code> to start in development mode.</p>
        </body></html>
      `);
    }
  });
});

// ────────────────────────────────────────────
// SESSION CLEANUP
// ────────────────────────────────────────────
setInterval(() => {
  const now = Date.now();
  const maxAge = 24 * 60 * 60 * 1000;
  for (const [id, session] of sessions) {
    if (now - session.createdAt > maxAge) {
      pool.removeConnection(id);
      sessions.delete(id);
    }
  }
}, 60 * 60 * 1000); // Cleanup every hour

// ────────────────────────────────────────────
// START SERVER
// ────────────────────────────────────────────
app.listen(PORT, () => {
  console.log(`\n  ╔══════════════════════════════════════════════╗`);
  console.log(`  ║   ChronosDB Web Admin Server                ║`);
  console.log(`  ║   Backend:  http://localhost:${PORT}             ║`);
  console.log(`  ║   ChronosDB: ${CHRONOS_HOST}:${CHRONOS_PORT}                  ║`);
  console.log(`  ╚══════════════════════════════════════════════╝\n`);
});

// Graceful shutdown
process.on('SIGINT', () => {
  console.log('\nShutting down...');
  pool.disconnectAll();
  process.exit(0);
});

process.on('SIGTERM', () => {
  pool.disconnectAll();
  process.exit(0);
});

export default app;
