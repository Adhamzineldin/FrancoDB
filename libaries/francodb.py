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
        self.last_raw_bytes = b''  # <--- NEW: Stores raw hex data

    def _recv_captured(self, n):
        """Helper: Reads n bytes from socket AND saves them to buffer."""
        data = self._conn.sock.recv(n)
        self.last_raw_bytes += data
        return data

    def execute(self, fql: str, mode: str = 'text') -> Union[str, List, Dict]:
        """
        Execute FQL. 
        Modes: 'text', 'json', 'binary'
        """
        if not self._conn.is_connected():
            raise OperationalError("Database is not connected")

        # Clear previous capture
        self.last_raw_bytes = b''

        # 1. Select Protocol
        msg_type = CMD_TEXT
        if mode == 'json': msg_type = CMD_JSON
        elif mode == 'binary': msg_type = CMD_BINARY

        # 2. Pack & Send
        payload_bytes = fql.encode('utf-8')
        header = HEADER_STRUCT.pack(msg_type, len(payload_bytes))

        try:
            self._conn.sock.sendall(header + payload_bytes)

            # 3. Read Response based on Mode
            if mode == 'binary':
                return self._read_binary_response()
            else:
                return self._read_text_response()

        except socket.error as e:
            raise OperationalError(f"Network error: {e}")

    def _read_text_response(self) -> str:
        """Standard text/json reader."""
        try:
            # We use _recv_captured even for text so we can debug it if needed
            response = self._recv_captured(65536).decode('utf-8').strip()
            if "ERROR" in response and not response.startswith("{"):
                raise QueryError(response)
            return response
        except socket.timeout:
            raise OperationalError("Query timed out")

    def _read_binary_response(self):
        """
        Unpacks the Professional Binary Protocol from C++.
        Now uses _recv_captured to store the raw hex trace.
        """
        try:
            # 1. Read Response Type (1 Byte)
            type_byte = self._recv_captured(1)
            if not type_byte: return None
            resp_type = type_byte[0]

            # --- CASE: ERROR (0xFF) ---
            if resp_type == 0xFF:
                msg_len = struct.unpack('!I', self._recv_captured(4))[0]
                error_msg = self._recv_captured(msg_len).decode('utf-8')
                raise QueryError(f"Server Error: {error_msg}")

            # --- CASE: SIMPLE MESSAGE (0x01) ---
            elif resp_type == 0x01:
                msg_len = struct.unpack('!I', self._recv_captured(4))[0]
                return self._recv_captured(msg_len).decode('utf-8')

            # --- CASE: TABLE DATA (0x02) ---
            elif resp_type == 0x02:
                # A. Read Metadata
                num_cols = struct.unpack('!I', self._recv_captured(4))[0]
                num_rows = struct.unpack('!I', self._recv_captured(4))[0]

                # B. Read Columns
                columns = []
                for _ in range(num_cols):
                    col_type = self._recv_captured(1)[0] # 1 byte type
                    name_len = struct.unpack('!I', self._recv_captured(4))[0]
                    col_name = self._recv_captured(name_len).decode('utf-8')
                    columns.append(col_name)

                # C. Read Rows
                result_rows = []
                for _ in range(num_rows):
                    row_data = []
                    for _ in range(num_cols):
                        # C++ sends everything as String (Type 2) currently
                        val_len = struct.unpack('!I', self._recv_captured(4))[0]
                        val_str = self._recv_captured(val_len).decode('utf-8')

                        # Auto-convert numbers
                        if val_str.isdigit():
                            row_data.append(int(val_str))
                        else:
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
                    res = cur.execute(f"LOGIN {username} {password};\n")
                    if "OK" not in str(res) and "SUCCESS" not in str(res):
                        raise AuthError(f"Login failed: {res}")
                if database:
                    cur.execute(f"2ESTA5DEM {database};\n")
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