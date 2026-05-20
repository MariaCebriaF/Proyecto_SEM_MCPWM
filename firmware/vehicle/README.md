# Firmware vehicle

Proyecto ESP-IDF para el ESP32-S3 montado en el PiCar-X.

Responsabilidades:

- MCPWM para motores.
- Sensor de distancia.
- Logica adaptativa de velocidad.
- Comunicacion WiFi con panel web.
- Telemetria para depuracion o panel web.

## Estado actual

- El ESP32-S3 arranca como punto de acceso WiFi `SEM-PICAR`.
- Al conectarte a esa red, abre `http://192.168.4.1`.
- La web envia comandos con WASD/flechas a `/api/control`.
- `control_task` recibe comandos por cola, aplica timeout de seguridad y limitacion por distancia.
- `motor_task` recibe la salida final por cola y controla el TB6612FNG con MCPWM.
- `/api/telemetry` devuelve throttle aplicado, steering aplicado, distancia, estado y secuencia.

## Antes de probar con motores

1. Confirma los GPIO de `TB6612_*_GPIO` en `main/app_main.c`.
2. Une GND de ESP32-S3, TB6612FNG y bateria de motores.
3. Alimenta `VCC` del TB6612FNG a 3.3 V y `VM` con la bateria de motores.
4. Prueba primero con las ruedas levantadas.
5. Si un motor gira al reves, cambia el flag `.inverted` del canal correspondiente.

El sensor de distancia sigue siendo un stub que devuelve 100 cm; por eso la limitacion por obstaculo esta preparada pero todavia no mide hardware real.
