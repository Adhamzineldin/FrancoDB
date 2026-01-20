# FrancoDB Test Suite - CMake/CTest Guide

## 🎯 Professional Test Execution (No Scripts!)

All tests are configured through CMake and run via CTest. No shell scripts needed.

---

## 📦 Build Tests

```bash
# From project root
mkdir -p build && cd build
cmake ..
cmake --build . --target run_all_tests
```

---

## 🚀 Run Tests

### Method 1: CTest (Recommended)
```bash
cd build

# Run comprehensive test suite
ctest -R ComprehensiveTestSuite --output-on-failure

# Verbose output
ctest -R ComprehensiveTestSuite -V

# Run all tests
ctest --output-on-failure

# Parallel execution
ctest -j8 --output-on-failure
```

### Method 2: Direct Execution
```bash
cd build

# Linux/macOS
./run_all_tests

# Windows
.\run_all_tests.exe
```

### Method 3: CMake Targets
```bash
cd build

# Quick test (comprehensive suite only)
cmake --build . --target test_quick

# All tests
cmake --build . --target test_all

# Verbose
cmake --build . --target test_verbose
```

---

## 📊 CTest Commands Reference

| Command | Description |
|---------|-------------|
| `ctest` | Run all tests |
| `ctest -R Pattern` | Run tests matching pattern |
| `ctest -L Label` | Run tests with specific label |
| `ctest -V` | Verbose output |
| `ctest --output-on-failure` | Show output only on failure |
| `ctest -j8` | Run 8 tests in parallel |
| `ctest --rerun-failed` | Rerun only failed tests |

---

## 🏷️ Test Labels

Filter tests by label:

```bash
# S+ features only
ctest -L s_plus

# Comprehensive suite
ctest -L comprehensive

# All labeled tests
ctest -L all
```

---

## 🔧 Development Workflow

### Before Committing
```bash
cd build
cmake --build . --target run_all_tests
ctest -R ComprehensiveTestSuite --output-on-failure
```

### Before Pushing
```bash
cd build
ctest --output-on-failure  # Run ALL tests
```

### Quick Check
```bash
cd build
cmake --build . --target test_quick
```

---

## 🐛 Debugging Failed Tests

### Get Detailed Output
```bash
# Verbose mode
ctest -R ComprehensiveTestSuite -V

# Run test directly
./run_all_tests
```

### Rerun Failed Tests
```bash
ctest --rerun-failed --output-on-failure
```

---

## 📈 CI/CD Integration

### GitHub Actions
```yaml
name: Tests
on: [push, pull_request]

jobs:
  test:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-latest, windows-latest, macos-latest]
    
    steps:
      - uses: actions/checkout@v2
      
      - name: Configure
        run: |
          mkdir build && cd build
          cmake ..
      
      - name: Build
        run: cmake --build build --target run_all_tests
      
      - name: Test
        run: |
          cd build
          ctest -R ComprehensiveTestSuite --output-on-failure
```

### GitLab CI
```yaml
test:
  script:
    - mkdir build && cd build
    - cmake ..
    - cmake --build . --target run_all_tests
    - ctest -R ComprehensiveTestSuite --output-on-failure
```

---

## 💡 Pro Tips

### 1. Watch Mode (Development)
```bash
# Linux/macOS
watch -n 1 'cd build && ctest -R ComprehensiveTestSuite'
```

### 2. Test Timing
```bash
# Show test execution time
ctest --verbose
```

### 3. Filter by Name
```bash
# Run only JOIN tests
ctest -R ".*join.*" -V
```

### 4. Generate Test Report
```bash
ctest --output-junit test-results.xml
```

---

## 📦 CMake Configuration

Tests are configured in `test/CMakeLists.txt`:

```cmake
# Build comprehensive test suite
add_executable(run_all_tests ${TEST_SUITE_SOURCES})
target_link_libraries(run_all_tests PRIVATE francodb_lib)

# Register with CTest
add_test(NAME ComprehensiveTestSuite COMMAND run_all_tests)

# Custom targets
add_custom_target(test_quick
    COMMAND ${CMAKE_CTEST_COMMAND} -R ComprehensiveTestSuite --output-on-failure
)
```

---

## ✅ Quick Reference

| Task | Command |
|------|---------|
| **Build tests** | `cmake --build . --target run_all_tests` |
| **Run tests** | `ctest -R ComprehensiveTestSuite --output-on-failure` |
| **Quick test** | `cmake --build . --target test_quick` |
| **All tests** | `ctest --output-on-failure` |
| **Verbose** | `ctest -V` |
| **Parallel** | `ctest -j8` |
| **Failed only** | `ctest --rerun-failed` |
| **Direct run** | `./run_all_tests` |

---

## 🎯 Clean and Professional

✅ No shell scripts  
✅ Standard CMake/CTest workflow  
✅ Cross-platform compatible  
✅ CI/CD ready  
✅ Industry standard  

**Just use CMake commands!**

