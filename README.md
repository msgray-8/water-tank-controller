# Water Tank Controller

A portable C11 controller for a pump that fills a water tank. It starts the
pump command at **30% or below**, stops it at **80% or above**, and retains its
previous command between those thresholds. Invalid or stale sensor data stops
the command. A maximum continuous run time provides an additional stop condition.

**Version 0.2: host-tested control module and simulation.** This repository
includes a runnable demo, 23 automated test groups, CMake builds, and a GitHub
Actions workflow. STM32 sensor/GPIO integration, Wokwi wiring, and phone
notifications are subsequent integration work. `pump_on` is a requested output;
there is no physical pump feedback in this version.

## What changed from the original program

- Control logic lives in a reusable module, separate from input and printing.
- Thresholds and timeouts are validated configuration values.
- Sensor-driver failures and missing fresh readings are handled explicitly.
- A run-time-limit fault remains latched until an explicit reset is accepted.
- Notifications cover command changes, fault detection, and recovery.
- Injected timestamps make timing and clock-rollover behavior testable instantly.
- No dynamic allocation, blocking delays, or hardware dependencies in the core.

## Control behavior

The pump command starts OFF. With a valid reading and no active fault:

| Water level | Pump command |
| --- | --- |
| 0% through 30% | ON |
| Above 30% and below 80% | Retain previous command |
| 80% through 100% | OFF |

The sequence `50, 20, 50, 80, 50` therefore produces
`OFF, ON, ON, OFF, OFF`. These are switching thresholds; mapping sensor values
to the tank's physical empty/full levels is the sensor driver's responsibility.

Faults override the level rules:

| Fault | Detection | Recovery |
| --- | --- | --- |
| Sensor | Reading outside 0–100%, driver marks it invalid, no first reading, or last valid reading becomes stale | A fresh valid reading clears it automatically |
| Run-time limit | Pump command continuously ON for at least `max_run_ms` | A new reset request together with a fresh valid reading |
| Configuration | Invalid thresholds or zero/unsupported timeout | Correct configuration and successful initialization |

A sensor recovery at 50% leaves the command OFF; recovery at 20% can start it
again, provided no run-time fault remains. Fault bits are independent.

Reset is recognized on the request's `false` to `true` transition. A held reset
cannot automatically clear a later fault. A reset cannot clear a run-time fault
on the same update that detects it. An accepted reset leaves the command OFF;
a later fresh reading at or below the start threshold can restart it.

## Build, test, and run in PowerShell

Requirements: GCC with C11 support, CMake 3.20 or newer, and Ninja, all available
on `PATH`. Run from the repository root:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
.\build\tank_demo.exe
```

The test executable reports **23/23 test groups passed**. CTest reports **2 tests**:
the grouped controller tests and a demo smoke test. To see the individual groups:

```powershell
.\build\test_tank_controller.exe
```

You can also build without CMake or Ninja, using GCC alone:

```powershell
gcc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Werror -Iinclude src/main.c src/tank_controller.c -o tank_demo.exe
.\tank_demo.exe
gcc -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Werror -Iinclude tests/test_tank_controller.c src/tank_controller.c -o test_tank_controller.exe
.\test_tank_controller.exe
```

Use these terminal commands so both source files are compiled. The old
single-file Code Runner command does not link the controller module.

On Linux, the same CMake commands work; run `./build/tank_demo` and
`./build/test_tank_controller` without the `.exe` suffix.

## Demo and configuration

Edit the `steps` array in `src/main.c` to change the simulated input sequence.
Each step supplies a timestamp, optional fresh reading, sensor-valid flag, and
reset request. No physical time passes between steps.

The demo covers hysteresis, repeated readings, out-of-range faults, recovery,
run-time expiry, reset, and stale data. Several event bits can be returned by
one update. Events repeat only after a new transition; ordinary status lines
are printed for every step.

| Setting | Module default | Demo override |
| --- | --- | --- |
| Start threshold | 30% | 30% |
| Stop threshold | 80% | 80% |
| Maximum continuous run | 120,000 ms | 3,000 ms |
| Valid sample timeout | 1,000 ms | 1,500 ms |

These timeouts are example settings, not calibrated limits for a real tank.
Change the configuration **before initialization**. The controller takes a copy.

## Project files

| File | Purpose |
| --- | --- |
| `include/tank_controller.h` | Public types, configuration, events, API contract |
| `src/tank_controller.c` | Hysteresis, input validation, timeouts, reset, events |
| `src/main.c` | Runnable computer simulation and terminal notifications |
| `tests/test_tank_controller.c` | Behavioral regression and boundary tests |
| `CMakeLists.txt` | Library, demo, tests, strict compiler warnings |
| `.github/workflows/ci.yml` | Debug, Release, and sanitizer checks on GitHub |
| `docs/design.md` | Timing, integration, and failure-handling decisions |
| `docs/validation.md` | Verification results and remaining limits |

## Integration scope

Call `tank_controller_update()` periodically, including between sensor readings.
Pass `NULL` when there is no new reading. Repeatedly passing cached data as new
would defeat stale-data detection. Apply `controller.pump_on` to the output
after every call; process returned notification events separately.

The core can be compiled into an STM32 project, but this repository does not
yet contain its sensor driver, pin configuration, reset-button handling, or
relay interface. Terminal events are not mobile notifications. Networking must
not block the local control loop.

This version does not detect a welded relay, confirm pump operation, measure
source-water availability, or identify every sensor stuck within its valid
range. The run-time limit bounds a continuous ON command; it does not establish
the cause of a fault. A blocked processor cannot enforce software timeouts.
Hardware integration and physical protection require their own design and
validation.
