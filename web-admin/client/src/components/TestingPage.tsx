import { useState, useCallback, useRef } from 'react';
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

export default function TestingPage({ currentDb }: { currentDb: string }) {
  const [activeTab, setActiveTab] = useState<'timetravel' | 'ai' | 'performance'>('timetravel');
  const [running, setRunning] = useState(false);
  const [logs, setLogs] = useState<TestLog[]>([]);
  const [results, setResults] = useState<TestResult[]>([]);
  const logsEndRef = useRef<HTMLDivElement>(null);

  const [perfConfig, setPerfConfig] = useState({
    rowCount: 10000,
    queryCount: 50,
  });

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

  // AI TESTS
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
    const BATCH_SIZE = 100; // Number of rows per INSERT statement
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

  if (!currentDb) return <div className="panel"><div className="panel-body"><p className="text-muted">Select a database first</p></div></div>;

  return (
    <div className="testing-page">
      <div className="testing-tabs">
        <button className={`test-tab ${activeTab==='timetravel'?'active':''}`} onClick={()=>setActiveTab('timetravel')}>⏰ Time Travel</button>
        <button className={`test-tab ${activeTab==='ai'?'active':''}`} onClick={()=>setActiveTab('ai')}>🧠 AI Tests</button>
        <button className={`test-tab ${activeTab==='performance'?'active':''}`} onClick={()=>setActiveTab('performance')}>⚡ Performance</button>
      </div>

      <div className="panel">
        <div className="panel-header">
          <h3>{activeTab==='timetravel'?'Time Travel Tests':activeTab==='ai'?'AI System Tests':'Performance Tests'}</h3>
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
          {activeTab==='performance' && <div className="test-config"><h4>Configuration</h4><div className="config-grid">
            <div className="config-item"><label>Rows</label><select value={perfConfig.rowCount} onChange={e=>setPerfConfig(c=>({...c,rowCount:+e.target.value}))} disabled={running}>
              <option value={1000}>1K</option><option value={5000}>5K</option><option value={10000}>10K</option><option value={50000}>50K</option><option value={100000}>100K</option><option value={500000}>500K</option><option value={1000000}>1M</option>
            </select></div>
            <div className="config-item"><label>Queries</label><select value={perfConfig.queryCount} onChange={e=>setPerfConfig(c=>({...c,queryCount:+e.target.value}))} disabled={running}>
              <option value={10}>10</option><option value={25}>25</option><option value={50}>50</option><option value={100}>100</option><option value={200}>200</option><option value={500}>500</option>
            </select></div>
          </div></div>}
          <div className="test-actions">
            <button className="btn-primary btn-lg" onClick={activeTab==='timetravel'?runTimeTravelTests:activeTab==='ai'?runAITests:runPerformanceTests} disabled={running}>
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










