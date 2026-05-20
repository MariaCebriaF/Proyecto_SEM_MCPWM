# Arquitectura del sistema

## Nodos

### ESP32-S3 Vehicle

Responsabilidades:

- Recibir comandos del mando.
- Servir una web de control por WiFi.
- Leer sensor de distancia.
- Calcular velocidad final con logica adaptativa.
- Generar PWM para motores mediante MCPWM.
- Exponer telemetria para depuracion o web.

Tareas previstas:

| Tarea | Entrada | Salida |
| --- | --- | --- |
| `http_server` | HTTP `/api/control` | `sem_control_command_t` |
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

Para el control desde ordenador, el vehiculo arranca como punto de acceso WiFi y sirve una web local. La web envia comandos periodicos por HTTP mientras hay teclas pulsadas. El vehiculo para si no recibe comandos durante un timeout corto, actualmente 500 ms.

Si se vuelve al mando fisico con joystick, UDP o ESP-NOW siguen siendo mejores candidatos que HTTP para baja latencia.
