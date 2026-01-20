# FrancoDB

A high-performance, multi-protocol database management system written in C++.

## Features

- 🚀 **High Performance** - Built with modern C++20
- 🔐 **Authentication & RBAC** - Role-based access control with SUPERADMIN, ADMIN, USER, READONLY, and DENIED roles
- 🌐 **Multi-Protocol Support** - TEXT, JSON, and BINARY protocols
- 💾 **Persistent Storage** - Automatic data persistence with crash recovery
- 🔄 **Auto-Save** - Periodic auto-save every 30 seconds
- 🐳 **Docker Ready** - Easy deployment with Docker
- 📚 **Multi-Database** - Support for multiple databases
- 🔍 **FQL (Franco Query Language)** - SQL-like syntax with Arabic-style keywords

## Quick Start

### Docker (Recommended)

```bash
# Start server
docker-compose up -d

# View logs
docker-compose logs -f francodb
```

### Manual Installation

**Linux/macOS:**
```bash
chmod +x install.sh
./install.sh
./build/francodb_server
```

**Windows:**
```powershell
.\install.ps1
.\build\francodb_server.exe
```

See [INSTALL.md](INSTALL.md) for detailed installation instructions.

## Usage

### Server

Start the server:
```bash
./francodb_server
```

The server listens on port `2501` by default.

### Client Shell

Use the interactive shell:
```bash
./francodb_shell
```

Connection string format:
```
maayn://username:password@host:port/database
```

Example:
```
maayn://maayn:root@localhost:2501/mydb
```

### Using in Your Code

See [SDK_README.md](docs/documentation/SDK_README.md) for client library usage.

**C++ Example:**
```cpp
#include "network/franco_client.h"
using namespace francodb;

FrancoClient client;
client.ConnectFromString("maayn://maayn:root@localhost:2501/mydb");
std::string result = client.Query("SELECT * FROM users;\n");
```

**Python Example:**
```python
from examples.python_example import FrancoDBClient

client = FrancoDBClient('localhost', 2501)
client.connect('maayn', 'root', 'mydb')
result = client.query("SELECT * FROM users;\n")
```

**JavaScript Example:**
```javascript
const FrancoDBClient = require('./examples/javascript_example');

const client = new FrancoDBClient('localhost', 2501);
await client.connect('maayn', 'root', 'mydb');
const result = await client.query('SELECT * FROM users;\n');
```

## Default Credentials

- **Username:** `maayn`
- **Password:** `root`
- **Port:** `2501`

## FQL (Franco Query Language)

FrancoDB supports both standard SQL and Arabic-style keywords:

| SQL | FQL (Arabic) | Description |
|-----|--------------|-------------|
| `SELECT` | `2esta5dem` | Query data |
| `INSERT` | `emla` | Insert data |
| `UPDATE` | `3adel` | Update data |
| `DELETE` | `7afez` | Delete data |
| `CREATE TABLE` | `2e3mel gadwal` | Create table |
| `SHOW TABLES` | `wareny gadwal` | List tables |
| `SHOW USERS` | `wareny user` | List users |
| `IN` | `FE` | IN operator |

## Examples

See the `examples/` directory for complete examples:
- `cpp_example.cpp` - C++ client usage
- `python_example.py` - Python client usage
- `javascript_example.js` - Node.js client usage

See `docs/test_commands_comprehensive.txt` for more query examples.

## Documentation

- [INSTALL.md](INSTALL.md) - Installation guide
- [SDK_README.md](docs/documentation/SDK_README.md) - Client SDK documentation
- [README_DOCKER.md](docs/documentation/README_DOCKER.md) - Docker deployment guide
- [docs/](docs/) - Additional documentation

## Architecture

- **Storage Layer** - Disk manager, buffer pool, page management
- **Catalog** - Metadata management (tables, indexes)
- **Parser** - SQL/FQL query parsing
- **Execution Engine** - Query execution with executors
- **Network Layer** - Multi-protocol server (TEXT, JSON, BINARY)
- **Authentication** - User management and RBAC

## Building

```bash
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

## Testing

```bash
cd build
ctest
```

## License

[Your License Here]

## Contributing

[Contributing guidelines]

## Support

For issues and questions, please open an issue on GitHub.
