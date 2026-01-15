import socket
import struct
from typing import Optional, Tuple, Any, List

# ==========================================
# 1. CUSTOM EXCEPTIONS
# ==========================================
class FrancoDBError(Exception):
    """Base exception for all FrancoDB errors."""
    pass

class OperationalError(FrancoDBError):
    """Connection issues or server not found."""
    pass

class AuthError(FrancoDBError):
    """Authentication failures."""
    pass

class QueryError(FrancoDBError):
    """Syntax errors or execution errors from the DB."""
    pass

# ==========================================
# 2. PROTOCOL CONSTANTS
# ==========================================
CMD_TEXT   = b'Q'
CMD_JSON   = b'J'
CMD_BINARY = b'B'
HEADER_STRUCT = struct.Struct('!cI')  # Type (char), Length (uint32)

# ==========================================
# 3. CURSOR CLASS
# ==========================================
class Cursor:
    """
    Handles query execution and result fetching.
    """
    def __init__(self, connection):
        self._conn = connection
        self._last_result = None

    def execute(self, fql: str, mode: str = 'text') -> str:
        """
        Execute a FrancoDB Query Language (FQL) statement.
        """
        if not self._conn.is_connected():
            raise OperationalError("Database is not connected")

        # 1. Determine Protocol Type
        msg_type = CMD_TEXT
        if mode == 'json': msg_type = CMD_JSON
        elif mode == 'binary': msg_type = CMD_BINARY

        # 2. Encode Payload
        payload_bytes = fql.encode('utf-8')
        length = len(payload_bytes)

        # 3. Pack Header and Send
        # Header: [Type: 1 byte] [Length: 4 bytes]
        header = HEADER_STRUCT.pack(msg_type, length)

        try:
            self._conn.sock.sendall(header + payload_bytes)
            self._last_result = self._read_response()
            return self._last_result
        except socket.error as e:
            raise OperationalError(f"Network error during query: {e}")

    def _read_response(self) -> str:
        """
        Reads the exact number of bytes specified by the server.
        """
        # Read the header first (assuming server sends back similar header)
        # Note: If your server sends raw text back without a header, 
        # you might need to adjust this. 
        # reliable libraries usually expect a framed response (Length + Data).

        # CURRENT IMPLEMENTATION ASSUMPTION: 
        # For this stage of your project, we assume the server sends raw bytes back
        # and closes/flushes. To make this professional, your C++ server 
        # SHOULD send a length header back.

        # Since the C++ server in previous steps just sent raw strings:
        # We will use a basic read for now, but wrapper in a robust way.
        try:
            # In a real protocol, you read 4 bytes for length first.
            # self._recv_n_bytes(4) -> length -> self._recv_n_bytes(length)

            # Fallback for simple server:
            response = self._conn.sock.recv(65536)
            decoded = response.decode('utf-8').strip()

            if "ERROR" in decoded:
                raise QueryError(decoded)

            return decoded
        except socket.timeout:
            raise OperationalError("Query timed out")

    def close(self):
        pass

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

# ==========================================
# 4. CONNECTION CLASS
# ==========================================
class FrancoDB:
    """
    Main connection class representing a session with the database.
    """
    def __init__(self, host: str = 'localhost', port: int = 2501, timeout: int = 10):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock: Optional[socket.socket] = None
        self._connected = False

    def connect(self, username: str = '', password: str = '', database: str = ''):
        try:
            self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
            self._connected = True

            # Internal cursor for handshake
            with self.cursor() as cur:
                # 1. Login
                if username and password:
                    res = cur.execute(f"LOGIN {username} {password};\n")
                    if "OK" not in res and "SUCCESS" not in res:
                        raise AuthError(f"Login failed: {res}")

                # 2. Select DB
                if database:
                    cur.execute(f"2ESTA5DEM {database};\n")

        except socket.error as e:
            self._connected = False
            raise OperationalError(f"Could not connect to {self.host}:{self.port} - {e}")

    def cursor(self) -> Cursor:
        """Factory method to create a cursor."""
        return Cursor(self)

    def is_connected(self) -> bool:
        return self._connected and self.sock is not None

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except socket.error:
                pass
        self.sock = None
        self._connected = False

    # Context Manager Support
    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

# ==========================================
# 5. HELPER FACTORY
# ==========================================
def connect(host='localhost', port=2501, user='', password='', database=''):
    """
    Helper function to create a connection in one line.
    """
    conn = FrancoDB(host, port)
    conn.connect(user, password, database)
    return conn