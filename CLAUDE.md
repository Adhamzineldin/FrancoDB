# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ChronosDB is a high-performance, multi-protocol database management system written in C++20. It features role-based access control (RBAC), persistent storage with crash recovery, and CQL (Chronos Query Language) - SQL with Arabic-style keyword alternatives.

## Build Commands

```bash
# Build (from repository root)
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

# Run tests
ctest -R ComprehensiveTestSuite --output-on-failure
# or
cmake --build . --target test_quick

# Run server (default port 2501)
./build/chronosdb_server

# Run interactive shell
./build/chronosdb_shell
```

## Architecture

### Core Components

- **Storage Layer** (`src/storage/`, `src/buffer/`): Disk manager, buffer pool (multiple strategies including adaptive partitioned), B+ tree indexes, page management
- **Catalog** (`src/catalog/`): Schema and metadata management for tables, columns, indexes
- **Parser** (`src/parser/`): Lexer and parser for SQL/CQL statements, supports Arabic keyword alternatives
- **Execution Engine** (`src/execution/`): Factory pattern with specialized executors (DDL, DML, database, transaction, user, system)
- **Network** (`src/network/`): Multi-protocol server (TEXT, JSON, BINARY) with connection handling and database registry
- **Recovery** (`src/recovery/`): Write-ahead logging, checkpointing, crash recovery, snapshots
- **Auth** (`src/common/auth_manager.cpp`): RBAC with 5 roles: SUPERADMIN, ADMIN, USER, READONLY, DENIED

### Entry Points

- Server: `src/cmd/server/main.cpp`
- Shell: `src/cmd/shell/shell.cpp`
- Setup: `src/cmd/setup/setup.cpp`
- Windows Service: `src/cmd/service/service_wrapper.cpp`

### Execution Flow

1. Client sends query via network (port 2501)
2. ConnectionHandler receives request
3. Lexer tokenizes, Parser creates Statement AST
4. ExecutionEngine dispatches via factory to specialized executor
5. Executor performs I/O via BufferPoolManager & Catalog
6. Result formatted and returned

### Design Patterns

- **Factory Pattern**: ExecutorFactory in `execution/executor_registry.cpp`
- **Dispatch Map**: Statement routing without switch statements
- **Interface Abstraction**: IBufferManager, ITableStorage in `src/include/storage/storage_interface.h`
- **Single Responsibility**: Separate executors for DDL, DML, transactions, etc.

## Key Configuration

Located in `src/include/common/config.h`:
- PAGE_SIZE: 4KB
- BUFFER_POOL_SIZE: 65536 pages (256MB default)
- Default port: 2501
- Default credentials: username=`chronos`, password=`root`

## Connection String Format

```
chronos://username:password@host:port/database
```

Example: `chronos://chronos:root@localhost:2501/mydb`

## Test Structure

Tests are in `test/` organized by module (buffer, concurrency, execution, network, parser, recovery, storage, system). Single comprehensive executable built from all test files, run via ctest.

## Platform Notes

- Windows: Links ws2_32, uses MinGW/Ninja, auto-copies runtime DLLs
- Linux: Links pthread, .deb package build available in `installers/linux/`


## Git Commit Guidelines

**IMPORTANT**: When creating git commits:
- **NEVER** add "Co-Authored-By: Claude" or any AI attribution to commit messages
- Keep commit messages professional and concise
- Focus on what changed and why, not who wrote it
- All code contributions should be attributed to the repository owner only
