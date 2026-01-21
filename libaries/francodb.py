import socket
import struct
from typing import Optional, Tuple, Any, List, Dict, Union

# ==========================================
# 1. CUSTOM EXCEPTIONS
# ==========================================
class FrancoDBError(Exception): pass
class OperationalError(FrancoDBError): pass
class AuthError(FrancoDBError): pass
class QueryError(FrancoDBError): pass

# ==========================================
# 2. PROTOCOL CONSTANTS
# ==========================================
CMD_TEXT   = b'Q'
CMD_JSON   = b'J'
CMD_BINARY = b'B'
HEADER_STRUCT = struct.Struct('!cI')

# ==========================================
# 3. CURSOR CLASS
# ==========================================
class Cursor:
    def __init__(self, connection):
        self._conn = connection
        self._last_result = None
        self.last_raw_bytes = b'' 

    def _recv_n(self, n):
        """Helper: Strictly reads n bytes."""
        data = b''
        while len(data) < n:
            packet = self._conn.sock.recv(n - len(data))
            if not packet:
                raise OperationalError("Connection closed by server")
            data += packet
        self.last_raw_bytes += data
        return data

    def execute(self, fql: str, mode: str = 'text') -> Union[str, List, Dict]:
        if not self._conn.is_connected():
            raise OperationalError("Database is not connected")

        self.last_raw_bytes = b''

        # 1. Select Protocol
        msg_type = CMD_TEXT
        if mode == 'json': msg_type = CMD_JSON
        elif mode == 'binary': msg_type = CMD_BINARY

        # 2. Pack & Send Request
        payload_bytes = fql.encode('utf-8')
        header = HEADER_STRUCT.pack(msg_type, len(payload_bytes))

        try:
            self._conn.sock.sendall(header + payload_bytes)

            # [FIX] 3. Read Response LENGTH Header (4 Bytes)
            # The server now sends the length first!
            len_bytes = self._recv_n(4)
            resp_len = struct.unpack('!I', len_bytes)[0]

            # 4. Read Response BODY
            # We read exactly 'resp_len' bytes. This is the Payload.
            body_data = self._recv_n(resp_len)

            # 5. Parse Payload
            if mode == 'binary':
                return self._parse_binary_body(body_data)
            else:
                return self._parse_text_body(body_data)

        except socket.error as e:
            raise OperationalError(f"Network error: {e}")

    def _parse_text_body(self, data: bytes) -> str:
        """Standard text/json reader."""
        try:
            response = data.decode('utf-8').strip()
            if "ERROR" in response and not response.startswith("{"):
                raise QueryError(response)
            return response
        except UnicodeDecodeError:
            raise OperationalError("Received non-text data")

    def _parse_binary_body(self, data: bytes):
        """
        Unpacks the Professional Binary Protocol from C++.
        Now works on the pre-fetched 'data' buffer.
        """
        try:
            # We need a stream-like object to read sequentially from 'data'
            # Or just use slicing logic. Let's use slicing for clarity.
            ptr = 0
            
            # 1. Read Response Type (1 Byte)
            resp_type = data[ptr]
            ptr += 1

            # --- CASE: ERROR (0xFF) ---
            if resp_type == 0xFF:
                msg_len = struct.unpack('!I', data[ptr:ptr+4])[0]
                ptr += 4
                error_msg = data[ptr:ptr+msg_len].decode('utf-8')
                raise QueryError(f"Server Error: {error_msg}")

            # --- CASE: SIMPLE MESSAGE (0x01) ---
            elif resp_type == 0x01:
                msg_len = struct.unpack('!I', data[ptr:ptr+4])[0]
                ptr += 4
                return data[ptr:ptr+msg_len].decode('utf-8')

            # --- CASE: TABLE DATA (0x02) ---
            elif resp_type == 0x02:
                # A. Read Metadata
                num_cols = struct.unpack('!I', data[ptr:ptr+4])[0]
                ptr += 4
                num_rows = struct.unpack('!I', data[ptr:ptr+4])[0]
                ptr += 4

                # B. Read Columns
                columns = []
                for _ in range(num_cols):
                    # col_type = data[ptr] # Unused in python currently
                    ptr += 1 
                    name_len = struct.unpack('!I', data[ptr:ptr+4])[0]
                    ptr += 4
                    col_name = data[ptr:ptr+name_len].decode('utf-8')
                    ptr += name_len
                    columns.append(col_name)

                # C. Read Rows
                result_rows = []
                for _ in range(num_rows):
                    row_data = []
                    for _ in range(num_cols):
                        val_len = struct.unpack('!I', data[ptr:ptr+4])[0]
                        ptr += 4
                        val_str = data[ptr:ptr+val_len].decode('utf-8')
                        ptr += val_len

                        # Auto-convert numbers
                        if val_str.isdigit() or (val_str.startswith("-") and val_str[1:].isdigit()):
                            row_data.append(int(val_str))
                        else:
                            # Try float
                            try:
                                row_data.append(float(val_str))
                            except ValueError:
                                row_data.append(val_str)

                    result_rows.append(row_data)

                return result_rows

        except Exception as e:
            if isinstance(e, QueryError): raise e
            raise OperationalError(f"Binary parse failed: {e}")

    def close(self): pass
    def __enter__(self): return self
    def __exit__(self, exc_type, exc_val, exc_tb): self.close()

# ==========================================
# 4. CONNECTION CLASS
# ==========================================
class FrancoDB:
    def __init__(self, host='localhost', port=2501, timeout=10):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self._connected = False

    def connect(self, username='', password='', database=''):
        try:
            self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
            self._connected = True
            with self.cursor() as cur:
                if username and password:
                    res = cur.execute(f"LOGIN {username} {password};")
                    if "OK" not in str(res) and "SUCCESS" not in str(res):
                        raise AuthError(f"Login failed: {res}")
                if database:
                    cur.execute(f"2ESTA5DEM {database};")
        except socket.error as e:
            self._connected = False
            raise OperationalError(f"Connection error: {e}")

    def cursor(self): return Cursor(self)
    def is_connected(self): return self._connected and self.sock is not None
    def close(self):
        if self.sock:
            try: self.sock.close()
            except: pass
        self.sock = None
        self._connected = False
    def __enter__(self): return self
    def __exit__(self, exc_type, exc_val, exc_tb): self.close()

def connect(host='localhost', port=2501, user='', password='', database=''):
    conn = FrancoDB(host, port)
    conn.connect(user, password, database)
    return conn