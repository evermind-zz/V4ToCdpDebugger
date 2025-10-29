# V4ToCdpDebugger – CDP (Chrome DevTools Protocol) Bridge for Qt V4 JavaScript Engine

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
![Qt Version](https://img.shields.io/badge/Qt-6.8.3-blue)
![C++17](https://img.shields.io/badge/C++-17-orange)

---

## What This Project Does

**V4ToCdpDebugger** enables debugging of JavaScript code running inside Qt’s **V4 JavaScript engine**
(used by `QJSEngine`, `QtQml`, etc.) using the **Chrome DevTools Protocol (CDP)**. Since every modern
browser exposes CDP, this project brings that powerful debugging interface to Qt’s internal JavaScript engine.

Currently only a subset of CDP features is implemented, but with community contributions, full compatibility can be achieved.

Goal:
- Connect **Chrome DevTools** or any CDP-compatible client.
- Set **breakpoints**.
- **Inspect variables**, call stack, and source code.
- **Evaluate expressions** in context.
- Receive **real-time events** (paused, resumed, script loaded).

All communication happens via **WebSocket + HTTP** on `localhost:9222/json/list`.

> **Built on top of [`NeoScriptTools/V4ScriptDebugger`](https://github.com/DavidXanatos/NeoScriptTools/tree/master/V4ScriptDebugger)**
> This project extends the V4 debugging backend from **David Xanatos** to expose it through CDP.

---

## Quick Start (TL;DR)

```bash
# Build
cmake -B build -S . -DCMAKE_PREFIX_PATH=/path/to/qt
cmake --build build

# Run demo CDP server
./build/V4EngineExtDemo/jsrunner --input examples/test.js

# Connect Chrome DevTools
chrome://inspect → Add http://localhost:9222/json/list
```

---

## Project Status

> **Work in Progress – Early Stage, Contributions Welcome**

This is an ongoing experimental project.

**Currently verified features (fully tested via `CdpTestClient/test-cases.ini`):**

| Feature                                     | Status  |
| ------------------------------------------- | ------- |
| Fetch script source from engine             | Working |
| `Debugger.setBreakpointByUrl` (by filename) | Working |
| `Debugger.removeBreakpoint`                 | Working |
| `Debugger.getScriptSource`                  | Working |
| `Debugger.scriptParsed` event               | Working |
| `Debugger.paused` on breakpoint hit         | Working |
| `Runtime.executionContextCreated`           | Working |

Further work is needed on:

- Step-over / step-in / step-out
- Variable inspection
- Eval in context
- Source maps
- General CDP feature coverage

---

## Architecture Overview

```
Chrome DevTools
       ↓ (WebSocket)
CdpDebuggerFrontend ←→ V4ScriptDebuggerBackend ←→ QV4 Engine
       ↑ (HTTP /json)           ↑
   V4EngineExtDemo          V4 Engine (NeoScriptTools)
```

- **Frontend:** Translates CDP requests/responses ↔ internal debugger API
- **Backend:** Hooks into V4 execution (breakpoints, pause/resume, eval)
- **Demo Application:** Runs JS and exposes a CDP server endpoint
- **Test Client:** Validates full end-to-end behavior

---

## Project Structure

```
V4ToCdpDebugger/
├── common/               Header-only debug utilities
├── V4ScriptDebugger/     Backend: Connects to QV4 internals (from NeoScriptTools)
├── V4ToCdpFrontend/      Frontend: HTTP + WebSocket CDP server
├── V4EngineExtDemo/      Demo app: Runs JS + exposes CDP (uses backend + frontend)
├── CdpTestClient/        Test client with automated test cases (connects to CDP server)
└── CMakeLists.txt        Root build configuration
```

### V4ScriptDebugger (Backend)

This library communicates with the V4 Engine and exposes Qt signals/slots for frontend interaction.
Some internal parts were modified for better integration with the CDP bridge.
See commit history for detailed changes.

### V4ToCdpFrontend (CDP Frontend)

Implements an HTTP + WebSocket server using Qt’s `QtHttpServer` and `QtWebSockets`.
It translates messages between the CDP client (e.g., Chrome DevTools) and the V4 backend.

### V4EngineExtDemo (Demo Application)

Simple application demonstrating a working CDP server.
Listens on port `9222`.

```bash
$ jsrunner --help
Usage: V4EngineExtDemo/jsrunner [options]
Minimal QJSEngine Script Runner

Options:
  -h, --help           Displays help on commandline options.
  --help-all           Displays help, including generic Qt options.
  -i, --input <file>   Path to a JavaScript file.
  -c, --count <n>      Number of times to call the function.
  -t, --interval <ms>  Interval in milliseconds between calls.
```

### CdpTestClient

A test client for validating the full debugging chain.

Data flow:

```
CDP Commands <-> WebSocket <-> Frontend <-> Backend <-> V4EngineExt
```

It communicates with the demo app (`V4EngineExtDemo/jsrunner`) but can be used with any CDP server.

```bash
$ ./CdpTestClient/cdp_test_client --help
Usage: ./CdpTestClient/cdp_test_client [options] url
CDP Test Client

Options:
  -h, --help                        Displays help.
  --help-all                        Displays all Qt options.
  -t, --test-cases <file>           Path to test cases file.
  -d, --delay <ms>                  Delay before starting tests.
  -e, --external-command <command>  External command to start CDP server.
  -l, --logfile <file>              Logfile for external command (default: log.txt).

Arguments:
  url                               CDP HTTP endpoint, e.g. http://localhost:9222
```

---

## Usage

### Running the Demo CDP Server (`jsrunner`)

```bash
./V4EngineExtDemo/jsrunner --input ../CdpTestClient/test-cases.js --interval 500 --count 500
```

**Options:**

```
$ jsrunner --help
Usage: V4EngineExtDemo/jsrunner [options]
Minimal QJSEngine Script Runner

Options:
  -h, --help           Displays help on commandline options.
  --help-all           Displays help, including generic Qt options.
  -i, --input <file>   Path to a JavaScript file.
  -c, --count <n>      How many times to call the function.
  -t, --interval <ms>  Interval in milliseconds between calls.
```

Listens on: `http://localhost:9222/json/list`

### Connecting with Chrome DevTools

1. Open Chrome
2. Navigate to `chrome://inspect`
3. Add `http://localhost:9222/json/list` (or select `jsrunner` if detected)
4. Click **Inspect**

---

### Running Automated Tests (`cdp_test_client`)

The test client can automatically launch `jsrunner`, execute test cases, and validate responses.

```bash
cd build
./CdpTestClient/cdp_test_client \
  --delay 500 \
  --external-command "V4EngineExtDemo/jsrunner --input ../CdpTestClient/test-cases.js --interval 500 --count 500" \
  --test-cases ../CdpTestClient/test-cases.ini \
  http://localhost:9222 \
  -l external-prog-logfile.log
```

Example output:

```
Starting external command: "V4EngineExtDemo/jsrunner --input ../CdpTestClient/test-cases.js --interval 500 --count 500"
Delay test for: 500 ms
Connecting to HTTP endpoint: QUrl("http://localhost:9222/json/list")
Switching to WebSocket: "ws://localhost:9222/devtools/page/jsrunner-js"
WebSocket connected. Loading test cases...

[TEST 0] PASS: onConnect receive Runtime.executionContextCreated
[TEST 1] PASS: onConnect receive Debugger.scriptParsed
[TEST 2] PASS: Debugger.setBreakpointByUrl
[TEST 3] PASS: Event_Hit_Breakpoint
[TEST 4] PASS: Debugger.removeBreakpoint
[TEST 5] PASS: Debugger.getScriptSource

All tests completed successfully.
```

---

### Test Cases (`test-cases.ini`)

Tests are written in an INI-like format:

- `[name]` – test name
- `request` – optional JSON request (one line)
- `response` – expected JSON response from the server

Example:

```ini
[Debugger.setBreakpointByUrl]
request={"id":2,"method":"Debugger.setBreakpointByUrl","params":{"lineNumber":3,"url":"jsrunner://test-cases.js"}}
response={"id":2,"result":{"breakpointId":"1"}}

[Event_Hit_Breakpoint]
response={"method":"Debugger.paused","params":{"callFrames":[],"hitBreakpoints":["1"],"reason":"other"}}
```

---

## Build Instructions

### Prerequisites

- **Qt 6.8.3+** (`QtQml`, `QtWebSockets`, `QtHttpServer` with private headers)
  *Note: A stripped-down Qt 6.8.3 library version is used internally (link TBD).*
- **C++17** compiler (GCC, Clang)
- **CMake 3.21+**
- Optional: `ninja`, `ccache`

### Optional Build Flags

```bash
-DBUILD_DEMO=OFF          # Skip demo app  (V4EngineExtDemo/jsrunner)
-DBUILD_TEST_CLIENT=OFF   # Skip test client (CdpTestClient)
-DBUILD_LIBS=OFF          # Skip libraries. Only useful if you just want `CdpTestClient`
```

### Enable Verbose Debug Logs

```bash
cmake .. -DENABLE_DEBUG_LOGGING=ON
```

Logs are written to `debug_output.log` (see `common/debug.h`).

### Building All Components

```bash
mkdir build && cd build
cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/path/to/qt/6.8.3/gcc_64 \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \ # optional use ccache
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \   # optional use ccache
  -G Ninja \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

ninja
```

---

## Todos

- Implement additional CDP commands for complete compatibility
- Properly shut down `Debugger_Worker` to free memory
- Properly shut down `CdpDebuggerFrontend` to free memory

---

## Known Issues

- Stepping (step-over / step-in / step-out) not yet implemented
- Variable inspection only partially functional
- Expression evaluation context handling incomplete
- Source map support missing
- Single client connection supported at a time
- No authentication or access control implemented
- Some CDP commands return stubs or placeholders

---

## Contributing

1. Fork the repository
2. Create a feature branch
3. Implement your feature or fix
4. Add tests in `CdpTestClient/test-cases.ini`
5. Submit a Pull Request

---

## License

- **Code by David Xanatos (`NeoScriptTools/V4ScriptDebugger`)**: GPL
- **This project (`V4ToCdpDebugger`)**: LGPL/GPL

---

## Acknowledgments

Built on top of:

- [Qt 6](https://www.qt.io/)
- [QV4 JavaScript Engine](https://code.qt.io/cgit/qt/qtdeclarative.git/tree/src/qml/jsruntime)
- [Chrome DevTools Protocol](https://chromedevtools.github.io/devtools-protocol/)
- Special thanks to [David Xanatos](https://github.com/DavidXanatos) for `V4ScriptDebugger`
