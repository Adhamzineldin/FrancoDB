/**
 * ChronosDB TCP Protocol Client
 *
 * Connects to ChronosDB server using its native binary protocol.
 * Protocol format:
 *   Request:  [1 byte MsgType] [4 bytes payload_length (big-endian)] [payload]
 *   Response: [4 bytes response_length (big-endian)] [response_data]
 */

import * as net from 'net';

// Protocol message types (must match server's MsgType enum)
const MSG_TYPE = {
  CMD_TEXT: 0x51,   // 'Q' - Text response
  CMD_JSON: 0x4A,   // 'J' - JSON response
  CMD_BINARY: 0x42, // 'B' - Binary response
  CMD_LOGIN: 0x4C,  // 'L' - Login handshake
} as const;

export interface ChronosResult {
  success: boolean;
  data?: {
    columns: string[];
    rows: string[][];
  };
  row_count?: number;
  message?: string;
  error?: string;
}

export class ChronosClient {
  private socket: net.Socket | null = null;
  private host: string;
  private port: number;
  private connected = false;

  constructor(host = 'localhost', port = 2501) {
    this.host = host;
    this.port = port;
  }

  async connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      this.socket = new net.Socket();
      this.socket.setTimeout(10000);

      this.socket.on('error', (err) => {
        this.connected = false;
        reject(new Error(`Connection error: ${err.message}`));
      });

      this.socket.on('timeout', () => {
        this.connected = false;
        this.socket?.destroy();
        reject(new Error('Connection timeout'));
      });

      this.socket.on('close', () => {
        this.connected = false;
      });

      this.socket.connect(this.port, this.host, () => {
        this.connected = true;
        resolve();
      });
    });
  }

  async login(username: string, password: string): Promise<ChronosResult> {
    const sql = `LOGIN '${username}' '${password}'`;
    return this.sendCommand(sql, MSG_TYPE.CMD_TEXT);
  }

  async query(sql: string): Promise<ChronosResult> {
    return this.sendCommand(sql, MSG_TYPE.CMD_JSON);
  }

  async textQuery(sql: string): Promise<string> {
    const result = await this.sendRaw(sql, MSG_TYPE.CMD_TEXT);
    return result;
  }

  private async sendCommand(sql: string, msgType: number): Promise<ChronosResult> {
    const raw = await this.sendRaw(sql, msgType);

    if (msgType === MSG_TYPE.CMD_JSON) {
      try {
        return JSON.parse(raw);
      } catch {
        // If JSON parsing fails, wrap as text result
        if (raw.startsWith('ERROR:')) {
          return { success: false, error: raw.replace('ERROR: ', '') };
        }
        return { success: true, message: raw };
      }
    }

    // Text mode response
    if (raw.startsWith('ERROR:')) {
      return { success: false, error: raw.replace('ERROR: ', '').trim() };
    }
    return { success: true, message: raw.trim() };
  }

  private async sendRaw(sql: string, msgType: number): Promise<string> {
    if (!this.socket || !this.connected) {
      throw new Error('Not connected to ChronosDB');
    }

    return new Promise((resolve, reject) => {
      const payload = Buffer.from(sql, 'utf-8');

      // Build packet: [1 byte type] [4 bytes length BE] [payload]
      const header = Buffer.alloc(5);
      header.writeUInt8(msgType, 0);
      header.writeUInt32BE(payload.length, 1);

      const packet = Buffer.concat([header, payload]);

      // Set up response reader
      let responseBuffer = Buffer.alloc(0);
      let expectedLength = -1;
      let headerRead = false;

      const timeout = setTimeout(() => {
        this.socket?.removeListener('data', onData);
        reject(new Error('Query timeout'));
      }, 30000);

      const onData = (data: Buffer) => {
        responseBuffer = Buffer.concat([responseBuffer, data]);

        // Read 4-byte response length header
        if (!headerRead && responseBuffer.length >= 4) {
          expectedLength = responseBuffer.readUInt32BE(0);
          responseBuffer = responseBuffer.subarray(4);
          headerRead = true;
        }

        // Check if we have the full response
        if (headerRead && responseBuffer.length >= expectedLength) {
          clearTimeout(timeout);
          this.socket?.removeListener('data', onData);
          const response = responseBuffer.subarray(0, expectedLength).toString('utf-8');
          resolve(response);
        }
      };

      this.socket!.on('data', onData);
      this.socket!.write(packet);
    });
  }

  isConnected(): boolean {
    return this.connected;
  }

  disconnect(): void {
    if (this.socket) {
      this.socket.destroy();
      this.socket = null;
      this.connected = false;
    }
  }
}

/**
 * Connection pool that manages per-session ChronosDB connections.
 */
export class ChronosConnectionPool {
  private connections = new Map<string, ChronosClient>();
  private host: string;
  private port: number;

  constructor(host = 'localhost', port = 2501) {
    this.host = host;
    this.port = port;
  }

  async getConnection(sessionId: string, username: string, password: string): Promise<ChronosClient> {
    let client = this.connections.get(sessionId);

    if (client && client.isConnected()) {
      return client;
    }

    // Create new connection
    client = new ChronosClient(this.host, this.port);
    await client.connect();

    // Authenticate
    const loginResult = await client.login(username, password);
    if (!loginResult.success && !loginResult.message?.includes('LOGIN OK')) {
      client.disconnect();
      throw new Error(loginResult.error || loginResult.message || 'Login failed');
    }

    this.connections.set(sessionId, client);
    return client;
  }

  removeConnection(sessionId: string): void {
    const client = this.connections.get(sessionId);
    if (client) {
      client.disconnect();
      this.connections.delete(sessionId);
    }
  }

  disconnectAll(): void {
    for (const [, client] of this.connections) {
      client.disconnect();
    }
    this.connections.clear();
  }
}
