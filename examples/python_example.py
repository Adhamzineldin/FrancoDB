# Example: Using FrancoDB Client in Python
import socket
import json

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
                    self.disconnect()
                    return False
            
            # Use database if provided
            if database:
                use_cmd = f"USE {database};\n"
                self.query(use_cmd)
            
            return True
        except Exception as e:
            print(f"Connection error: {e}")
            return False
    
    def query(self, sql):
        """Execute SQL query"""
        if not self.connected:
            return "ERROR: Not connected"
        
        try:
            self.sock.sendall(sql.encode())
            response = self.sock.recv(4096).decode()
            return response
        except Exception as e:
            return f"ERROR: {e}"
    
    def disconnect(self):
        """Disconnect from server"""
        if self.sock:
            self.sock.close()
            self.connected = False

# Example usage
if __name__ == "__main__":
    client = FrancoDBClient('localhost', 2501)
    
    if client.connect('maayn', 'root', 'mydb'):
        print("Connected to FrancoDB!")
        
        # Create table
        result = client.query("2e3mel gadwal users (id rakam asasi, name gomla, age rakam);\n")
        print(f"Create table: {result}")
        
        # Insert data
        result = client.query("emla gowa users elkeyam (1, 'Alice', 25);\n")
        print(f"Insert: {result}")
        
        # Query data
        result = client.query("2e5tar * men users lama age > 20;\n")
        print(f"Query result:\n{result}")
        
        client.disconnect()
    else:
        print("Failed to connect")
