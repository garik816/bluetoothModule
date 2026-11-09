# BluetoothModule — Raspberry Pi Pico 2 (RP2350)

**Гілка:** `hw/rp2350-pico2`. Окремий порт проєкту під звичайну **Raspberry Pi Pico 2 (RP2350A)**. Версія ESP32-S3 у корені репозиторію залишається недоторканою.

## Схема пристрою

```
Магнітола LINE OUT L / R --аналогове аудіо--> KCX_BT_EMITTER v1.7
                                             |
                                             | Bluetooth A2DP Source
                                             v
                                      Навушники / колонка

Raspberry Pi Pico 2 (RP2350A)
    |  UART0 115200 (через узгодження рівнів)
    +------------------------------------------ KCX_BT_EMITTER v1.7
    |
    |  PIO0 + DMA, 8-bit 8080
    +------------------------------------------ FSA506 480x272
    |
    |  GP26 / GP27 ADC1 + GP20 / GP21
    +------------------------------------------ 4-wire резистивний Touch
```

Pico 2 **не виконує кодування Bluetooth-аудіо**: звук іде безпосередньо до KCX, а RP2350 керує пошуком/сполученням та екраном.

## Підключення Pico 2 (GPIO, не фізичний номер контакту)

| FSA506 / Touch / KCX | Raspberry Pi Pico 2 |
|---|---|
| FSA506 DB0...DB7 | GP2 GP3 GP4 GP5 GP6 GP7 GP8 GP9 |
| FSA506 /WR | GP10 (PIO side-set) |
| FSA506 RS/DC | GP11 |
| FSA506 /CS | GP12 |
| FSA506 /RD | GP13, утримується HIGH |
| Touch X1 (FFC30) | GP26 / ADC0 |
| Touch X2 (FFC31) | GP20 |
| Touch Y1 (FFC32) | GP27 / ADC1 |
| Touch Y2 (FFC33) | GP21 |
| KCX RX | GP0 / UART0 TX **через перетворювач рівнів** |
| KCX TX | GP1 / UART0 RX **через перетворювач рівнів** |
| KCX MODE | GP15, HIGH = TX/EMITTER **через перетворювач рівнів** |
| KCX LINK | не підключено; зв’язок перевіряється UART STATUS |
| KCX VCC | згідно з платою KCX (зазвичай 5 В) |
| KCX LINE L/R | лінійний вихід магнітоли |

Розпайка FSA506, 480×272 RGB565, послідовність регістрів, Ampire H525/V286 і схема сенсора базуються на робочій гілці [Silvus Configurator / hw/esp32s3-fsa506](https://github.com/garik816/silvus-configurator/tree/hw/esp32s3-fsa506). **GPIO з ESP32-S3 не переносилися!**

**Безпека:** GPIO RP2350 не є 5-вольт-толерантними. UART/MODE KCX потрібно узгоджувати за напругами, бо ревізії KCX v1.7 можуть мати 5V-логіку. Не подавати на Pico 2 живлення 5 В через GPIO, не під’єднувати вихід автомобільного підсилювача до KCX LINE L/R (потрібен справжній LINE OUT або BTL speaker-level-to-line адаптер). Підсвічування FSA506 і /RESET працюють за штатною схемою самого дисплея, як у Silvus.

## Збірка Pico SDK 2.3.1

Встановіть **Pico SDK 2.3.1**, ARM GNU Toolchain, CMake та Ninja. Це C/C++ проєкт, **без ESP-IDF / Arduino / PlatformIO**.

PowerShell 7:
```powershell
git clone -b hw/rp2350-pico2 https://github.com/garik816/bluetoothModule.git
cd bluetoothModule
$env:PICO_SDK_PATH = "C:\pico\pico-sdk"
cmake -S rp2350-pico2 -B build-pico2 -G Ninja -DPICO_BOARD=pico2
cmake --build build-pico2 --parallel
```

**UF2:** `build-pico2/bluetooth_module_pico2.uf2`. На Pico 2 утримуйте **BOOTSEL** при підключенні USB; скопіюйте UF2 на диск **RP2350**. Через USB CDC прошивка виводить журнал команд `KCX TX`/`KCX RX`, якщо комп’ютер відкриває CDC-порт.

CI: [GitHub Actions — RP2350 Pico 2 UF2](https://github.com/garik816/bluetoothModule/actions/workflows/pico2.yml), артефакт `bluetoothModule-RP2350-Pico2-UF2`.

## Робота з пристроєм

1. Переведіть Bluetooth-навушники в режим сполучення, KCX — у режим EMITTER.
2. При першому запуску натисніть п’ять червоних хрестиків Touch: лівий верхній, правий верхній, правий нижній, лівий нижній, центр. Параметри зберігаються у **останньому 4 КБ секторі Flash**. Збірка повинна вміщуватися без перетину цього сектора.
3. **ПОШУК** — `AT+PAIR`; список `MacAdd:` + `Name:`, 4 елементи на сторінку.
4. Виберіть рядок і натисніть **ПІДКЛ.**: прошивка видаляє попередні адреси з пам’яті KCX (`AT+DELVMLINK`), зберігає вибрану (`AT+ADDLINKADD=<MAC>`) і запускає сполучення.
5. **ВІДКЛ.** надсилає `AT+DISCON`, **ДАЛІ** перемикає сторінку, **CAL** у шапці повторює калібрування.

**Обмеження KCX v1.7:** у протестованих сторонніми авторами ревізіях `AT+SCAN` не працює надійно. Заміна на `AT+PAIR` не гарантує суто пасивного сканування: модуль **може автоматично приєднатися** до першого пристрою. Успішність пошуку та обмеження підключення одним MAC необхідно підтвердити на реальному KCX.

## Примітки щодо реалізації

- LCD 8080: PIO0 SM0, 8 GPIO data + GP10 /WR side-set, DMA для послідовності байтів, GRAM смугами по 8 рядків, без повного framebuffer.
- Touch: ADC0 GP26 та ADC1 GP27, драйвінг X/Y на двох цифрових GPIO, опитування 30 мс.
- Калібрування: останній 4 КБ сектор зовнішньої Flash, CRC32, одноразовий запис після калібрування. Не запускати друге ядро під час операцій із Flash.
- UART: приймання в кільцевий буфер 4096 байтів із IRQ, щоб не губити відповіді KCX під час перемальовування LCD.
- Текст: український інтерфейс, 8×12 піксельний шрифт із Unicode Cyrillic; гліфи на основі [512_8_sans, public domain](https://github.com/alexfru/512_8).
- Аудіо: виключно лінійний стереовхід KCX, жодних аудіоперетворень у RP2350.

**Статус:** перша версія порту; автоматична компіляція й фізичні вимірювання станом на створення цього документа ще мають бути підтверджені. Зокрема перевірити byte order IM4, /WR PIO timing, реальні рівні KCX UART, зберігання калібровки, затримку аудіо.

### Вихідні матеріали

- [Raspberry Pi Pico 2](https://www.raspberrypi.com/products/raspberry-pi-pico-2/)
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
- [Silvus FSA506 та Touch](https://github.com/garik816/silvus-configurator/tree/hw/esp32s3-fsa506)
- [KCX_BT_EMITTER v1.7 serial tests](https://github.com/Mark-MDO47/BluetoothAudioTransmitter_KCX_BT_EMITTER/tree/KCX_BT_EMITTER_V1.7)
