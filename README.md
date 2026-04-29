# Control Adaptativo de Vehiculo con MCPWM

Proyecto SEM basado en dos placas ESP32-S3 y un kit PiCar-X:

- `firmware/vehicle`: ESP32-S3 montado en el vehiculo. Controla motores con MCPWM, lee el sensor de distancia, aplica la reduccion automatica de velocidad y publica telemetria.
- `firmware/remote`: ESP32-S3 del mando. Lee el joystick y envia comandos al vehiculo por WiFi.
- `shared/components/sem_protocol`: componente compartido con las estructuras de comandos/telemetria para que ambos proyectos hablen el mismo idioma.

La separacion en dos proyectos ESP-IDF es intencionada: el mando y el vehiculo tienen hardware, tareas y configuracion distinta, aunque compartan el protocolo.

## Estructura

```text
.
├── firmware/
│   ├── remote/        # Proyecto ESP-IDF del mando con joystick
│   └── vehicle/       # Proyecto ESP-IDF del vehiculo PiCar-X
├── shared/
│   └── components/
│       └── sem_protocol/
├── docs/
│   └── architecture.md
├── hardware/
│   └── pinout.md
├── sem.pdf
├── IMG_0145.jpeg
└── IMG_0162.JPG
```

## Arquitectura

```text
[ESP32-S3 remote + joystick]
        |
        | WiFi: comando de velocidad/direccion
        v
[ESP32-S3 vehicle]
        |-- MCPWM -> motores PiCar-X
        |-- GPIO/RMT -> sensor distancia
        |-- HTTP/telemetria -> velocidad, distancia, estado
```

## Logica del vehiculo

| Distancia | Comportamiento |
| --- | --- |
| `> 50 cm` | Velocidad normal segun joystick |
| `20-50 cm` | Reduccion progresiva |
| `< 20 cm` | Parada automatica |

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

1. Definir pines reales del PiCar-X y del joystick en `hardware/pinout.md`.
2. Elegir transporte WiFi: UDP para baja latencia, HTTP solo para panel web, o ESP-NOW si no hace falta router.
3. Implementar drivers por capas:
   - `vehicle`: motores MCPWM, direccion/servo, sensor distancia, comunicacion, web/telemetria.
   - `remote`: lectura ADC del joystick, botones, comunicacion.

## Equipo

Javier Baena, Adriana Baghdasaryan, Paula Barona y Maria Fatima Cebria.
