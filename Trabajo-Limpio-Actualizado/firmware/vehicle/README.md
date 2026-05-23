# Firmware vehicle

Proyecto ESP-IDF para el ESP32-S3 montado en el PiCar-X.

Responsabilidades:

- MCPWM para motores.
- Servo de direccion.
- Sensor de distancia.
- Logica adaptativa de velocidad.
- Comunicacion WiFi con panel web.
- Telemetria para depuracion o panel web.

## Estado actual

- El ESP32-S3 arranca como punto de acceso WiFi `SEM-PICAR`.
- La web externa se conecta por WebSocket a `ws://192.168.4.1/ws`.
- La web envia comandos con WASD/flechas en formato `throttle=...&steering=...&enable=...&sequence=...&modo=...`.
- `control_task` recibe comandos por cola, aplica timeout de seguridad y limitacion por distancia.
- `motor_task` recibe la salida final por cola y controla el TB6612FNG con MCPWM.
- El servo de direccion usa `GPIO18` con LEDC a 50 Hz.
- El sensor HC-SR04 usa `TRIG=GPIO13` y `ECHO=GPIO12`.

## Antes de probar con motores

1. Confirma los GPIO de `TB6612_*_GPIO` en `main/app_main.c`.
2. Une GND de ESP32-S3, TB6612FNG y bateria de motores.
3. Alimenta `VCC` del TB6612FNG a 3.3 V y `VM` con la bateria de motores.
4. Prueba primero con las ruedas levantadas.
5. Si un motor gira al reves, cambia el flag `.inverted` del canal correspondiente.
6. Alimenta el servo correctamente y confirma que `1500 us` deja la direccion centrada.
7. Usa divisor de tension en `ECHO` del HC-SR04 para no meter 5 V al ESP32-S3.

El firmware compila con el sensor real integrado. Si el sensor falla durante una lectura, se conserva la ultima distancia valida.
