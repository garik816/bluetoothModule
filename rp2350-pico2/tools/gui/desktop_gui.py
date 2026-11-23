#!/usr/bin/env python3
"""Windows GUI for Raspberry Pi Pico 2 / KCX_BT_EMITTER v1.7.

Connect to Pico 2 USB CDC (not the KCX UART). Tkinter UI is only accessed
from the main thread; the serial worker passes complete lines via a queue.
"""
from __future__ import annotations

import queue
import threading
import time
import tkinter as tk
from tkinter import messagebox, scrolledtext, ttk

from protocol import State

BAUD = 115200
BG = "#0e1727"
SURFACE = "#1a293e"
FG = "#edf3fa"
MUTED = "#a0b2c8"
CYAN = "#51d0c1"


class Connection:
    def __init__(self, port: str, inbox: queue.Queue):
        import serial

        self.inbox = inbox
        self.port = port
        self.closed = threading.Event()
        self.lock = threading.Lock()
        self.device = serial.Serial(port, BAUD, timeout=0.15, write_timeout=0.8)
        self.thread = threading.Thread(target=self._reader, name="Pico2-USB", daemon=True)
        self.thread.start()

    def write(self, cmd: str):
        if not cmd or any(c in cmd for c in "\r\n"):
            return
        with self.lock:
            if self.closed.is_set():
                raise RuntimeError("COM-порт закрито")
            self.device.write((cmd + "\n").encode("ascii"))

    def _reader(self):
        pending = bytearray()
        try:
            while not self.closed.is_set():
                pending.extend(self.device.read(512))
                while b"\n" in pending:
                    raw, _, rest = pending.partition(b"\n")
                    pending = bytearray(rest)
                    self.inbox.put((self, "line", raw.rstrip(b"\r").decode("utf-8", "replace")))
                if len(pending) > 8192:
                    pending.clear()
                    self.inbox.put((self, "line", "[GUI] UART рядок понад 8 КБ скинуто"))
        except Exception as exc:
            if not self.closed.is_set():
                self.inbox.put((self, "error", str(exc)))

    def close(self):
        self.closed.set()
        try:
            self.device.close()
        except Exception:
            pass


class GUI:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.conn: Connection | None = None
        self.inbox: queue.Queue = queue.Queue()
        self.state = State()
        self.port = tk.StringVar()
        self.cmd = tk.StringVar()
        self.connection = tk.StringVar(value="НЕ ПІДКЛЮЧЕНО")
        self.labels = {k: tk.StringVar(value="—") for k in
                       ("firmware", "kcx", "mode", "link", "peer", "last_mac", "auto", "retries")}
        self.scan_until = 0.
        self.last_list = 0.
        self.next_status = 0.
        root.title("Universal Bluetooth Module — RP2350 Pico 2")
        root.geometry("1080x760")
        root.minsize(880, 620)
        root.configure(bg=BG)
        self._style()
        self._layout()
        self.refresh_ports()
        root.after(70, self._poll)
        root.after(900, self._periodic)
        root.protocol("WM_DELETE_WINDOW", self._exit)

    def _style(self):
        st = ttk.Style()
        st.theme_use("clam")
        st.configure(".", background=BG, foreground=FG, font=("Segoe UI", 10))
        st.configure("TFrame", background=BG)
        st.configure("TLabel", background=BG, foreground=FG)
        st.configure("TButton", padding=(9, 8), background=SURFACE, foreground=FG)
        st.map("TButton", background=[("active", "#355277")])
        st.configure("Accent.TButton", background="#146d70", foreground="#ffffff")
        st.map("Accent.TButton", background=[("active", "#238f89")])
        st.configure("TCombobox", fieldbackground=SURFACE, foreground=FG, padding=5)
        st.configure("Treeview", rowheight=29, background=SURFACE,
                     fieldbackground=SURFACE, foreground=FG)
        st.map("Treeview", background=[("selected", "#225b64")])
        st.configure("Treeview.Heading", background="#29425b", foreground=FG)

    def _layout(self):
        header = tk.Frame(self.root, bg=BG)
        header.pack(fill="x", padx=19, pady=(17, 10))
        tk.Label(header, text="◉  UNIVERSAL BLUETOOTH MODULE", bg=BG, fg=CYAN,
                 font=("Segoe UI Semibold", 16)).pack(side="left")
        tk.Label(header, text="RP2350 Pico 2  •  KCX v1.7", bg=BG,
                 fg=MUTED).pack(side="right")

        ports = ttk.Frame(self.root)
        ports.pack(fill="x", padx=19, pady=(0, 15))
        ttk.Label(ports, text="USB COM:").pack(side="left", padx=(0, 8))
        self.box = ttk.Combobox(ports, textvariable=self.port, state="readonly", width=17)
        self.box.pack(side="left")
        ttk.Button(ports, text="Оновити порти", command=self.refresh_ports).pack(
            side="left", padx=7)
        self.connect_btn = ttk.Button(ports, text="Підключити",
                                      style="Accent.TButton", command=self.toggle)
        self.connect_btn.pack(side="left", padx=7)
        ttk.Label(ports, textvariable=self.connection, foreground=CYAN).pack(
            side="left", padx=(9, 0))

        body = ttk.Frame(self.root)
        body.pack(fill="both", expand=True, padx=19)
        sidebar = ttk.Frame(body, width=270)
        sidebar.pack(side="left", fill="y", padx=(0, 17))
        sidebar.pack_propagate(False)
        main = ttk.Frame(body)
        main.pack(side="left", fill="both", expand=True)

        ttk.Label(sidebar, text="СТАН BLUETOOTH", foreground=CYAN,
                  font=("Segoe UI Semibold", 11)).pack(anchor="w", pady=(0, 10))
        for text, key in [("Прошивка", "firmware"), ("KCX UART", "kcx"),
                          ("Режим", "mode"), ("З'єднання", "link"),
                          ("Пристрій", "peer"), ("Останній MAC", "last_mac"),
                          ("Автоз'єднання", "auto"), ("Повтори", "retries")]:
            ttk.Label(sidebar, text=text.upper(), foreground=MUTED,
                      font=("Segoe UI", 8)).pack(anchor="w", pady=(5, 2))
            tk.Label(sidebar, textvariable=self.labels[key], bg=BG, fg=FG,
                     font=("Segoe UI Semibold", 10), anchor="w",
                     wraplength=260).pack(fill="x", pady=(0, 5))

        ttk.Label(sidebar, text="КОМАНДИ", foreground=CYAN,
                  font=("Segoe UI Semibold", 11)).pack(anchor="w", pady=(13, 7))
        for text, cmd in [("Оновити стан", "status"), ("Авто УВІМК.", "auto on"),
                          ("Авто ВИМК.", "auto off"), ("Авто ЗАРАЗ", "auto now"),
                          ("Від'єднати / Пауза", "drop"), ("Тест UART", "ping")]:
            ttk.Button(sidebar, text=text, command=lambda c=cmd: self.send(c)).pack(
                fill="x", pady=3)

        row = ttk.Frame(main)
        row.pack(fill="x", pady=(0, 8))
        ttk.Label(row, text="BLUETOOTH-ПРИСТРОЇ", foreground=CYAN,
                  font=("Segoe UI Semibold", 11)).pack(side="left")
        ttk.Button(row, text="Пошук", command=self.scan).pack(side="right", padx=(7, 0))
        ttk.Button(row, text="Оновити список",
                   command=lambda: self.send("devices")).pack(side="right")

        box = ttk.Frame(main)
        box.pack(fill="both", expand=True)
        self.devices = ttk.Treeview(box, show="headings",
                                    columns=("n", "name", "mac"), selectmode="browse", height=10)
        for col, label, width in [("n", "#", 40), ("name", "Назва", 230),
                                  ("mac", "MAC", 140)]:
            self.devices.heading(col, text=label)
            self.devices.column(col, width=width, minwidth=width,
                                anchor="center" if col == "n" else "w")
        self.devices.pack(side="left", fill="both", expand=True)
        scroll = ttk.Scrollbar(box, orient="vertical", command=self.devices.yview)
        scroll.pack(side="right", fill="y")
        self.devices.configure(yscrollcommand=scroll.set)
        self.devices.bind("<Double-1>", lambda _: self.pair())

        actions = ttk.Frame(main)
        actions.pack(fill="x", pady=(7, 15))
        ttk.Button(actions, text="Підключити вибраний",
                   style="Accent.TButton", command=self.pair).pack(side="left", padx=(0, 9))
        ttk.Button(actions, text="Список на LCD",
                   command=lambda: self.send("view list")).pack(side="left", padx=(0, 9))
        ttk.Button(actions, text="Анімація LCD",
                   command=lambda: self.send("view main")).pack(side="left")

        row = ttk.Frame(main)
        row.pack(fill="x", pady=(0, 6))
        ttk.Label(row, text="USB / UART ЛОГ", foreground=CYAN,
                  font=("Segoe UI Semibold", 11)).pack(side="left")
        ttk.Button(row, text="Очистити", command=self.clear_log).pack(side="right")
        self.log = scrolledtext.ScrolledText(
            main, height=11, state="disabled", bg="#07111f", fg="#d3e7f4",
            insertbackground=FG, font=("Consolas", 9), relief="flat")
        self.log.pack(fill="both", expand=True)
        cmdbar = ttk.Frame(main)
        cmdbar.pack(fill="x", pady=(9, 0))
        ttk.Label(cmdbar, text="Команда:").pack(side="left", padx=(0, 7))
        entry = ttk.Entry(cmdbar, textvariable=self.cmd)
        entry.pack(side="left", fill="x", expand=True, padx=(0, 8))
        entry.bind("<Return>", lambda _: self.manual())
        ttk.Button(cmdbar, text="Надіслати", command=self.manual).pack(side="right")

        tk.Label(self.root, bg=BG, fg=MUTED, anchor="w",
                 text="Керування через USB CDC Pico 2. Touch на FSA506 вимкнений.").pack(
            fill="x", padx=19, pady=11)

    def refresh_ports(self):
        try:
            from serial.tools.list_ports import comports
            ports = sorted(p.device for p in comports())
        except ImportError:
            ports = []
        selected = self.port.get()
        self.box.configure(values=ports)
        self.port.set(selected if selected in ports else (ports[0] if ports else ""))

    def toggle(self):
        if self.conn:
            self.disconnect()
            return
        if not self.port.get():
            messagebox.showwarning("COM", "Підключіть Pico 2 та оберіть COM-порт.")
            return
        try:
            self.conn = Connection(self.port.get(), self.inbox)
        except ImportError:
            messagebox.showerror("Залежності", "Встановіть pyserial:\npy -3 -m pip install pyserial")
            return
        except Exception as exc:
            messagebox.showerror("COM-порт", str(exc))
            return
        self.state = State()
        self.render_state()
        self.connect_btn.configure(text="Відключити")
        self.connection.set(f"ПІДКЛЮЧЕНО: {self.port.get()}")
        self.append_log(f"[GUI] Відкрито {self.port.get()} @ {BAUD}")
        self.root.after(650, self.probe)

    def probe(self):
        if self.conn:
            for cmd in ("status", "auto status", "devices", "rawstatus"):
                self.send(cmd)

    def disconnect(self):
        conn, self.conn = self.conn, None
        if conn:
            conn.close()
        self.connect_btn.configure(text="Підключити")
        self.connection.set("НЕ ПІДКЛЮЧЕНО")
        self.scan_until = 0.
        self.append_log("[GUI] Відключено")

    def send(self, cmd: str) -> bool:
        if not self.conn:
            self.append_log(f"[GUI] Немає з'єднання: {cmd}")
            return False
        try:
            self.conn.write(cmd)
            self.append_log(f"> {cmd}")
            return True
        except Exception as exc:
            self.append_log(f"[GUI] Помилка запису: {exc}")
            self.disconnect()
            return False

    def manual(self):
        cmd = self.cmd.get().strip()
        if not cmd:
            return
        try:
            cmd.encode("ascii")
        except UnicodeEncodeError:
            messagebox.showwarning("Команда", "Консоль Pico 2 приймає ASCII-команди.")
            return
        if self.send(cmd):
            self.cmd.set("")

    def scan(self):
        if self.send("scan"):
            self.scan_until = time.monotonic() + 16.

    def pair(self):
        selection = self.devices.selection()
        if not selection:
            messagebox.showinfo("Підключення", "Виберіть Bluetooth-пристрій.")
            return
        num = int(selection[0])
        dev = self.state.devices.get(num)
        if not dev:
            messagebox.showerror("Список", "Список пристроїв застарів. Оновіть.")
            return
        if not messagebox.askyesno(
            "Підтвердження", f"Підключити {dev.name} ({dev.mac})?\n\n"
            "УВАГА: KCX видалить ВСІ раніше збережені Bluetooth-адреси. Продовжити?",
            icon="warning",
        ):
            return
        if self.send(f"select {num}"):
            self.root.after(200, lambda: self.send("connect CONFIRM"))

    def render_state(self):
        s = self.state
        for key, value in {
            "firmware": s.firmware, "kcx": s.kcx, "mode": s.mode,
            "link": "ПІДКЛЮЧЕНО" if s.link == "YES" else
                    "ВІДКЛЮЧЕНО" if s.link == "NO" else "НЕВІДОМО",
            "peer": s.peer or "—", "last_mac": s.last_mac,
            "auto": s.auto + (" (ПАУЗА)" if s.auto_paused else ""),
            "retries": str(s.retries),
        }.items():
            self.labels[key].set(value)
        selected = self.devices.selection()
        selected_id = selected[0] if selected else ""
        for row in self.devices.get_children():
            self.devices.delete(row)
        for number, d in sorted(s.devices.items()):
            ident = str(number)
            self.devices.insert("", "end", iid=ident, values=(number, d.name, d.mac))
            if ident == selected_id or (not selected_id and d.selected):
                self.devices.selection_set(ident)

    def append_log(self, line: str):
        self.log.configure(state="normal")
        self.log.insert("end", line + "\n")
        last = int(self.log.index("end-1c").split(".")[0])
        if last > 900:
            self.log.delete("1.0", f"{last - 600}.0")
        self.log.see("end")
        self.log.configure(state="disabled")

    def clear_log(self):
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")

    def _poll(self):
        for _ in range(280):
            try:
                owner, kind, line = self.inbox.get_nowait()
            except queue.Empty:
                break
            if owner is not self.conn:
                continue
            if kind == "error":
                self.append_log(f"[GUI] Втрачено USB COM: {line}")
                self.disconnect()
            else:
                self.append_log(line)
                if self.state.ingest(line):
                    self.render_state()
        self.root.after(65, self._poll)

    def _periodic(self):
        now = time.monotonic()
        if self.conn:
            if now >= self.next_status:
                self.send("status")
                self.send("auto status")
                self.next_status = now + 6.0
            if self.scan_until and now <= self.scan_until and now - self.last_list >= 2.0:
                self.last_list = now
                self.send("devices")
            if self.scan_until and now > self.scan_until:
                self.scan_until = 0.
                self.send("devices")
        self.root.after(900, self._periodic)

    def _exit(self):
        self.disconnect()
        self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    GUI(root)
    root.mainloop()
