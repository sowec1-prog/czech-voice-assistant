# Czech Voice Assistant — ESP32-C3 + local Whisper + Piper Lili

Soukromý domácí hlasový systém bez cloudového rozpoznávání:

1. ESP32-C3 nahraje 7 sekund řeči z INMP441 na microSD jako `/last.wav`.
2. Až po bezpečném uzavření WAV jej po domácí Wi-Fi odešle na PC.
3. PC přepíše češtinu lokálním **faster-whisper**.
4. Text se zobrazí na OLED a v Sériovém monitoru.
5. PC stejný přepis přečte lokálním hlasem **Piper Lili** do výchozích reproduktorů Windows.

> ESP32 zatím zvuk nepřehrává. Text-to-speech hraje z počítače. Žádný zvuk se při rozpoznávání neposílá do cloudu.

## Struktura

```text
firmware/              ESP32-C3 Arduino firmware
  spechtotext.ino
  config.h.example     šablona Wi-Fi a IP adresy PC
  platformio.ini
local-czech-stt/       lokální Python server pro Windows
  server.py
  download_voice.py
  run_local_stt.cmd
  pyproject.toml
```

## Hardware

| Modul | Signál | ESP32-C3 SuperMini |
|---|---|---:|
| OLED SSH1106 | SDA | GPIO 8 |
| OLED SSH1106 | SCL | GPIO 9 |
| INMP441 | SCK/BCLK | GPIO 10 |
| INMP441 | WS/LRCK | GPIO 20 |
| INMP441 | SD/DOUT | GPIO 21 |
| tlačítko | druhý kontakt | GPIO 3 |
| tlačítko | první kontakt | GND |
| microSD SPI | CS | GPIO 7 |
| microSD SPI | CLK | GPIO 4 |
| microSD SPI | MOSI | GPIO 6 |
| microSD SPI | MISO | GPIO 5 |

Všechny moduly musí mít společnou **GND**. INMP441 i modul microSD napájej pouze z **3,3 V**.

## Instalace serveru na PC (Windows)

1. Nainstaluj [uv](https://docs.astral.sh/uv/) a Python 3.11+.
2. V příkazovém řádku otevři `local-czech-stt`.
3. Proveď:

   ```bash
   uv sync
   uv run download_voice.py
   uv run server.py
   ```

4. Server ověř v prohlížeči: `http://127.0.0.1:8080/health`.

   Musí vrátit například:

   ```json
   {"status":"ok","model":"small","tts":"sk_SK-lili-medium"}
   ```

5. Pro automatický start po přihlášení do Windows vytvoř ve složce **Po spuštění** zástupce na `run_local_stt.cmd`.

Pokud ESP32 nedosáhne na server, povol v privátní síti Windows Firewall příchozí TCP port **8080**.

## Nastavení a nahrání ESP32

1. V `firmware` přejmenuj `config.h.example` na `config.h`.
2. Vyplň `WIFI_SSID`, `WIFI_PASSWORD` a `LOCAL_STT_HOST` (LAN IP počítače, například `192.168.0.180`).
3. V Arduino IDE otevři `firmware/spechtotext.ino`.
4. Nastav:
   - Board: **ESP32C3 Dev Module**
   - Flash Size: **4MB**
   - USB CDC On Boot: **Enabled**
   - Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**
5. Nainstaluj knihovny: `ArduinoJson`, `Adafruit GFX Library`, `Adafruit SH110X`.
6. Nahraj firmware a otevři Sériový monitor na **115200 baud**.

## Použití

- Po startu ověř na OLED `microSD: OK` a `Pripraven`.
- Krátce stiskni tlačítko, potom mluv. ESP32 nahraje přesně 7 sekund.
- Záznam je uložen jako `/last.wav` na microSD a teprve pak odeslán.
- Text se objeví na OLED, v Serial Monitoru a PC jej přečte hlasem Lili.

## Bezpečnost a zálohy

- `config.h` se do GitHubu **neukládá**; obsahuje Wi-Fi údaje.
- Stahované modely Piper se také neukládají; obnoví je `download_voice.py`.
- Repository je zamýšlené jako **private** GitHub repository.
