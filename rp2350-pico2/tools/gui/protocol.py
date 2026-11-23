"""Parse Pico 2 USB CDC text reports. No GUI/serial dependencies."""
from dataclasses import dataclass, field
import re


@dataclass
class Device:
    number: int
    name: str
    mac: str
    selected: bool = False


@dataclass
class State:
    firmware: str = "—"
    kcx: str = "UNKNOWN"
    mode: str = "UNKNOWN"
    link: str = "UNKNOWN"
    peer: str = ""
    peer_mac: str = ""
    last_mac: str = "UNKNOWN"
    auto: str = "UNKNOWN"
    auto_paused: bool = False
    retries: int = 0
    rx_bytes: int = 0
    rx_lines: int = 0
    overflow: int = 0
    devices: dict[int, Device] = field(default_factory=dict)

    def ingest(self, line: str) -> bool:
        """Ingest a serial report. Return True when a displayed value changes."""
        m = re.search(
            r"\[STATUS\] fw=(\S+) KCX=(.+?) BT_MODE=(\S+)"
            r" BT_LINK=(\S+) devices=(\d+) busy=(\d+)", line
        )
        if m:
            self.firmware, self.kcx, self.mode, self.link = m.group(1, 2, 3, 4)
            if self.link == "NO":
                self.peer, self.peer_mac = "", ""
            return True
        m = re.search(
            r"\[AUTO\] (ON|OFF)(?: \((PAUSED)\))?"
            r" last_MAC=(\S+) retries=(\d+) next_ms=(\d+)", line
        )
        if m:
            self.auto, self.auto_paused = m.group(1), bool(m.group(2))
            self.last_mac, self.retries = m.group(3), int(m.group(4))
            return True
        m = re.search(r"\[BT PEER\] name=(.*?) MAC=([0-9a-fA-F]{12})", line)
        if m:
            self.peer, self.peer_mac = m.group(1), m.group(2).lower()
            return True
        m = re.search(r"\[UART\].*rx_bytes=(\d+) lines=(\d+) overflow=(\d+)", line)
        if m:
            self.rx_bytes, self.rx_lines, self.overflow = map(int, m.groups())
            return True
        if line.startswith("[DEVICES] found="):
            self.devices.clear()
            return True
        m = re.search(
            r"^\[DEVICES\]\s+(\d+):\s+(.+?)\s+MAC=([0-9a-fA-F]{12})( \[SELECTED\])?\s*$",
            line
        )
        if m:
            i = int(m.group(1))
            self.devices[i] = Device(i, m.group(2), m.group(3).lower(), bool(m.group(4)))
            return True
        m = re.search(r"\[USB\] selected #(\d+) MAC=", line)
        if m:
            number = int(m.group(1))
            for dev in self.devices.values():
                dev.selected = dev.number == number
            return True
        return False
