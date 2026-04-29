# Arquitectura del sistema

## Nodos

### ESP32-S3 Vehicle

Responsabilidades:

- Recibir comandos del mando.
- Leer sensor de distancia.
- Calcular velocidad final con logica adaptativa.
- Generar PWM para motores mediante MCPWM.
- Exponer telemetria para depuracion o web.

Tareas previstas:

| Tarea | Entrada | Salida |
| --- | --- | --- |
| `communication_task` | WiFi | `sem_control_command_t` |
| `sensor_task` | sensor distancia | distancia en cm |
| `control_task` | comando + distancia | velocidad limitada |
| `motor_task` | velocidad limitada | MCPWM |
| `telemetry_task` | estado interno | `sem_vehicle_telemetry_t` |

### ESP32-S3 Remote

Responsabilidades:

- Leer joystick y posibles botones.
- Convertir valores analogicos en velocidad/direccion normalizadas.
- Enviar comandos al vehiculo por WiFi.

Tareas previstas:

| Tarea | Entrada | Salida |
| --- | --- | --- |
| `joystick_task` | ADC joystick | `sem_control_command_t` |
| `communication_task` | cola de comandos | WiFi |

## Protocolo compartido

El componente `sem_protocol` define:

- `sem_control_command_t`: velocidad, direccion y flags del mando.
- `sem_vehicle_telemetry_t`: velocidad aplicada, distancia y estado del vehiculo.

La idea es que cualquier cambio en el formato de mensajes se haga una sola vez en `shared/components/sem_protocol`.

## Recomendacion de comunicacion

Para empezar, UDP es la opcion mas simple: baja latencia, poco codigo y suficiente para comandos periodicos de joystick. El vehiculo deberia parar si no recibe comandos durante un timeout corto, por ejemplo 300-500 ms.

El panel web puede convivir en el ESP32-S3 del vehiculo, pero no deberia ser el canal principal del joystick.
