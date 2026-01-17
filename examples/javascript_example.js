const francodb = require('./francodb');
const readline = require('readline');

// --- UI Helpers ---
class UI {
    static HEADER = '\x1b[95m';
    static BLUE = '\x1b[94m';
    static GREEN = '\x1b[92m';
    static WARNING = '\x1b[93m';
    static FAIL = '\x1b[91m';
    static ENDC = '\x1b[0m';
    static BOLD = '\x1b[1m';

    static printError(msg) {
        console.log(`${UI.FAIL}[ERROR] ${msg}${UI.ENDC}`);
    }

    static printSuccess(msg) {
        console.log(`${UI.GREEN}[SUCCESS] ${msg}${UI.ENDC}`);
    }

    static printJson(dataStr) {
        try {
            // If the driver returned a list directly (Binary/Object mode)
            if (Array.isArray(dataStr)) {
                console.log(`${UI.BLUE}${JSON.stringify(dataStr, null, 4)}${UI.ENDC}`);
                console.log(`${UI.WARNING}Records: ${dataStr.length}${UI.ENDC}`);
                return;
            }

            // If it's a JSON string
            const parsed = JSON.parse(dataStr);
            const prettyOutput = JSON.stringify(parsed, null, 4);
            console.log(`${UI.BLUE}${prettyOutput}${UI.ENDC}`);
            if (Array.isArray(parsed)) {
                console.log(`${UI.WARNING}Records: ${parsed.length}${UI.ENDC}`);
            }
        } catch (e) {
            // Fallback for plain text
            console.log(`${UI.BLUE}${dataStr}${UI.ENDC}`);
        }
    }

    static printBinary(data) {
        console.log(`${UI.BLUE}--- BINARY PROTOCOL RESPONSE ---${UI.ENDC}`);

        // 1. Handle List (The Result Set)
        if (Array.isArray(data)) {
            console.log(`${UI.BOLD}Type: Result Set (Parsed from Binary)${UI.ENDC}`);
            console.log(`Row Count: ${data.length}`);
            console.log('-'.repeat(40));
            data.forEach((row, i) => {
                console.log(`[${String(i).padStart(3, '0')}] ${JSON.stringify(row)}`);
            });
            console.log('-'.repeat(40));
            return;
        }

        // 2. Handle String (Simple Messages like "OK") - Hex Dump
        try {
            const dataBytes = Buffer.from(String(data), 'utf-8');
            console.log(`${UI.BOLD}Type: Raw Message (Hex Dump)${UI.ENDC}`);

            const chunkSize = 16;
            for (let i = 0; i < dataBytes.length; i += chunkSize) {
                const chunk = dataBytes.slice(i, i + chunkSize);
                const hexPart = Array.from(chunk)
                    .map(b => b.toString(16).padStart(2, '0'))
                    .join(' ');
                const textPart = Array.from(chunk)
                    .map(b => (b >= 32 && b < 127) ? String.fromCharCode(b) : '.')
                    .join('');
                console.log(`${i.toString(16).padStart(4, '0')}  ${hexPart.padEnd(48, ' ')}  |${textPart}|`);
            }

            console.log(`${UI.BLUE}--- END DUMP (${dataBytes.length} bytes) ---${UI.ENDC}`);
        } catch (e) {
            console.log(`Binary display error: ${e.message}`);
        }
    }
}

// --- Readline Interface ---
const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
});

function question(prompt) {
    return new Promise((resolve) => {
        rl.question(prompt, resolve);
    });
}

async function mainApp() {
    console.log(`${UI.HEADER}${UI.BOLD}=== FrancoDB Admin Console ===${UI.ENDC}`);

    let conn;
    try {
        conn = await francodb.connect({
            host: '127.0.0.1',
            port: 2501,
            user: 'maayn',
            password: 'root',
            database: 'mydb'
        });
        UI.printSuccess("Connected!");
    } catch (e) {
        UI.printError(`Connection failed: ${e.message}`);
        rl.close();
        return;
    }

    const cur = conn.cursor();

    while (true) {
        console.log(`\n${UI.BOLD}--- MENU ---${UI.ENDC}`);
        console.log("1. Initialize Database");
        console.log("2. Add User");
        console.log("3. View Users (Select Mode)");
        console.log("4. Delete User");
        console.log("5. Custom Query");
        console.log("0. Exit");

        const choice = await question(`\n${UI.WARNING}Select > ${UI.ENDC}`);

        // --- OPTION 1: INIT ---
        if (choice === '1') {
            try {
                await cur.execute("2E3MEL GADWAL users (id RAKAM, name GOMLA);");
                UI.printSuccess("Table created.");
            } catch (e) {
                if (e.message.includes("exists")) {
                    UI.printSuccess("Table already exists.");
                } else {
                    UI.printError(e.message);
                }
            }
        }

        // --- OPTION 2: ADD ---
        else if (choice === '2') {
            const uid = await question("ID: ");
            const name = await question("Name: ");

            if (/^\d+$/.test(uid)) {
                try {
                    await cur.execute(`EMLA GOWA users ELKEYAM (${uid}, '${name}');`);
                    UI.printSuccess("User added.");
                } catch (e) {
                    UI.printError(e.message);
                }
            } else {
                UI.printError("ID must be numeric");
            }
        }

        // --- OPTION 3: VIEW (MODE SELECTION) ---
        else if (choice === '3') {
            console.log("\n[Select Protocol Mode]");
            console.log("t - Text   (Standard Table)");
            console.log("j - JSON   (Web/API Format)");
            console.log("b - Binary (High Performance List)");

            const modeInput = (await question("Mode [t/j/b] > ")).toLowerCase().trim();

            let selectedMode = 'text';
            if (modeInput === 'j') selectedMode = 'json';
            else if (modeInput === 'b') selectedMode = 'binary';

            console.log(`Fetching data in '${selectedMode}' mode...`);

            try {
                const response = await cur.execute("2E5TAR * MEN users;", selectedMode);

                if (selectedMode === 'json') {
                    UI.printJson(response);
                } else if (selectedMode === 'binary') {
                    UI.printBinary(response);
                } else {
                    console.log(`${UI.BLUE}--- TEXT RESPONSE ---${UI.ENDC}`);
                    console.log(response);
                }
            } catch (e) {
                UI.printError(e.message);
            }
        }

        // --- OPTION 4: DELETE ---
        else if (choice === '4') {
            const uid = await question("ID to delete: ");
            try {
                await cur.execute(`2EMSA7 MEN users LAMA id = ${uid};`);
                UI.printSuccess("Deleted.");
            } catch (e) {
                UI.printError(e.message);
            }
        }

        // --- OPTION 5: CUSTOM ---
        else if (choice === '5') {
            const q = await question("FQL Query: ");
            try {
                const result = await cur.execute(q);
                console.log(result);
            } catch (e) {
                UI.printError(e.message);
            }
        }

        // --- EXIT ---
        else if (choice === '0') {
            console.log("Goodbye!");
            break;
        }
    }

    conn.close();
    rl.close();
}

// --- Entry Point ---
if (require.main === module) {
    mainApp().catch((err) => {
        console.error(err);
        process.exit(1);
    });
}