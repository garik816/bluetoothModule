# universal bluetooth module — Raspberry Pi Pico 2 (RP2350A)

**Гілка:** `hw/rp2350-pico2`. Прошивка керує KCX_BT_EMITTER v1.7 через UART0 та виводить статус на FSA506 480×272. Аудіо передається від лінійного виходу магнітоли до KCX без обробки на Pico.

**Поточний стан:** Touch та калібрування повністю видалені. KCX увімкнено одразу після старту. Режим TX/RX задається апаратною перемичкою на платі KCX, а не GPIO.

## Архітектура

```text
Магнітола LINE OUT (L/R/GND) -> KCX AUDIO_L/AUDIO_R/AGND
KCX_BT_EMITTER v1.7 -> Bluetooth -> навушники/колонка
Raspberry Pi Pico 2 <-> UART0 115200 <-> KCX
Raspberry Pi Pico 2 -> PIO0/DMA/8080 -> FSA506 LCD 480x272
Комп'ютер -> USB CDC COM -> Pico 2 (команди та логи)
```

## KCX_BT_EMITTER v1.7 — підключення

| KCX | Raspberry Pi Pico 2 | Фізичний контакт Pico | Примітка |
|---|---|---:|---|
| +5V | Стабілізовані 5 В | — | Живлення KCX за маркуванням його плати |
| PGND | GND | 3 або інший GND | Спільна цифрова земля |
| RX (вхід KCX) | GP0 / UART0 TX | 1 | Pico передає AT-команди; вихід 3,3 В може вимагати підвищення до 5 В |
| TX (вихід KCX) | GP1 / UART0 RX | 2 | KCX передає відповіді; перевірити рівень та послідовність живлення |
| LINK | — | — | У поточному коді не використовується |
| AUDIO_L | LINE OUT L магнітоли | — | Тільки лінійний аудіосигнал |
| AUDIO_R | LINE OUT R магнітоли | — | Тільки лінійний аудіосигнал |
| AGND | Audio GND магнітоли | — | Без петель землі, за схемою аудіотракту |
| MODE | — | — | **Не використовується. GP15 відключений у коді** |

**Важливо:** на поширених платах KCX v1.7 вибір EMITTER/TX здійснюється перемичкою `TX/RX` на самій платі. Якщо раніше під'єднували GP15 до неперевіреного контакту KCX, від'єднайте цей провід. UART GP0/GP1 не має спільних ліній з LCD.

**Рівні напруги:** цифровий GP1 RP2350 є FT-входом і може приймати 5 В за штатного живлення IOVDD=3,3 В; за вимкненої Pico умови інші. Перед підключенням виміряйте вихід KCX TX. GP0 видає приблизно 3,3 В: якщо KCX RX не розпізнає HIGH, використовуйте перетворювач рівнів або транзисторний каскад. **Не підключайте 5 В до GP26–GP29**.

Не підключайте виходи автомобільного підсилювача безпосередньо до аудіовходів KCX (особливо BTL). Використовуйте LINE OUT або належний BTL-to-line перетворювач.

## Роз'єм дисплея FSA506 (40 контактів)

Тут номер контакту FSA506 не означає номер контакту Pico 2. Контакт 1 необхідно перевірити на реальній платі/FFC; проводка LCD і його PIO/DMA драйвер **не змінювалися** після попередньої збірки, що виводила зображення.

| FSA506 pin | Позначення | Pico 2 | Використання |
|---:|---|---|---|
| 1–2 | GND | GND | Загальна земля дисплея |
| 3 | VCC | — | Живлення панелі за схемою конкретної плати |
| 4–5 | Не позначено | — | Не підключати без перевірки |
| 6 | RS | GP11 / pin 15 | Команда / дані |
| 7 | /CS | GP12 / pin 16 | Chip Select |
| 8 | /WR | GP10 / pin 14 | PIO запис |
| 9 | /RD | GP13 / pin 17 | HIGH / без читання |
| 10 | DB00 | GP2 / pin 4 | D0 |
| 11 | DB01 | GP3 / pin 5 | D1 |
| 12 | DB02 | GP4 / pin 6 | D2 |
| 13 | DB03 | GP5 / pin 7 | D3 |
| 14 | DB04 | GP6 / pin 9 | D4 |
| 15 | DB05 | GP7 / pin 10 | D5 |
| 16 | DB06 | GP8 / pin 11 | D6 |
| 17 | DB07 | GP9 / pin 12 | D7 |
| 18–29 | DB08–DB19 | — | Не використовуються у 8-бітному режимі |
| 30 | TOUCH1 | — | Не використовується |
| 31 | TOUCH3 | — | Не використовується |
| 32 | TOUCH2 | — | Не використовується |
| 33 | TOUCH4 | — | Не використовується |
| 34 | DB24 | — | Точне маркування з вашого роз'єму; не використовується |
| 35–37 | VCC | — | Живлення дисплея; напруга потребує перевірки |
| 38–40 | GND | GND | Земля |

Потрібний апаратний режим інтерфейсу дисплея — **8-bit 8080 / RGB565**. Позначення `VCC` не визначає автоматично безпечну напругу живлення чи струм підсвічування. Touch (30–33) можна фізично залишити непідключеним.

## USB CDC керування і діагностика

Знайти COM-порт у Windows 11:

```powershell
python -m serial.tools.list_ports -v
python -m serial.tools.miniterm COM22 115200
```

COM22 тут приклад із попереднього підключення: після перезавантаження номер може змінитися. Команди вводяться рядками з Enter; вихід із miniterm — `Ctrl+]`.

| Команда | Дія |
|---|---|
| `help` | Список команд |
| `status` | Стан KCX, прийняті байти UART, запит `AT+STATUS?` |
| `ping` | Надіслати `AT+` |
| `version` | Версія прошивки та `AT+GMR?` |
| `rawstatus` | Запросити режим `AT+BT_MODE?` та стан з'єднання |
| `scan` | `AT+PAIR` — пошук / сполучення, **може автоматично під'єднатися** |
| `devices` | Повний список отриманих Bluetooth-пристроїв з номерами та MAC |
| `select N` | Вибрати пристрій за номером (починаючи з 1) |
| `connect CONFIRM` | Видалити збережені KCX адреси, записати вибраний MAC і запустити сполучення |
| `drop` | Відключити поточний Bluetooth-зв'язок |
| `next` | Перемкнути сторінку екрана списку |
| `redraw` | Перемалювати екран FSA506 |

**Увага:** `connect CONFIRM` надсилає `AT+DELVMLINK` і стирає перелік збережених у KCX обраних Bluetooth-пристроїв. Для перевірки UART спочатку використовуйте `status`, `ping`, `version`. `scan` на ревізії v1.7 не гарантовано пасивний: використовує `AT+PAIR`, бо `AT+SCAN` може не працювати.

## Стартовий лог

```text
[BOOT] universal bluetooth module v.0.2.<number>
[BOOT] touch=REMOVED; calibration=REMOVED; existing LCD PIO/DMA driver UNCHANGED
[KCX] UART0 115200 IRQ RX active; GP15 unused, mode set on KCX PCB
KCX TX: AT+
KCX TX: AT+GMR?
KCX RX: ...
[HEARTBEAT] uptime=... touch=OFF KCX=ON rx_bytes=... rx_lines=...
```

Самі логи ініціалізації LCD підтверджують проходження програмного коду, але не підтверджують фізичне відображення. Якщо LCD знову білий — перевірте живлення, налаштування 8-бітного інтерфейсу та шлейф; якщо на COM-логах `KCX RX` немає відповіді — перевірте `+5V`, `PGND`, правильність схрещення TX/RX та логічні рівні.

## Збірка з Pico SDK 2.3.1

```powershell
git clone -b hw/rp2350-pico2 https://github.com/garik816/bluetoothModule.git
cd bluetoothModule
$env:PICO_SDK_PATH = 'C:\pico\pico-sdk'
cmake -S rp2350-pico2 -B build-pico2 -G Ninja -DPICO_BOARD=pico2
cmake --build build-pico2 --parallel
```

Артефакт: `build-pico2/bluetooth_module_pico2.uf2`. Для запису затисніть BOOTSEL під час підключення USB і скопіюйте UF2 на диск RP2350.

**GitHub Actions:** [RP2350 Pico 2 UF2](https://github.com/garik816/bluetoothModule/actions/workflows/pico2.yml), артефакт `bluetoothModule-RP2350-Pico2-UF2` (UF2 + ELF + BIN).

Номер версії генерується автоматично при кожній збірці скриптом `tools/gen_version.py`: у GitHub Actions `0.2.<GITHUB_RUN_NUMBER>`, локально — незалежний лічильник у каталозі збірки. Реальну апаратну роботу KCX необхідно перевірити на стенді.

## Контрольні точки

- [KCX UART v1.7 — команда і практичні випробування](https://github.com/Mark-MDO47/BluetoothAudioTransmitter_KCX_BT_EMITTER/tree/KCX_BT_EMITTER_V1.7)
- [Raspberry Pi Pico 2](https://www.raspberrypi.com/products/raspberry-pi-pico-2/)
- [Pico SDK](https://github.com/raspberrypi/pico-sdk)
