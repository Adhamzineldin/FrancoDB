# Example: Using FrancoDB Client in Python (Packet Protocol)
import socket
import struct

# Protocol Constants
CMD_TEXT   = b'Q'
CMD_JSON   = b'J'
CMD_BINARY = b'B'

class FrancoDBClient:
    def __init__(self, host='localhost', port=2501):
        self.host = host
        self.port = port
        self.sock = None
        self.connected = False
    
    def connect(self, username='', password='', database=''):
        """Connect to FrancoDB server"""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((self.host, self.port))
            self.connected = True
            
            # Login if credentials provided
            if username and password:
                login_cmd = f"LOGIN {username} {password};\n"
                response = self.query(login_cmd)
                if "LOGIN OK" not in response:
                    print(f"Login Failed: {response}")
                    self.disconnect()
                    return False
            
            # Use database if provided
            if database:
                self.query(f"USE {database};\n")
            
            return True
        except Exception as e:
            print(f"Connection error: {e}")
            return False
    
    def query(self, fql, mode='text'):
        """
        Execute fql query.
        mode can be: 'text', 'json', 'binary'
        """
        if not self.connected:
            return "ERROR: Not connected"
        
        try:
            # 1. Select Protocol Type
            msg_type = CMD_TEXT
            if mode == 'json': msg_type = CMD_JSON
            elif mode == 'binary': msg_type = CMD_BINARY
            
            # 2. Encode Payload
            payload = fql.encode('utf-8')
            length = len(payload)
            
            # 3. Pack Header: [Type (1 byte)] [Length (4 bytes, Big Endian)]
            # ! = Network Endian, c = char, I = int
            header = struct.pack('!cI', msg_type, length)
            
            # 4. Send Header + Payload
            self.sock.sendall(header + payload)
            
            # 5. Receive Response (Basic implementation)
            response = self.sock.recv(65536).decode()
            return response.strip()
            
        except Exception as e:
            return f"ERROR: {e}"
    
    def disconnect(self):
        if self.sock:
            self.sock.close()
            self.connected = False

# Example usage
if __name__ == "__main__":
    client = FrancoDBClient('localhost', 2501)
    
    if client.connect('maayn', 'root', 'mydb'):
        print("Connected to FrancoDB!")
        
        # Text Mode
        print("--- Text Mode ---")
        client.query("2e3mel gadwal users (id rakam asasi, name gomla);")
        client.query("emla gowa users elkeyam (1, 'Alice');")
        print(client.query("2e5tar * men users;"))
        
        # JSON Mode (New Protocol Feature)
        print("\n--- JSON Mode ---")
        json_res = client.query("2e5tar * men users;", mode='json')
        print(f"JSON: {json_res}")
        
        client.disconnect()
    else:
        print("Failed to connect")