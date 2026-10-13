#include "tank_controller.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    uint32_t time_ms;
    bool new_reading;
    int level_percent;
    bool sensor_valid;
    bool reset;
} DemoStep;

static void print_events(TankEvents events)
{
    static const struct {
        TankEvents flag;
        const char *message;
    } messages[] = {
        {TANK_EVENT_PUMP_ON, "pump ON command"},
        {TANK_EVENT_PUMP_OFF, "pump OFF command"},
        {TANK_EVENT_SENSOR_FAULT, "sensor reading invalid or stale"},
        {TANK_EVENT_SENSOR_OK, "valid sensor reading restored"},
        {TANK_EVENT_RUN_TIMEOUT, "maximum run time reached; reset required"},
        {TANK_EVENT_RUN_RESET, "run-time fault reset; pump remains OFF"}
    };

    for (size_t i = 0; i < sizeof messages / sizeof messages[0]; ++i) {
        if ((events & messages[i].flag) != 0U) {
            printf("  Notification: %s\n", messages[i].message);
        }
    }
}

int main(void)
{
    TankController controller;
    TankConfig config = tank_controller_default_config();
    config.max_run_ms = UINT32_C(3000);       /* Short limits for this demo. */
    config.sample_timeout_ms = UINT32_C(1500);
    if (!tank_controller_init(&controller, &config)) {
        fputs("Invalid controller configuration.\n", stderr);
        return EXIT_FAILURE;
    }

    static const DemoStep steps[] = {
        {   0U, true,   50, true, false},
        { 100U, true,   20, true, false},
        { 200U, true,   20, true, false},
        { 300U, true,   50, true, false},
        { 400U, true,   80, true, false},
        { 500U, true,   20, true, false},
        { 600U, true,  101, true, false},
        { 700U, true,  101, true, false},
        { 800U, true,   50, true, false},
        { 900U, true,   20, true, false},
        {1900U, true,   20, true, false},
        {2900U, true,   20, true, false},
        {3900U, true,   20, true, false},
        {4000U, true,   20, true, false},
        {4100U, true,   20, true, true },
        {4200U, true,   20, true, false},
        {4300U, false,   0, false, false},
        {5700U, false,   0, false, false},
        {5800U, true,   80, true, false}
    };

    puts("Host simulation: time is supplied by the scenario, not by sleeping.");
    puts("Demo limits: maximum run = 3000 ms, sample timeout = 1500 ms.\n");

    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; ++i) {
        const DemoStep *step = &steps[i];
        const TankReading reading = {step->level_percent, step->sensor_valid};
        printf("t=%" PRIu32 " ms | ", step->time_ms);
        if (step->new_reading) {
            printf("level=%d%% | sensor_valid=%u", reading.level_percent,
                   (unsigned)reading.valid);
        } else {
            printf("no new sensor sample");
        }
        printf(" | reset=%u\n", (unsigned)step->reset);

        const TankEvents events = tank_controller_update(
            &controller, step->new_reading ? &reading : NULL,
            step->time_ms, step->reset);
        print_events(events);
        printf("  pump=%s | sensor_fault=%u | run_time_fault=%u\n\n",
               controller.pump_on ? "ON" : "OFF",
               (unsigned)((controller.faults & TANK_FAULT_SENSOR) != 0U),
               (unsigned)((controller.faults & TANK_FAULT_RUN_TIMEOUT) != 0U));
    }

    return EXIT_SUCCESS;
}
