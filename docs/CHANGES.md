# Cambios
## Auditoria (ZIP original vs parche UI/RT)
* Parche: solo anadio PluginEditor y cambio el PDC a `L+O-4`. **Defecto**: 4 muestras por debajo del retardo medido (su propio test lo habria detectado con tolerancia 1). Corregido.
* Motor PSOLA: latencia estructural 2,95 T (ver LATENCY_REPORT).
* Interfaz anterior: ningun control Humanize / Flex / Tracking / Input Type.
## Nuevo
* DSP separado en Tracker (control) / LiveShifter (causal) / PsolaShifter / Engine. PSOLA conservado como modo Quality.
* Humanize, Flex-Tune (zona muerta hasta 50 cents), Tracking (umbral YIN + debounce), Input Type (rango de deteccion, ventana YIN y PDC), Bypass (parametro de bypass del host, alineado al PDC).
* Compensacion del retraso de control con pendiente de pitch (vibrato).
* Dry/Mix a retardo constante igual al PDC (un dry con retardo movil se desafinaba: detectado por el test de bypass).
* Editor propio redimensionable; Formant deshabilitado en Live.
* Parametro `range` sustituido por `input`; ids nuevos: humanize, flex, bypass, mode (los presets antiguos no son compatibles).
* Tests: 10 grupos, 4 frecuencias de muestreo, bloques 1-4096, mono/estereo, asignaciones, CPU.
