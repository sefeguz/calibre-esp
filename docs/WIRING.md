# Cableado — Calibre Hamilton + ESP32-C3

## El conector del calibre

Bajo la tapa corrediza en la parte superior del cursor (ver `../Calibre02.png`)
hay un conector de borde con **4 contactos**. Con el calibre orientado normal
(display de frente, tapa hacia arriba), de izquierda a derecha:

```
 ┌─────────────────────┐
 │  1     2     3    4 │   1 = GND   (lado del riel / borde exterior)
 │ GND  DATA  CLK  VCC │   2 = DATA
 └─────────────────────┘   3 = CLK   (reposa en nivel ALTO)
                           4 = VCC  (≈ voltaje de la pila)
```

**Verificación con téster** (calibre encendido, contra el pad GND):

| Pad | Esperado |
|---|---|
| VCC | ≈ 3.0 V (pila CR2032) |
| CLK | nivel alto estable la mayor parte del tiempo (reposa alto, pulsa abajo en ráfagas) |
| DATA | variable según el valor |

El nivel que marque **CLK en reposo es el nivel de señal real**: puede ser
~3 V (lectura digital directa) o ~1.5 V (el calibre regula internamente;
el firmware usa el modo ADC).

> Si los pads no responden a este patrón, probá el orden inverso: algunos
> calibres montan el conector espejado.

## Conexión al ESP32-C3 (sin componentes extra)

Solo **3 cables**. VCC del calibre queda sin conectar (sigue con su pila).

| Calibre | ESP32-C3 | Nota |
|---|---|---|
| GND | GND | referencia común — obligatorio |
| DATA | GPIO0 | entrada flotante (el firmware no activa pull-up) |
| CLK | GPIO1 | entrada flotante |

Más el botón de captura:

| Componente | Conexión |
|---|---|
| Pulsador | entre **GPIO3** y **GND** (pull-up interno) |
| LED estado | GPIO8 — ya integrado en la placa C3 SuperMini |

### Conector físico

- Los pads tienen paso de **1.6 mm** (demasiado fino para Dupont directo).
- Opciones: el conector imprimible en 3D de EspDRO
  ([Thingiverse 3141366](https://www.thingiverse.com/thing:3141366), fuente
  OpenSCAD en `../reference/EspDRO/CAD/`), o soldar 3 cables finos
  (wrapping AWG30) a una tirita de PCB perfboard que calce en la ranura, o un
  conector flex de 4 pines paso 1.6 si se consigue.
- Cables cortos (< 20 cm): la señal es débil y sensible a ruido.
- Tip de EspDRO/Matthias Wandel: si hay lecturas erráticas, un capacitor de
  100 nF entre GND del circuito y el chasis metálico del calibre ayuda.

## ¿No detecta nada? (diagnóstico)

1. Abrí `http://calibre.local/api/status` o usá el comando serial `status`.
2. `clkMv` debería mostrar el nivel de reposo de CLK (~3000 o ~1500 mV).
   - **~0 mV** → revisá continuidad/orden de pads (probá espejado).
   - **~1500 mV y modo ADC sin frames** → tu calibre transmite rápido (~90 kHz):
     necesitás el level-shifter NPN (abajo) y activar "Señal invertida".
3. `lastBits` distinto de 24 → variante de protocolo; anotá `lastRaw` y abrí
   un issue/consulta.

## Level-shifter NPN (plan B, 2 transistores)

Solo para calibres de 1.5 V con clock rápido que el modo ADC no alcanza:

```
                      3.3V
                       │
                      ┌┴┐ 10k
                      └┬┘
 DATA o CLK ──[10k]──┐ ├──────── GPIO (0 o 1)
   (calibre)         │ │
                    B │ C
                     ▽ NPN (BC548 / 2N2222)
                    E │
                      │
                     GND
```

La señal queda **invertida** → activar "Señal invertida (level-shifter NPN)"
en la configuración web. El calibre sigue con su pila; GND común.

> **Nunca** alimentes el calibre con 3.3 V con la pila puesta (riesgo de
> "cargar" la CR2032), y a más de ~1.8 V el LCD se oscurece hasta quedar
> ilegible (reportado por jkent).
