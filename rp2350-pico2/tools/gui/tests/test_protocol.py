import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from protocol import State


class ProtocolTests(unittest.TestCase):
    def test_status(self):
        s = State()
        self.assertTrue(s.ingest("[STATUS] fw=0.2.12 KCX=NO RESPONSE YET BT_MODE=TX BT_LINK=NO devices=0 busy=0"))
        self.assertEqual(s.kcx, "NO RESPONSE YET")
        self.assertEqual(s.link, "NO")
        self.assertTrue(s.ingest("[STATUS] fw=0.2.12 KCX=RESPONDING BT_MODE=TX BT_LINK=YES devices=2 busy=0"))
        self.assertTrue(s.ingest("[BT PEER] name=WH-1000XM3 MAC=6e73594c1838"))
        self.assertEqual(s.peer, "WH-1000XM3")
        s.ingest("[STATUS] fw=0.2.12 KCX=RESPONDING BT_MODE=TX BT_LINK=NO devices=2 busy=0")
        self.assertEqual(s.peer, "")

    def test_auto_and_uart(self):
        s = State()
        s.ingest("[AUTO] ON (PAUSED) last_MAC=deadbeef0001 retries=2 next_ms=150000")
        self.assertEqual((s.auto, s.auto_paused, s.last_mac, s.retries),
                         ("ON", True, "deadbeef0001", 2))
        s.ingest("[UART] UART0 115200 8N1 GP0->KCX RX GP1<-KCX TX rx_bytes=201 lines=7 overflow=0")
        self.assertEqual((s.rx_bytes, s.rx_lines, s.overflow), (201, 7, 0))

    def test_device_list(self):
        s = State()
        s.ingest("[DEVICES] found=2 selected=2 page=1")
        s.ingest("[DEVICES]  1: WH-1000XM3  MAC=6e73594c1838")
        s.ingest("[DEVICES]  2: Speaker B  MAC=001122334455 [SELECTED]")
        self.assertEqual(len(s.devices), 2)
        self.assertTrue(s.devices[2].selected)
        s.ingest("[USB] selected #1 MAC=6e73594c1838")
        self.assertTrue(s.devices[1].selected)
        self.assertFalse(s.devices[2].selected)
        s.ingest("[DEVICES] found=0 selected=0 page=1")
        self.assertFalse(s.devices)


if __name__ == "__main__":
    unittest.main()
