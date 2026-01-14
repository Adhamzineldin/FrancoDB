// Example: Using FrancoDB Client in Node.js
const net = require('net');

class FrancoDBClient {
    constructor(host = 'localhost', port = 2501) {
        this.host = host;
        this.port = port;
        this.socket = null;
        this.connected = false;
    }
    
    connect(username = '', password = '', database = '') {
        return new Promise((resolve, reject) => {
            this.socket = new net.Socket();
            
            this.socket.connect(this.port, this.host, () => {
                this.connected = true;
                
                // Login if credentials provided
                if (username && password) {
                    const loginCmd = `LOGIN ${username} ${password};\n`;
                    this.query(loginCmd).then(response => {
                        if (!response.includes('LOGIN OK')) {
                            this.disconnect();
                            reject(new Error('Login failed'));
                            return;
                        }
                        
                        // Use database if provided
                        if (database) {
                            this.query(`USE ${database};\n`).then(() => {
                                resolve(true);
                            });
                        } else {
                            resolve(true);
                        }
                    });
                } else {
                    resolve(true);
                }
            });
            
            this.socket.on('error', (err) => {
                reject(err);
            });
        });
    }
    
    query(sql) {
        return new Promise((resolve, reject) => {
            if (!this.connected) {
                reject(new Error('Not connected'));
                return;
            }
            
            let response = '';
            this.socket.once('data', (data) => {
                response = data.toString();
                resolve(response);
            });
            
            this.socket.write(sql);
        });
    }
    
    disconnect() {
        if (this.socket) {
            this.socket.end();
            this.connected = false;
        }
    }
}

// Example usage
async function main() {
    const client = new FrancoDBClient('localhost', 2501);
    
    try {
        await client.connect('maayn', 'root', 'mydb');
        console.log('Connected to FrancoDB!');
        
        // Create table
        let result = await client.query('2e3mel gadwal users (id rakam asasi, name gomla, age rakam);\\n');
        console.log('Create table:', result);
        
        // Insert data
        result = await client.query("emla gowa users elkeyam (1, 'Alice', 25);\\n");
        console.log('Insert:', result);
        
        // Query data
        result = await client.query('2e5tar * men users lama age > 20;\\n');
        console.log('Query result:', result);
        
        client.disconnect();
    } catch (error) {
        console.error('Error:', error);
    }
}

main();
