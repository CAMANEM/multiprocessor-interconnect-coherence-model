"""
mp_studio.py  —  CE4302 Multiprocessor Coherence Studio
========================================================
UI para compilar pseudocodigo .txt, generar traces y
ejecutar mp_sim mostrando la salida en consola integrada.

Requiere: PyQt6  (pip install PyQt6)
Ejecutar : python mp_studio.py

Estructura de carpetas esperada (ajustar en Settings si difiere):
  <proyecto>/
    src/build/tracegen    (o tracegen.exe en Windows)
    src/build/mp_sim      (o mp_sim.exe en Windows)
    src/traces/           (directorio de traces)
    tools/                (fuentes .txt)
"""

import os
import sys
import platform
import subprocess
import threading
from pathlib import Path

from PyQt6.QtCore import (Qt, QThread, pyqtSignal, QTimer,
                           QSettings, QSize)
from PyQt6.QtGui  import (QFont, QColor, QPalette, QTextCursor,
                           QSyntaxHighlighter, QTextCharFormat,
                           QFontMetrics, QIcon, QKeySequence,
                           QShortcut)
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QSplitter,
    QVBoxLayout, QHBoxLayout, QGridLayout,
    QLabel, QPushButton, QComboBox, QLineEdit,
    QTextEdit, QPlainTextEdit, QFileDialog,
    QGroupBox, QStatusBar, QTabWidget,
    QCheckBox, QSpinBox, QFrame, QToolBar,
    QMessageBox, QSizePolicy
)

# ---------------------------------------------------------------
#  Colores — tema oscuro estilo terminal
# ---------------------------------------------------------------
C_BG        = "#0d1117"
C_BG2       = "#161b22"
C_BG3       = "#21262d"
C_BORDER    = "#30363d"
C_FG        = "#e6edf3"
C_FG2       = "#8b949e"
C_GREEN     = "#3fb950"
C_RED       = "#f85149"
C_YELLOW    = "#d29922"
C_BLUE      = "#58a6ff"
C_PURPLE    = "#bc8cff"
C_CYAN      = "#39d353"
C_ORANGE    = "#ffa657"
C_ACCENT    = "#1f6feb"

FONT_MONO   = "JetBrains Mono, Consolas, Courier New, monospace"
FONT_UI     = "Segoe UI, SF Pro Display, system-ui, sans-serif"

# ---------------------------------------------------------------
#  Syntax Highlighter para pseudocodigo .txt
# ---------------------------------------------------------------
class CE4302Highlighter(QSyntaxHighlighter):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._rules = []

        def rule(pattern, color, bold=False, italic=False):
            import re
            fmt = QTextCharFormat()
            fmt.setForeground(QColor(color))
            if bold:   fmt.setFontWeight(700)
            if italic: fmt.setFontItalic(True)
            self._rules.append((re.compile(pattern), fmt))

        # Comentarios
        rule(r"#[^\n]*",           C_FG2,    italic=True)
        # Keywords
        rule(r"\b(workload|description|pes|parallel|barrier|repeat)\b",
             C_PURPLE, bold=True)
        # pe keyword
        rule(r"\bpe\b",            C_BLUE,   bold=True)
        # Operaciones
        rule(r"\b(read|write|add|sub|r|w|R|W|ADD|SUB)\b",
             C_ORANGE, bold=True)
        # Numeros hex
        rule(r"0[xX][0-9a-fA-F]+", C_GREEN)
        # Numeros decimales
        rule(r"\b\d+\b",           C_CYAN)
        # Variables $i
        rule(r"\$\w+",             C_YELLOW, bold=True)
        # Strings
        rule(r'"[^"]*"',           C_GREEN)
        # Llaves
        rule(r"[{}()]",            C_FG2)

    def highlightBlock(self, text):
        for pattern, fmt in self._rules:
            for m in pattern.finditer(text):
                self.setFormat(m.start(), m.end() - m.start(), fmt)


# ---------------------------------------------------------------
#  Worker: ejecuta un proceso y emite su stdout linea por linea
# ---------------------------------------------------------------
def _find_msys2_bash() -> str:
    """Busca el bash de MSYS2 en ubicaciones comunes de Windows."""
    candidates = [
        r"C:\msys64\usr\bin\bash.exe",
        r"C:\msys2\usr\bin\bash.exe",
        r"C:\tools\msys64\usr\bin\bash.exe",
    ]
    for c in candidates:
        if Path(c).exists():
            return c
    return ""


def _wrap_cmd_for_windows(cmd: list) -> list:
    """
    En Windows envuelve el comando en bash de MSYS2 para que el exe
    tenga acceso al PATH de MinGW64 y pueda cargar sus DLLs.
    Sin esto, ejecutables compilados con MinGW fallan con -1073741515.
    """
    if platform.system() != "Windows":
        return cmd

    bash = _find_msys2_bash()

    if not bash:
        # Sin bash MSYS2: agregar mingw64/bin al PATH del proceso actual
        mingw_bin = r"C:\msys64\mingw64\bin"
        if Path(mingw_bin).exists():
            os.environ["PATH"] = mingw_bin + os.pathsep + os.environ.get("PATH", "")
        return cmd

    # Convertir ruta Windows a formato MSYS2: C:\foo\bar -> /c/foo/bar
    def to_msys(p: str) -> str:
        try:
            parts = Path(p).parts
            if parts and len(parts[0]) == 3 and parts[0][1] == ":":
                drive = parts[0][0].lower()
                rest  = "/".join(parts[1:]).replace("\\", "/")
                return f"/{drive}/{rest}"
        except Exception:
            pass
        return p.replace("\\", "/")

    # Construir comando para bash: el exe y sus argumentos
    msys_parts = []
    for i, part in enumerate(cmd):
        # Convertir rutas (exe y paths de archivos)
        if i == 0 or Path(part).exists() or part.endswith(".exe") or \
           part.endswith(".trace") or part.endswith(".txt") or \
           part.endswith(".csv"):
            msys_parts.append(f'"{to_msys(part)}"')
        else:
            msys_parts.append(f'"{part}"')

    inner = " ".join(msys_parts)
    return [bash, "--login", "-c",
            f'export PATH="/mingw64/bin:/usr/bin:$PATH"; {inner}']


class ProcessWorker(QThread):
    line_ready   = pyqtSignal(str)
    finished_sig = pyqtSignal(int)   # exit code

    def __init__(self, cmd: list, cwd: str = None):
        super().__init__()
        self.cmd  = cmd
        self.cwd  = cwd
        self._proc = None

    def run(self):
        try:
            cmd = _wrap_cmd_for_windows(self.cmd)
            self._proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                cwd=self.cwd,
                bufsize=1,
                encoding="utf-8",
                errors="replace"
            )
            for line in self._proc.stdout:
                self.line_ready.emit(line.rstrip("\n"))
            self._proc.wait()
            self.finished_sig.emit(self._proc.returncode)
        except FileNotFoundError:
            self.line_ready.emit(
                f"[ERROR] Ejecutable no encontrado: {self.cmd[0]}\n"
                "Verifica las rutas en la pestana Configuracion.")
            self.finished_sig.emit(-1)
        except Exception as e:
            self.line_ready.emit(f"[ERROR] {e}")
            self.finished_sig.emit(-1)

    def kill(self):
        if self._proc:
            self._proc.terminate()


# ---------------------------------------------------------------
#  ConsoleWidget: area de texto con estilo terminal
# ---------------------------------------------------------------
class ConsoleWidget(QPlainTextEdit):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setReadOnly(True)
        self.setFont(QFont("JetBrains Mono, Consolas, Courier New", 11))
        self.setStyleSheet(f"""
            QPlainTextEdit {{
                background-color: {C_BG};
                color: {C_FG};
                border: 1px solid {C_BORDER};
                border-radius: 6px;
                padding: 8px;
                selection-background-color: {C_ACCENT};
            }}
        """)
        self.setMaximumBlockCount(5000)

    def append_line(self, text: str, color: str = None):
        cursor = self.textCursor()
        cursor.movePosition(QTextCursor.MoveOperation.End)
        fmt = QTextCharFormat()
        if color:
            fmt.setForeground(QColor(color))
        cursor.setCharFormat(fmt)
        cursor.insertText(text + "\n")
        self.setTextCursor(cursor)
        self.ensureCursorVisible()

    def append_ok(self, text):   self.append_line(text, C_GREEN)
    def append_err(self, text):  self.append_line(text, C_RED)
    def append_info(self, text): self.append_line(text, C_BLUE)
    def append_warn(self, text): self.append_line(text, C_YELLOW)

    def clear_console(self):
        self.clear()


# ---------------------------------------------------------------
#  Editor de pseudocodigo con numeros de linea
# ---------------------------------------------------------------
class CodeEditor(QPlainTextEdit):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFont(QFont("JetBrains Mono, Consolas, Courier New", 12))
        self.setTabStopDistance(
            QFontMetrics(self.font()).horizontalAdvance(" ") * 4)
        self.setStyleSheet(f"""
            QPlainTextEdit {{
                background-color: {C_BG2};
                color: {C_FG};
                border: 1px solid {C_BORDER};
                border-radius: 6px;
                padding: 8px;
                selection-background-color: {C_ACCENT};
            }}
        """)
        self._highlighter = CE4302Highlighter(self.document())
        self.setPlaceholderText(
            "# Escribe pseudocodigo CE4302 aqui...\n"
            "# Ejemplo:\n"
            "# workload \"mi_programa\"\n"
            "# pe(0): write 0x8000 100\n"
            "# pe(1): add   0x8000 10\n"
            "# pe(2): read  0x8000\n"
        )


# ---------------------------------------------------------------
#  Panel de configuracion de rutas
# ---------------------------------------------------------------
class PathSettings(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._settings = QSettings("CE4302", "MPStudio")
        self._build()
        self._load()

    def _build(self):
        lay = QGridLayout(self)
        lay.setSpacing(6)

        def row(label, attr, browse_fn):
            lbl = QLabel(label)
            lbl.setStyleSheet(f"color: {C_FG2}; font-size: 12px;")
            edit = QLineEdit()
            edit.setStyleSheet(self._input_style())
            btn = QPushButton("...")
            btn.setFixedWidth(32)
            btn.setStyleSheet(self._btn_style())
            btn.clicked.connect(browse_fn)
            setattr(self, attr, edit)
            return lbl, edit, btn

        lbl1, self.tracegen_path, btn1 = row(
            "tracegen:", "_te", self._browse_tracegen)
        lbl2, self.mpsim_path,   btn2 = row(
            "mp_sim:",   "_ms", self._browse_mpsim)
        lbl3, self.traces_dir,   btn3 = row(
            "traces/:",  "_td", self._browse_traces_dir)

        for i, (l, e, b) in enumerate(
                [(lbl1, self.tracegen_path, btn1),
                 (lbl2, self.mpsim_path,   btn2),
                 (lbl3, self.traces_dir,   btn3)]):
            lay.addWidget(l, i, 0)
            lay.addWidget(e, i, 1)
            lay.addWidget(b, i, 2)

    def _input_style(self):
        return f"""
            QLineEdit {{
                background: {C_BG3};
                color: {C_FG};
                border: 1px solid {C_BORDER};
                border-radius: 4px;
                padding: 4px 8px;
                font-size: 12px;
                font-family: Consolas, monospace;
            }}
        """

    def _btn_style(self):
        return f"""
            QPushButton {{
                background: {C_BG3};
                color: {C_FG2};
                border: 1px solid {C_BORDER};
                border-radius: 4px;
                font-size: 14px;
            }}
            QPushButton:hover {{ background: {C_ACCENT}; color: white; }}
        """

    def _browse_tracegen(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Seleccionar tracegen")
        if path: self.tracegen_path.setText(path)

    def _browse_mpsim(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Seleccionar mp_sim")
        if path: self.mpsim_path.setText(path)

    def _browse_traces_dir(self):
        path = QFileDialog.getExistingDirectory(
            self, "Seleccionar directorio de traces")
        if path: self.traces_dir.setText(path)

    def _load(self):
        exe = ".exe" if platform.system() == "Windows" else ""
        self.tracegen_path.setText(
            self._settings.value("tracegen",
                str(Path("src/build") / f"tracegen{exe}")))
        self.mpsim_path.setText(
            self._settings.value("mpsim",
                str(Path("src/build") / f"mp_sim{exe}")))
        self.traces_dir.setText(
            self._settings.value("traces_dir", "src/traces"))

    def save(self):
        self._settings.setValue("tracegen",   self.tracegen_path.text())
        self._settings.setValue("mpsim",      self.mpsim_path.text())
        self._settings.setValue("traces_dir", self.traces_dir.text())

    def get(self):
        return {
            "tracegen":   self.tracegen_path.text(),
            "mpsim":      self.mpsim_path.text(),
            "traces_dir": self.traces_dir.text(),
        }


# ---------------------------------------------------------------
#  Ventana principal
# ---------------------------------------------------------------
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("CE4302 — Multiprocessor Coherence Studio")
        self.resize(1400, 860)
        self._worker = None
        self._apply_theme()
        self._build_ui()
        self._connect_signals()

    # ── Tema ────────────────────────────────────────────────────
    def _apply_theme(self):
        self.setStyleSheet(f"""
            QMainWindow, QWidget {{
                background-color: {C_BG};
                color: {C_FG};
                font-family: {FONT_UI};
            }}
            QSplitter::handle {{
                background: {C_BORDER};
                width: 2px;
                height: 2px;
            }}
            QTabWidget::pane {{
                border: 1px solid {C_BORDER};
                border-radius: 6px;
                background: {C_BG2};
            }}
            QTabBar::tab {{
                background: {C_BG3};
                color: {C_FG2};
                border: 1px solid {C_BORDER};
                border-bottom: none;
                border-radius: 4px 4px 0 0;
                padding: 6px 16px;
                margin-right: 2px;
                font-size: 13px;
            }}
            QTabBar::tab:selected {{
                background: {C_BG2};
                color: {C_FG};
                border-bottom: 2px solid {C_ACCENT};
            }}
            QGroupBox {{
                border: 1px solid {C_BORDER};
                border-radius: 6px;
                margin-top: 8px;
                padding-top: 8px;
                font-size: 12px;
                color: {C_FG2};
            }}
            QGroupBox::title {{
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 4px;
                color: {C_FG2};
            }}
            QComboBox {{
                background: {C_BG3};
                color: {C_FG};
                border: 1px solid {C_BORDER};
                border-radius: 4px;
                padding: 5px 10px;
                font-size: 13px;
                min-width: 120px;
            }}
            QComboBox::drop-down {{ border: none; }}
            QComboBox QAbstractItemView {{
                background: {C_BG3};
                color: {C_FG};
                border: 1px solid {C_BORDER};
                selection-background-color: {C_ACCENT};
            }}
            QScrollBar:vertical {{
                background: {C_BG2};
                width: 8px;
                border-radius: 4px;
            }}
            QScrollBar::handle:vertical {{
                background: {C_BORDER};
                border-radius: 4px;
                min-height: 20px;
            }}
            QScrollBar::handle:vertical:hover {{
                background: {C_FG2};
            }}
            QStatusBar {{
                background: {C_BG2};
                color: {C_FG2};
                border-top: 1px solid {C_BORDER};
                font-size: 12px;
            }}
            QLabel {{ color: {C_FG}; }}
        """)

    # ── Layout principal ─────────────────────────────────────────
    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # Toolbar superior
        root.addWidget(self._build_toolbar())

        # Splitter principal: editor | panel derecho
        main_split = QSplitter(Qt.Orientation.Horizontal)
        main_split.setChildrenCollapsible(False)

        main_split.addWidget(self._build_left_panel())
        main_split.addWidget(self._build_right_panel())
        main_split.setSizes([560, 840])

        root.addWidget(main_split, 1)

        # Status bar
        self.status = QStatusBar()
        self.setStatusBar(self.status)
        self._set_status("Listo")

    # ── Toolbar ─────────────────────────────────────────────────
    def _build_toolbar(self):
        bar = QFrame()
        bar.setFixedHeight(52)
        bar.setStyleSheet(f"""
            QFrame {{
                background: {C_BG2};
                border-bottom: 1px solid {C_BORDER};
            }}
        """)
        lay = QHBoxLayout(bar)
        lay.setContentsMargins(12, 0, 12, 0)
        lay.setSpacing(8)

        # Logo / titulo
        title = QLabel("CE4302  ·  Coherence Studio")
        title.setStyleSheet(f"""
            font-size: 15px;
            font-weight: 700;
            color: {C_FG};
            letter-spacing: 0.5px;
        """)
        lay.addWidget(title)
        lay.addStretch()

        # Botones principales
        self.btn_compile = self._toolbar_btn(
            "⚙  Compilar", C_PURPLE,
            "Compilar pseudocodigo .txt a .trace (tracegen)")
        self.btn_run = self._toolbar_btn(
            "▶  Ejecutar", C_GREEN,
            "Ejecutar mp_sim con el trace y protocolo seleccionados")
        self.btn_stop = self._toolbar_btn(
            "■  Detener", C_RED,
            "Detener la simulacion en curso")
        self.btn_clear = self._toolbar_btn(
            "⌫  Limpiar", C_FG2,
            "Limpiar la consola")

        self.btn_stop.setEnabled(False)

        for btn in [self.btn_compile, self.btn_run,
                    self.btn_stop, self.btn_clear]:
            lay.addWidget(btn)

        return bar

    def _toolbar_btn(self, text, color, tooltip=""):
        btn = QPushButton(text)
        btn.setFixedHeight(34)
        btn.setMinimumWidth(110)
        btn.setToolTip(tooltip)
        btn.setStyleSheet(f"""
            QPushButton {{
                background: transparent;
                color: {color};
                border: 1px solid {color};
                border-radius: 6px;
                padding: 0 16px;
                font-size: 13px;
                font-weight: 600;
            }}
            QPushButton:hover {{
                background: {color};
                color: {C_BG};
            }}
            QPushButton:disabled {{
                border-color: {C_BORDER};
                color: {C_FG2};
            }}
        """)
        return btn

    # ── Panel izquierdo: editor + configuracion ──────────────────
    def _build_left_panel(self):
        panel = QWidget()
        lay = QVBoxLayout(panel)
        lay.setContentsMargins(8, 8, 4, 8)
        lay.setSpacing(8)

        # Tabs: Editor | Configuracion
        tabs = QTabWidget()

        # Tab Editor
        editor_tab = QWidget()
        et_lay = QVBoxLayout(editor_tab)
        et_lay.setContentsMargins(0, 4, 0, 0)
        et_lay.setSpacing(6)

        # Barra de archivo
        file_bar = QHBoxLayout()
        self.file_label = QLabel("Sin archivo")
        self.file_label.setStyleSheet(
            f"color: {C_FG2}; font-size: 12px; font-family: Consolas;")
        btn_open = self._small_btn("Abrir", C_BLUE)
        btn_save = self._small_btn("Guardar", C_CYAN)
        btn_open.clicked.connect(self._open_file)
        btn_save.clicked.connect(self._save_file)
        file_bar.addWidget(self.file_label, 1)
        file_bar.addWidget(btn_open)
        file_bar.addWidget(btn_save)
        et_lay.addLayout(file_bar)

        self.editor = CodeEditor()
        self.editor.setPlainText(self._default_program())
        et_lay.addWidget(self.editor, 1)

        tabs.addTab(editor_tab, "📝  Editor")

        # Tab Configuracion
        config_tab = QWidget()
        ct_lay = QVBoxLayout(config_tab)
        ct_lay.setContentsMargins(8, 8, 8, 8)
        ct_lay.setSpacing(12)

        paths_box = QGroupBox("Rutas de ejecutables")
        pb_lay = QVBoxLayout(paths_box)
        self.path_settings = PathSettings()
        pb_lay.addWidget(self.path_settings)
        btn_save_cfg = self._small_btn("Guardar configuracion", C_GREEN)
        btn_save_cfg.clicked.connect(self._save_config)
        pb_lay.addWidget(btn_save_cfg)
        ct_lay.addWidget(paths_box)
        ct_lay.addStretch()

        tabs.addTab(config_tab, "⚙  Configuracion")

        lay.addWidget(tabs)
        return panel

    # ── Panel derecho: controles + consola ──────────────────────
    def _build_right_panel(self):
        panel = QWidget()
        lay = QVBoxLayout(panel)
        lay.setContentsMargins(4, 8, 8, 8)
        lay.setSpacing(8)

        # Controles de simulacion
        ctrl = QGroupBox("Simulacion")
        ctrl_lay = QGridLayout(ctrl)
        ctrl_lay.setSpacing(8)

        # Trace
        ctrl_lay.addWidget(
            self._label("Trace:"), 0, 0)
        self.trace_combo = QComboBox()
        self.trace_combo.setEditable(True)
        self.trace_combo.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        btn_refresh = self._small_btn("↺", C_FG2)
        btn_refresh.setFixedWidth(30)
        btn_refresh.setToolTip("Recargar lista de traces")
        btn_refresh.clicked.connect(self._refresh_traces)
        t_lay = QHBoxLayout()
        t_lay.addWidget(self.trace_combo, 1)
        t_lay.addWidget(btn_refresh)
        ctrl_lay.addLayout(t_lay, 0, 1, 1, 2)

        # Protocolo
        ctrl_lay.addWidget(self._label("Protocolo:"), 1, 0)
        self.proto_combo = QComboBox()
        self.proto_combo.addItems(["msi", "firefly"])
        ctrl_lay.addWidget(self.proto_combo, 1, 1)

        # CSV output
        ctrl_lay.addWidget(self._label("CSV:"), 2, 0)
        self.csv_check = QCheckBox("Guardar estadisticas")
        self.csv_check.setStyleSheet(f"color: {C_FG};")
        self.csv_path = QLineEdit("resultados.csv")
        self.csv_path.setStyleSheet(self._input_style())
        self.csv_path.setEnabled(False)
        self.csv_check.toggled.connect(self.csv_path.setEnabled)
        ctrl_lay.addWidget(self.csv_check, 2, 1)
        ctrl_lay.addWidget(self.csv_path, 2, 2)

        # Log level
        ctrl_lay.addWidget(self._label("Log:"), 3, 0)
        self.log_combo = QComboBox()
        self.log_combo.addItems(["warn", "info", "debug", "error"])
        ctrl_lay.addWidget(self.log_combo, 3, 1)

        lay.addWidget(ctrl)

        # Output del trace compilado
        trace_box = QGroupBox("Trace generado (preview)")
        tb_lay = QVBoxLayout(trace_box)
        self.trace_preview = ConsoleWidget()
        self.trace_preview.setMaximumHeight(160)
        tb_lay.addWidget(self.trace_preview)
        lay.addWidget(trace_box)

        # Consola principal
        console_box = QGroupBox("Consola — Salida de mp_sim")
        cb_lay = QVBoxLayout(console_box)
        self.console = ConsoleWidget()
        cb_lay.addWidget(self.console)
        lay.addWidget(console_box, 1)

        # Cargar traces al arrancar
        self._refresh_traces()

        return panel

    # ── Helpers de widgets ──────────────────────────────────────
    def _label(self, text):
        lbl = QLabel(text)
        lbl.setStyleSheet(f"color: {C_FG2}; font-size: 12px;")
        return lbl

    def _small_btn(self, text, color):
        btn = QPushButton(text)
        btn.setFixedHeight(28)
        btn.setStyleSheet(f"""
            QPushButton {{
                background: transparent;
                color: {color};
                border: 1px solid {color};
                border-radius: 4px;
                padding: 0 10px;
                font-size: 12px;
            }}
            QPushButton:hover {{
                background: {color};
                color: {C_BG};
            }}
        """)
        return btn

    def _input_style(self):
        return f"""
            QLineEdit {{
                background: {C_BG3};
                color: {C_FG};
                border: 1px solid {C_BORDER};
                border-radius: 4px;
                padding: 4px 8px;
                font-size: 12px;
            }}
        """

    # ── Signals ─────────────────────────────────────────────────
    def _connect_signals(self):
        self.btn_compile.clicked.connect(self._do_compile)
        self.btn_run.clicked.connect(self._do_run)
        self.btn_stop.clicked.connect(self._do_stop)
        self.btn_clear.clicked.connect(self.console.clear_console)

    # ── Acciones ─────────────────────────────────────────────────
    def _do_compile(self):
        """Compila el pseudocodigo del editor a un .trace via tracegen."""
        paths = self.path_settings.get()
        tracegen = paths["tracegen"]
        traces_dir = paths["traces_dir"]

        if not Path(tracegen).exists():
            self._error(f"tracegen no encontrado: {tracegen}")
            return

        # Guardar el pseudocodigo en un temp .txt
        os.makedirs(traces_dir, exist_ok=True)
        src_text = self.editor.toPlainText().strip()
        if not src_text:
            self._error("El editor esta vacio.")
            return

        # Extraer nombre del workload del codigo
        wl_name = "programa"
        for line in src_text.splitlines():
            line = line.strip()
            if line.startswith("workload"):
                parts = line.split('"')
                if len(parts) >= 2:
                    wl_name = parts[1].replace(" ", "_")
                break

        ce4302_path = str(Path(traces_dir) / f"{wl_name}.txt")
        trace_path  = str(Path(traces_dir) / f"{wl_name}.trace")

        with open(ce4302_path, "w", encoding="utf-8") as f:
            f.write(src_text)

        self.trace_preview.clear_console()
        self.trace_preview.append_info(
            f"Compilando {ce4302_path} -> {trace_path} ...")

        cmd = [tracegen, "--compile", ce4302_path, "--output", trace_path]
        self._run_process(
            cmd,
            on_line=self._on_compile_line,
            on_done=lambda rc: self._on_compile_done(rc, trace_path, wl_name),
            use_console=False)

    def _on_compile_line(self, line: str):
        color = C_RED if "error" in line.lower() else C_FG2
        self.trace_preview.append_line(line, color)

    def _on_compile_done(self, rc: int, trace_path: str, wl_name: str):
        if rc == 0:
            self.trace_preview.append_ok(f"✓ Trace generado exitosamente")
            # Mostrar contenido del trace
            try:
                content = Path(trace_path).read_text(encoding="utf-8")
                for line in content.splitlines()[:40]:
                    c = C_FG2 if line.startswith("#") else C_FG
                    self.trace_preview.append_line(line, c)
                if content.count("\n") > 40:
                    self.trace_preview.append_line("... (truncado)", C_FG2)
            except Exception:
                pass
            # Actualizar combo de traces y seleccionar el nuevo
            self._refresh_traces()
            for i in range(self.trace_combo.count()):
                if wl_name in self.trace_combo.itemText(i):
                    self.trace_combo.setCurrentIndex(i)
                    break
            self._set_status(f"Compilado: {wl_name}.trace", C_GREEN)
        else:
            self.trace_preview.append_err(f"✗ Error en compilacion (code {rc})")
            self._set_status("Error de compilacion", C_RED)

    def _do_run(self):
        """Ejecuta mp_sim con el trace y protocolo seleccionados."""
        paths = self.path_settings.get()
        mpsim   = paths["mpsim"]
        traces_dir = paths["traces_dir"]

        if not Path(mpsim).exists():
            self._error(f"mp_sim no encontrado: {mpsim}")
            return

        # Resolver trace seleccionado
        trace_text = self.trace_combo.currentText().strip()
        if not trace_text:
            self._error("Selecciona un trace para ejecutar.")
            return

        trace_path = trace_text
        if not Path(trace_path).is_absolute():
            trace_path = str(Path(traces_dir) / trace_text)
        if not trace_path.endswith(".trace"):
            trace_path += ".trace"

        if not Path(trace_path).exists():
            self._error(f"Trace no encontrado: {trace_path}")
            return

        proto     = self.proto_combo.currentText()
        log_level = self.log_combo.currentText()

        cmd = [mpsim,
               "--trace",    trace_path,
               "--protocol", proto,
               "--log-level", log_level]

        if self.csv_check.isChecked() and self.csv_path.text().strip():
            cmd += ["--csv", self.csv_path.text().strip()]

        self.console.clear_console()
        self.console.append_info(
            f"$ {' '.join(cmd)}\n")
        self._set_status(f"Ejecutando: {Path(trace_path).name} [{proto}]",
                         C_YELLOW)
        self.btn_run.setEnabled(False)
        self.btn_compile.setEnabled(False)
        self.btn_stop.setEnabled(True)

        self._run_process(cmd,
                          on_line=self._on_sim_line,
                          on_done=self._on_sim_done)

    def _on_sim_line(self, line: str):
        """Colorea la salida de mp_sim segun el contenido."""
        lo = line.lower()
        if any(k in lo for k in ["error", "fallo", "[error"]):
            self.console.append_err(line)
        elif any(k in lo for k in ["miss ", "miss\t"]):
            self.console.append_warn(line)
        elif any(k in lo for k in ["hit ", "hit\t"]):
            self.console.append_ok(line)
        elif line.startswith("[pe-"):
            self.console.append_line(line, C_BLUE)
        elif line.startswith("  [mem"):
            self.console.append_line(line, C_PURPLE)
        elif line.startswith("[t="):
            self.console.append_line(line, C_FG2)
        elif line.startswith("bus_txns="):
            self.console.append_line("\n" + line, C_CYAN)
        elif line.startswith("---"):
            self.console.append_line(line, C_BORDER)
        elif line.startswith("csv actualizado") or "csv" in lo:
            self.console.append_ok(line)
        else:
            self.console.append_line(line)

    def _on_sim_done(self, rc: int):
        self.btn_run.setEnabled(True)
        self.btn_compile.setEnabled(True)
        self.btn_stop.setEnabled(False)
        if rc == 0:
            self.console.append_ok("\n✓ Simulacion completada exitosamente")
            self._set_status("Simulacion completada", C_GREEN)
        elif rc == -1:
            self._set_status("Error al ejecutar mp_sim", C_RED)
        else:
            self.console.append_err(f"\n✗ mp_sim termino con codigo {rc}")
            self._set_status(f"mp_sim: error code {rc}", C_RED)

    def _do_stop(self):
        if self._worker:
            self._worker.kill()
            self.console.append_warn("\n[Simulacion detenida por el usuario]")
            self.btn_run.setEnabled(True)
            self.btn_compile.setEnabled(True)
            self.btn_stop.setEnabled(False)
            self._set_status("Detenido", C_YELLOW)

    # ── Proceso generico ─────────────────────────────────────────
    def _run_process(self, cmd, on_line, on_done, use_console=True):
        self._worker = ProcessWorker(cmd)
        self._worker.line_ready.connect(on_line)
        self._worker.finished_sig.connect(on_done)
        self._worker.start()

    # ── Traces ──────────────────────────────────────────────────
    def _refresh_traces(self):
        traces_dir = self.path_settings.get()["traces_dir"]
        self.trace_combo.clear()
        p = Path(traces_dir)
        if p.exists():
            traces = sorted(p.glob("*.trace"))
            for t in traces:
                self.trace_combo.addItem(t.name.replace(".trace", ""))
        if self.trace_combo.count() == 0:
            self.trace_combo.addItem("(sin traces — compilar primero)")

    # ── Archivo ─────────────────────────────────────────────────
    def _open_file(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Abrir pseudocodigo", "",
            "Programas CE4302 (*.txt);;Todos (*)")
        if path:
            self.editor.setPlainText(
                Path(path).read_text(encoding="utf-8"))
            self.file_label.setText(Path(path).name)
            self._set_status(f"Abierto: {path}")

    def _save_file(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "Guardar pseudocodigo", "",
            "Programas CE4302 (*.txt);;Todos (*)")
        if path:
            Path(path).write_text(
                self.editor.toPlainText(), encoding="utf-8")
            self.file_label.setText(Path(path).name)
            self._set_status(f"Guardado: {path}", C_GREEN)

    def _save_config(self):
        self.path_settings.save()
        self._set_status("Configuracion guardada", C_GREEN)
        self._refresh_traces()

    # ── Utilidades ──────────────────────────────────────────────
    def _set_status(self, msg: str, color: str = None):
        self.status.showMessage(msg)
        if color:
            self.status.setStyleSheet(
                f"QStatusBar {{ color: {color}; }}")
        else:
            self.status.setStyleSheet(
                f"QStatusBar {{ color: {C_FG2}; }}")

    def _error(self, msg: str):
        self.console.append_err(f"[ERROR] {msg}")
        self._set_status(msg, C_RED)

    def _default_program(self):
        return """\
# CE4302 — Pseudocodigo de ejemplo
# Modifica este programa y presiona "Compilar"

workload "contador_compartido"
description "Contador modificado atomicamente con ADD/SUB"

pes 4

# PE0 inicializa el contador
pe(0): write 0x8000 100

# PE1 incrementa, PE2 decrementa
pe(1): add 0x8000 10
pe(2): sub 0x8000 5

# PE3 verifica el resultado
pe(3): read 0x8000

barrier

# Segunda ronda
parallel {
    pe(0): add 0x8000 1
    pe(2): write 0x9000 50
}
parallel {
    pe(1): sub 0x8000 3
    pe(3): read 0x9000
}
"""

    def closeEvent(self, event):
        if self._worker and self._worker.isRunning():
            self._worker.kill()
        self.path_settings.save()
        event.accept()


# ---------------------------------------------------------------
#  Entry point
# ---------------------------------------------------------------
def main():
    app = QApplication(sys.argv)
    app.setApplicationName("CE4302 Coherence Studio")
    app.setOrganizationName("CE4302")

    # Fuente global
    font = QFont()
    font.setFamily("Segoe UI, SF Pro Display, system-ui")
    font.setPointSize(10)
    app.setFont(font)

    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()