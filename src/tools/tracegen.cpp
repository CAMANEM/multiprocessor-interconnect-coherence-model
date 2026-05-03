#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <vector>

// ---------------------------------------------------------------
//  Latencias del sistema (deben coincidir con top.cpp)
//    L1 lookup : 1 ns
//    Interconnect hop : 2 ns
//    Memoria read/write : 40 ns
//    Total miss completo : 43 ns
//
//  Causalidad por dependencia de direccion:
//    Dos operaciones son dependientes si acceden a la MISMA
//    direccion y existe una escritura antes de una lectura
//    (o escritura antes de escritura).
//
//    Solo en ese caso se exige que el tick de la segunda
//    operacion sea >= tick_primera + MISS_LATENCY.
//
//    Operaciones sobre DISTINTAS direcciones pueden tener
//    el mismo tick (paralelismo real entre PEs).
//
//  Soporte futuro ADD/SUB:
//    ADD y SUB son operaciones read-modify-write atomicas.
//    Se tratan como escrituras para calculo de dependencias:
//    cualquier operacion posterior sobre la misma direccion
//    debe esperar MISS_LATENCY desde que el ADD/SUB empezo.
// ---------------------------------------------------------------
static constexpr std::uint64_t L1_LATENCY   =  1;
static constexpr std::uint64_t IC_LATENCY   =  2;
static constexpr std::uint64_t MEM_LATENCY  = 40;
static constexpr std::uint64_t MISS_LATENCY = L1_LATENCY + IC_LATENCY + MEM_LATENCY; // 43 ns
static constexpr std::uint64_t HIT_LATENCY  = L1_LATENCY;                            //  1 ns

namespace {

void print_usage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " --workload pc|migratory --output <file.trace>\n";
}

// ---------------------------------------------------------------
//  DependencyTracker
//
//  Lleva registro del ultimo tick de escritura por direccion.
//  Permite calcular el tick minimo causal para la siguiente
//  operacion sobre esa direccion.
//
//  Uso:
//    tracker.write(addr, tick)   -> registra escritura
//    tracker.next_safe(addr)     -> tick minimo para leer/escribir
//
//  Cuando se agregen ADD/SUB, llamar write() igualmente porque
//  ambas operaciones modifican la memoria.
// ---------------------------------------------------------------
class DependencyTracker {
public:
  // Registra que se hizo una escritura (o ADD/SUB) en addr en tick t
  void write(std::uint64_t addr, std::uint64_t tick) {
    last_write_[addr] = tick;
  }

  // Devuelve el tick minimo seguro para operar sobre addr.
  // Si no hubo escritura previa, cualquier tick es seguro (retorna 0).
  std::uint64_t next_safe(std::uint64_t addr) const {
    auto it = last_write_.find(addr);
    if (it == last_write_.end()) return 0;
    return it->second + MISS_LATENCY;
  }

private:
  std::unordered_map<std::uint64_t, std::uint64_t> last_write_;
};

// ---------------------------------------------------------------
//  Workload 1: Producer-Consumer con paralelismo real
//
//  Dos pares productor-consumidor trabajando en paralelo:
//    Par A: PE0 produce en 0x8000, PE1 consume de 0x8000
//    Par B: PE2 produce en 0x9000, PE3 consume de 0x9000
//
//  Las dos parejas son independientes entre si: sus ticks
//  coinciden (paralelismo real). Dentro de cada pareja,
//  se respeta causalidad: el consumidor espera a que el
//  productor haya terminado su escritura.
//
//  Comportamiento esperado MSI:
//    Cada lectura del consumidor es MISS porque BusRdX
//    invalido su copia en la ronda anterior.
//    Alto trafico: BusRd + BusRdX por cada ronda, en dos buses
//    paralelos.
//
//  Comportamiento esperado Firefly:
//    Primera lectura de cada consumidor es MISS.
//    Las siguientes son HIT porque BusUpd mantiene la copia
//    actualizada. Menor trafico total, mas BusUpd.
//    La diferencia con MSI es clara en bus_txns y total_latency.
// ---------------------------------------------------------------
void write_producer_consumer(std::ostream& os) {
  os << "# CE4302 trace v1\n"
     << "# Workload: producer-consumer con dos pares en paralelo\n"
     << "#   Par A: PE0 escribe 0x8000, PE1 lee 0x8000\n"
     << "#   Par B: PE2 escribe 0x9000, PE3 lee 0x9000\n"
     << "# Causalidad: consumidor espera MISS_LATENCY=" << MISS_LATENCY
     << " ns desde ultima escritura del productor\n"
     << "# Paralelismo: Par A y Par B tienen ticks identicos (independientes)\n"
     << "# Soporte ADD/SUB: los productores usaran ADD cuando se implemente\n"
     << "#\n"
     << "# tick  pe  op  addr    size  valor\n";

  const std::uint64_t addr_a = 0x8000;
  const std::uint64_t addr_b = 0x9000;

  DependencyTracker dep;

  // tick base de cada ronda: avanza por la dependencia mas lenta
  // Como ambos pares son simetricos, el tick base es el mismo para ambos
  std::uint64_t t = 0;

  for (int round = 0; round < 8; ++round) {
    // --- Paso 1: productores escriben en paralelo (mismo tick) ---
    // PE0 escribe addr_a
    std::uint64_t t_write_a = std::max(t, dep.next_safe(addr_a));
    os << t_write_a << " 0 W 0x" << std::hex << addr_a << std::dec
       << " 4 " << round << "\n";
    dep.write(addr_a, t_write_a);

    // PE2 escribe addr_b al mismo tiempo (direccion distinta -> independiente)
    std::uint64_t t_write_b = std::max(t, dep.next_safe(addr_b));
    os << t_write_b << " 2 W 0x" << std::hex << addr_b << std::dec
       << " 4 " << (round + 100) << "\n";
    dep.write(addr_b, t_write_b);

    // --- Paso 2: consumidores leen en paralelo ---
    // PE1 lee addr_a: debe esperar a que PE0 haya terminado
    std::uint64_t t_read_a = dep.next_safe(addr_a);
    os << t_read_a << " 1 R 0x" << std::hex << addr_a << std::dec << " 4\n";

    // PE3 lee addr_b al mismo tiempo (direccion distinta -> independiente)
    std::uint64_t t_read_b = dep.next_safe(addr_b);
    os << t_read_b << " 3 R 0x" << std::hex << addr_b << std::dec << " 4\n";

    // Avanzar t para la proxima ronda: despues de que las lecturas completen
    t = std::max(t_read_a, t_read_b) + MISS_LATENCY;
  }
}

// ---------------------------------------------------------------
//  Workload 2: Migratory con dos lineas en paralelo
//
//  Dos lineas migrando independientemente en paralelo:
//    Linea A (0xA000): migra PE0 -> PE1 -> PE2 -> PE3 -> PE0 ...
//    Linea B (0xB000): migra PE1 -> PE2 -> PE3 -> PE0 -> PE1 ...
//                      (desfasada un PE respecto a linea A)
//
//  Dentro de cada linea: el PE activo hace R+W antes de pasar.
//  Causalidad: el siguiente PE espera MISS_LATENCY desde la
//  ultima escritura sobre esa linea.
//  Paralelismo: las dos lineas son independientes; sus operaciones
//  pueden coincidir en tick.
//
//  Comportamiento esperado MSI y Firefly:
//    Ambos protocolos se comportan similar porque la linea siempre
//    tiene un unico dueno exclusivo (estado M).
//    BusUpd de Firefly no ayuda porque no hay copias compartidas
//    estables. Los contadores bus_rdx seran similares en ambos.
//    Esto demuestra cuando Firefly NO tiene ventaja sobre MSI.
// ---------------------------------------------------------------
void write_migratory(std::ostream& os) {
  os << "# CE4302 trace v1\n"
     << "# Workload: migratory con dos lineas en paralelo\n"
     << "#   Linea A (0xA000): migra PE0->PE1->PE2->PE3->PE0...\n"
     << "#   Linea B (0xB000): migra PE1->PE2->PE3->PE0->PE1...\n"
     << "# Causalidad: cada PE espera MISS_LATENCY=" << MISS_LATENCY
     << " ns desde ultima escritura sobre esa linea\n"
     << "# Paralelismo: Linea A y Linea B son independientes; ticks coinciden\n"
     << "# Soporte ADD/SUB: las escrituras W seran reemplazadas por ADD/SUB\n"
     << "#                  cuando se implemente; la causalidad no cambia\n"
     << "#\n"
     << "# tick  pe  op  addr    size  valor\n";

  const std::uint64_t addr_a = 0xA000;
  const std::uint64_t addr_b = 0xB000;
  const int pe_count = 4;

  DependencyTracker dep;
  std::uint64_t t = 0;

  for (int lap = 0; lap < 6; ++lap) {
    for (int step = 0; step < pe_count; ++step) {
      int pe_a = step % pe_count;
      int pe_b = (step + 1) % pe_count;  // desfasado un PE
      int val_a = lap * pe_count + step;
      int val_b = 100 + lap * pe_count + step;

      // --- Lectura en paralelo sobre las dos lineas ---
      std::uint64_t t_r_a = std::max(t, dep.next_safe(addr_a));
      std::uint64_t t_r_b = std::max(t, dep.next_safe(addr_b));

      os << t_r_a << " " << pe_a << " R 0x" << std::hex << addr_a
         << std::dec << " 4\n";
      os << t_r_b << " " << pe_b << " R 0x" << std::hex << addr_b
         << std::dec << " 4\n";

      // --- Escritura: debe esperar a que la lectura complete ---
      std::uint64_t t_w_a = t_r_a + MISS_LATENCY;
      std::uint64_t t_w_b = t_r_b + MISS_LATENCY;

      os << t_w_a << " " << pe_a << " W 0x" << std::hex << addr_a
         << std::dec << " 4 " << val_a << "\n";
      dep.write(addr_a, t_w_a);

      os << t_w_b << " " << pe_b << " W 0x" << std::hex << addr_b
         << std::dec << " 4 " << val_b << "\n";
      dep.write(addr_b, t_w_b);

      // Avanzar t para el siguiente step
      t = std::max(t_w_a, t_w_b) + MISS_LATENCY;
    }
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string workload;
  std::string output;

  for (int i = 1; i < argc; ++i) {
    const std::string a(argv[i]);
    if (a == "--workload" && i + 1 < argc) {
      workload = argv[++i];
    } else if (a == "--output" && i + 1 < argc) {
      output = argv[++i];
    } else {
      print_usage(argv[0]);
      return 2;
    }
  }

  if (workload.empty() || output.empty()) {
    print_usage(argv[0]);
    return 2;
  }

  std::ofstream out(output, std::ios::binary);
  if (!out) {
    std::cerr << "cannot open output: " << output << "\n";
    return 3;
  }

  if (workload == "pc" || workload == "producer-consumer") {
    write_producer_consumer(out);
  } else if (workload == "migratory" || workload == "migrate") {
    write_migratory(out);
  } else {
    std::cerr << "unknown workload: " << workload << "\n";
    print_usage(argv[0]);
    return 2;
  }

  std::cout << "Trace generado en: " << output << "\n";
  return 0;
}