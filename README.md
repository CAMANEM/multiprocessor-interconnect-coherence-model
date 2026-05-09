# multiprocessor-interconnect-coherence-model

Simulador de coherencia de caché e interconnect en **SystemC / TLM-2.0** (CE4302 — Arquitectura de Computadores II). Este repositorio contiene una base ejecutable: **4 PEs** como *trace players*, **L1 privadas** con selección de protocolo (**MSI** y **Firefly simplificado**), **interconnect** con cuatro puertos hacia **memoria compartida**, y **monitor** de métricas agregadas.

**Código:** cabeceras `.hpp` en [`include/sim/`](include/sim/) (modelo SystemC) y [`include/common/`](include/common/) (parser de trazas); implementaciones `.cpp` en [`src/sim/`](src/sim/) y [`src/common/`](src/common/). CMake añade esas rutas al compilador; en el código se usa `#include "nombre.hpp"`.

## Requisitos

- **CMake** 3.16 o superior
- Compilador **C++17**
- **SystemC** (IEEE 1666) con cabeceras TLM; por ejemplo [Accellera SystemC](https://www.accellera.org/downloads/standards/systemc)

## Instalación

### En windows instalar [`PyQt6`]:
```
pip install PyQt6
```

### En windows instalar [`Msys2`](https://www.msys2.org/) y ejecutar en él:

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

Evictions

```bash
./src/build/tracegen --workload eviction --output src/traces/eviction.trace
./src/build/mp_sim --trace src/traces/eviction.trace --protocol msi
./src/build/mp_sim --trace src/traces/eviction.trace        --protocol msi     --csv resultados.csv
```


`--protocol` se acepta en CLI y se aplica en `L1Cache` como **selección arquitectónica activa**:

- `msi`: política write-invalidate (BusRd / BusRdX).
- `firefly`: modelo simplificado write-update sobre líneas compartidas (`BusUpd`). En escrituras sobre estado `S`, la caché emisora se mantiene en `S` y publica update al resto de caches.
- En ambos protocolos, miss de escritura en `I` sigue usando `BusRdX`.

Alcance actual de Firefly: el simulador modela coherencia de **estados y tráfico** y, para `BusUpd`, aplica los bytes del write en las líneas compartidas de los sharers.

**Log / bitácora:** los mensajes van a **stderr** (no a stdout). `> bus.log` en PowerShell o bash solo redirige stdout, así que la bitácora **no** entra ahí. Para guardarla: `./src/build/mp_sim ... --log-level debug 2> bus.log` (el resumen del monitor sigue en stdout). `--log-level` acepta el nivel en cualquier mezcla de mayúsculas (`debug`, `Debug`, `DEBUG`). Para guardar bitácora y resumen en un solo archivo: `... > all.log 2>&1` (PowerShell y bash).

Al terminar, `mp_sim` imprime una línea resumen del monitor, por ejemplo:

`bus_txns=... bus_bytes=... total_latency=... bus_rd=... bus_rdx=... bus_upd=... state_transitions=...`

## Git y Demo 2 (referencia de flujo)

- Rama principal estable (`main` / acordada con el profesor).
- Ramas cortas por entrega: `demo/tracegen`, `feature/l1-msi`, etc.
- Objetivo Demo 2: generadores de trazas funcionales, diseño de L1 y tabla de transacciones TLM — usar issues o comentarios en PR para enlazar decisiones con el documento del curso.

## Arquitectura almacenamiento memoria

- Se está utilizando arquitectura Little Endian para almacenar la infomacion en las direcciones de memoria tanto de la compartida como la caché.

## Próximos pasos sugeridos (no implementados aún)

- Tabla y justificación de **transacciones de coherencia** (p. ej. `GetS`, `GetM`, `Inv`, `WB`) sobre TLM.
- Estados extendidos para Firefly completo (p. ej. `E`/`O`) y tabla formal de transiciones.
- Tercer workload de **alta contención** para el rubro final (tres trazas frente a ambos protocolos).

## 1. Parámetros de la jerarquía

| Parámetro | Valor en el modelo | Justificación breve |
|-----------|-------------------|---------------------|
| Tamaño de línea L1 | **64 B** | Línea típica en caches L1 de procesadores comerciales; facilita comparar con literatura de coherencia (Hennessy & Patterson; primer TOCS). |
| Capacidad L1 (por PE) | **`kL1NumLines` líneas** (por defecto **8** → **512 B**) | L1 deliberadamente pequeña respecto al working set de ciertas trazas para provocar **fallos por capacidad** y **evicciones LRU**; permite observar **BusWrBack** al expulsar líneas **M**. |
| Memoria compartida | **16 MiB** lineales (`SharedMemory::kSizeBytes`) | Muy superior al footprint de las trazas de ejemplo; evita que el modelo se limite por falta de espacio de direcciones y centra el análisis en coherencia e interconnect. |
| Latencia L1 | **1 ns** | Órdenes de magnitud menores que memoria principal; aciertos baratos frente a fallos. |
| Latencia bus / interconnect | **2 ns** por *beat* de datos | Cada porción que cruza el bus hasta DRAM paga este salto además de la latencia DRAM del beat. |
| Ancho de datos del bus | **`Interconnect::kBusDataBytes = 8`** (64 bits) | Si una transferencia supera 8 B, el modelo la **parte en varios viajes** consecutivos (burst software), cada uno con su latencia de bus + DRAM. |
| Latencia DRAM (lectura/escritura) | **40 ns** | Valor agregado de acceso a fila; suficiente para separar impacto de protocolo del coste de memoria. |

