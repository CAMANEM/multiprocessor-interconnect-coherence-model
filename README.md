# multiprocessor-interconnect-coherence-model

Simulador de coherencia de caché e interconnect en **SystemC / TLM-2.0** (CE4302 — Arquitectura de Computadores II). Este repositorio contiene una base ejecutable: **4 PEs** como *trace players*, **L1 privadas** (stub passthrough), **interconnect** con cuatro puertos hacia **memoria compartida**, y **monitor** de métricas agregadas.

**Código:** cabeceras `.hpp` en [`include/sim/`](include/sim/) (modelo SystemC) y [`include/common/`](include/common/) (parser de trazas); implementaciones `.cpp` en [`src/sim/`](src/sim/) y [`src/common/`](src/common/). CMake añade esas rutas al compilador; en el código se usa `#include "nombre.hpp"`.

## Requisitos

- **CMake** 3.16 o superior
- Compilador **C++17**
- **SystemC** (IEEE 1666) con cabeceras TLM; por ejemplo [Accellera SystemC](https://www.accellera.org/downloads/standards/systemc)

## Instalación

### En windows instalar Msys2 y ejecutar en él:

```bash
pacman -Syu
# cierra y vuelve a abrir la terminal si MSYS2 lo pide
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake make git
```

### Ya sea Linux o Windows (en Mingw64/Ucrt64 en el caso de windows) ejecutar:

```bash
pacman -S --needed git
git clone https://github.com/accellera-official/systemc.git
cd systemc
mkdir build
cd build
cmake -G "MinGW Makefiles" -DCMAKE_INSTALL_PREFIX=/mingw64 ..
cmake --build . --config Release
cmake --install .
```

## Ejecución

### Ahora abrir otra consola en el directorio src de este proyecto y ejecutar:

```bash
# Windows la primera vez:
cmake -S . -B build -DCMAKE_PREFIX_PATH=/mingw64
# Linux:
cmake -S . -B build
# Windows/Linux:
cmake --build build
```



## Configurar SystemC (portable) (ignorar, la mayoria de esta seccion deberia borrarla)

1. Instala o compila SystemC y localiza el prefijo que contiene `include/systemc/` y la biblioteca (`libsystemc` / `systemc.lib`).
2. Exporta la variable de entorno apuntando al prefijo:

**Windows (PowerShell)**

```powershell
$env:SYSTEMC_HOME = "C:\ruta\a\systemc"
```

**Linux/macOS**

```bash
export SYSTEMC_HOME=/ruta/a/systemc
```

Alternativa equivalente:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/ruta/a/systemc
```

Si tu instalación provee un paquete CMake con el objetivo importado `SystemC::systemc`, el proyecto lo usará automáticamente.

## Compilar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Si CMake muestra una advertencia de que **no encontró SystemC**, la configuración igualmente genera `tracegen` y la librería `mp_common`; vuelve a ejecutar CMake tras definir `SYSTEMC_HOME` / `CMAKE_PREFIX_PATH` para habilitar `mp_sim`.

Ejecutables:

- `build/mp_sim` — simulador principal (solo si SystemC está disponible)
- `build/tracegen` — generador de trazas de ejemplo (productor-consumidor y migratorio)

## Formato de traza (CE4302 trace v1)

Archivo texto UTF-8. Líneas que empiezan por `#` son comentarios. Cada acceso es una línea:

```text
tick pe_id R|W address [size]
```

- **tick**: tiempo lógico entero; en `mp_sim` se interpreta como **nanosegundos** desde el inicio de la simulación.
- **pe_id**: `0`…`3` (hay cuatro PEs instanciados).
- **R / W**: lectura o escritura.
- **address**: dirección (acepta prefijo `0x` o constante numérica interpretable por `stoull(..., 0)`).
- **size** (opcional): bytes; por defecto **4**.

Ejemplos versionados en [`traces/producer_consumer.trace`](traces/producer_consumer.trace) y [`traces/migratory.trace`](traces/migratory.trace).

Desde la raiz del proyecto:

### Regenerar trazas largas

```bash
./src/build/tracegen --workload pc --output src/traces/pc_long.trace
./src/build/tracegen --workload migratory --output src/traces/migratory_long.trace
```

## Ejecutar la simulación

```bash
./src/build/mp_sim --trace src/traces/producer_consumer.trace --protocol msi
./src/build/mp_sim --trace src/traces/migratory.trace --protocol firefly
```

Con logs y monitor en consola :

```bash
./src/build/mp_sim --trace src/traces/producer_consumer.trace --protocol msi --log-level debug
./src/build/mp_sim --trace src/traces/migratory.trace --protocol firefly --log-level debug
```

Con logs en mp.log y monitor en consola:

```bash
./src/build/mp_sim --trace src/traces/producer_consumer.trace --protocol msi --log-level debug 2> mp.log
./src/build/mp_sim --trace src/traces/migratory.trace --protocol firefly --log-level debug 2> mp.log
```

Con logs y monitor en mp.log:

```bash
./src/build/mp_sim --trace src/traces/producer_consumer.trace --protocol msi --log-level debug > mp.log 2>&1
./src/build/mp_sim --trace src/traces/migratory.trace --protocol firefly --log-level debug > mp.log 2>&1
```


`--protocol` se acepta en CLI y se propaga al modelo L1 como **selección arquitectónica** (la lógica MSI/Firefly aún es un stub).

**Log / bitácora:** los mensajes van a **stderr** (no a stdout). `> bus.log` en PowerShell o bash solo redirige stdout, así que la bitácora **no** entra ahí. Para guardarla: `./src/build/mp_sim ... --log-level debug 2> bus.log` (el resumen del monitor sigue en stdout). `--log-level` acepta el nivel en cualquier mezcla de mayúsculas (`debug`, `Debug`, `DEBUG`). Para guardar bitácora y resumen en un solo archivo: `... > all.log 2>&1` (PowerShell y bash).

Al terminar, `mp_sim` imprime una línea resumen del monitor, por ejemplo:

`bus_txns=... bus_bytes=... total_latency=...`

## Git y Demo 2 (referencia de flujo)

- Rama principal estable (`main` / acordada con el profesor).
- Ramas cortas por entrega: `demo/tracegen`, `feature/l1-msi`, etc.
- Objetivo Demo 2: generadores de trazas funcionales, diseño de L1 y tabla de transacciones TLM — usar issues o comentarios en PR para enlazar decisiones con el documento del curso.

## Arquitectura almacenamiento memoria

- Se está utilizando arquitectura Little Endian para almacenar la infomacion en las direcciones de memoria tanto de la compartida como la caché.

## Próximos pasos sugeridos (no implementados aún)

- Tabla y justificación de **transacciones de coherencia** (p. ej. `GetS`, `GetM`, `Inv`, `WB`) sobre TLM.
- Estados **MSI** y **Firefly** en `L1Cache` + tráfico en `Interconnect`.
- Tercer workload de **alta contención** para el rubro final (tres trazas frente a ambos protocolos).

## Licencia y curso

Proyecto académico CE4302. Revisa el enunciado oficial y las políticas de integridad académica del curso antes de reutilizar código de terceros.
