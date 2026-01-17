const net = require('net');

// ==========================================
// 1. CUSTOM EXCEPTIONS
// ==========================================
class FrancoDBError extends Error {
    constructor(message) {
        super(message);
        this.name = 'FrancoDBError';
    }
}

class OperationalError extends FrancoDBError {
    constructor(message) {
        super(message);
        this.name = 'OperationalError';
    }
}

class AuthError extends FrancoDBError {
    constructor(message) {
        super(message);
        this.name = 'AuthError';
    }
}

class QueryError extends FrancoDBError {
    constructor(message) {
        super(message);
        this.name = 'QueryError';
    }
}

// ==========================================
// 2. PROTOCOL CONSTANTS
// ==========================================
const CMD_TEXT = Buffer.from('Q');
const CMD_JSON = Buffer.from('J');
const CMD_BINARY = Buffer.from('B');

// ==========================================
// 3. CURSOR CLASS
// ==========================================
class Cursor {
    constructor(connection) {
        this._conn = connection;
        this._lastResult = null;
        this.lastRawBytes = Buffer.alloc(0);
    }

    async execute(fql, mode = 'text') {
        if (!this._conn.isConnected()) {
            throw new OperationalError("Database is not connected");
        }

        // Clear previous capture
        this.lastRawBytes = Buffer.alloc(0);

        // 1. Select Protocol
        let msgType = CMD_TEXT;
        if (mode === 'json') msgType = CMD_JSON;
        else if (mode === 'binary') msgType = CMD_BINARY;

        // 2. Pack & Send
        const payloadBytes = Buffer.from(fql, 'utf-8');
        const header = Buffer.alloc(5);
        msgType.copy(header, 0);
        header.writeUInt32BE(payloadBytes.length, 1);

        try {
            this._conn.socket.write(Buffer.concat([header, payloadBytes]));

            // 3. Read Response based on Mode
            if (mode === 'binary') {
                return await this._readBinaryResponse();
            } else {
                return await this._readTextResponse();
            }
        } catch (e) {
            throw new OperationalError(`Network error: ${e.message}`);
        }
    }

    async _readTextResponse() {
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new OperationalError("Query timed out"));
            }, this._conn.timeout);

            this._conn.socket.once('data', (data) => {
                clearTimeout(timeout);
                this.lastRawBytes = Buffer.concat([this.lastRawBytes, data]);

                const response = data.toString('utf-8').trim();
                if (response.includes("ERROR") && !response.startsWith("{")) {
                    reject(new QueryError(response));
                } else {
                    resolve(response);
                }
            });
        });
    }

    async _readBinaryResponse() {
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new OperationalError("Query timed out"));
            }, this._conn.timeout);

            let buffer = Buffer.alloc(0);
            let offset = 0;

            const onData = (chunk) => {
                buffer = Buffer.concat([buffer, chunk]);
                this.lastRawBytes = Buffer.concat([this.lastRawBytes, chunk]);

                try {
                    // 1. Read Response Type (1 Byte)
                    if (buffer.length < 1) return;
                    const respType = buffer.readUInt8(0);
                    offset = 1;

                    // --- CASE: ERROR (0xFF) ---
                    if (respType === 0xFF) {
                        if (buffer.length < offset + 4) return;
                        const msgLen = buffer.readUInt32BE(offset);
                        offset += 4;

                        if (buffer.length < offset + msgLen) return;
                        const errorMsg = buffer.toString('utf-8', offset, offset + msgLen);

                        clearTimeout(timeout);
                        this._conn.socket.removeListener('data', onData);
                        reject(new QueryError(`Server Error: ${errorMsg}`));
                        return;
                    }

                    // --- CASE: SIMPLE MESSAGE (0x01) ---
                    if (respType === 0x01) {
                        if (buffer.length < offset + 4) return;
                        const msgLen = buffer.readUInt32BE(offset);
                        offset += 4;

                        if (buffer.length < offset + msgLen) return;
                        const message = buffer.toString('utf-8', offset, offset + msgLen);

                        clearTimeout(timeout);
                        this._conn.socket.removeListener('data', onData);
                        resolve(message);
                        return;
                    }

                    // --- CASE: TABLE DATA (0x02) ---
                    if (respType === 0x02) {
                        // A. Read Metadata
                        if (buffer.length < offset + 8) return;
                        const numCols = buffer.readUInt32BE(offset);
                        offset += 4;
                        const numRows = buffer.readUInt32BE(offset);
                        offset += 4;

                        // B. Read Columns
                        const columns = [];
                        for (let i = 0; i < numCols; i++) {
                            if (buffer.length < offset + 1) return;
                            const colType = buffer.readUInt8(offset);
                            offset += 1;

                            if (buffer.length < offset + 4) return;
                            const nameLen = buffer.readUInt32BE(offset);
                            offset += 4;

                            if (buffer.length < offset + nameLen) return;
                            const colName = buffer.toString('utf-8', offset, offset + nameLen);
                            offset += nameLen;
                            columns.push(colName);
                        }

                        // C. Read Rows
                        const resultRows = [];
                        for (let i = 0; i < numRows; i++) {
                            const rowData = [];
                            for (let j = 0; j < numCols; j++) {
                                if (buffer.length < offset + 4) return;
                                const valLen = buffer.readUInt32BE(offset);
                                offset += 4;

                                if (buffer.length < offset + valLen) return;
                                const valStr = buffer.toString('utf-8', offset, offset + valLen);
                                offset += valLen;

                                // Auto-convert numbers
                                if (/^\d+$/.test(valStr)) {
                                    rowData.push(parseInt(valStr, 10));
                                } else {
                                    rowData.push(valStr);
                                }
                            }
                            resultRows.push(rowData);
                        }

                        clearTimeout(timeout);
                        this._conn.socket.removeListener('data', onData);
                        resolve(resultRows);
                        return;
                    }

                    clearTimeout(timeout);
                    this._conn.socket.removeListener('data', onData);
                    reject(new OperationalError(`Unknown response type: 0x${respType.toString(16)}`));

                } catch (e) {
                    clearTimeout(timeout);
                    this._conn.socket.removeListener('data', onData);
                    if (e instanceof QueryError) {
                        reject(e);
                    } else {
                        reject(new OperationalError(`Binary parse failed: ${e.message}`));
                    }
                }
            };

            this._conn.socket.on('data', onData);
        });
    }

    close() {
        // No-op for now
    }
}

// ==========================================
// 4. CONNECTION CLASS
// ==========================================
class FrancoDB {
    constructor(host = 'localhost', port = 2501, timeout = 10000) {
        this.host = host;
        this.port = port;
        this.timeout = timeout;
        this.socket = null;
        this._connected = false;
    }

    async connect(username = '', password = '', database = '') {
        return new Promise((resolve, reject) => {
            this.socket = net.createConnection({ host: this.host, port: this.port }, async () => {
                this._connected = true;

                try {
                    const cur = this.cursor();

                    if (username && password) {
                        const res = await cur.execute(`LOGIN ${username} ${password};\n`);
                        if (!res.includes("OK") && !res.includes("SUCCESS")) {
                            throw new AuthError(`Login failed: ${res}`);
                        }
                    }

                    if (database) {
                        await cur.execute(`2ESTA5DEM ${database};\n`);
                    }

                    resolve();
                } catch (e) {
                    this._connected = false;
                    reject(e);
                }
            });

            this.socket.on('error', (err) => {
                this._connected = false;
                reject(new OperationalError(`Connection error: ${err.message}`));
            });
        });
    }

    cursor() {
        return new Cursor(this);
    }

    isConnected() {
        return this._connected && this.socket !== null;
    }

    close() {
        if (this.socket) {
            try {
                this.socket.destroy();
            } catch (e) {
                // Ignore errors on close
            }
        }
        this.socket = null;
        this._connected = false;
    }
}

// ==========================================
// 5. EXPORTS & CONVENIENCE FUNCTION
// ==========================================
async function connect(options = {}) {
    const {
        host = 'localhost',
        port = 2501,
        user = '',
        password = '',
        database = ''
    } = options;

    const conn = new FrancoDB(host, port);
    await conn.connect(user, password, database);
    return conn;
}

module.exports = {
    FrancoDB,
    Cursor,
    connect,
    FrancoDBError,
    OperationalError,
    AuthError,
    QueryError
};