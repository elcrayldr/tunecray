# Informe de latencia - CRAY AutoTune LDR

Medido con `Tests/dsp_test.cpp` (resultados completos en `docs/test_results.txt`). Voz sintetica, DSP aislado. **No** incluye driver/ASIO.

## Cuatro latencias distintas (no confundir)
| # | Latencia | Valor |
|---|---|---|
| 1 | **DSP interna** | Live: 4 muestras en reposo (0,04-0,09 ms); con correccion activa oscila entre 4 muestras y 1 periodo (tabla). Quality: 29,6 ms fijos |
| 2 | **Declarada al host (PDC)** | Live: retardo MEDIO del tipo de voz (tabla). Quality: igual al retardo real, exacto |
| 3 | **Ida y vuelta del sistema** | = buffer ASIO de entrada + salida + conversores + DSP. No medible aqui. Ej.: 128 muestras @48k = 2,7 ms por sentido |
| 4 | **Driver / buffer ASIO** | Depende de tu interfaz; el plugin no lo controla |

## Origen de la latencia del motor anterior (PSOLA)
`L = 1,95·Tmax` (0,5 T busqueda de pico + 1,45 T de grano estirado) y `O = Tmax` (lectura tras OLA completo) -> **2,95 periodos del tono MINIMO** (42 ms a 70 Hz). Es lookahead estructural: no se arregla ajustando. Ademas el parche anterior declaraba 4 muestras menos que el retardo real (descuadre de PDC; corregido).

## Motor nuevo (modo Live): causal
Sin lookahead. El detector (YIN) solo genera CONTROL; su retraso (~8-12 ms) afecta a *cuando* empieza a corregir, no al retardo del audio. El audio pasa por una linea de retardo cuya posicion se mueve a `1 - ratio` y salta +-1 periodo (con crossfade y refinado por correlacion sobre muestras pasadas) al salir de [4 muestras, 4 + T].

Retardo bajo correccion continua (48 kHz, 4 s, nota +35 cents; coincide con la xcorr independiente, 0 ventanas discordantes):
| Input Type | nota | minimo | medio (= PDC) | maximo |
|---|---|---|---|---|
| Low Male | 122 Hz | 0,09 ms | 4,2 ms (PDC 4,6) | 8,2 ms |
| Alto/Tenor | 194 Hz | 0,08 ms | 2,7 ms (PDC 2,85) | 5,2 ms |
| Soprano | 327 Hz | 0,09 ms | 1,6 ms (PDC 1,75) | 3,1 ms |
Resultados analogos a 44,1 / 88,2 / 96 kHz (en ms).

**Cumplimiento del objetivo:** menos de 3 ms medios para Alto/Tenor y Soprano; menos de 5 ms medios en Low Male; el MAXIMO en Low Male (8,2 ms) lo supera. Es un limite fisico del metodo: un salto coherente exige un periodo completo (a 122 Hz = 8,2 ms). Alternativas con menos retardo maximo (phase vocoder, saltos de medio periodo) ya se descartaron: fase invertida o 20-60 ms de ventana.
Declarar el retardo medio deja el audio grabado desplazado +-T/2 (max +-4 ms en voz grave) respecto al PDC. Documentado, no oculto.

## Respuesta del Retune (Live, 48 kHz, +40 cents -> +-10 cents)
1 ms: 16 ms | 20 ms: 39 ms | 60 ms: 95 ms | 150 ms: 218 ms (ventana de medida 16 ms). Monotono y predecible. Vibrato +-40 cents @5,5 Hz: retune 5 ms -> queda 34 % (10,5/30,7 cents); retune 150 ms -> se conserva (29/30,7).
