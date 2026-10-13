#include "tank_controller.h"

#include <stddef.h>

TankConfig tank_controller_default_config(void)
{
    const TankConfig config = {30, 80, UINT32_C(120000), UINT32_C(1000)};
    return config;
}

static bool config_is_valid(const TankConfig *config)
{
    return config != NULL &&
           config->start_percent >= 0 &&
           config->start_percent < config->stop_percent &&
           config->stop_percent <= 100 &&
           config->max_run_ms > 0U &&
           config->max_run_ms <= (uint32_t)INT32_MAX &&
           config->sample_timeout_ms > 0U &&
           config->sample_timeout_ms <= (uint32_t)INT32_MAX;
}

bool tank_controller_init(TankController *controller, const TankConfig *config)
{
    if (controller == NULL) {
        return false;
    }

    /* Copy before clearing: config may refer to controller->config. */
    const bool valid = config_is_valid(config);
    const TankConfig copy = valid ? *config : (TankConfig){0};
    *controller = (TankController){0};
    if (!valid) {
        controller->faults = TANK_FAULT_CONFIG;
        return false;
    }

    controller->config = copy;
    controller->initialized = true;
    return true;
}

static TankEvents transition_events(bool was_on, uint32_t old_faults,
                                    const TankController *controller)
{
    TankEvents events = TANK_EVENT_NONE;
    if (was_on != controller->pump_on) {
        events |= controller->pump_on ? TANK_EVENT_PUMP_ON : TANK_EVENT_PUMP_OFF;
    }

    const uint32_t changed = old_faults ^ controller->faults;
    if ((changed & TANK_FAULT_SENSOR) != 0U) {
        events |= (controller->faults & TANK_FAULT_SENSOR) != 0U
                      ? TANK_EVENT_SENSOR_FAULT : TANK_EVENT_SENSOR_OK;
    }
    if ((changed & TANK_FAULT_RUN_TIMEOUT) != 0U) {
        events |= (controller->faults & TANK_FAULT_RUN_TIMEOUT) != 0U
                      ? TANK_EVENT_RUN_TIMEOUT : TANK_EVENT_RUN_RESET;
    }
    return events;
}

TankEvents tank_controller_update(TankController *controller,
                                  const TankReading *reading,
                                  uint32_t now_ms,
                                  bool reset_requested)
{
    if (controller == NULL || !controller->initialized) {
        return TANK_EVENT_NONE;
    }

    const bool was_on = controller->pump_on;
    const uint32_t old_faults = controller->faults;
    const bool reset_edge = reset_requested && !controller->previous_reset;
    controller->previous_reset = reset_requested;

    /* Check the previous ON interval before a new reading can stop the pump.
     * The exact run-time limit wins over a full-tank reading at the same tick.
     * Unsigned subtraction also works when the millisecond counter wraps.
     */
    const bool timed_out = was_on &&
        (uint32_t)(now_ms - controller->pump_started_ms) >=
            controller->config.max_run_ms;
    if (timed_out) {
        controller->faults |= TANK_FAULT_RUN_TIMEOUT;
    }

    bool fresh_valid = false;
    if (reading != NULL) {
        fresh_valid = reading->valid && reading->level_percent >= 0 &&
                      reading->level_percent <= 100;
        if (fresh_valid) {
            controller->level_percent = reading->level_percent;
            controller->last_sample_ms = now_ms;
            controller->has_reading = true;
            controller->faults &= ~TANK_FAULT_SENSOR;
        } else {
            controller->faults |= TANK_FAULT_SENSOR;
        }
    }

    if (!controller->has_reading ||
        (uint32_t)(now_ms - controller->last_sample_ms) >=
            controller->config.sample_timeout_ms) {
        controller->faults |= TANK_FAULT_SENSOR;
    }

    /* A reset cannot dismiss a fault on the same call that raises it. */
    const bool reset_applied = reset_edge && fresh_valid && !timed_out &&
                              (old_faults & TANK_FAULT_RUN_TIMEOUT) != 0U;
    if (reset_applied) {
        controller->faults &= ~TANK_FAULT_RUN_TIMEOUT;
    }

    if (controller->faults != TANK_FAULT_NONE || reset_applied) {
        controller->pump_on = false;
    } else if (controller->level_percent <= controller->config.start_percent) {
        if (!controller->pump_on && fresh_valid) {
            controller->pump_on = true;
            controller->pump_started_ms = now_ms;
        }
    } else if (controller->level_percent >= controller->config.stop_percent) {
        controller->pump_on = false;
    }
    /* Middle range: retain the previous pump command. */

    return transition_events(was_on, old_faults, controller);
}
