# Water Tank Controller

A C project that simulates an automatic controller for a pump that fills a water tank. It uses two water-level thresholds: the pump command turns ON at 30% or below and OFF at 80% or above. Between those thresholds, it keeps its previous state.

The controller also handles out-of-range readings and prints notifications when the pump command changes or a new sensor fault is detected.

**Current stage:** A program running on a computer with simulated readings and terminal notifications. Microcontroller integration, physical pump control, and phone notifications are planned extensions.

## Control Rules

Water level is represented as an integer percentage. The pump starts OFF, and the sensor-fault flag starts clear.

| Water-level reading | Pump command | Sensor-fault flag |
| --- | --- | --- |
| 0% to 30%, inclusive | ON | Clear |
| Above 30% and below 80% | Keep previous state | Clear |
| 80% to 100%, inclusive | OFF | Clear |
| Below 0% or above 100% | OFF | Set |

A valid reading clears the fault flag. If the first valid reading after a fault is between 30% and 80%, the pump stays OFF. A reading of 30% or below is needed to turn it ON again.

## Hysteresis

Using different ON and OFF thresholds is called **hysteresis**. It prevents the pump command from repeatedly switching as the level fluctuates around a single threshold.

For example:

| Reading sequence | Pump command |
| --- | --- |
| 50% | OFF |
| 20% | ON |
| 50% | ON |
| 80% | OFF |
| 50% | OFF |

At 50%, either command can be correct: it depends on the previous state.

## Event Notifications

At the start of each loop iteration, the program saves the current pump and fault states. After processing the next reading, it compares those saved values with the updated states.

Notifications are printed for these changes:

- Pump command OFF to ON: `Notification: pump ON command sent`
- Pump command ON to OFF: `Notification: pump OFF command sent`
- Fault flag clear to set: `Notification: sensor fault detected`

Each notification appears once per state transition.

Multiple notifications can appear during one iteration. For example, an invalid reading can turn an active pump command OFF and set the fault flag.

Fault recovery currently clears the flag without a separate recovery notification.

These messages are simulated events printed in the terminal. They do not confirm that a physical pump has started or stopped, and they are not yet sent to a phone.

## Build and Run

### Requirements

- GCC with C11 support, available on `PATH`.
- PowerShell on Windows for the commands below.

### Commands

From the repository root, compile:

```powershell
gcc -std=c11 -Wall -Wextra -Wpedantic .\src\main.c -o .\water-tank-controller.exe
```

Run:

```powershell
.\water-tank-controller.exe
```

The program processes the readings in the `levels` array and then exits.

It prints:

- Water-level reading.
- Control messages.
- Event notifications.
- Pump state.
- Sensor-fault state.

Boolean states are displayed as `0` for false and `1` for true.

## Manual Notification Check

The sequence `20, 20, 101, 101, 80` was checked using the program's terminal output:

| Step | Reading | Pump command | Fault flag | Notifications |
| --- | --- | --- | --- | --- |
| 1 | 20 | ON | 0 | Pump ON |
| 2 | 20 | ON | 0 | None |
| 3 | 101 | OFF | 1 | Pump OFF and sensor fault |
| 4 | 101 | OFF | 1 | None |
| 5 | 80 | OFF | 0 | None |

This checks that:

- Repeated readings do not repeat notifications.
- Two events can be reported during the same iteration.
- A valid high-level reading clears the fault.

Regular status messages still print on every iteration.

To try another sequence, edit `levels` in `src/main.c`. Keep the array size and loop limit consistent with the number of readings.

These are manual checks; the project does not yet include an automated test suite.

## Project Files

| File | Purpose |
| --- | --- |
| `src/main.c` | Simulated input readings, control logic, and event notifications |
| `.gitignore` | Ignores generated executables, object files, and build directories |
| `README.md` | Project behavior and instructions |

## Current Limitations

- Readings come from an array rather than a physical sensor.
- The fault check detects only values outside 0% to 100%. A sensor stuck at a plausible value, such as 20%, would not be detected.
- `pump_on` represents the requested pump state; there is no feedback confirming physical operation.
- Pump run-time limits, source-water monitoring, and independent overflow protection are not implemented.

## Planned Development

- Separate the control logic into a reusable module and add automated tests.
- Integrate sensor input and pump-command output on an STM32, with Wokwi simulation.
- Design the supporting schematic and PCB in KiCad.
- Add phone notifications through suitable networking hardware and a notification service.

Local pump control should continue working when the network is unavailable.