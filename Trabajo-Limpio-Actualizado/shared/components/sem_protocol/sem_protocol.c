#include "sem_protocol.h"

#include <stdlib.h>

sem_control_command_t sem_protocol_default_command(void)
{
    return (sem_control_command_t) {
        .version = SEM_PROTOCOL_VERSION,
        .throttle = 0,
        .steering = 0,
        .enable = false,
        .sequence = 0,
    };
}

sem_vehicle_telemetry_t sem_protocol_default_telemetry(void)
{
    return (sem_vehicle_telemetry_t) 
    {
        .version = SEM_PROTOCOL_VERSION,
        .applied_throttle = 0,
        .applied_steering = 0,
        .distance_cm = 0,
        .state = SEM_VEHICLE_STATE_STOPPED,
        .last_command_sequence = 0,
    };
}

int16_t sem_protocol_clamp_axis(int32_t value)
{
    if (value > 1000) 
    {
        return 1000;
    }
    if (value < -1000) 
    {
        return -1000;
    }
    return (int16_t)value;
}

int16_t sem_protocol_limit_throttle_by_distance(int16_t requested_throttle, uint16_t distance_cm)
{
    if (distance_cm == 0 || distance_cm > 50) 
    {
        return requested_throttle;
    }

    if (distance_cm < 20) 
    {
        return 0;
    }

    int32_t magnitude = abs(requested_throttle);
    int32_t limited = (magnitude * (distance_cm - 20)) / 30;

    if (requested_throttle < 0)
    {
        limited = -limited;
    }

    return sem_protocol_clamp_axis(limited);
}

sem_vehicle_state_t sem_protocol_state_from_distance(uint16_t distance_cm, bool link_ok)
{
    if (!link_ok) 
    {
        return SEM_VEHICLE_STATE_LINK_LOST;
    }
    if (distance_cm > 0 && distance_cm < 20) 
    {
        return SEM_VEHICLE_STATE_OBSTACLE_STOP;
    }
    if (distance_cm >= 20 && distance_cm <= 50) 
    {
        return SEM_VEHICLE_STATE_OBSTACLE_SLOWDOWN;
    }
    return SEM_VEHICLE_STATE_RUNNING;
}
