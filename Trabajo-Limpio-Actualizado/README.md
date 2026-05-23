# Control Adaptativo de Vehiculo con MCPWM

Proyecto SEM basado en dos placas ESP32-S3 y un kit PiCar-X:

- `firmware/vehicle`: ESP32-S3 montado en el vehiculo. Controla motores con MCPWM, sirve una interfaz web por WiFi, aplica logica de seguridad y publica telemetria.
- `firmware/remote`: ESP32-S3 del mando. Lee el joystick y envia comandos al vehiculo por WiFi. Queda como alternativa al control web.
- `shared/components/sem_protocol`: componente compartido con las estructuras de comandos/telemetria para que ambos proyectos hablen el mismo idioma.

La separacion en dos proyectos ESP-IDF es intencionada: el mando y el vehiculo tienen hardware, tareas y configuracion distinta, aunque compartan el protocolo.

## Estructura

```text
.
├── firmware/
│   ├── remote/        # Proyecto ESP-IDF de las comunicaciones
│   └── vehicle/       # Proyecto ESP-IDF del coche PiCar-X
├── shared/
│   └── components/
│       └── sem_protocol/
├── docs/
│   └── architecture.md
├── hardware/
│   └── pinout.md
└── hcsr04/            # Driver HC-SR04 integrado en el firmware del vehiculo
```

## Arquitectura

```text
[Ordenador con navegador / interfaz React]
        |
        | WiFi AP + WebSocket: ws://192.168.4.1/ws
        v
[ESP32-S3 vehicle]
        |-- MCPWM -> Driver TB6612FNG -> motores DC
        |-- LEDC -> servo de direccion
        |-- GPIO -> sensor distancia HC-SR04
        |-- WebSocket -> comandos y distancia
```

El vehiculo crea la red `SEM-PICAR` y escucha comandos en `ws://192.168.4.1/ws`.
La interfaz web se ejecuta aparte y se conecta a ese WebSocket.

## Logica del vehiculo

| Distancia | Comportamiento |
| --- | --- |
| `> 50 cm` | Velocidad normal segun control |
| `20-50 cm` | Reduccion progresiva |
| `< 20 cm` | Parada automatica |

Actualmente el firmware del vehiculo compila, mueve motores con el TB6612FNG,
controla el servo de direccion y lee el HC-SR04 con `TRIG=GPIO13` y `ECHO=GPIO12`.
El pin `ECHO` debe entrar al ESP32-S3 mediante divisor de tension a 3.3 V.

## Compilacion

Cada firmware se compila desde su propia carpeta:

```bash
cd firmware/vehicle
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```

```bash
cd firmware/remote
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/tty.usbmodemXXXX flash monitor
```

## Siguientes decisiones tecnicas

1. Calibrar el centro y los limites del servo de direccion.
2. Probar el sentido real de los motores con las ruedas levantadas.
3. Validar mediciones reales del HC-SR04 con el divisor de tension en `ECHO`.
4. Mantener los drivers por capas:
   - `vehicle`: motores MCPWM, direccion/servo, sensor distancia, comunicacion, web/telemetria.
   - `remote`: lectura ADC del joystick, botones, comunicacion.

## Equipo

Javier Baena, Adriana Baghdasaryan, Paula Barona y Maria Fatima Cebria.
