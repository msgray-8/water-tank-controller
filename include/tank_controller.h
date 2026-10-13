#ifndef TANK_CONTROLLER_H
#define TANK_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

/* Every time value uses the same unsigned, millisecond clock. */
typedef struct {
    int start_percent;
    int stop_percent;
    uint32_t max_run_ms;
    uint32_t sample_timeout_ms;
} TankConfig;

typedef struct {
    int level_percent;
    bool valid; /* False when the sensor driver reports a failed reading. */
} TankReading;

#define TANK_FAULT_NONE          UINT32_C(0)
#define TANK_FAULT_SENSOR        (UINT32_C(1) << 0)
#define TANK_FAULT_RUN_TIMEOUT   (UINT32_C(1) << 1)
#define TANK_FAULT_CONFIG        (UINT32_C(1) << 2)

typedef uint32_t TankEvents;
#define TANK_EVENT_NONE          UINT32_C(0)
#define TANK_EVENT_PUMP_ON       (UINT32_C(1) << 0)
#define TANK_EVENT_PUMP_OFF      (UINT32_C(1) << 1)
#define TANK_EVENT_SENSOR_FAULT  (UINT32_C(1) << 2)
#define TANK_EVENT_SENSOR_OK     (UINT32_C(1) << 3)
#define TANK_EVENT_RUN_TIMEOUT   (UINT32_C(1) << 4)
#define TANK_EVENT_RUN_RESET     (UINT32_C(1) << 5)

/* Caller owns this object. Read its fields; modify it only through this API. */
typedef struct {
    TankConfig config;
    bool initialized;
    bool pump_on; /* Requested output, not confirmation of physical operation. */
    bool has_reading;
    bool previous_reset;
    int level_percent; /* Last accepted reading; meaningful if has_reading. */
    uint32_t last_sample_ms;
    uint32_t pump_started_ms;
    uint32_t faults;
} TankController;

/* Defaults: 30%, 80%, 120 s maximum run, 1 s without a valid fresh sample. */
TankConfig tank_controller_default_config(void);

/* Copies config; initializes the pump OFF. False means do not enable output.
 * Invalid config leaves a non-NULL controller OFF with CONFIG fault set.
 */
bool tank_controller_init(TankController *controller, const TankConfig *config);

/* Call periodically, even between sensor readings. Pass NULL for reading if
 * no NEW reading is available. Never resubmit cached data as a fresh sample.
 * now_ms must be a monotonic uint32_t millisecond counter (wrap is supported).
 * Call well within both configured timeouts; a blocked loop cannot stop an
 * output. Never allow an entire 32-bit clock cycle to pass between calls.
 * A reset is its false->true edge. RUN_TIMEOUT clears only on a reset edge
 * accompanied by a fresh valid reading; that call leaves the pump OFF.
 * A later fresh reading <= start_percent can restart it.
 * Returns transition events once per change. No I/O, allocation, or delays.
 * A NULL or zero-initialized controller returns no events; init success is
 * required. Do not pass an object containing uninitialized memory.
 * Single-owner API: do not call concurrently from an ISR and the main loop.
 */
TankEvents tank_controller_update(TankController *controller,
                                  const TankReading *reading,
                                  uint32_t now_ms,
                                  bool reset_requested);

#endif
