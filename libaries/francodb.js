// ==========================================
// 3. CURSOR CLASS (FIXED)
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

        // 1. Select Protocol
        let msgType = CMD_TEXT;
        if (mode === 'json') msgType = CMD_JSON;
        else if (mode === 'binary') msgType = CMD_BINARY;

        // 2. Pack & Send Request
        const payloadBytes = Buffer.from(fql, 'utf-8');
        const header = Buffer.alloc(5);
        msgType.copy(header, 0); // Byte 0: Type
        header.writeUInt32BE(payloadBytes.length, 1); // Bytes 1-4: Length

        try {
            this._conn.socket.write(Buffer.concat([header, payloadBytes]));

            // 3. Read Response Packet (Length + Body)
            // We use a helper to handle the Length-Prefix framing
            const packet = await this._readPacket();

            // Store for debugging
            this.lastRawBytes = packet;

            // 4. Parse based on Mode
            if (mode === 'binary') {
                return this._parseBinary(packet);
            } else {
                return this._parseText(packet);
            }
        } catch (e) {
            if (e instanceof FrancoDBError) throw e;
            throw new OperationalError(`Network error: ${e.message}`);
        }
    }

    // [FIX] Generic Packet Reader: Reads 4 bytes Length, then N bytes Body
    async _readPacket() {
        return new Promise((resolve, reject) => {
            let buffer = Buffer.alloc(0);
            let needed = 4; // Start by needing 4 bytes for Length Header
            let readingBody = false;

            const timeout = setTimeout(() => {
                cleanup();
                reject(new OperationalError("Query timed out"));
            }, this._conn.timeout);

            const onData = (chunk) => {
                buffer = Buffer.concat([buffer, chunk]);

                // Phase 1: Read Header
                if (!readingBody) {
                    if (buffer.length >= 4) {
                        // We have the length header!
                        needed = buffer.readUInt32BE(0);

                        // Consume header, keep remainder
                        buffer = buffer.subarray(4);
                        readingBody = true;
                    }
                }

                // Phase 2: Read Body (Check repeatedly in case chunk was large)
                if (readingBody) {
                    if (buffer.length >= needed) {
                        // We have the full body!
                        const payload = buffer.subarray(0, needed);
                        cleanup();
                        resolve(payload);
                    }
                }
            };

            const onError = (err) => {
                cleanup();
                reject(err);
            };

            const cleanup = () => {
                clearTimeout(timeout);
                this._conn.socket.removeListener('data', onData);
                this._conn.socket.removeListener('error', onError);
            };

            this._conn.socket.on('data', onData);
            this._conn.socket.on('error', onError);
        });
    }

    _parseText(buffer) {
        const response = buffer.toString('utf-8').trim();
        if (response.includes("ERROR") && !response.startsWith("{")) {
            throw new QueryError(response);
        }
        return response;
    }

    _parseBinary(buffer) {
        let offset = 0;

        // 1. Read Response Type (1 Byte)
        if (buffer.length < 1) throw new OperationalError("Empty binary response");
        const respType = buffer.readUInt8(offset);
        offset += 1;

        // --- CASE: ERROR (0xFF) ---
        if (respType === 0xFF) {
            const msgLen = buffer.readUInt32BE(offset);
            offset += 4;
            const errorMsg = buffer.toString('utf-8', offset, offset + msgLen);
            throw new QueryError(`Server Error: ${errorMsg}`);
        }

        // --- CASE: SIMPLE MESSAGE (0x01) ---
        if (respType === 0x01) {
            const msgLen = buffer.readUInt32BE(offset);
            offset += 4;
            return buffer.toString('utf-8', offset, offset + msgLen);
        }

        // --- CASE: TABLE DATA (0x02) ---
        if (respType === 0x02) {
            const numCols = buffer.readUInt32BE(offset); offset += 4;
            const numRows = buffer.readUInt32BE(offset); offset += 4;

            // B. Read Columns
            const columns = [];
            for (let i = 0; i < numCols; i++) {
                // Skip Type (1 byte)
                offset += 1;
                const nameLen = buffer.readUInt32BE(offset); offset += 4;
                const colName = buffer.toString('utf-8', offset, offset + nameLen);
                offset += nameLen;
                columns.push(colName);
            }

            // C. Read Rows
            const resultRows = [];
            for (let i = 0; i < numRows; i++) {
                const rowData = [];
                for (let j = 0; j < numCols; j++) {
                    const valLen = buffer.readUInt32BE(offset); offset += 4;
                    const valStr = buffer.toString('utf-8', offset, offset + valLen);
                    offset += valLen;

                    // Auto-convert numbers
                    if (/^-?\d+$/.test(valStr)) {
                        rowData.push(parseInt(valStr, 10));
                    } else if (/^-?\d*\.\d+$/.test(valStr)) {
                        rowData.push(parseFloat(valStr));
                    } else {
                        rowData.push(valStr);
                    }
                }
                resultRows.push(rowData);
            }
            return resultRows;
        }

        throw new OperationalError(`Unknown response type: 0x${respType.toString(16)}`);
    }

    close() {
        // No-op
    }
}