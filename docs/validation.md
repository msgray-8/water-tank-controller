# Validation record

Host verification performed on 2026-10-13 using GCC 13.3.0 and CMake 4.4.4 on
Linux. The repository includes Windows PowerShell build instructions, but no
Windows or STM32 execution was performed for this record.

## Results

| Check | Result |
| --- | --- |
| Debug build with strict warnings treated as errors | Passed |
| Release build with strict warnings treated as errors | Passed |
| Controller behavioral test executable | 23/23 groups passed in Debug and Release |
| CTest | 2/2 passed in each configuration: controller suite plus demo smoke test |
| AddressSanitizer and UndefinedBehaviorSanitizer | 2/2 CTest entries passed with leak detection disabled |
| Demo output review | Expected command changes, fault recovery, reset, and stale-data shutdown observed |

LeakSanitizer could not run under this container's tracing environment; its
first run exited with the explicit "does not work under ptrace" diagnostic.
The sanitizer run was repeated with `ASAN_OPTIONS=detect_leaks=0`. Memory-access
and undefined-behavior checks remained enabled, with sanitizer recovery
disabled. The controller itself uses no dynamic allocation. Leak checking is
not claimed. The GitHub workflow does not disable leak detection.

## Behavioral coverage

The 23 groups exercise:

- Startup defaults and the original hysteresis and notification sequences.
- Both prior pump states at 0, 30, 31, 79, 80, and 100 percent.
- Negative, above-range, extreme integer, and driver-invalid inputs.
- Missing startup data, invalid-data persistence, recovery, and stale-data boundaries.
- Fresh data arriving at the old reading's expiry.
- Continuous run timing, duplicate readings, new filling cycles, and latched faults.
- Reset freshness, request edges, held requests, and same-call fault precedence.
- An accepted reset holding OFF until another fresh low reading arrives.
- Concurrent sensor/run-time faults and independent recovery.
- Run-time expiry coinciding with a full-tank reading.
- Millisecond-counter rollover for run-time and sensor timeouts.
- Custom configuration, invalid configuration, and permitted limits.
- Null/zero-initialized handling, configuration aliasing on reinitialization,
  and independent controller instances.

The runner uses explicit checks instead of `assert()`, so Release builds do
not silently disable validation. CTest's demo entry is a smoke test; its exit
code is checked, not every output line. Controller tests assert state and event
results directly. Test-group count is not a code-coverage percentage.

## Reproduce on Linux

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
ctest --test-dir build-release --output-on-failure

cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

For environments with the specific LeakSanitizer tracing limitation above,
the last command can be run as:

```sh
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-sanitize --output-on-failure
```

GitHub Actions is configured to build and test Debug, Release, and sanitizer
versions after publication. No remote Actions run is claimed in this record.

## Limits of this evidence

These checks validate the portable software rules under simulated input and
time. They do not measure sensor accuracy, physical switching, timing on an
STM32, electrical protection, network delivery, or real-tank performance.
Those require separate integration and acceptance tests.
