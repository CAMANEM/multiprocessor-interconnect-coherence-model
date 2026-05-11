
import sys
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from matplotlib.patches import FancyBboxPatch
from matplotlib.lines import Line2D
import numpy as np
from pathlib import Path

# ── Rutas ──────────────────────────────────────────────────────────────────
BASE_DIR = Path(__file__).resolve().parent
CSV_PATH = BASE_DIR / "src" / "traces" / "resultados.csv"
OUTPUT_PATH = BASE_DIR / "dashboard.png"

# ── Tema ───────────────────────────────────────────────────────────────────
BG       = "#0d1117"
BG2      = "#161b22"
BG3      = "#21262d"
BORDER   = "#30363d"
FG       = "#e6edf3"
FG2      = "#8b949e"
GREEN    = "#3fb950"
BLUE     = "#58a6ff"
PURPLE   = "#bc8cff"
ORANGE   = "#ffa657"
RED      = "#f85149"
YELLOW   = "#d29922"
CYAN     = "#39d353"

# Color por protocolo
PROTO_COLORS = {
    "msi":     BLUE,
    "firefly": GREEN,
}

def proto_color(name: str) -> str:
    return PROTO_COLORS.get(name.lower(), ORANGE)

# ── Validacion ─────────────────────────────────────────────────────────────
if not CSV_PATH.exists():
    print(f"ERROR: No se encontro el CSV en {CSV_PATH}")
    print("Asegurate de haber corrido al menos una simulacion con --csv resultados.csv")
    sys.exit(1)

df = pd.read_csv(CSV_PATH)
df = df[df["trace"].notna() & (df["trace"].str.strip() != "")]

if df.empty:
    print("ERROR: El CSV esta vacio o no tiene datos validos.")
    sys.exit(1)

traces    = sorted(df["trace"].unique())
protocols = sorted(df["protocol"].unique())

# ── Configuracion de matplotlib ────────────────────────────────────────────
plt.rcParams.update({
    "figure.facecolor":  BG,
    "axes.facecolor":    BG2,
    "axes.edgecolor":    BORDER,
    "axes.labelcolor":   FG2,
    "axes.titlecolor":   FG,
    "axes.titlesize":    10,
    "axes.titleweight":  "bold",
    "axes.labelsize":    8,
    "axes.spines.top":   False,
    "axes.spines.right": False,
    "axes.grid":         True,
    "grid.color":        BORDER,
    "grid.linewidth":    0.5,
    "grid.alpha":        0.6,
    "xtick.color":       FG2,
    "ytick.color":       FG2,
    "xtick.labelsize":   7,
    "ytick.labelsize":   7,
    "legend.facecolor":  BG3,
    "legend.edgecolor":  BORDER,
    "legend.labelcolor": FG,
    "legend.fontsize":   7,
    "text.color":        FG,
    "font.family":       "monospace",
})

# ── Metricas a graficar ────────────────────────────────────────────────────
# Agrupadas por categoria para el layout
GROUPS = {
    "Trafico de Bus": [
        ("bus_txns",      "Transacciones totales"),
        ("bus_bytes",     "Bytes transferidos"),
        ("bus_rd",        "BusRd  (lecturas miss)"),
        ("bus_rdx",       "BusRdX (escrituras/upgrade)"),
        ("bus_upd",       "BusUpd (Firefly update)"),
        ("bus_wb",        "BusWrBack (evictions)"),
    ],
    "Latencia y Ancho de Banda": [
        ("total_lat_ns",    "Latencia total (ns)"),
        ("avg_lat_ns",      "Latencia promedio (ns)"),
        ("bw_bytes_per_ns", "Ancho de banda (B/ns)"),
    ],
    "Coherencia y Operaciones PE": [
        ("state_trans", "Transiciones de estado"),
        ("pe_reads",    "Lecturas (R)"),
        ("pe_writes",   "Escrituras (W)"),
        ("pe_adds",     "ADD atomicos"),
        ("pe_subs",     "SUB atomicos"),
    ],
}

all_metrics = [(m, lbl) for grp in GROUPS.values() for m, lbl in grp]
n_metrics   = len(all_metrics)   # 14

# ── Layout: 3 columnas, filas dinamicas por grupo ──────────────────────────
COLS = 3
n_rows_bus  = 2   # 6 metricas / 3 cols
n_rows_lat  = 1   # 3 metricas / 3 cols
n_rows_coh  = 2   # 5 metricas -> ceil(5/3)=2, ultima fila con 2 graficas

total_rows = 1 + n_rows_bus + n_rows_lat + n_rows_coh   # +1 header

FIG_W = 18
FIG_H = 3.8 * (total_rows - 1) + 1.2   # header mas compacto

fig = plt.figure(figsize=(FIG_W, FIG_H), facecolor=BG)

# Usar height_ratios para dar menos espacio al header
height_ratios = [0.35] + [1] * (total_rows - 1)
gs = gridspec.GridSpec(
    total_rows, COLS,
    figure=fig,
    hspace=0.72,
    wspace=0.38,
    left=0.05, right=0.98,
    top=0.96,  bottom=0.06,
    height_ratios=height_ratios,
)

# ── Header ─────────────────────────────────────────────────────────────────
ax_header = fig.add_subplot(gs[0, :])
ax_header.set_axis_off()

ax_header.text(
    0.0, 0.75,
    "Coherence Analytics",
    transform=ax_header.transAxes,
    fontsize=16, fontweight="bold",
    color=FG, fontfamily="monospace",
    va="top",
)
ax_header.text(
    0.0, 0.15,
    f"Traces: {', '.join(traces)}   |   Protocolos: {', '.join(protocols)}   |   {len(df)} runs",
    transform=ax_header.transAxes,
    fontsize=8, color=FG2, fontfamily="monospace",
    va="top",
)

# Leyenda de colores de protocolo
legend_x = 0.68
for i, proto in enumerate(protocols):
    col = proto_color(proto)
    ax_header.add_patch(FancyBboxPatch(
        (legend_x + i * 0.14, 0.3), 0.06, 0.4,
        boxstyle="round,pad=0.01",
        facecolor=col, edgecolor="none",
        transform=ax_header.transAxes, clip_on=False,
    ))
    ax_header.text(
        legend_x + i * 0.14 + 0.075, 0.5,
        proto.upper(),
        transform=ax_header.transAxes,
        fontsize=8, 
        color="white" if col in [BLUE, GREEN] else FG,  # Texto blanco sobre azul/verde
        fontweight="bold",
        va="center", ha="center",
        fontfamily="monospace",
    )

# Separador
separator = Line2D([0, 1], [0, 0], transform=ax_header.transAxes, 
                   color=BORDER, linewidth=1)
ax_header.add_line(separator)

# ── Helper para dibujar una grafica de barras ──────────────────────────────
def draw_bar(ax, metric: str, label: str):
    """Dibuja barras agrupadas por trace, coloreadas por protocolo."""
    x      = np.arange(len(traces))
    n_p    = len(protocols)
    width  = 0.7 / n_p
    offset = -(n_p - 1) * width / 2

    max_val = 0

    for j, proto in enumerate(protocols):
        sub  = df[df["protocol"] == proto]
        vals = []
        for tr in traces:
            row = sub[sub["trace"] == tr]
            v   = float(row[metric].mean()) if not row.empty and metric in row.columns else 0.0
            vals.append(v)
            max_val = max(max_val, v)

        bars = ax.bar(
            x + offset + j * width,
            vals,
            width=width * 0.88,
            color=proto_color(proto),
            alpha=0.85,
            label=proto.upper(),
            zorder=3,
        )

        # Valor encima de cada barra si caben
        for bar, val in zip(bars, vals):
            if val > 0:
                ax.text(
                    bar.get_x() + bar.get_width() / 2,
                    bar.get_height() * 1.02,
                    f"{val:.0f}" if val >= 1 else f"{val:.3f}",
                    ha="center", va="bottom",
                    fontsize=5.5, color=FG2,
                )

    ax.set_title(label, pad=4)
    ax.set_xticks(x)
    ax.set_xticklabels(
        [tr.replace("_", "\n") for tr in traces],
        fontsize=6.5,
    )
    ax.tick_params(axis="x", length=0)
    ax.set_ylim(bottom=0, top=max_val * 1.22 if max_val > 0 else 1)
    ax.yaxis.set_major_formatter(
        plt.FuncFormatter(lambda v, _: f"{v:,.0f}" if v >= 1 else f"{v:.3f}")
    )

# ── Dibujar metricas en el grid ────────────────────────────────────────────
# Mapear cada metrica a su posicion en el grid
positions = []

# Fila 1-2: Bus (6 metricas, 3 cols)
bus_metrics = GROUPS["Trafico de Bus"]
for idx, (m, lbl) in enumerate(bus_metrics):
    row = 1 + idx // COLS
    col = idx % COLS
    positions.append((row, col, m, lbl))

# Fila 3: Latencia (3 metricas, 3 cols)
lat_metrics = GROUPS["Latencia y Ancho de Banda"]
for idx, (m, lbl) in enumerate(lat_metrics):
    row = 1 + n_rows_bus + idx // COLS
    col = idx % COLS
    positions.append((row, col, m, lbl))

# Fila 4-5: Coherencia (5 metricas)
coh_metrics = GROUPS["Coherencia y Operaciones PE"]
for idx, (m, lbl) in enumerate(coh_metrics):
    row = 1 + n_rows_bus + n_rows_lat + idx // COLS
    col = idx % COLS
    positions.append((row, col, m, lbl))

first_ax = None
for row, col, metric, label in positions:
    ax = fig.add_subplot(gs[row, col])
    if first_ax is None:
        first_ax = ax
    if metric in df.columns:
        draw_bar(ax, metric, label)
    else:
        ax.text(0.5, 0.5, f"{metric}\n(sin datos)",
                ha="center", va="center",
                color=FG2, fontsize=8,
                transform=ax.transAxes)
        ax.set_title(label, pad=4)

# Leyenda compartida en la primera grafica
if first_ax:
    first_ax.legend(
        loc="upper right",
        framealpha=0.8,
        handlelength=1.0,
        handletextpad=0.4,
    )

# ── Guardar y mostrar ──────────────────────────────────────────────────────
plt.savefig(OUTPUT_PATH, dpi=150, bbox_inches="tight", facecolor=BG)
print(f"Dashboard guardado en: {OUTPUT_PATH}")
plt.show()