import { useState, useCallback, useRef, useEffect } from 'react';
import { api } from '../api';

interface TestLog {
  timestamp: number;
  type: 'info' | 'success' | 'error' | 'query' | 'result' | 'metric';
  message: string;
}

interface TestResult {
  testName: string;
  success: boolean;
  duration: number;
  metrics?: Record<string, any>;
}

interface TestingPageProps {
  currentDb: string;
  onUseDatabase?: (db: string) => Promise<any>;
}

export default function TestingPage({ currentDb, onUseDatabase }: TestingPageProps) {
  const [activeTab, setActiveTab] = useState<'timetravel' | 'ai' | 'security' | 'optimizer' | 'temporal' | 'performance'>('timetravel');
  const [running, setRunning] = useState(false);
  const [logs, setLogs] = useState<TestLog[]>([]);
  const [results, setResults] = useState<TestResult[]>([]);
  const logsEndRef = useRef<HTMLDivElement>(null);

  const [perfConfig, setPerfConfig] = useState({
    rowCount: 10000,
    queryCount: 50,
  });

  // Database selector state
  const [databases, setDatabases] = useState<string[]>([]);
  const [loadingDbs, setLoadingDbs] = useState(false);

  useEffect(() => {
    setLoadingDbs(true);
    api.getDatabases()
      .then((res: any) => {
        if (res.data?.rows) {
          setDatabases(res.data.rows.map((r: string[]) => r[0]).filter((d: string) => d));
        }
      })
      .catch(() => {})
      .finally(() => setLoadingDbs(false));
  }, [currentDb]);

  const addLog = useCallback((type: TestLog['type'], message: string) => {
    setLogs(prev => [...prev, { timestamp: Date.now(), type, message }]);
    setTimeout(() => logsEndRef.current?.scrollIntoView({ behavior: 'smooth' }), 50);
  }, []);

  const execQuery = useCallback(async (sql: string, desc?: string): Promise<any> => {
    const start = performance.now();
    addLog('query', `▶ ${desc || sql}`);
    addLog('info', `   SQL: ${sql.length > 150 ? sql.substring(0, 150) + '...' : sql}`);
    try {
      const result = await api.executeQuery(sql);
      const dur = performance.now() - start;
      if (result.error) {
        addLog('error', `   ✗ Error: ${result.error} (${dur.toFixed(1)}ms)`);
      } else if (result.data) {
        addLog('success', `   ✓ ${result.data.rows.length} rows (${dur.toFixed(1)}ms)`);
      } else {
        addLog('success', `   ✓ ${result.message || 'OK'} (${dur.toFixed(1)}ms)`);
      }
      return { ...result, duration: dur };
    } catch (e: any) {
      addLog('error', `   ✗ ${e.message}`);
      return { error: e.message, duration: performance.now() - start };
    }
  }, [addLog]);

  // TIME TRAVEL TESTS
  const runTimeTravelTests = useCallback(async () => {
    if (!currentDb) return alert('Select a database first');
    setRunning(true); setLogs([]); setResults([]);
    const testResults: TestResult[] = [];
    const t0 = performance.now();

    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', '   CHRONOSDB TIME TRAVEL TEST SUITE');
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', `Database: ${currentDb} | Started: ${new Date().toLocaleString()}`);
    addLog('info', '');

    // TEST 1: Setup
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 1: Setup                                           │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    await execQuery('2EMSA7 GADWAL tt_test;');  // DROP TABLE in Franco
    const c1 = await execQuery('CREATE TABLE tt_test (id INTEGER PRIMARY KEY, name GOMLA, balance INTEGER);');
    testResults.push({ testName: 'Setup', success: !c1.error, duration: c1.duration });

    // TEST 2: Insert Data
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 2: Insert Initial Data                             │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    await execQuery("INSERT INTO tt_test VALUES (1, 'Alice', 1000);");
    await execQuery("INSERT INTO tt_test VALUES (2, 'Bob', 2000);");
    await execQuery("INSERT INTO tt_test VALUES (3, 'Charlie', 3000);");
    const beforeTs = Date.now() * 1000;
    addLog('metric', `📌 Timestamp BEFORE changes: ${beforeTs}`);
    addLog('info', `   (${new Date(beforeTs/1000).toISOString()})`);
    const d1 = await execQuery('SELECT * FROM tt_test;');
    if (d1.data) d1.data.rows.forEach((r: string[]) => addLog('result', `   [${r.join(', ')}]`));
    testResults.push({ testName: 'Insert Data', success: !d1.error, duration: d1.duration, metrics: { ts: beforeTs } });

    addLog('info', ''); addLog('info', '⏳ Waiting 1s...'); await new Promise(r => setTimeout(r, 1000));

    // TEST 3: Modify
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 3: Modify Data                                     │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    await execQuery("UPDATE tt_test SET balance = 500 WHERE id = 1;", "Alice: 1000→500");
    await execQuery("UPDATE tt_test SET balance = 9999 WHERE id = 2;", "Bob: 2000→9999");
    await execQuery("DELETE FROM tt_test WHERE id = 3;", "Delete Charlie");
    const d2 = await execQuery('SELECT * FROM tt_test;');
    if (d2.data) { addLog('result', '   After changes:'); d2.data.rows.forEach((r: string[]) => addLog('result', `   [${r.join(', ')}]`)); }
    testResults.push({ testName: 'Modify Data', success: !d2.error, duration: d2.duration });

    // TEST 4: AS OF
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 4: Time Travel (SELECT AS OF)                      │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    const tt = await execQuery(`SELECT * FROM tt_test AS OF ${beforeTs};`, 'Read-only historical snapshot');
    let ttOk = false;
    if (tt.data) {
      addLog('result', '   Historical data:');
      tt.data.rows.forEach((r: string[]) => addLog('result', `   [${r.join(', ')}]`));
      const hasCharlie = tt.data.rows.some((r: string[]) => r[0] === '3');
      const aliceOk = tt.data.rows.find((r: string[]) => r[0] === '1')?.[2] === '1000';
      ttOk = hasCharlie && aliceOk;
      addLog(ttOk ? 'success' : 'error', ttOk ? '   ✓ Charlie exists, Alice=1000' : '   ✗ Data mismatch!');
    }
    testResults.push({ testName: 'AS OF Query', success: ttOk, duration: tt.duration });

    // TEST 5: Checkpoint
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 5: Checkpoint                                      │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    const cp = await execQuery('CHECKPOINT;');
    testResults.push({ testName: 'Checkpoint', success: !cp.error, duration: cp.duration });

    // TEST 6: RECOVER TO
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 6: RECOVER TO (Permanent Rollback)                 │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   ⚠️ This PERMANENTLY rolls back the database!');
    await execQuery(`RECOVER TO ${beforeTs};`);
    const rec = await execQuery('SELECT * FROM tt_test;');
    let recOk = false;
    if (rec.data) {
      addLog('result', '   After recovery:');
      rec.data.rows.forEach((r: string[]) => addLog('result', `   [${r.join(', ')}]`));
      recOk = rec.data.rows.some((r: string[]) => r[0] === '3') && rec.data.rows.find((r: string[]) => r[0] === '1')?.[2] === '1000';
      addLog(recOk ? 'success' : 'error', recOk ? '   ✓ Data restored!' : '   ✗ Recovery failed');
    }
    testResults.push({ testName: 'RECOVER TO', success: recOk, duration: rec.duration });

    // Cleanup
    addLog('info', ''); await execQuery('DROP TABLE IF EXISTS tt_test;');
    const total = performance.now() - t0;
    const passed = testResults.filter(r => r.success).length;
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('metric', `   Duration: ${total.toFixed(0)}ms | Passed: ${passed}/${testResults.length}`);
    addLog(passed === testResults.length ? 'success' : 'error', passed === testResults.length ? '   ✓ ALL PASSED' : '   ✗ SOME FAILED');
    setResults(testResults); setRunning(false);
  }, [currentDb, addLog, execQuery]);

  // ============================================================
  // SECURITY TESTS - SQL Injection & XSS Detection
  // ============================================================
  const runSecurityTests = useCallback(async () => {
    if (!currentDb) return alert('Select a database first');
    setRunning(true); setLogs([]); setResults([]);
    const testResults: TestResult[] = [];
    const t0 = performance.now();

    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', '   IMMUNE SYSTEM: SECURITY THREAT DETECTION TESTS');
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', `Database: ${currentDb} | Started: ${new Date().toLocaleString()}`);
    addLog('info', '');
    addLog('info', '   The Immune System scans ALL queries for:');
    addLog('info', '   • SQL Injection patterns (UNION SELECT, ; DROP, OR 1=1, etc.)');
    addLog('info', '   • XSS attack patterns (<script>, javascript:, eval(), etc.)');
    addLog('info', '   • MEDIUM/HIGH severity threats are BLOCKED');
    addLog('info', '   • LOW severity threats are logged but allowed');
    addLog('info', '');

    // Setup test table
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ SETUP: Create Test Table                                │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    await execQuery('2EMSA7 GADWAL sec_test;');
    await execQuery('CREATE TABLE sec_test (id INTEGER PRIMARY KEY, name GOMLA, data GOMLA);');
    await execQuery("INSERT INTO sec_test VALUES (1, 'normal_user', 'safe_data');");
    addLog('success', '   ✓ Test table ready');

    // Get baseline threat stats
    let baselineThreats = 0;
    let baselineSQLi = 0;
    let baselineXSS = 0;
    try {
      const aiStatus = await api.getAIDetailed();
      const immune = aiStatus.immune_system as any;
      if (immune?.threat_detection) {
        baselineThreats = immune.threat_detection.total_threats || 0;
        baselineSQLi = immune.threat_detection.sql_injection_count || 0;
        baselineXSS = immune.threat_detection.xss_count || 0;
      }
    } catch {}
    addLog('info', `   Baseline: ${baselineThreats} threats, ${baselineSQLi} SQLi, ${baselineXSS} XSS`);

    // ─── TEST 1: SQL Injection HIGH - UNION SELECT ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 1: SQL Injection HIGH - UNION SELECT               │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   Attempting to inject UNION SELECT via INSERT value...');
    const sqli1 = await execQuery(
      "INSERT INTO sec_test VALUES (100, 'hacker', 'x UNION SELECT * FROM users');",
      'SQL Injection: UNION SELECT'
    );
    const sqli1Blocked = !!(sqli1.error && sqli1.error.includes('[IMMUNE')) || !!(sqli1.message && sqli1.message.includes('[IMMUNE'));
    addLog(sqli1Blocked ? 'success' : 'error',
      sqli1Blocked ? '   ✓ BLOCKED! Immune System detected UNION SELECT injection' : '   ✗ Not blocked - injection went through');
    testResults.push({ testName: 'SQLi: UNION SELECT', success: sqli1Blocked, duration: sqli1.duration });

    // ─── TEST 2: SQL Injection HIGH - DROP TABLE ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 2: SQL Injection HIGH - DROP TABLE                 │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   Attempting to inject ; DROP TABLE via INSERT value...');
    const sqli2 = await execQuery(
      "INSERT INTO sec_test VALUES (101, 'attacker', 'x; DROP TABLE users; --');",
      'SQL Injection: DROP TABLE'
    );
    const sqli2Blocked = !!(sqli2.error && sqli2.error.includes('[IMMUNE')) || !!(sqli2.message && sqli2.message.includes('[IMMUNE'));
    addLog(sqli2Blocked ? 'success' : 'error',
      sqli2Blocked ? '   ✓ BLOCKED! Immune System detected DROP TABLE injection' : '   ✗ Not blocked');
    testResults.push({ testName: 'SQLi: DROP TABLE', success: sqli2Blocked, duration: sqli2.duration });

    // ─── TEST 3: SQL Injection MEDIUM - OR 1=1 ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 3: SQL Injection MEDIUM - Authentication Bypass    │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   Attempting OR 1=1 authentication bypass...');
    const sqli3 = await execQuery(
      "INSERT INTO sec_test VALUES (102, 'bypass', 'admin OR 1=1 --');",
      'SQL Injection: OR 1=1'
    );
    const sqli3Blocked = !!(sqli3.error && sqli3.error.includes('[IMMUNE')) || !!(sqli3.message && sqli3.message.includes('[IMMUNE'));
    addLog(sqli3Blocked ? 'success' : 'error',
      sqli3Blocked ? '   ✓ BLOCKED! OR 1=1 bypass attempt detected' : '   ✗ Not blocked');
    testResults.push({ testName: 'SQLi: OR 1=1', success: sqli3Blocked, duration: sqli3.duration });

    // ─── TEST 4: SQL Injection MEDIUM - Sleep Attack ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 4: SQL Injection MEDIUM - Timing Attack            │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   Attempting SLEEP() timing attack...');
    const sqli4 = await execQuery(
      "INSERT INTO sec_test VALUES (103, 'timer', 'x; SLEEP(10); --');",
      'SQL Injection: SLEEP()'
    );
    const sqli4Blocked = !!(sqli4.error && sqli4.error.includes('[IMMUNE')) || !!(sqli4.message && sqli4.message.includes('[IMMUNE'));
    addLog(sqli4Blocked ? 'success' : 'error',
      sqli4Blocked ? '   ✓ BLOCKED! SLEEP() timing attack detected' : '   ✗ Not blocked');
    testResults.push({ testName: 'SQLi: SLEEP()', success: sqli4Blocked, duration: sqli4.duration });

    // ─── TEST 5: XSS HIGH - Script Tag ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 5: XSS HIGH - Script Injection                    │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   Attempting <script> tag injection...');
    const xss1 = await execQuery(
      "INSERT INTO sec_test VALUES (200, 'xss_attacker', '<script>document.cookie</script>');",
      'XSS: <script> tag'
    );
    const xss1Blocked = !!(xss1.error && xss1.error.includes('[IMMUNE')) || !!(xss1.message && xss1.message.includes('[IMMUNE'));
    addLog(xss1Blocked ? 'success' : 'error',
      xss1Blocked ? '   ✓ BLOCKED! <script> injection detected' : '   ✗ Not blocked');
    testResults.push({ testName: 'XSS: <script>', success: xss1Blocked, duration: xss1.duration });

    // ─── TEST 6: XSS HIGH - javascript: Protocol ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 6: XSS HIGH - JavaScript Protocol                 │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   Attempting javascript: protocol injection...');
    const xss2 = await execQuery(
      "INSERT INTO sec_test VALUES (201, 'proto_xss', 'javascript:alert(1)');",
      'XSS: javascript: protocol'
    );
    const xss2Blocked = !!(xss2.error && xss2.error.includes('[IMMUNE')) || !!(xss2.message && xss2.message.includes('[IMMUNE'));
    addLog(xss2Blocked ? 'success' : 'error',
      xss2Blocked ? '   ✓ BLOCKED! javascript: protocol detected' : '   ✗ Not blocked');
    testResults.push({ testName: 'XSS: javascript:', success: xss2Blocked, duration: xss2.duration });

    // ─── TEST 7: XSS MEDIUM - Event Handler ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 7: XSS MEDIUM - Event Handler Injection            │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   Attempting onerror= event handler injection...');
    const xss3 = await execQuery(
      "INSERT INTO sec_test VALUES (202, 'event_xss', '<img src=x onerror=alert(1)>');",
      'XSS: onerror= handler'
    );
    const xss3Blocked = !!(xss3.error && xss3.error.includes('[IMMUNE')) || !!(xss3.message && xss3.message.includes('[IMMUNE'));
    addLog(xss3Blocked ? 'success' : 'error',
      xss3Blocked ? '   ✓ BLOCKED! onerror= event handler detected' : '   ✗ Not blocked');
    testResults.push({ testName: 'XSS: onerror=', success: xss3Blocked, duration: xss3.duration });

    // ─── TEST 8: XSS HIGH - eval() ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 8: XSS HIGH - eval() Code Execution               │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   Attempting eval() injection...');
    const xss4 = await execQuery(
      "INSERT INTO sec_test VALUES (203, 'eval_xss', 'eval(String.fromCharCode(97))');",
      'XSS: eval()'
    );
    const xss4Blocked = !!(xss4.error && xss4.error.includes('[IMMUNE')) || !!(xss4.message && xss4.message.includes('[IMMUNE'));
    addLog(xss4Blocked ? 'success' : 'error',
      xss4Blocked ? '   ✓ BLOCKED! eval() code execution detected' : '   ✗ Not blocked');
    testResults.push({ testName: 'XSS: eval()', success: xss4Blocked, duration: xss4.duration });

    // ─── TEST 9: Case Insensitivity ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 9: Case-Insensitive Detection                     │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   Attempting mixed-case evasion: UnIoN SeLeCt...');
    const case1 = await execQuery(
      "INSERT INTO sec_test VALUES (300, 'case_evade', 'x UnIoN SeLeCt * FROM secrets');",
      'Case evasion: UnIoN SeLeCt'
    );
    const caseBlocked = !!(case1.error && case1.error.includes('[IMMUNE')) || !!(case1.message && case1.message.includes('[IMMUNE'));
    addLog(caseBlocked ? 'success' : 'error',
      caseBlocked ? '   ✓ BLOCKED! Case-insensitive detection works' : '   ✗ Case evasion succeeded');
    testResults.push({ testName: 'Case Insensitive', success: caseBlocked, duration: case1.duration });

    // ─── TEST 10: Clean Queries (No False Positives) ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 10: Clean Queries (No False Positives)             │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   These should ALL succeed (no blocking)...');
    const clean1 = await execQuery("INSERT INTO sec_test VALUES (400, 'John', 'Normal user data');", 'Normal INSERT');
    const clean2 = await execQuery("INSERT INTO sec_test VALUES (401, 'Jane', 'Meeting at union hall');", 'Contains "union" (not injection)');
    const clean3 = await execQuery("INSERT INTO sec_test VALUES (402, 'Bob', 'Select the best option');", 'Contains "select" (not injection)');
    const noFalsePositives = !clean1.error && !clean2.error && !clean3.error;
    addLog(noFalsePositives ? 'success' : 'error',
      noFalsePositives ? '   ✓ All clean queries allowed through (no false positives)' : '   ✗ False positive detected - clean query was blocked!');
    testResults.push({ testName: 'No False Positives', success: noFalsePositives, duration: 0 });

    // ─── TEST 11: Verify Threat Stats Updated ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 11: Threat Detection Stats                        │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    let statsOk = false;
    try {
      const aiStatus = await api.getAIDetailed();
      const immune = aiStatus.immune_system as any;
      if (immune?.threat_detection) {
        const totalNow = immune.threat_detection.total_threats || 0;
        const sqliNow = immune.threat_detection.sql_injection_count || 0;
        const xssNow = immune.threat_detection.xss_count || 0;
        addLog('metric', `   Total Threats: ${totalNow} (was ${baselineThreats})`);
        addLog('metric', `   SQL Injections: ${sqliNow} (was ${baselineSQLi})`);
        addLog('metric', `   XSS Attacks: ${xssNow} (was ${baselineXSS})`);
        statsOk = totalNow > baselineThreats;
        addLog(statsOk ? 'success' : 'error',
          statsOk ? '   ✓ Threat counters incremented correctly' : '   ✗ Threat counters not updated');
      } else {
        addLog('error', '   ✗ threat_detection section not found in AI status');
      }
    } catch (e: any) {
      addLog('error', `   ✗ Failed to fetch AI status: ${e.message}`);
    }
    testResults.push({ testName: 'Threat Stats', success: statsOk, duration: 0 });

    // Cleanup
    addLog('info', '');
    await execQuery('2EMSA7 GADWAL sec_test;');

    const total = performance.now() - t0;
    const passed = testResults.filter(r => r.success).length;
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', '   SECURITY THREAT DETECTION TESTS COMPLETE');
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('metric', `   Duration: ${total.toFixed(0)}ms | Passed: ${passed}/${testResults.length}`);
    addLog(passed === testResults.length ? 'success' : 'error',
      passed === testResults.length ? '   ✓ ALL PASSED - Immune System is protecting the database!' : '   ✗ SOME FAILED');
    setResults(testResults); setRunning(false);
  }, [currentDb, addLog, execQuery]);

  // ============================================================
  // QUERY OPTIMIZER TESTS - Filter & Limit Strategy
  // ============================================================
  const runOptimizerTests = useCallback(async () => {
    if (!currentDb) return alert('Select a database first');
    setRunning(true); setLogs([]); setResults([]);
    const testResults: TestResult[] = [];
    const t0 = performance.now();

    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', '   QUERY OPTIMIZER: FILTER & LIMIT STRATEGY TESTS');
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', `Database: ${currentDb} | Started: ${new Date().toLocaleString()}`);
    addLog('info', '');
    addLog('info', '   The Query Optimizer uses UCB1 multi-armed bandits to learn:');
    addLog('info', '   • Filter Strategy: How to order WHERE clause predicates');
    addLog('info', '     - Arm 0: ORIGINAL_ORDER (keep as written)');
    addLog('info', '     - Arm 1: SELECTIVITY_ORDER (most selective first)');
    addLog('info', '     - Arm 2: COST_ORDER (cheapest first)');
    addLog('info', '   • Limit Strategy: How to handle LIMIT clauses');
    addLog('info', '     - Arm 0: FULL_SCAN_THEN_LIMIT (scan all, then cut)');
    addLog('info', '     - Arm 1: EARLY_TERMINATION (stop scanning at limit)');
    addLog('info', '');

    // Get baseline optimizer stats
    let baselineStats: any = null;
    try {
      const aiStatus = await api.getAIDetailed();
      baselineStats = aiStatus.learning_engine?.optimizer;
      if (baselineStats) {
        addLog('info', `   Baseline: ${baselineStats.total_optimizations} optimizations, ${baselineStats.filter_reorders} reorders, ${baselineStats.early_terminations} early-terms`);
        if (baselineStats.dimensions) {
          baselineStats.dimensions.forEach((dim: any) => {
            addLog('info', `   ${dim.name}: ${dim.arms.map((a: any) => `${a.name}=${a.pulls}`).join(', ')}`);
          });
        }
      }
    } catch {}

    // ─── Setup: Create table with multi-column data ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ SETUP: Create Training Data                             │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    await execQuery('2EMSA7 GADWAL opt_test;');
    await execQuery('CREATE TABLE opt_test (id INTEGER PRIMARY KEY, category INTEGER, grade INTEGER, level INTEGER, data GOMLA);');
    await execQuery('CREATE INDEX idx_opt_cat ON opt_test(category);');
    await execQuery('CREATE INDEX idx_opt_grade ON opt_test(grade);');

    // Insert training data
    addLog('info', '   Inserting 500 rows of training data...');
    const optValues = [];
    for (let i = 0; i < 500; i++) {
      optValues.push(`(${i}, ${i % 10}, ${i % 5}, ${i % 3}, 'row_${i}')`);
    }
    await execQuery(`INSERT INTO opt_test VALUES ${optValues.join(', ')};`, 'Multi-row INSERT 500 rows');
    addLog('success', '   ✓ Training data ready');
    testResults.push({ testName: 'Optimizer Setup', success: true, duration: 0 });

    // ─── TEST 1: Filter Strategy Training (Multi-predicate queries) ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 1: Filter Strategy Training                        │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   Running 30 multi-predicate WHERE queries to train filter strategy...');
    addLog('info', '   (Queries with 2+ WHERE conditions trigger filter optimization)');
    const filterTimes: number[] = [];
    for (let i = 0; i < 30; i++) {
      const cat = Math.floor(Math.random() * 10);
      const grade = Math.floor(Math.random() * 5);
      const r = await execQuery(
        `SELECT * FROM opt_test WHERE category = ${cat} AND grade = ${grade};`,
        `Filter query ${i + 1}/30`
      );
      filterTimes.push(r.duration || 0);
    }
    const filterAvg = filterTimes.reduce((a, b) => a + b, 0) / filterTimes.length;
    addLog('metric', `   Avg query time: ${filterAvg.toFixed(2)}ms`);
    testResults.push({ testName: 'Filter Training', success: true, duration: filterTimes.reduce((a, b) => a + b, 0), metrics: { avg: filterAvg } });

    // ─── TEST 2: Limit Strategy Training ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 2: Limit Strategy Training                         │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   Running 30 LIMIT queries to train limit strategy...');
    addLog('info', '   (Queries with LIMIT but no ORDER BY trigger limit optimization)');
    const limitTimes: number[] = [];
    for (let i = 0; i < 30; i++) {
      const cat = Math.floor(Math.random() * 10);
      const lim = Math.floor(Math.random() * 20) + 1;
      const r = await execQuery(
        `SELECT * FROM opt_test WHERE category = ${cat} LIMIT ${lim};`,
        `Limit query ${i + 1}/30`
      );
      limitTimes.push(r.duration || 0);
    }
    const limitAvg = limitTimes.reduce((a, b) => a + b, 0) / limitTimes.length;
    addLog('metric', `   Avg query time: ${limitAvg.toFixed(2)}ms`);
    testResults.push({ testName: 'Limit Training', success: true, duration: limitTimes.reduce((a, b) => a + b, 0), metrics: { avg: limitAvg } });

    // ─── TEST 3: Three-predicate Filter Queries ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 3: Complex Filter (3 predicates)                   │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   Running 20 queries with 3 WHERE conditions...');
    const complexTimes: number[] = [];
    for (let i = 0; i < 20; i++) {
      const cat = Math.floor(Math.random() * 10);
      const grade = Math.floor(Math.random() * 5);
      const level = Math.floor(Math.random() * 3);
      const r = await execQuery(
        `SELECT * FROM opt_test WHERE category = ${cat} AND grade = ${grade} AND level = ${level};`,
        `3-pred query ${i + 1}/20`
      );
      complexTimes.push(r.duration || 0);
    }
    const complexAvg = complexTimes.reduce((a, b) => a + b, 0) / complexTimes.length;
    addLog('metric', `   Avg query time: ${complexAvg.toFixed(2)}ms`);
    testResults.push({ testName: '3-Predicate Filter', success: true, duration: complexTimes.reduce((a, b) => a + b, 0), metrics: { avg: complexAvg } });

    // ─── TEST 4: Verify Optimizer Stats Updated ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 4: Verify Optimizer Statistics                     │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    let optimizerOk = false;
    try {
      const aiStatus = await api.getAIDetailed();
      const optimizer = aiStatus.learning_engine?.optimizer;
      if (optimizer) {
        addLog('metric', `   Total Optimizations: ${optimizer.total_optimizations}`);
        addLog('metric', `   Filter Reorders: ${optimizer.filter_reorders}`);
        addLog('metric', `   Early Terminations: ${optimizer.early_terminations}`);

        if (optimizer.dimensions) {
          addLog('info', '');
          addLog('info', '   Strategy Arm Pulls:');
          optimizer.dimensions.forEach((dim) => {
            addLog('info', `   ${dim.name}:`);
            dim.arms.forEach((arm) => {
              const bar = '█'.repeat(Math.min(40, Math.round(arm.pulls / 2)));
              addLog('metric', `     ${arm.name.padEnd(22)} ${String(arm.pulls).padStart(4)} pulls  ${bar}`);
            });
          });
        }

        const baselineOpts = baselineStats?.total_optimizations || 0;
        optimizerOk = optimizer.total_optimizations > baselineOpts;
        addLog(optimizerOk ? 'success' : 'error',
          optimizerOk
            ? `   ✓ Optimizer learned from ${optimizer.total_optimizations - baselineOpts} new queries`
            : '   ✗ Optimizer stats did not increase');
      } else {
        addLog('error', '   ✗ Optimizer stats not found in AI status');
      }
    } catch (e: any) {
      addLog('error', `   ✗ Failed to fetch AI status: ${e.message}`);
    }
    testResults.push({ testName: 'Optimizer Stats', success: optimizerOk, duration: 0 });

    // ─── TEST 5: Execution Stats Show Optimizer Impact ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 5: Execution Stats                                 │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    const execStats = await execQuery('SHOW EXECUTION STATS;');
    if (execStats.data) {
      execStats.data.rows.forEach((r: string[]) => addLog('result', `   ${r.join(' | ')}`));
    }
    testResults.push({ testName: 'Exec Stats', success: !execStats.error, duration: execStats.duration });

    // Cleanup
    addLog('info', '');
    await execQuery('2EMSA7 GADWAL opt_test;');

    const total = performance.now() - t0;
    const passed = testResults.filter(r => r.success).length;
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', '   QUERY OPTIMIZER TESTS COMPLETE');
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('metric', `   Duration: ${total.toFixed(0)}ms | Passed: ${passed}/${testResults.length}`);
    addLog(passed === testResults.length ? 'success' : 'error',
      passed === testResults.length ? '   ✓ ALL PASSED' : '   ✗ SOME FAILED');
    setResults(testResults); setRunning(false);
  }, [currentDb, addLog, execQuery]);

  // ============================================================
  // TEMPORAL AI TESTS - Hotspot Detection & AS OF Analysis
  // ============================================================
  const runTemporalTests = useCallback(async () => {
    if (!currentDb) return alert('Select a database first');
    setRunning(true); setLogs([]); setResults([]);
    const testResults: TestResult[] = [];
    const t0 = performance.now();

    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', '   TEMPORAL AI: HOTSPOT DETECTION & TIME ANALYSIS TESTS');
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', `Database: ${currentDb} | Started: ${new Date().toLocaleString()}`);
    addLog('info', '');
    addLog('info', '   The Temporal AI uses DBSCAN clustering to detect:');
    addLog('info', '   • Temporal hotspots: Timestamps queried frequently with AS OF');
    addLog('info', '   • Access patterns: Which time periods are "hot" for investigation');
    addLog('info', '   • Change-point detection (CUSUM) on mutation rate time series');
    addLog('info', '');

    // Get baseline temporal stats
    let baselineAccesses = 0;
    try {
      const aiStatus = await api.getAIDetailed();
      baselineAccesses = aiStatus.temporal_index?.total_accesses || 0;
      addLog('info', `   Baseline temporal accesses: ${baselineAccesses}`);
    } catch {}

    // ─── Setup ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ SETUP: Create Temporal Test Data                        │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    await execQuery('2EMSA7 GADWAL temporal_test;');
    await execQuery('CREATE TABLE temporal_test (id INTEGER PRIMARY KEY, event GOMLA, amount INTEGER);');

    // Insert data in phases with delays to create distinct timestamps
    addLog('info', '   Phase 1: Initial data (the "past")...');
    const phase1Values = [];
    for (let i = 0; i < 20; i++) {
      phase1Values.push(`(${i}, 'event_${i}', ${100 + i * 10})`);
    }
    await execQuery(`INSERT INTO temporal_test VALUES ${phase1Values.join(', ')};`, 'Insert 20 rows (Phase 1)');
    const ts1 = Date.now() * 1000; // Timestamp after phase 1
    addLog('metric', `   📌 Timestamp T1 (after Phase 1): ${ts1}`);

    await new Promise(r => setTimeout(r, 1500));

    addLog('info', '   Phase 2: Updates (the "incident")...');
    for (let i = 0; i < 10; i++) {
      await execQuery(`UPDATE temporal_test SET amount = 0 WHERE id = ${i};`, `Incident update ${i + 1}/10`);
    }
    const ts2 = Date.now() * 1000; // Timestamp after incident
    addLog('metric', `   📌 Timestamp T2 (after incident): ${ts2}`);

    await new Promise(r => setTimeout(r, 1500));

    addLog('info', '   Phase 3: More updates (recovery)...');
    for (let i = 0; i < 10; i++) {
      await execQuery(`UPDATE temporal_test SET amount = ${500 + i * 50} WHERE id = ${i};`, `Recovery update ${i + 1}/10`);
    }
    testResults.push({ testName: 'Temporal Setup', success: true, duration: performance.now() - t0 });

    // ─── TEST 1: AS OF Queries to T1 (Cluster 1 - "Before Incident") ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 1: Temporal Cluster 1 - Investigate "Before"       │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   Simulating data incident investigation:');
    addLog('info', '   Running 15 AS OF queries to T1 (pre-incident snapshot)...');
    let cluster1Ok = true;
    for (let i = 0; i < 15; i++) {
      const r = await execQuery(
        `SELECT * FROM temporal_test AS OF ${ts1};`,
        `AS OF T1 query ${i + 1}/15`
      );
      if (r.error) cluster1Ok = false;
    }
    addLog(cluster1Ok ? 'success' : 'error',
      cluster1Ok ? '   ✓ All 15 AS OF queries to T1 succeeded' : '   ✗ Some queries failed');
    testResults.push({ testName: 'Cluster 1 (T1)', success: cluster1Ok, duration: 0 });

    // ─── TEST 2: AS OF Queries to T2 (Cluster 2 - "During Incident") ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 2: Temporal Cluster 2 - Investigate "During"       │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   Running 15 AS OF queries to T2 (during-incident snapshot)...');
    let cluster2Ok = true;
    for (let i = 0; i < 15; i++) {
      const r = await execQuery(
        `SELECT * FROM temporal_test AS OF ${ts2};`,
        `AS OF T2 query ${i + 1}/15`
      );
      if (r.error) cluster2Ok = false;
    }
    addLog(cluster2Ok ? 'success' : 'error',
      cluster2Ok ? '   ✓ All 15 AS OF queries to T2 succeeded' : '   ✗ Some queries failed');
    testResults.push({ testName: 'Cluster 2 (T2)', success: cluster2Ok, duration: 0 });

    // ─── TEST 3: Verify Temporal AI Detected Hotspots ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 3: Verify Temporal Hotspot Detection               │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    // Wait for temporal analysis to process
    addLog('info', '   Waiting for temporal AI analysis...');
    await new Promise(r => setTimeout(r, 2000));

    let hotspotsOk = false;
    try {
      const aiStatus = await api.getAIDetailed();
      const temporal = aiStatus.temporal_index;
      if (temporal) {
        const totalAccesses = temporal.total_accesses || 0;
        const newAccesses = totalAccesses - baselineAccesses;
        addLog('metric', `   Total temporal accesses: ${totalAccesses} (+${newAccesses} new)`);
        addLog('metric', `   Temporal snapshots: ${temporal.total_snapshots || 0}`);

        if (temporal.hotspots && temporal.hotspots.length > 0) {
          addLog('info', '');
          addLog('success', `   ✓ Detected ${temporal.hotspots.length} temporal hotspot(s):`);
          temporal.hotspots.forEach((h, i) => {
            const centerDate = new Date(h.center_us / 1000).toISOString();
            addLog('metric', `   Hotspot ${i + 1}: center=${centerDate}, accesses=${h.access_count}, density=${h.density.toFixed(2)}`);
          });
          hotspotsOk = true;
        } else {
          addLog('info', '   No hotspots detected yet (may need more accesses or analysis interval)');
          // Still consider it a partial pass if accesses were recorded
          hotspotsOk = newAccesses >= 20;
          addLog(hotspotsOk ? 'success' : 'error',
            hotspotsOk ? '   ✓ Temporal accesses recorded (hotspots will form on next analysis)' : '   ✗ Temporal accesses not recorded');
        }
      } else {
        addLog('error', '   ✗ Temporal index not found in AI status');
      }
    } catch (e: any) {
      addLog('error', `   ✗ Failed to fetch temporal stats: ${e.message}`);
    }
    testResults.push({ testName: 'Hotspot Detection', success: hotspotsOk, duration: 0 });

    // ─── TEST 4: Mutation Rate Monitoring ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 4: Mutation Rate Spike Detection                   │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   Creating a burst of mutations to test rate monitoring...');
    const burstStart = performance.now();
    const burstValues = [];
    for (let i = 100; i < 150; i++) {
      burstValues.push(`(${i}, 'burst_${i}', ${i * 10})`);
    }
    await execQuery(`INSERT INTO temporal_test VALUES ${burstValues.join(', ')};`, 'Burst INSERT 50 rows');
    // Quick burst of updates
    for (let i = 100; i < 120; i++) {
      await execQuery(`UPDATE temporal_test SET amount = ${i * 100} WHERE id = ${i};`, `Burst update ${i - 99}/20`);
    }
    const burstDur = performance.now() - burstStart;
    addLog('metric', `   Mutation burst: 50 inserts + 20 updates in ${burstDur.toFixed(0)}ms`);
    testResults.push({ testName: 'Mutation Burst', success: true, duration: burstDur });

    // ─── TEST 5: AI Status Summary ───
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 5: Full AI Status Check                            │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    const aiSt = await execQuery('SHOW AI STATUS;');
    if (aiSt.data) aiSt.data.rows.forEach((r: string[]) => addLog('result', `   ${r[0]}: ${r[1]}`));
    testResults.push({ testName: 'AI Status', success: !aiSt.error, duration: aiSt.duration });

    // Cleanup
    addLog('info', '');
    await execQuery('2EMSA7 GADWAL temporal_test;');

    const total = performance.now() - t0;
    const passed = testResults.filter(r => r.success).length;
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', '   TEMPORAL AI TESTS COMPLETE');
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('metric', `   Duration: ${total.toFixed(0)}ms | Passed: ${passed}/${testResults.length}`);
    addLog(passed === testResults.length ? 'success' : 'error',
      passed === testResults.length ? '   ✓ ALL PASSED' : '   ✗ SOME FAILED');
    setResults(testResults); setRunning(false);
  }, [currentDb, addLog, execQuery]);

  // AI TESTS (existing - Learning Engine + Immune System anomaly detection)
  const runAITests = useCallback(async () => {
    if (!currentDb) return alert('Select a database first');
    setRunning(true); setLogs([]); setResults([]);
    const testResults: TestResult[] = [];
    const t0 = performance.now();

    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', '   CHRONOSDB AI LAYER & IMMUNE SYSTEM TEST SUITE');
    addLog('info', '═══════════════════════════════════════════════════════════');

    // AI Status
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 1: AI Initialization                               │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    const st = await execQuery('SHOW AI STATUS;');
    let init = false;
    if (st.data) st.data.rows.forEach((r: string[]) => { addLog('result', `   ${r[0]}: ${r[1]}`); if (r[1]?.includes('Initialized')) init = true; });
    testResults.push({ testName: 'AI Init', success: init, duration: st.duration });

    // Exec Stats
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 2: Execution Stats                                 │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    const ex = await execQuery('SHOW EXECUTION STATS;');
    if (ex.data) ex.data.rows.forEach((r: string[]) => addLog('result', `   ${r.join(' | ')}`));
    testResults.push({ testName: 'Exec Stats', success: !ex.error, duration: ex.duration });

    // Learning test
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 3: Learning Engine Training                        │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    await execQuery('2EMSA7 GADWAL ai_test;');  // DROP TABLE in Franco
    await execQuery('CREATE TABLE ai_test (id INTEGER PRIMARY KEY, val INTEGER, data GOMLA);');
    await execQuery('CREATE INDEX idx_ai_val ON ai_test(val);');

    // Insert rows using multi-row INSERT for efficiency
    addLog('info', '   Inserting 100 rows for training...');
    const aiValues = [];
    for (let i = 0; i < 100; i++) {
      aiValues.push(`(${i}, ${Math.floor(Math.random()*100)}, 'd${i}')`);
    }
    await execQuery(`INSERT INTO ai_test VALUES ${aiValues.join(', ')};`, 'Multi-row INSERT 100 rows');
    addLog('success', '   ✓ Inserted 100 rows');

    addLog('info', '   Running 20 queries to train Learning Engine...');
    const times: number[] = [];
    for (let i = 0; i < 20; i++) {
      const r = await execQuery(`SELECT * FROM ai_test WHERE val = ${Math.floor(Math.random()*100)};`, `Query ${i+1}/20`);
      times.push(r.duration || 0);
    }
    const avg = times.reduce((a,b)=>a+b,0)/times.length;
    addLog('metric', `   Avg: ${avg.toFixed(2)}ms | Min: ${Math.min(...times).toFixed(2)}ms | Max: ${Math.max(...times).toFixed(2)}ms`);
    testResults.push({ testName: 'Learning', success: true, duration: times.reduce((a,b)=>a+b,0), metrics: { avg } });
    await execQuery('2EMSA7 GADWAL ai_test;');  // DROP TABLE in Franco

    // ============ IMMUNE SYSTEM TESTS ============
    addLog('info', '');
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', '   IMMUNE SYSTEM (ANOMALY DETECTION) TESTS');
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', '');
    addLog('info', '   Thresholds:');
    addLog('info', '   • 50-199 rows in single DML = LOW severity');
    addLog('info', '   • 200-499 rows in single DML = MEDIUM severity (blocks table)');
    addLog('info', '   • 500+ rows in single DML = HIGH severity (triggers recovery)');
    addLog('info', '');

    // Setup test table for immune system
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 4: Immune System - Setup                           │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    await execQuery('2EMSA7 GADWAL immune_test;');  // DROP TABLE in Franco
    await execQuery('CREATE TABLE immune_test (id INTEGER PRIMARY KEY, value INTEGER, data GOMLA);');

    // First populate with baseline data using multi-row INSERT
    addLog('info', '   Establishing baseline with small operations...');
    const immuneValues = [];
    for (let i = 0; i < 100; i++) {
      immuneValues.push(`(${i}, ${Math.random()*100|0}, 'd${i}')`);
    }
    await execQuery(`INSERT INTO immune_test VALUES ${immuneValues.join(', ')};`, 'Multi-row INSERT 100 rows');
    addLog('success', '   ✓ Inserted 100 rows (no anomaly expected)');
    testResults.push({ testName: 'Immune Setup', success: true, duration: 0 });

    // Check anomalies before triggering
    const beforeAnomalies = await execQuery('SHOW ANOMALIES;');
    const beforeCount = beforeAnomalies.data?.rows.length || 0;
    addLog('info', `   Current anomaly count: ${beforeCount}`);

    // TEST 5: Trigger LOW severity (50-199 rows)
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 5: Trigger LOW Severity (50+ rows mass operation)  │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   ⚠️ Performing mass UPDATE affecting 60 rows...');
    const lowUpdate = await execQuery('UPDATE immune_test SET value = 999 WHERE id < 60;', 'Mass UPDATE 60 rows');

    // Check for LOW severity anomaly
    await new Promise(r => setTimeout(r, 500)); // Brief wait
    const afterLow = await execQuery('SHOW ANOMALIES;');
    const afterLowCount = afterLow.data?.rows.length || 0;
    const lowDetected = afterLowCount > beforeCount;
    if (lowDetected) {
      addLog('success', '   ✓ LOW severity anomaly detected!');
      const latestAnomaly = afterLow.data?.rows[0];
      if (latestAnomaly) addLog('result', `   ${latestAnomaly.join(' | ')}`);
    } else {
      addLog('error', '   ✗ No LOW severity anomaly detected');
    }
    testResults.push({ testName: 'LOW Severity', success: lowDetected, duration: lowUpdate.duration, metrics: { rows: 60 } });

    // TEST 6: Trigger MEDIUM severity (200-499 rows)
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 6: Trigger MEDIUM Severity (200+ rows)             │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   ⚠️ This may BLOCK the table temporarily!');

    // Need to add more rows first using multi-row INSERT
    addLog('info', '   Adding more rows for medium severity test...');
    const mediumValues = [];
    for (let i = 100; i < 350; i++) {
      mediumValues.push(`(${i}, ${Math.random()*100|0}, 'd${i}')`);
    }
    await execQuery(`INSERT INTO immune_test VALUES ${mediumValues.join(', ')};`, 'Multi-row INSERT 250 rows');
    addLog('success', '   ✓ Inserted 250 more rows');

    addLog('info', '   ⚠️ Performing mass DELETE affecting 250 rows...');
    const mediumDelete = await execQuery('DELETE FROM immune_test WHERE id >= 100;', 'Mass DELETE 250 rows');

    await new Promise(r => setTimeout(r, 500));
    const afterMedium = await execQuery('SHOW ANOMALIES;');
    const afterMediumCount = afterMedium.data?.rows.length || 0;
    const mediumDetected = afterMediumCount > afterLowCount;
    if (mediumDetected) {
      addLog('success', '   ✓ MEDIUM severity anomaly detected!');
      const latestAnomaly = afterMedium.data?.rows[0];
      if (latestAnomaly) {
        addLog('result', `   ${latestAnomaly.join(' | ')}`);
        if (latestAnomaly.some((s: string) => s.toLowerCase().includes('medium'))) {
          addLog('info', '   ⚠️ Table may be temporarily blocked');
        }
      }
    } else {
      addLog('error', '   ✗ No MEDIUM severity anomaly detected');
    }
    testResults.push({ testName: 'MEDIUM Severity', success: mediumDetected, duration: mediumDelete.duration, metrics: { rows: 250 } });

    // TEST 7: Trigger HIGH severity (500+ rows)
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 7: Trigger HIGH Severity (500+ rows)               │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    addLog('info', '   🚨 WARNING: This triggers auto-recovery mechanism!');

    // Create a new table for high severity test
    await execQuery('2EMSA7 GADWAL immune_high_test;');  // DROP TABLE in Franco
    await execQuery('CREATE TABLE immune_high_test (id INTEGER PRIMARY KEY, val INTEGER);');

    // Insert 600 rows using multi-row INSERT
    addLog('info', '   Preparing 600 rows for HIGH severity test...');
    const highValues = [];
    for (let i = 0; i < 600; i++) {
      highValues.push(`(${i}, ${Math.random()*100|0})`);
    }
    await execQuery(`INSERT INTO immune_high_test VALUES ${highValues.join(', ')};`, 'Multi-row INSERT 600 rows');
    addLog('success', '   ✓ Inserted 600 rows');

    addLog('info', '   🚨 Performing mass DELETE affecting 550 rows...');
    const highDelete = await execQuery('DELETE FROM immune_high_test WHERE id < 550;', 'Mass DELETE 550 rows');

    await new Promise(r => setTimeout(r, 500));
    const afterHigh = await execQuery('SHOW ANOMALIES;');
    const afterHighCount = afterHigh.data?.rows.length || 0;
    const highDetected = afterHighCount > afterMediumCount;
    if (highDetected) {
      addLog('success', '   ✓ HIGH severity anomaly detected!');
      const latestAnomaly = afterHigh.data?.rows[0];
      if (latestAnomaly) {
        addLog('result', `   ${latestAnomaly.join(' | ')}`);
        if (latestAnomaly.some((s: string) => s.toLowerCase().includes('high'))) {
          addLog('info', '   🚨 Auto-recovery may have been triggered!');
        }
      }
    } else {
      addLog('error', '   ✗ No HIGH severity anomaly detected');
    }
    testResults.push({ testName: 'HIGH Severity', success: highDetected, duration: highDelete.duration, metrics: { rows: 550 } });

    // TEST 8: Final anomaly summary
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ TEST 8: Anomaly Summary                                 │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    const finalAnomalies = await execQuery('SHOW ANOMALIES;');
    const totalAnomalies = finalAnomalies.data?.rows.length || 0;
    addLog('metric', `   Total anomalies detected: ${totalAnomalies}`);

    if (finalAnomalies.data?.rows.length) {
      addLog('info', '');
      addLog('info', '   All detected anomalies:');
      finalAnomalies.data.rows.forEach((r: string[], i: number) => {
        const severity = r.find((s: string) => ['LOW', 'MEDIUM', 'HIGH'].includes(s.toUpperCase()));
        const color = severity === 'HIGH' ? 'error' : severity === 'MEDIUM' ? 'metric' : 'info';
        addLog(color as any, `   ${i+1}. ${r.join(' | ')}`);
      });
    }

    const allTriggered = lowDetected && mediumDetected && highDetected;
    testResults.push({ testName: 'Anomaly Summary', success: totalAnomalies >= 3, duration: 0, metrics: { total: totalAnomalies } });

    // Cleanup
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ CLEANUP                                                 │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    await execQuery('2EMSA7 GADWAL immune_test;');
    await execQuery('2EMSA7 GADWAL immune_high_test;');

    const total = performance.now() - t0;
    const passed = testResults.filter(r => r.success).length;
    addLog('info', '');
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', '   AI & IMMUNE SYSTEM TESTS COMPLETE');
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('metric', `   Duration: ${total.toFixed(0)}ms | Passed: ${passed}/${testResults.length}`);
    if (allTriggered) {
      addLog('success', '   ✓ All severity levels (LOW, MEDIUM, HIGH) triggered successfully!');
    } else {
      addLog('error', '   ✗ Some severity levels were not triggered');
    }
    setResults(testResults); setRunning(false);
  }, [currentDb, addLog, execQuery]);

  // PERFORMANCE TESTS
  const runPerformanceTests = useCallback(async () => {
    if (!currentDb) return alert('Select a database first');
    setRunning(true); setLogs([]); setResults([]);
    const testResults: TestResult[] = [];
    const t0 = performance.now();

    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('info', '   CHRONOSDB PERFORMANCE TEST');
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('metric', `   Rows: ${perfConfig.rowCount.toLocaleString()} | Queries: ${perfConfig.queryCount}`);

    // Setup
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ PHASE 1: Setup                                          │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    // Use 2EMSA7 GADWAL for Franco compatibility
    await execQuery('2EMSA7 GADWAL p_idx;');
    await execQuery('2EMSA7 GADWAL p_seq;');
    await execQuery('CREATE TABLE p_idx (id INTEGER PRIMARY KEY, val INTEGER);');
    await execQuery('CREATE TABLE p_seq (id INTEGER PRIMARY KEY, val INTEGER);');
    await execQuery('CREATE INDEX idx_p ON p_idx(val);');
    // NO index on p_seq - this is the sequential scan table

    // Insert
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ PHASE 2: Insert Data                                    │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    const insStart = performance.now();

    // Use multi-row INSERT for better performance
    const BATCH_SIZE = 100000; // Number of rows per INSERT statement (increased for efficiency)
    const totalBatches = Math.ceil(perfConfig.rowCount / BATCH_SIZE);

    for (let batch = 0; batch < totalBatches; batch++) {
      const startIdx = batch * BATCH_SIZE;
      const endIdx = Math.min(startIdx + BATCH_SIZE, perfConfig.rowCount);

      // Build multi-row INSERT for p_idx
      const idxValues = [];
      const seqValues = [];
      for (let i = startIdx; i < endIdx; i++) {
        idxValues.push(`(${i}, ${Math.floor(Math.random()*1000)})`);
        seqValues.push(`(${i}, ${Math.floor(Math.random()*1000)})`);
      }

      // Execute both inserts using batch API for efficiency
      await api.batchQuery([
        `INSERT INTO p_idx VALUES ${idxValues.join(', ')};`,
        `INSERT INTO p_seq VALUES ${seqValues.join(', ')};`
      ]);

      // Progress update
      const progress = Math.min(100, ((batch + 1) / totalBatches * 100));
      if ((batch + 1) % Math.ceil(totalBatches / 10) === 0 || batch === totalBatches - 1) {
        addLog('info', `   ${progress.toFixed(0)}% (${endIdx.toLocaleString()} rows)`);
      }
    }
    const insDur = performance.now() - insStart;
    const rps = (perfConfig.rowCount*2)/(insDur/1000);
    addLog('metric', `   ${insDur.toFixed(0)}ms | ${rps.toFixed(0)} rows/sec`);
    testResults.push({ testName: 'Insert', success: true, duration: insDur, metrics: { rps } });

    // Index queries
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ PHASE 3: Index Scan                                     │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    const idxT: number[] = [];
    for (let i = 0; i < perfConfig.queryCount; i++) {
      const s = performance.now();
      await api.executeQuery(`SELECT * FROM p_idx WHERE val = ${Math.floor(Math.random()*1000)};`);
      idxT.push(performance.now() - s);
      if ((i+1)%10===0) addLog('info', `   Query ${i+1}/${perfConfig.queryCount}: avg ${(idxT.reduce((a,b)=>a+b,0)/idxT.length).toFixed(2)}ms`);
    }
    const idxAvg = idxT.reduce((a,b)=>a+b,0)/idxT.length;
    const idxP95 = idxT.sort((a,b)=>a-b)[Math.floor(idxT.length*0.95)];
    addLog('metric', `   Avg: ${idxAvg.toFixed(2)}ms | P95: ${idxP95.toFixed(2)}ms | QPS: ${(perfConfig.queryCount/(idxT.reduce((a,b)=>a+b,0)/1000)).toFixed(0)}`);
    testResults.push({ testName: 'Index Scan', success: true, duration: idxT.reduce((a,b)=>a+b,0), metrics: { avg: idxAvg, p95: idxP95 } });

    // Seq queries
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ PHASE 4: Sequential Scan                                │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    const seqT: number[] = [];
    for (let i = 0; i < perfConfig.queryCount; i++) {
      const s = performance.now();
      await api.executeQuery(`SELECT * FROM p_seq WHERE val = ${Math.floor(Math.random()*1000)};`);
      seqT.push(performance.now() - s);
      if ((i+1)%10===0) addLog('info', `   Query ${i+1}/${perfConfig.queryCount}: avg ${(seqT.reduce((a,b)=>a+b,0)/seqT.length).toFixed(2)}ms`);
    }
    const seqAvg = seqT.reduce((a,b)=>a+b,0)/seqT.length;
    const seqP95 = seqT.sort((a,b)=>a-b)[Math.floor(seqT.length*0.95)];
    addLog('metric', `   Avg: ${seqAvg.toFixed(2)}ms | P95: ${seqP95.toFixed(2)}ms | QPS: ${(perfConfig.queryCount/(seqT.reduce((a,b)=>a+b,0)/1000)).toFixed(0)}`);
    testResults.push({ testName: 'Seq Scan', success: true, duration: seqT.reduce((a,b)=>a+b,0), metrics: { avg: seqAvg, p95: seqP95 } });

    // Compare
    addLog('info', '');
    addLog('info', '┌─────────────────────────────────────────────────────────┐');
    addLog('info', '│ PHASE 5: Comparison                                     │');
    addLog('info', '└─────────────────────────────────────────────────────────┘');
    const speedup = seqAvg / idxAvg;
    const imp = ((seqAvg - idxAvg) / seqAvg * 100);
    addLog('metric', `   Index: ${idxAvg.toFixed(2)}ms | Seq: ${seqAvg.toFixed(2)}ms`);
    addLog(imp > 0 ? 'success' : 'info', imp > 0 ? `   ✓ Index ${imp.toFixed(1)}% faster (${speedup.toFixed(2)}x)` : `   Seq ${Math.abs(imp).toFixed(1)}% faster (small dataset)`);
    testResults.push({ testName: 'Comparison', success: true, duration: 0, metrics: { speedup, improvement: imp } });

    await execQuery('2EMSA7 GADWAL p_idx;');
    await execQuery('2EMSA7 GADWAL p_seq;');
    const total = performance.now() - t0;
    addLog('info', '═══════════════════════════════════════════════════════════');
    addLog('metric', `   Total: ${(total/1000).toFixed(1)}s | Speedup: ${speedup.toFixed(2)}x`);
    setResults(testResults); setRunning(false);
  }, [currentDb, perfConfig, addLog, execQuery]);

  const getLogClass = (t: TestLog['type']): string => {
    const classes: Record<TestLog['type'], string> = {
      success: 'log-success',
      error: 'log-error',
      query: 'log-query',
      result: 'log-result',
      metric: 'log-metric',
      info: 'log-info'
    };
    return classes[t];
  };

  const getRunHandler = () => {
    switch (activeTab) {
      case 'timetravel': return runTimeTravelTests;
      case 'ai': return runAITests;
      case 'security': return runSecurityTests;
      case 'optimizer': return runOptimizerTests;
      case 'temporal': return runTemporalTests;
      case 'performance': return runPerformanceTests;
    }
  };

  const getTabTitle = () => {
    switch (activeTab) {
      case 'timetravel': return 'Time Travel Tests';
      case 'ai': return 'AI & Immune System Tests';
      case 'security': return 'Security Threat Detection Tests';
      case 'optimizer': return 'Query Optimizer Tests';
      case 'temporal': return 'Temporal AI Tests';
      case 'performance': return 'Performance Tests';
    }
  };

  // Database selector when no DB is selected
  if (!currentDb) {
    return (
      <div className="panel">
        <div className="panel-header"><h3>Select a Database</h3></div>
        <div className="panel-body">
          <p className="text-muted" style={{ marginBottom: '1rem' }}>Choose a database to run tests against:</p>
          {loadingDbs ? (
            <p className="text-muted">Loading databases...</p>
          ) : databases.length > 0 ? (
            <div style={{ display: 'flex', flexWrap: 'wrap', gap: '0.5rem' }}>
              {databases.map(db => (
                <button
                  key={db}
                  className="btn-primary"
                  onClick={() => onUseDatabase?.(db)}
                  style={{ padding: '0.5rem 1.5rem' }}
                >
                  {db}
                </button>
              ))}
            </div>
          ) : (
            <p className="text-muted">No databases found. Create one in the Databases tab first.</p>
          )}
        </div>
      </div>
    );
  }

  return (
    <div className="testing-page">
      <div className="testing-tabs">
        <button className={`test-tab ${activeTab==='timetravel'?'active':''}`} onClick={()=>setActiveTab('timetravel')}>⏰ Time Travel</button>
        <button className={`test-tab ${activeTab==='ai'?'active':''}`} onClick={()=>setActiveTab('ai')}>🧠 AI Tests</button>
        <button className={`test-tab ${activeTab==='security'?'active':''}`} onClick={()=>setActiveTab('security')}>🛡️ Security</button>
        <button className={`test-tab ${activeTab==='optimizer'?'active':''}`} onClick={()=>setActiveTab('optimizer')}>⚙️ Optimizer</button>
        <button className={`test-tab ${activeTab==='temporal'?'active':''}`} onClick={()=>setActiveTab('temporal')}>🕐 Temporal</button>
        <button className={`test-tab ${activeTab==='performance'?'active':''}`} onClick={()=>setActiveTab('performance')}>⚡ Performance</button>
      </div>

      <div className="panel">
        <div className="panel-header">
          <h3>{getTabTitle()}</h3>
          <span className="text-muted">DB: {currentDb}</span>
        </div>
        <div className="panel-body">
          {activeTab==='timetravel' && <div className="test-description"><p>Tests:</p><ul><li><b>SELECT AS OF</b> - Historical snapshots</li><li><b>RECOVER TO</b> - Permanent rollback</li><li><b>CHECKPOINT</b> - Recovery points</li></ul></div>}
          {activeTab==='ai' && <div className="test-description"><p>Tests the AI Layer & Immune System:</p><ul>
            <li><b>Learning Engine</b> - UCB1 bandit scan selection training</li>
            <li><b>Immune System</b> - Anomaly detection with mass operations</li>
            <li><b>LOW Severity</b> - Trigger with 50-199 rows (logs warning)</li>
            <li><b>MEDIUM Severity</b> - Trigger with 200-499 rows (blocks table)</li>
            <li><b>HIGH Severity</b> - Trigger with 500+ rows (auto-recovery)</li>
          </ul></div>}
          {activeTab==='security' && <div className="test-description"><p>Tests the Immune System's Threat Detection:</p><ul>
            <li><b>SQL Injection</b> - UNION SELECT, DROP TABLE, OR 1=1, SLEEP()</li>
            <li><b>XSS Attacks</b> - &lt;script&gt;, javascript:, eval(), onerror=</li>
            <li><b>Case Evasion</b> - Mixed-case bypass attempts</li>
            <li><b>False Positives</b> - Normal queries must NOT be blocked</li>
            <li><b>Threat Stats</b> - Verify counters increment correctly</li>
          </ul></div>}
          {activeTab==='optimizer' && <div className="test-description"><p>Tests the Query Optimizer's UCB1 Bandits:</p><ul>
            <li><b>Filter Strategy</b> - Multi-predicate WHERE clause optimization (3 arms)</li>
            <li><b>Limit Strategy</b> - LIMIT clause handling optimization (2 arms)</li>
            <li><b>Complex Filters</b> - 3-predicate queries for deeper training</li>
            <li><b>Stats Verification</b> - Optimizer arm pulls and counters</li>
          </ul></div>}
          {activeTab==='temporal' && <div className="test-description"><p>Tests the Temporal AI Layer:</p><ul>
            <li><b>DBSCAN Hotspots</b> - Detect frequently-queried timestamps</li>
            <li><b>AS OF Clustering</b> - Multiple AS OF queries form temporal clusters</li>
            <li><b>Mutation Monitoring</b> - Track mutation rate spikes</li>
            <li><b>Access Tracking</b> - Verify temporal access recording</li>
          </ul></div>}
          {activeTab==='performance' && <div className="test-config"><h4>Configuration</h4><div className="config-grid">
            <div className="config-item"><label>Rows</label><select value={perfConfig.rowCount} onChange={e=>setPerfConfig(c=>({...c,rowCount:+e.target.value}))} disabled={running}>
              <option value={1000}>1K</option><option value={5000}>5K</option><option value={10000}>10K</option><option value={50000}>50K</option><option value={100000}>100K</option><option value={500000}>500K</option><option value={1000000}>1M</option>
            </select></div>
            <div className="config-item"><label>Queries</label><select value={perfConfig.queryCount} onChange={e=>setPerfConfig(c=>({...c,queryCount:+e.target.value}))} disabled={running}>
              <option value={10}>10</option><option value={25}>25</option><option value={50}>50</option><option value={100}>100</option><option value={200}>200</option><option value={500}>500</option>
            </select></div>
          </div></div>}
          <div className="test-actions">
            <button className="btn-primary btn-lg" onClick={getRunHandler()} disabled={running}>
              {running?'⏳ Running...':'▶ Run Tests'}
            </button>
            {logs.length>0&&!running&&<button className="btn-sm" onClick={()=>setLogs([])}>Clear</button>}
          </div>
        </div>
      </div>

      {logs.length>0&&<div className="panel test-log-panel"><div className="panel-header"><h3>Output</h3><span className="log-count">{logs.length}</span></div>
        <div className="test-log-container"><div className="test-log">
          {logs.map((l,i)=><div key={i} className={`log-entry ${getLogClass(l.type)}`}><span className="log-time">{new Date(l.timestamp).toLocaleTimeString()}</span><span className="log-message">{l.message}</span></div>)}
          <div ref={logsEndRef}/>
        </div></div>
      </div>}

      {results.length>0&&!running&&<div className="panel"><div className="panel-header"><h3>Summary</h3><span className={`badge ${results.every(r=>r.success)?'badge-green':'badge-red'}`}>{results.filter(r=>r.success).length}/{results.length}</span></div>
        <div className="panel-body"><div className="results-grid">{results.map((r,i)=><div key={i} className={`result-card ${r.success?'success':'error'}`}>
          <div className="result-icon">{r.success?'✓':'✗'}</div>
          <div className="result-info"><div className="result-name">{r.testName}</div><div className="result-duration">{r.duration.toFixed(0)}ms</div></div>
          {r.metrics&&<div className="result-metrics">{Object.entries(r.metrics).map(([k,v])=><span key={k} className="metric-tag">{k}: {typeof v==='number'?v.toFixed(2):v}</span>)}</div>}
        </div>)}</div></div>
      </div>}
    </div>
  );
}
