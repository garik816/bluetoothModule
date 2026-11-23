# universal bluetooth module — RP2350 Pico 2

**Актуальна гілка:** `hw/rp2350-pico2`. **Поточна конфігурація:** Raspberry Pi Pico 2 (RP2350A), FSA506 480×272 (PIO/DMA, 8-bit 8080), KCX_BT_EMITTER v1.7 (UART0 115200).

**Touch і калібрування повністю видалені.** KCX увімкнено з запуску, а режим Bluetooth TX обирається апаратною перемичкою на модулі; GP15 у прошивці не використовується. Функції пошуку та підключення доступні через USB CDC.

Основна інструкція (розпайка 40-контактного дисплея, KCX, логи й команди): **[rp2350-pico2/README.md](rp2350-pico2/README.md)**.

## Основні команди через USB

```powershell
python -m serial.tools.miniterm COM22 115200
```

Введіть `help`, `status`, `ping`, `version`, `scan`, `devices`, `select 1`, `connect CONFIRM`, `drop`, `redraw` (Enter після кожної команди). `scan` може автоматично під'єднати навушники, а `connect CONFIRM` видаляє раніше збережені адреси KCX.

**Прошивка:** [GitHub Actions — RP2350 Pico 2 UF2](https://github.com/garik816/bluetoothModule/actions/workflows/pico2.yml), артефакт `bluetoothModule-RP2350-Pico2-UF2`. Номер версії збільшується при кожній збірці.

Код у [`rp2350-pico2/src/`](rp2350-pico2/src/). Дисплейний драйвер `fsa506.c` та PIO програма `fsa506.pio` залишені без змін від збірки, яка вже показувала зображення. Нова інтеграція KCX вимагає стендової перевірки.
