# CRAY AutoTune LDR - VST3 x64 (Windows, JUCE 8.0.6, MSVC)

Dos motores:
* **Live** (por defecto): causal, retardo 0,1-8 ms segun voz y momento. Sin preservacion de formantes (correcciones tipicas < 3 %).
* **Quality**: PSOLA con preservacion de formantes. 29,6 ms. Para mezcla, no para monitorizar.

Controles principales: Input Type, Key, Scale, Retune Speed, Humanize, Flex-Tune, Formant (solo Quality), Tracking, Bypass, medidor, nota detectada/objetivo. ADVANCED: Mode, Snap, Glide, Transition, Correction, Gate, Mix, Output, Formant Shift.

## Compilar (Windows 10/11, VS 2022 "C++ de escritorio", CMake >= 3.22, Git)
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target dsp_test ; .\build\Release\dsp_test.exe     # debe acabar en TODO OK
cmake --build build --config Release --target CrayAutoTune_VST3 --parallel
```
Salida: `build\CrayAutoTune_artefacts\Release\VST3\CRAY AutoTune LDR.vst3`

## Instalar (PowerShell como administrador)
```powershell
Copy-Item -Recurse -Force ".\build\CrayAutoTune_artefacts\Release\VST3\CRAY AutoTune LDR.vst3" "C:\Program Files\Common Files\VST3\"
```
FL Studio 26: Options > Manage plugins > Find installed plugins. Cubase 13: Studio > VST Plug-in Manager > actualizar.

## Uso en directo
Input Type = tu voz; Mode = Live; buffer del interfaz 64-128 muestras. Retune 1-5 ms = efecto duro; 15-40 ms + Humanize = natural.
El workflow `.github/workflows/build-windows.yml` compila, pasa los tests y publica el .vst3.
