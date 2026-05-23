import styles from './Main.module.css';
import { useState } from 'react'; 

// Ya no importamos Avatar.svg, lo haremos con CSS dinámico
import imgRobot from './assets/foto-robot 1.png';

const App = () => {
    // ══════════════════════════════════════════════════════════
    // ESTADOS DE SIMULACIÓN Y CONEXIÓN
    // ══════════════════════════════════════════════════════════
    const [distanciaSimulada, setDistanciaSimulada] = useState(45); 
    const [modo, setModo] = useState(0); 
    const [lastKeyPressed, setLastKeyPressed] = useState('up'); 
    
    // NUEVO: Estado de la conexión (Empieza en rojo/falso)
    const [isConnected, setIsConnected] = useState(false);

    const MAX_DISTANCIA = 100;
    const DIST_COLISION = 20;

    // ────────────────────────────────────────────────────────────
    // CÁLCULOS DINÁMICOS
    // ────────────────────────────────────────────────────────────
    const calculateDynamicColor = (dist) => {
        if (dist <= DIST_COLISION) return "#FF0000";
        if (dist >= MAX_DISTANCIA) return "#15FF00";

        const ratio = (dist - DIST_COLISION) / (MAX_DISTANCIA - DIST_COLISION);
        const r = Math.floor(255 - (255 - 21) * ratio);
        const g = Math.floor(255 * ratio);
        return `rgb(${r}, ${g}, 0)`;
    };

    const currentColor = calculateDynamicColor(distanciaSimulada);
    const progressWidth = Math.min(100, Math.max(0, (distanciaSimulada / MAX_DISTANCIA) * 100));

    return (
        <div className={styles.main}>
            <div className={styles.header}>
                <div className={styles.navBar}>
                    {/* Botón de Menú (Izquierda) */}
                    <div className={styles.leadingIcon}>
                        <div className={styles.hamburgerMenu}>≡</div>
                    </div>
                    
                    {/* Título Centrado (Centro) */}
                    <div className={styles.textContent}>
                        <div className={styles.headline}>Robot Control</div>
                    </div>

                    {/* Círculo de Estado de Conexión (Derecha) */}
                    <div className={styles.trailingElements}>
                        <div 
                            className={styles.statusCircle} 
                            style={{ backgroundColor: isConnected ? '#00ff51' : '#ff0000' }}
                            onClick={() => setIsConnected(!isConnected)} // <- TRUCO: Clica el círculo para ver cómo cambia!
                            title={isConnected ? "Conectado" : "Desconectado"}
                        />
                    </div>
                </div>
            </div>
            
            <div className={styles.main2}>
                <img className={styles.fotoRobot1Icon} src={imgRobot} alt="Robot PiCar" />
                
                <div className={styles.switchButtons}>
                    <div className={`${styles.autonomo} ${modo === 1 ? styles.activeMode : ''}`} onClick={() => setModo(1)}>
                        <div className={styles.autnomo}>Autónomo</div>
                    </div>
                    <div className={`${styles.manual} ${modo === 0 ? styles.activeMode : ''}`} onClick={() => setModo(0)}>
                        <div className={styles.autnomo}>Manual</div>
                    </div>
                </div>
                
                <div className={styles.controlFrame}>
                    <div className={styles.direccionesDeControl}>Direcciones de Control</div>
                    <div className={styles.utilizaLasFlechas}>Utiliza las flechas de tu teclado para controlarlo.</div>

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
                        <div 
                            className={styles.numGlow}
                            style={{ backgroundColor: currentColor, filter: `blur(60px)` }}
                        />
                        <div className={styles.div}>{distanciaSimulada}</div>
                        <div className={styles.cm}>cm</div>
                    </div>
                </div>
            </div>
        </div>
    );
};

export default App;