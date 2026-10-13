#include "tank_controller.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* Unlike assert(), these checks remain active in Release builds. */
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

static bool setup(TankController *controller, uint32_t run_ms, uint32_t stale_ms)
{
    TankConfig config = tank_controller_default_config();
    config.max_run_ms = run_ms;
    config.sample_timeout_ms = stale_ms;
    return tank_controller_init(controller, &config);
}

static TankEvents sample(TankController *controller, int level, bool valid,
                         uint32_t time_ms, bool reset)
{
    const TankReading reading = {level, valid};
    return tank_controller_update(controller, &reading, time_ms, reset);
}

static bool test_startup_defaults(void)
{
    TankController controller;
    const TankConfig config = tank_controller_default_config();
    CHECK(config.start_percent == 30 && config.stop_percent == 80);
    CHECK(config.max_run_ms == 120000U && config.sample_timeout_ms == 1000U);
    CHECK(tank_controller_init(&controller, &config));
    CHECK(!controller.pump_on && !controller.has_reading);
    CHECK(controller.faults == TANK_FAULT_NONE);
    CHECK(sample(&controller, 50, true, 0U, false) == TANK_EVENT_NONE);
    CHECK(!controller.pump_on);
    return true;
}

static bool test_original_hysteresis(void)
{
    TankController controller;
    const int levels[] = {50, 20, 50, 80, 50};
    const bool expected_on[] = {false, true, true, false, false};
    const TankEvents expected_events[] = {
        TANK_EVENT_NONE, TANK_EVENT_PUMP_ON, TANK_EVENT_NONE,
        TANK_EVENT_PUMP_OFF, TANK_EVENT_NONE
    };
    CHECK(setup(&controller, 10000U, 1000U));
    for (size_t i = 0U; i < sizeof levels / sizeof levels[0]; ++i) {
        CHECK(sample(&controller, levels[i], true, (uint32_t)i, false) ==
              expected_events[i]);
        CHECK(controller.pump_on == expected_on[i]);
        CHECK(controller.faults == TANK_FAULT_NONE);
    }
    return true;
}

static bool test_original_notification_sequence(void)
{
    TankController controller;
    CHECK(setup(&controller, 10000U, 1000U));
    CHECK(sample(&controller, 20, true, 0U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 20, true, 1U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 101, true, 2U, false) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_SENSOR_FAULT));
    CHECK(sample(&controller, 101, true, 3U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 80, true, 4U, false) == TANK_EVENT_SENSOR_OK);
    CHECK(!controller.pump_on && controller.faults == TANK_FAULT_NONE);
    return true;
}

static bool test_threshold_boundaries(void)
{
    const int levels[] = {0, 30, 31, 79, 80, 100};
    const bool from_off[] = {true, true, false, false, false, false};
    const bool from_on[] = {true, true, true, true, false, false};
    for (size_t i = 0U; i < sizeof levels / sizeof levels[0]; ++i) {
        TankController controller;
        CHECK(setup(&controller, 10000U, 1000U));
        CHECK(sample(&controller, levels[i], true, 0U, false) ==
              (from_off[i] ? TANK_EVENT_PUMP_ON : TANK_EVENT_NONE));
        CHECK(controller.pump_on == from_off[i]);
        CHECK(setup(&controller, 10000U, 1000U));
        CHECK(sample(&controller, 0, true, 0U, false) == TANK_EVENT_PUMP_ON);
        CHECK(sample(&controller, levels[i], true, 1U, false) ==
              (from_on[i] ? TANK_EVENT_NONE : TANK_EVENT_PUMP_OFF));
        CHECK(controller.pump_on == from_on[i]);
        CHECK(controller.faults == TANK_FAULT_NONE);
    }
    return true;
}

static bool test_invalid_ranges(void)
{
    const int levels[] = {-1, 101, INT_MIN, INT_MAX};
    for (size_t i = 0U; i < sizeof levels / sizeof levels[0]; ++i) {
        TankController controller;
        CHECK(setup(&controller, 10000U, 1000U));
        CHECK(sample(&controller, 20, true, 0U, false) == TANK_EVENT_PUMP_ON);
        CHECK(sample(&controller, levels[i], true, 1U, false) ==
              (TANK_EVENT_PUMP_OFF | TANK_EVENT_SENSOR_FAULT));
        CHECK(!controller.pump_on && controller.faults == TANK_FAULT_SENSOR);
        CHECK(controller.level_percent == 20); /* Last valid reading retained. */
        CHECK(sample(&controller, levels[i], true, 2U, false) == TANK_EVENT_NONE);
        CHECK(sample(&controller, 50, true, 3U, false) == TANK_EVENT_SENSOR_OK);
        CHECK(!controller.pump_on && controller.faults == TANK_FAULT_NONE);
    }
    return true;
}

static bool test_driver_failure_and_recovery(void)
{
    TankController controller;
    CHECK(setup(&controller, 10000U, 1000U));
    CHECK(sample(&controller, 20, true, 0U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 50, false, 1U, false) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_SENSOR_FAULT));
    CHECK(tank_controller_update(&controller, NULL, 2U, false) == TANK_EVENT_NONE);
    CHECK(controller.faults == TANK_FAULT_SENSOR && !controller.pump_on);
    CHECK(sample(&controller, 20, true, 3U, false) ==
          (TANK_EVENT_SENSOR_OK | TANK_EVENT_PUMP_ON));
    CHECK(controller.faults == TANK_FAULT_NONE && controller.pump_on);
    return true;
}

static bool test_missing_startup_reading(void)
{
    TankController controller;
    CHECK(setup(&controller, 10000U, 1000U));
    CHECK(tank_controller_update(&controller, NULL, 0U, false) ==
          TANK_EVENT_SENSOR_FAULT);
    CHECK(!controller.pump_on && !controller.has_reading);
    CHECK(tank_controller_update(&controller, NULL, 10U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 50, true, 11U, false) == TANK_EVENT_SENSOR_OK);
    CHECK(controller.has_reading && !controller.pump_on);
    return true;
}

static bool test_stale_reading_boundary(void)
{
    TankController controller;
    CHECK(setup(&controller, 10000U, 500U));
    CHECK(sample(&controller, 20, true, 100U, false) == TANK_EVENT_PUMP_ON);
    CHECK(tank_controller_update(&controller, NULL, 599U, false) == TANK_EVENT_NONE);
    CHECK(controller.pump_on);
    CHECK(tank_controller_update(&controller, NULL, 600U, false) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_SENSOR_FAULT));
    CHECK(!controller.pump_on);
    CHECK(tank_controller_update(&controller, NULL, 601U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 50, true, 602U, false) == TANK_EVENT_SENSOR_OK);
    CHECK(!controller.pump_on);
    return true;
}

static bool test_fresh_reading_at_expiry(void)
{
    TankController controller;
    CHECK(setup(&controller, 10000U, 500U));
    CHECK(sample(&controller, 20, true, 0U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 50, true, 500U, false) == TANK_EVENT_NONE);
    CHECK(controller.pump_on && controller.faults == TANK_FAULT_NONE);
    CHECK(tank_controller_update(&controller, NULL, 999U, false) == TANK_EVENT_NONE);
    CHECK(tank_controller_update(&controller, NULL, 1000U, false) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_SENSOR_FAULT));
    return true;
}

static bool test_continuous_runtime(void)
{
    TankController controller;
    CHECK(setup(&controller, 1000U, 5000U));
    CHECK(sample(&controller, 20, true, 100U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 20, true, 500U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 50, true, 1099U, false) == TANK_EVENT_NONE);
    CHECK(controller.pump_on);
    CHECK(sample(&controller, 20, true, 1100U, false) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_RUN_TIMEOUT));
    CHECK(!controller.pump_on && controller.faults == TANK_FAULT_RUN_TIMEOUT);
    CHECK(sample(&controller, 20, true, 1101U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 50, true, 1102U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 80, true, 1103U, false) == TANK_EVENT_NONE);
    CHECK(!controller.pump_on && controller.faults == TANK_FAULT_RUN_TIMEOUT);
    return true;
}

static bool test_new_cycle_restarts_runtime(void)
{
    TankController controller;
    CHECK(setup(&controller, 1000U, 5000U));
    CHECK(sample(&controller, 20, true, 100U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 80, true, 500U, false) == TANK_EVENT_PUMP_OFF);
    CHECK(sample(&controller, 20, true, 700U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 50, true, 1100U, false) == TANK_EVENT_NONE);
    CHECK(controller.pump_on);
    CHECK(sample(&controller, 50, true, 1700U, false) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_RUN_TIMEOUT));
    return true;
}

static bool test_reset_requires_fresh_valid_reading(void)
{
    TankController controller;
    CHECK(setup(&controller, 1000U, 5000U));
    CHECK(sample(&controller, 20, true, 0U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 20, true, 1000U, false) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_RUN_TIMEOUT));
    CHECK(tank_controller_update(&controller, NULL, 1001U, true) == TANK_EVENT_NONE);
    CHECK(controller.faults == TANK_FAULT_RUN_TIMEOUT);
    CHECK(sample(&controller, 20, true, 1002U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 20, false, 1003U, true) == TANK_EVENT_SENSOR_FAULT);
    CHECK(controller.faults == (TANK_FAULT_SENSOR | TANK_FAULT_RUN_TIMEOUT));
    CHECK(sample(&controller, 20, true, 1004U, true) == TANK_EVENT_SENSOR_OK);
    CHECK(controller.faults == TANK_FAULT_RUN_TIMEOUT); /* No new reset edge. */
    CHECK(sample(&controller, 20, true, 1005U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 20, true, 1006U, true) == TANK_EVENT_RUN_RESET);
    CHECK(!controller.pump_on && controller.faults == TANK_FAULT_NONE);
    return true;
}

static bool test_held_reset_cannot_clear_later_fault(void)
{
    TankController controller;
    CHECK(setup(&controller, 1000U, 5000U));
    CHECK(sample(&controller, 20, true, 0U, true) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 20, true, 1000U, true) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_RUN_TIMEOUT));
    CHECK(sample(&controller, 20, true, 1001U, true) == TANK_EVENT_NONE);
    CHECK(controller.faults == TANK_FAULT_RUN_TIMEOUT && !controller.pump_on);
    CHECK(sample(&controller, 20, true, 1002U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 20, true, 1003U, true) == TANK_EVENT_RUN_RESET);
    CHECK(!controller.pump_on);
    return true;
}

static bool test_new_fault_wins_over_reset(void)
{
    TankController controller;
    CHECK(setup(&controller, 1000U, 5000U));
    CHECK(sample(&controller, 20, true, 0U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 20, true, 1000U, true) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_RUN_TIMEOUT));
    CHECK(controller.faults == TANK_FAULT_RUN_TIMEOUT);
    CHECK(sample(&controller, 20, true, 1001U, true) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 20, true, 1002U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 20, true, 1003U, true) == TANK_EVENT_RUN_RESET);
    return true;
}

static bool test_reset_holds_off_until_new_sample(void)
{
    TankController controller;
    CHECK(setup(&controller, 1000U, 5000U));
    CHECK(sample(&controller, 20, true, 0U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 20, true, 1000U, false) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_RUN_TIMEOUT));
    CHECK(sample(&controller, 20, true, 1001U, true) == TANK_EVENT_RUN_RESET);
    CHECK(!controller.pump_on);
    CHECK(tank_controller_update(&controller, NULL, 1002U, false) == TANK_EVENT_NONE);
    CHECK(tank_controller_update(&controller, NULL, 1003U, false) == TANK_EVENT_NONE);
    CHECK(!controller.pump_on);
    CHECK(sample(&controller, 20, true, 1004U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 20, true, 2003U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 20, true, 2004U, false) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_RUN_TIMEOUT));
    return true;
}

static bool test_independent_faults(void)
{
    TankController controller;
    CHECK(setup(&controller, 1000U, 5000U));
    CHECK(sample(&controller, 20, true, 0U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 101, true, 1000U, false) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_SENSOR_FAULT | TANK_EVENT_RUN_TIMEOUT));
    CHECK(controller.faults == (TANK_FAULT_SENSOR | TANK_FAULT_RUN_TIMEOUT));
    CHECK(sample(&controller, 20, true, 1001U, false) == TANK_EVENT_SENSOR_OK);
    CHECK(!controller.pump_on && controller.faults == TANK_FAULT_RUN_TIMEOUT);
    CHECK(sample(&controller, 20, true, 1002U, true) == TANK_EVENT_RUN_RESET);
    CHECK(!controller.pump_on && controller.faults == TANK_FAULT_NONE);
    return true;
}

static bool test_timeout_wins_over_full_tank(void)
{
    TankController controller;
    CHECK(setup(&controller, 1000U, 5000U));
    CHECK(sample(&controller, 20, true, 0U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 80, true, 1000U, false) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_RUN_TIMEOUT));
    CHECK(controller.faults == TANK_FAULT_RUN_TIMEOUT);
    return true;
}

static bool test_clock_rollover(void)
{
    TankController controller;
    const uint32_t start = UINT32_MAX - UINT32_C(5);
    CHECK(setup(&controller, 10U, 20U));
    CHECK(sample(&controller, 20, true, start, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 50, true, 3U, false) == TANK_EVENT_NONE);
    CHECK(controller.pump_on);
    CHECK(sample(&controller, 50, true, 4U, false) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_RUN_TIMEOUT));

    CHECK(setup(&controller, 100U, 10U));
    CHECK(sample(&controller, 20, true, start, false) == TANK_EVENT_PUMP_ON);
    CHECK(tank_controller_update(&controller, NULL, 3U, false) == TANK_EVENT_NONE);
    CHECK(controller.pump_on);
    CHECK(tank_controller_update(&controller, NULL, 4U, false) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_SENSOR_FAULT));
    return true;
}

static bool test_custom_configuration_is_copied(void)
{
    TankController controller;
    TankConfig config = {25, 75, 1000U, 500U};
    CHECK(tank_controller_init(&controller, &config));
    config.start_percent = 40;
    config.stop_percent = 90;
    CHECK(sample(&controller, 30, true, 0U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 25, true, 1U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 74, true, 2U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 75, true, 3U, false) == TANK_EVENT_PUMP_OFF);
    CHECK(controller.config.start_percent == 25 && controller.config.stop_percent == 75);
    return true;
}

static bool test_invalid_configuration(void)
{
    const TankConfig invalid[] = {
        {-1, 80, 1000U, 500U}, {30, 101, 1000U, 500U},
        {80, 80, 1000U, 500U}, {80, 30, 1000U, 500U},
        {30, 80, 0U, 500U}, {30, 80, 1000U, 0U},
        {30, 80, UINT32_MAX, 500U}, {30, 80, 1000U, UINT32_MAX}
    };
    TankController controller;
    for (size_t i = 0U; i < sizeof invalid / sizeof invalid[0]; ++i) {
        CHECK(setup(&controller, 1000U, 500U));
        CHECK(sample(&controller, 20, true, 0U, false) == TANK_EVENT_PUMP_ON);
        CHECK(!tank_controller_init(&controller, &invalid[i]));
        CHECK(!controller.initialized && !controller.pump_on);
        CHECK(controller.faults == TANK_FAULT_CONFIG);
        CHECK(sample(&controller, 20, true, 1U, false) == TANK_EVENT_NONE);
        CHECK(!controller.pump_on);
    }
    CHECK(!tank_controller_init(&controller, NULL));
    CHECK(!controller.pump_on && controller.faults == TANK_FAULT_CONFIG);
    return true;
}

static bool test_configuration_limits(void)
{
    TankController controller;
    const TankConfig config = {0, 100, (uint32_t)INT32_MAX, (uint32_t)INT32_MAX};
    CHECK(tank_controller_init(&controller, &config));
    CHECK(sample(&controller, 0, true, 0U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&controller, 99, true, 1U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 100, true, 2U, false) == TANK_EVENT_PUMP_OFF);
    return true;
}

static bool test_null_zero_and_reinitialization(void)
{
    TankController controller = {0};
    const TankConfig config = tank_controller_default_config();
    CHECK(!tank_controller_init(NULL, &config));
    CHECK(tank_controller_update(NULL, NULL, 0U, false) == TANK_EVENT_NONE);
    CHECK(sample(&controller, 20, true, 0U, false) == TANK_EVENT_NONE);
    CHECK(tank_controller_init(&controller, &config));
    CHECK(sample(&controller, 20, true, 0U, false) == TANK_EVENT_PUMP_ON);
    CHECK(tank_controller_init(&controller, &controller.config));
    CHECK(!controller.pump_on && !controller.has_reading);
    CHECK(controller.config.start_percent == 30 && controller.config.stop_percent == 80);
    return true;
}

static bool test_two_instances_are_independent(void)
{
    TankController first;
    TankController second;
    CHECK(setup(&first, 1000U, 5000U));
    CHECK(setup(&second, 2000U, 5000U));
    CHECK(sample(&first, 20, true, 0U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&second, 50, true, 0U, false) == TANK_EVENT_NONE);
    CHECK(sample(&second, 20, true, 100U, false) == TANK_EVENT_PUMP_ON);
    CHECK(sample(&first, 20, true, 1000U, false) ==
          (TANK_EVENT_PUMP_OFF | TANK_EVENT_RUN_TIMEOUT));
    CHECK(second.pump_on && second.faults == TANK_FAULT_NONE);
    CHECK(sample(&second, 50, true, 1000U, false) == TANK_EVENT_NONE);
    CHECK(second.pump_on && !first.pump_on);
    return true;
}

int main(void)
{
    const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"startup defaults", test_startup_defaults},
        {"original hysteresis", test_original_hysteresis},
        {"original notification sequence", test_original_notification_sequence},
        {"threshold boundaries from both pump states", test_threshold_boundaries},
        {"invalid ranges", test_invalid_ranges},
        {"driver failure and recovery", test_driver_failure_and_recovery},
        {"missing startup reading", test_missing_startup_reading},
        {"stale reading boundary", test_stale_reading_boundary},
        {"fresh reading at expiry", test_fresh_reading_at_expiry},
        {"continuous run time and latched fault", test_continuous_runtime},
        {"new cycle restarts run time", test_new_cycle_restarts_runtime},
        {"reset requires a fresh valid reading", test_reset_requires_fresh_valid_reading},
        {"held reset cannot clear later fault", test_held_reset_cannot_clear_later_fault},
        {"new fault wins over reset", test_new_fault_wins_over_reset},
        {"reset holds OFF until a new sample", test_reset_holds_off_until_new_sample},
        {"independent faults", test_independent_faults},
        {"timeout wins over full tank", test_timeout_wins_over_full_tank},
        {"clock rollover", test_clock_rollover},
        {"custom configuration is copied", test_custom_configuration_is_copied},
        {"invalid configuration", test_invalid_configuration},
        {"configuration limits", test_configuration_limits},
        {"NULL, zero initialization, reinitialization", test_null_zero_and_reinitialization},
        {"independent controller instances", test_two_instances_are_independent}
    };
    size_t passed = 0U;
    const size_t total = sizeof tests / sizeof tests[0];
    for (size_t i = 0U; i < total; ++i) {
        const bool ok = tests[i].run();
        printf("[%s] %s\n", ok ? "PASS" : "FAIL", tests[i].name);
        if (ok) {
            ++passed;
        }
    }
    printf("\n%zu/%zu test groups passed\n", passed, total);
    return passed == total ? EXIT_SUCCESS : EXIT_FAILURE;
}
