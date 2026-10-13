# Controller design

## Separation of responsibilities

The application acquires readings, supplies time and reset requests, drives
the output, and dispatches events. The controller owns the control state.
It has no dependency on `stdio`, an operating system, STM32 HAL, or a network.

`TankController` is caller-owned so it can be statically allocated. Its fields
are visible for inspection, but applications must change them through the API.
Each instance has separate state. Initialize successfully before using it.
This is a single-owner API: an interrupt handler and the main loop must not
update the same object concurrently.

## Configuration contract

- `0 <= start_percent < stop_percent <= 100`.
- Both timeouts must be in `1..INT32_MAX` milliseconds.
- All times use the same unsigned 32-bit millisecond clock.
- Initialization copies the configuration and starts the command OFF.
- Invalid configuration returns `false` and leaves a non-null object OFF with
  `TANK_FAULT_CONFIG`. Initialization does not generate transition events.

Treat initialization as a startup operation. Reinitializing clears runtime
history and latches; use the reset input for normal run-time-fault recovery.
Latches are held in RAM and do not survive a power cycle.

## Update order

1. Capture the previous pump command and fault bits; detect the reset edge.
2. Check the elapsed time of the previous continuous ON interval.
3. Accept a fresh valid reading, or record an invalid/stale sensor condition.
4. Accept an eligible reset of an already-existing run-time fault.
5. Apply fault overrides, then the level thresholds and hysteresis.
6. Compare old and new state to produce a bitmask of transition events.

At the exact run-time limit, timeout takes priority even if a new reading is
80% or higher. That preserves evidence that the continuous command reached
its configured limit. The same-call reset cannot erase that event.

## Freshness and time

A non-null reading means a newly acquired sample available on this update.
`valid=false` reports a sensor-driver error even when its numeric value looks
plausible. Invalid samples never replace the last accepted level or refresh
its timestamp. The sensor fault remains set until a fresh valid sample arrives.

A null reading means there is no new sample. If a valid sample was previously
accepted, it may be used until its age reaches `sample_timeout_ms`. With no
initial reading, the first update reports a sensor fault and stays OFF.
A fresh valid sample received exactly at the old sample's expiry refreshes it.

Elapsed time is computed by unsigned subtraction. This supports wrapping of
the 32-bit counter. The caller must continue invoking the controller well
within both configured timeouts; never allow a full 32-bit clock cycle to pass
unobserved. Do not use an adjustable wall clock or feed timestamps backwards.

Timeouts are evaluated only when an update runs. If the loop runs every 10 ms,
its timeout detection can be up to approximately one loop period late, plus
scheduling and output latency. A stalled loop cannot stop an output. Incoming
samples must also have bounded acquisition/transport delay: the API timestamps
acceptance and cannot discover that an upstream queue held an old sample.

## Reset and restart

The accepted reset is an explicit acknowledgement, not an indefinite enable.
It requires a fresh valid reading and a new `false -> true` request after the
run-time fault already exists. A request held high through a fault does not
clear it; release and request again.

An accepted reset keeps the pump command OFF on that update. A later fresh
low-level reading is needed to start a new filling cycle. Cached input alone
cannot immediately restart the pump. Sensor faults recover automatically, as
in the original project; this can restart the command on a valid low reading.

## Event handling

Events are flags: test them with `events & TANK_EVENT_...`, since a single
update can contain both a pump-OFF event and a fault event. Events represent
changes in commanded state and diagnostics, not acknowledgement from hardware.
Read the returned value every update; the core does not queue it for later.

An application may enqueue these events for UART, MQTT, or a phone-notification
service. Any such queue, retry policy, authentication, and network adapter are
outside this version. The local control path must keep running without them.

## STM32 adapter work remaining

1. Establish the inactive output level during startup and handle init failure.
2. Convert calibrated sensor measurements to integer percent; report acquisition
   errors through `TankReading.valid` rather than clamping every error into range.
3. Call the update regularly with one consistent millisecond counter and a
   debounced reset request. Coordinate the reset edge with a fresh sensor sample.
4. Write the requested output after every update, including fault updates.
5. Dispatch events without delaying the loop; validate timing and electrical
   output behavior in simulation, then on hardware when available.

The output driver must account for relay polarity and startup behavior.
Physical overflow protection, motor protection, and watchdog behavior are
separate integration responsibilities. No such hardware validation is claimed
by the host test suite.
