import { useState, useEffect, useRef } from 'react'; 
import styles from './Main.module.css';
import imgRobot from './assets/foto-robot 1.png';


//CONEXIÓN CON MODULO WIFI
// La IP ESP32 (Suele ser 192.168.4.1 si crea su propia red)
const ESP32_IP = "192.168.4.1";

const App = () => {
    // ══════════════════════════════════════════════════════════
    // ESTADOS DE SIMULACIÓN Y CONEXIÓN
    // ══════════════════════════════════════════════════════════
    // 1. Estados reales
    const [distancia, setDistancia] = useState(null); 
    const [modo, setModo] = useState(0); 
    const [lastKeyPressed, setLastKeyPressed] = useState(null); 
    const [isConnected, setIsConnected] = useState(false);

    // 2. Referencias para el WebSocket y el mando
    const ws = useRef(null);
    const keysPressed = useRef(new Set());
    const sequence = useRef(0);

    // 3. Efecto para conectar al Wi-Fi del Robot
    useEffect(() => {
        ws.current = new WebSocket(`ws://${ESP32_IP}/ws`);

        ws.current.onopen = () => setIsConnected(true);
        ws.current.onclose = () => {
			setIsConnected(false);
			setDistancia(null); // Reseteamos la distancia al perder conexión
		};
        ws.current.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                // Si el robot manda telemetría, actualizamos la pantalla
                if (data.distance_cm !== undefined) setDistancia(data.distance_cm);
            } catch (err) {}
        };

        return () => ws.current?.close(); // Limpieza al cerrar la web
    }, []);

    // 4. Efecto para capturar el teclado y enviar comandos
    useEffect(() => {

		// Latido (Heartbeat) que envía el estado 4 veces por segundo
        const interval = setInterval(enviarComando, 250);

        const handleKeyDown = (e) => {
            const key = e.key.toLowerCase();
            if (['w', 'a', 's', 'd', 'arrowup', 'arrowdown', 'arrowleft', 'arrowright'].includes(key)) {
                keysPressed.current.add(key);
                updateUI(key);
                enviarComando();
            }
        };

        const handleKeyUp = (e) => {
            const key = e.key.toLowerCase();
            if (['w', 'a', 's', 'd', 'arrowup', 'arrowdown', 'arrowleft', 'arrowright'].includes(key)) {
                keysPressed.current.delete(key);
                setLastKeyPressed(null); // Apaga la luz de la cruceta
                enviarComando();
            }
        };

        window.addEventListener('keydown', handleKeyDown);
        window.addEventListener('keyup', handleKeyUp);
        return () => {
            window.removeEventListener('keydown', handleKeyDown);
            window.removeEventListener('keyup', handleKeyUp);
			clearInterval(interval); //Limpieza el latido al cerrar la web
        };
    }, [modo]); // Re-ejecuta si cambiamos de modo

    // 5. Función traductora a formato ESP32
    const enviarComando = () => {
        if (!ws.current || ws.current.readyState !== WebSocket.OPEN) return;

        let throttle = 0;
        let steering = 0;

        if (keysPressed.current.has('w') || keysPressed.current.has('arrowup')) throttle += 600;
        if (keysPressed.current.has('s') || keysPressed.current.has('arrowdown')) throttle -= 600;
        if (keysPressed.current.has('a') || keysPressed.current.has('arrowleft')) steering -= 1000;
        if (keysPressed.current.has('d') || keysPressed.current.has('arrowright')) steering += 1000;

        sequence.current += 1;
        ws.current.send(`throttle=${throttle}&steering=${steering}&enable=1&sequence=${sequence.current}&modo=${modo}`);
    };

    // 6. Enviar el modo al instante cuando se clica el botón
    const cambiarModo = (nuevoModo) => {
        setModo(nuevoModo);
        if (ws.current && ws.current.readyState === WebSocket.OPEN) {
            sequence.current += 1;
            ws.current.send(`throttle=0&steering=0&enable=1&sequence=${sequence.current}&modo=${nuevoModo}`);
        }
    };

    // Ilumina la flecha correcta en pantalla
    const updateUI = (key) => {
        if (key === 'w' || key === 'arrowup') setLastKeyPressed('up');
        if (key === 's' || key === 'arrowdown') setLastKeyPressed('down');
        if (key === 'a' || key === 'arrowleft') setLastKeyPressed('left');
        if (key === 'd' || key === 'arrowright') setLastKeyPressed('right');
    };
    

    // ══════════════════════════════════════════════════════════
    // LÓGICA DE ESCALADO ADAPTATIVO (¡Requisito 2 - BLINDADO!)
    // ══════════════════════════════════════════════════════════
    const [scale, setScale] = useState(1);

    useEffect(() => {
        const handleResize = () => {
            // Tamaño objetivo del diseño central de Figma (sin el header)
            const targetWidth = 1920;
            const targetHeight = 1080; 

            // Tamaño actual de la ventana del navegador
            const windowWidth = window.innerWidth;
            const windowHeight = window.innerHeight;

            // Calculamos la escala necesaria para encajar en ancho y en alto
            const widthScale = windowWidth / targetWidth;
            const heightScale = windowHeight / targetHeight;

            // La escala final es la MÍNIMA de las dos para asegurar que todo encaja
            // y la limitamos a 1 para no agrandar el diseño en pantallas gigantes
            let newScale = Math.min(1, Math.min(widthScale, heightScale));

            setScale(newScale);
        };

        handleResize(); // Calcula la escala al cargar
        window.addEventListener('resize', handleResize); // Re-calcula si cambian tamaño ventana
        
        return () => window.removeEventListener('resize', handleResize);
    }, []);

    // ────────────────────────────────────────────────────────────
    // CÁLCULOS DINÁMICOS
    // ────────────────────────────────────────────────────────────
    const MAX_DISTANCIA = 100;
    const DIST_COLISION = 20;

    const calculateDynamicColor = (dist) => {
		if (dist === null) return "#15FF00";
        if (dist <= DIST_COLISION) return "#FF0000";
        if (dist >= MAX_DISTANCIA) return "#15FF00";

        const ratio = (dist - DIST_COLISION) / (MAX_DISTANCIA - DIST_COLISION);
        const r = Math.floor(255 - (255 - 21) * ratio);
        const g = Math.floor(255 * ratio);
        return `rgb(${r}, ${g}, 0)`;
    };

    const currentColor = calculateDynamicColor(distancia);
	const progressWidth = distancia === null ? 100 : Math.min(100, Math.max(0, (distancia / MAX_DISTANCIA) * 100));

    // ────────────────────────────────────────────────────────────
    // MAQUETACIÓN
    // ────────────────────────────────────────────────────────────
    return (
        <div className={styles.main}>
            {/* El Header se mantiene siempre arriba y al 100% */}
            <div className={styles.header}>
                <div className={styles.navBar}>
                    <div className={styles.leadingIcon}>
                        <div className={styles.hamburgerMenu}>≡</div>
                    </div>
                    <div className={styles.textContent}>
                        <div className={styles.headline}>Robot Control</div>
                    </div>
                    <div className={styles.trailingElements}>
                        {/* Círculo de estado dinámico (Rojo/Verde) */}
                        <div 
                            className={styles.statusCircle} 
                            style={{ backgroundColor: isConnected ? '#00ff51' : '#ff0000' }}
                            title={isConnected ? "Conectado" : "Desconectado"}
                        />
                    </div>
                </div>
            </div>
            
            {/* NUEVO: Envolvedor que aplica el escalado adaptativo blindado */}
            <div 
                className={styles.contentScaleWrapper}
                style={{ 
                    // Aplicamos la escala y el punto de origen (arriba-centro)
                    transform: `scale(${scale})`, 
                    transformOrigin: 'top center',
                    // Ajustamos la altura total aparente para no dejar huecos
                    height: `${1080 * scale}px` 
                }}
            >
                {/* La interfaz central ahora vive felizmente dentro del escalador */}
                <div className={styles.main2}>
                    <img className={styles.fotoRobot1Icon} src={imgRobot} alt="Robot PiCar" />
                    
                    <div className={styles.switchButtons}>
                        {/* Botón Autónomo - Estilos dinámicos para saber cuál está activo */}
                        <div className={`${styles.autonomo} ${modo === 1 ? styles.activeMode : ''}`} onClick={() => cambiarModo(1)}>
                            <div className={styles.autnomo}>Autónomo</div>
                        </div>
                        {/* Botón Manual - Estilos dinámicos */}
                        <div className={`${styles.manual} ${modo === 0 ? styles.activeMode : ''}`} onClick={() => cambiarModo(0)}>
                            <div className={styles.autnomo}>Manual</div>
                        </div>
                    </div>
                    
                    <div className={styles.controlFrame}>
                        <div className={styles.direccionesDeControl}>Direcciones de Control</div>
                        <div className={styles.utilizaLasFlechas}>Utiliza las flechas de tu teclado para controlarlo.</div>

                        {/* RECREACIÓN DE LA CRUCETA EN CSS (Para Select-Glow individual) */}
                        <div className={styles.dpadContainer}>
                            <div className={`${styles.arrowBtn} ${lastKeyPressed === 'up' ? styles.arrowGlow : ''}`}>^</div>
                            <div className={styles.dpadRow}>
                                <div className={`${styles.arrowBtn} ${lastKeyPressed === 'left' ? styles.arrowGlow : ''}`}>{`<`}</div>
                                <div className={`${styles.arrowBtn} ${lastKeyPressed === 'down' ? styles.arrowGlow : ''}`}>v</div>
                                <div className={`${styles.arrowBtn} ${lastKeyPressed === 'right' ? styles.arrowGlow : ''}`}>{`>`}</div>
                            </div>
                        </div>
                    </div>
                    
                    <div className={styles.medidaColision}>
                        <div className={styles.distanciaColisin}>Distancia Colisión</div>
                        <div className={styles.distanciaALa}>Distancia a la que se encuentra el Robot del próximo obstáculo.</div>
                        
                        {/* RECREACIÓN DE LA BARRA DE PROGRESO EN CSS (Para Color Dinámico) */}
                        <div className={styles.progressBarTrack}>
                            <div 
                                className={styles.progressBarFill}
                                style={{ 
                                    width: `${progressWidth}%`,
                                    backgroundColor: currentColor,
                                    boxShadow: `0 0 20px ${currentColor}`
                                }}
                            />
                        </div>

                        <div className={styles.distanceNum}>
                            {/* El Glow ahora tiene la animación de respiración CSS exagerada */}
                            <div 
                                className={styles.numGlow}
                                style={{ backgroundColor: currentColor }}
                            />
                            <div className={styles.div}>{distancia === null ? "??" : distancia}</div>
                            <div className={styles.cm}>cm</div>
                        </div>
                    </div>
                    
                    {/* El Footer también vive dentro del escalador */}
                    <div className={styles.footer} />
                </div>
            </div>
        </div>
    );
};

export default App;