# Firmware remote

Proyecto ESP-IDF para el ESP32-S3 del mando con joystick.

Responsabilidades:

- Lectura ADC del joystick.
- Normalizacion de ejes a `-1000..1000`.
- Envio periodico de comandos al vehiculo por WiFi.

Estado actual: scaffold con tareas FreeRTOS y stubs de hardware.
