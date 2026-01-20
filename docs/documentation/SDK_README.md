# FrancoDB Client SDK

This document describes how to use FrancoDB in your applications.

## Supported Languages

- **C++** - Native client library
- **Python** - Socket-based client
- **JavaScript/Node.js** - Socket-based client
- **Any language** - Connect via TCP socket on port 2501

## Connection

### Connection String Format

```
maayn://username:password@host:port/database
```

### Default Values
- **Host:** localhost
- **Port:** 2501
- **Username:** maayn (default admin)
- **Password:** root
- **Database:** (optional)

## C++ Client API

```cpp
#include "network/franco_client.h"

using namespace francodb;

// Create client
FrancoClient client;

// Connect
client.ConnectFromString("maayn://maayn:root@localhost:2501/mydb");

// Execute query
std::string result = client.Query("SELECT * FROM users;\n");

// Disconnect
client.Disconnect();
```

## Python Client API

```python
from francodb_client import FrancoDBClient

# Create client
client = FrancoDBClient('localhost', 2501)

# Connect
client.connect('maayn', 'root', 'mydb')

# Execute query
result = client.query("SELECT * FROM users;\n")

# Disconnect
client.disconnect()
```

## JavaScript/Node.js Client API

```javascript
const FrancoDBClient = require('./francodb-client');

// Create client
const client = new FrancoDBClient('localhost', 2501);

// Connect
await client.connect('maayn', 'root', 'mydb');

// Execute query
const result = await client.query('SELECT * FROM users;\n');

// Disconnect
client.disconnect();
```

## Protocol

FrancoDB uses a simple text-based protocol over TCP:

1. **Connect** to server on port 2501
2. **Authenticate** with `LOGIN username password;`
3. **Select database** with `USE database;`
4. **Send SQL queries** terminated with `\n`
5. **Receive responses** as text

### Example Protocol Flow

```
Client -> Server: LOGIN maayn root;\n
Server -> Client: LOGIN OK\n

Client -> Server: USE mydb;\n
Server -> Client: Using database: mydb\n

Client -> Server: SELECT * FROM users;\n
Server -> Client: [query results]\n
```

## Error Handling

All clients return error messages prefixed with "ERROR:". Check responses for error conditions:

```cpp
std::string result = client.Query("SELECT * FROM nonexistent;\n");
if (result.find("ERROR:") != std::string::npos) {
    // Handle error
}
```

## Best Practices

1. **Connection Pooling** - Reuse connections when possible
2. **Error Handling** - Always check for "ERROR:" in responses
3. **Transaction Management** - Use `BEGIN;`, `COMMIT;`, `ROLLBACK;`
4. **Prepared Statements** - (Future feature)
5. **Connection Timeout** - Implement timeout handling

## Examples

See the `examples/` directory for complete working examples in each language.

## Building the C++ Client Library

```bash
# Build the library
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --target francodb_lib

# Link in your project
# Add to your CMakeLists.txt:
# target_link_libraries(your_app francodb_lib)
# target_include_directories(your_app PRIVATE ${FRANCODB_SOURCE_DIR}/src/include)
```

## License

[Your License Here]
