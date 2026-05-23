# Arquitectura del sistema

## Nodos

### ESP32-S3 Vehicle

Responsabilidades:

- Recibir comandos de la interfaz web por WebSocket.
- Leer sensor de distancia.
- Calcular velocidad final con logica adaptativa.
- Generar PWM para motores mediante MCPWM.
- Controlar el servo de direccion.
- Exponer telemetria para depuracion o web.

Tareas previstas:

| Tarea | Entrada | Salida |
| --- | --- | --- |
| `websocket_server` | WebSocket `/ws` | `sem_control_command_t` |
| `sensor_task` | sensor distancia | distancia en cm |
| `control_task` | comando + distancia | throttle limitado + steering |
| `motor_task` | throttle + steering | MCPWM motores + PWM servo |
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

Para el control desde ordenador, el vehiculo arranca como punto de acceso WiFi y escucha comandos en `ws://192.168.4.1/ws`.
La web envia comandos periodicos por WebSocket mientras hay teclas pulsadas. El vehiculo para si no recibe comandos durante un timeout corto, actualmente 500 ms.

Si se vuelve al mando fisico con joystick, UDP o ESP-NOW siguen siendo mejores candidatos que WebSocket para baja latencia.
