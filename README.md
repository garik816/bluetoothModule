# Bluetooth Audio для магнітоли

Два мікроконтролери: **ESP32-S3-N16R8** керує сенсорним дисплеєм **FSA506 480×272**, а звичайний **ESP32 (WROOM-32/DevKit V1)** передає стереозвук на Bluetooth-навушники/колонки через **Classic Bluetooth A2DP Source (SBC)**.

> **ESP32-S3 не підтримує Bluetooth Classic/A2DP.** Обидві прошивки потрібні. Не плутайте A2DP *Source* (передавач) з *Sink* (приймач).

## Можливості прототипу

- Пошук доступних Bluetooth-пристроїв і список на дисплеї; обрати пристрій дотиком, підключитися або відключитися.
- Стан **SCAN / CONNECTING / CONNECTED / PLAYING**.
- Сторінки списку та 5-точкова калібровка резистивного Touch зі збереженням у NVS.
- Аудіо з **лінійного стереовиходу** магнітоли через зовнішній **I²S ADC** в A2DP Source.
- Незалежні прошивки ESP-IDF 5.5.x, журнал USB/UART та збірка GitHub Actions.

**Статус:** початкова інженерна реалізація. Драйвери FSA506 і Touch адаптовано з гілки [Silvus Configurator: hw/esp32s3-fsa506](https://github.com/garik816/silvus-configurator/tree/hw/esp32s3-fsa506). Аудіотракт, сполучення та UX ще **не перевірені на зібраному обладнанні**; завершення CI-компіляції саме по собі не доводить відтворення звуку.

## Схема

```text
Магнітола LINE OUT L/R
         |
         v
  Стерео I²S ADC (PCM1808, 44.1 кГц)
         | BCK / WS / DATA / MCLK
         v
   ESP32 WROOM-32 -- Bluetooth Classic A2DP --> Навушники/колонка
         ^
         | UART (3.3 V TTL, перехресні TX/RX + GND)
         v
    ESP32-S3 ---- i80 8-bit ---- FSA506 480x272 + 4-wire Touch
```

**Не підключати виходи підсилювача/динаміків напряму до ADC!** Для них потрібен окремий ізольований/узгоджений перетворювач speaker-level → line-level, з урахуванням мостового (BTL) виходу. Усі логічні сигнали — 3,3 В.

## Підключення

### ESP32-S3 — FSA506 (робочі GPIO з Silvus)

| Сигнал | GPIO S3 |
|---|---:|
| DB0…DB7 | 14, 15, 16, 17, 18, 21, 38, 39 |
| RS/DC | 40 |
| /WR | 41 |
| /CS | 47 |
| /RD (постійно HIGH) | 42 |
| Touch X1, X2, Y1, Y2 (ADC1) | 1, 2, 7, 8 |
| UART1 TX → ESP32 RX | 4 |
| UART1 RX ← ESP32 TX | 5 |
| GND | спільна земля |

FSA506 **/RESET** залишено на апаратній POR-схемі (як у Silvus); підсвічування живиться штатно. GPIO19/20 залишені під рідний USB, GPIO43/44 — UART0.

### Звичайний ESP32 — I²S ADC та зв’язок

| Сигнал | GPIO ESP32 |
|---|---:|
| UART2 RX ← S3 TX4 | 16 |
| UART2 TX → S3 RX5 | 17 |
| I²S BCLK → ADC | 26 |
| I²S LRCK/WS → ADC | 25 |
| I²S SDOUT ← ADC | 22 |
| I²S MCLK/SCKI → ADC | 0 |
| GND | спільна земля |

**GPIO0 — strapping/BOOT!** Лише високoімпедансний вхід тактування ADC, що не тягне GPIO0 до низького рівня під час старту. Використовуйте PCM1808/інший I²S АЦП з правильною конфігурацією **44,1 кГц, stereo, Philips I²S, 24 біти в 32-бітному слоті, MCLK=256×Fs (11,2896 МГц)**. Конкретна плата ADC може мати власний генератор, режими master/slave та іншу напругу живлення — перевірте її документацію. Не підключайте вихід навушників із великою амплітудою без узгодження рівнів.

## Збірка та прошивання (ESP-IDF 5.5.x, Windows PowerShell)

```powershell
git clone https://github.com/garik816/bluetoothModule.git
cd bluetoothModule

cd display-s3
idf.py set-target esp32s3
idf.py build
idf.py -p COM12 flash monitor

cd ..\a2dp-esp32
idf.py set-target esp32
idf.py build
idf.py -p COM13 flash monitor
```

Змініть COM-порти під ваші плати. **PlatformIO не використовується.** Прошивка `display-s3` спочатку попросить натиснути 5 міток для калібровки, далі відкриє список пристроїв. Навушники переведіть у pairing/discovery mode, натисніть **SCAN**, оберіть пристрій і **CONNECT**.

## Зв'язок UART

115200 8N1, ASCII, `\n` як завершення; приклади:

```text
S3 -> ESP32: STATUS
ESP32 -> S3: READY
S3 -> ESP32: SCAN
ESP32 -> S3: DEVICE 11:22:33:44:55:66 -48 Headphones
ESP32 -> S3: SCAN_DONE
S3 -> ESP32: CONNECT 11:22:33:44:55:66
ESP32 -> S3: STATE CONNECTING
ESP32 -> S3: STATE PLAYING
S3 -> ESP32: DISCONNECT
ESP32 -> S3: STATE IDLE
```

Протокол описано в [docs/PROTOCOL.md](docs/PROTOCOL.md). Вбудований BLE ESP32-S3 **не використовується для аудіо**.

## План валідації

1. Увімкнути обидві плати, перевірити на екрані `READY` і збереження Touch-калібровки.
2. Пошук у режимі pairing, відображення назви, вибір, підключення та перепідключення.
3. Перевірити ADC (LINE L/R, канали, рівень, 44,1 кГц), беззвучність на вході та відсутність кліпінгу.
4. Перевірити звук на різних A2DP-пристроях; виміряти затримку, dropout та поведінку при зникненні зв’язку.
5. Перевірити запуск магнітоли, перезапуск будь-якого ESP32 і безпечне вимкнення живлення.

### Джерела

- [Silvus FSA506 driver/touch](https://github.com/garik816/silvus-configurator/tree/hw/esp32s3-fsa506/main)
- [Espressif: ESP32 A2DP Source API](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/bluetooth/esp_a2dp.html)
- [Espressif: ESP32-S3 Bluetooth LE only](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/bluetooth.html)
