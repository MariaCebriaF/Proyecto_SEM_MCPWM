#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEM_PROTOCOL_VERSION 1

typedef enum 
{
    SEM_VEHICLE_STATE_STOPPED = 0,
    SEM_VEHICLE_STATE_RUNNING,
    SEM_VEHICLE_STATE_OBSTACLE_SLOWDOWN,
    SEM_VEHICLE_STATE_OBSTACLE_STOP,
    SEM_VEHICLE_STATE_LINK_LOST,
} sem_vehicle_state_t;

typedef struct 
{
    uint8_t version;
    int16_t throttle;  // -1000..1000
    int16_t steering;  // -1000..1000
    bool enable;
    uint32_t sequence;
} sem_control_command_t;

typedef struct 
{
    uint8_t version;
    int16_t applied_throttle;  // -1000..1000
    int16_t applied_steering;  // -1000..1000
    uint16_t distance_cm;
    sem_vehicle_state_t state;
    uint32_t last_command_sequence;
} sem_vehicle_telemetry_t;

sem_control_command_t sem_protocol_default_command(void);
sem_vehicle_telemetry_t sem_protocol_default_telemetry(void);
int16_t sem_protocol_clamp_axis(int32_t value);
int16_t sem_protocol_limit_throttle_by_distance(int16_t requested_throttle, uint16_t distance_cm);
sem_vehicle_state_t sem_protocol_state_from_distance(uint16_t distance_cm, bool link_ok);

#ifdef __cplusplus
}
#endif
