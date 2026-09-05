#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "config.h"

constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 64;
constexpr i2s_port_t MIC_PORT = I2S_NUM_0;
constexpr int SAMPLE_RATE = 16000;
constexpr size_t I2S_SAMPLES = 256;

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
SPIClass sdSPI(FSPI);
WebServer webServer(80);
bool oledReady = false;
bool sdReady = false;
bool webServerStarted = false;

void writeWavHeader(File &file, uint32_t pcmBytes) {
  const uint32_t byteRate = SAMPLE_RATE * 2;  // 16-bit mono
  const uint32_t riffSize = 36 + pcmBytes;
  const uint16_t audioFormat = 1;
  const uint16_t channels = 1;
  const uint16_t bitsPerSample = 16;
  const uint32_t dataSize = pcmBytes;

  file.write((const uint8_t *)"RIFF", 4);
  file.write((const uint8_t *)&riffSize, 4);
  file.write((const uint8_t *)"WAVEfmt ", 8);
  const uint32_t fmtSize = 16;
  file.write((const uint8_t *)&fmtSize, 4);
  file.write((const uint8_t *)&audioFormat, 2);
  file.write((const uint8_t *)&channels, 2);
  file.write((const uint8_t *)&SAMPLE_RATE, 4);
  file.write((const uint8_t *)&byteRate, 4);
  const uint16_t blockAlign = 2;
  file.write((const uint8_t *)&blockAlign, 2);
  file.write((const uint8_t *)&bitsPerSample, 2);
  file.write((const uint8_t *)"data", 4);
  file.write((const uint8_t *)&dataSize, 4);
}

void startWebServer() {
  if (webServerStarted) return;

  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdReady = SD.begin(SD_CS, sdSPI, 10000000);
  if (!sdReady) {
    Serial.println("microSD nenalezena - zkontroluj FAT32 kartu a zapojeni.");
  } else {
    Serial.println("microSD: OK");
  }

  webServer.on("/", HTTP_GET, []() {
    String page = "<html><body><h2>ESP32 hlasovy zaznam</h2>";
    page += (sdReady && SD.exists("/last.wav"))
      ? "<p><a href='/last.wav'>Prehrat nebo stahnout posledni nahravku</a></p>"
      : "<p>Zatim neni ulozena zadna nahravka na microSD.</p>";
    page += "</body></html>";
    webServer.send(200, "text/html; charset=utf-8", page);
  });
  webServer.on("/last.wav", HTTP_GET, []() {
    if (!sdReady || !SD.exists("/last.wav")) {
      webServer.send(404, "text/plain", "Zatim neni zadna nahravka na microSD.");
      return;
    }
    File wav = SD.open("/last.wav", FILE_READ);
    webServer.streamFile(wav, "audio/wav");
    wav.close();
  });
  webServer.begin();
  webServerStarted = true;
  Serial.print("Nahravku otevri: http://");
  Serial.print(WiFi.localIP());
  Serial.println("/last.wav");
}

String asciiForOled(String text) {
  // Vestavěný Adafruit font neumí české UTF-8 znaky. Přepis ponechá text čitelný.
  const char* from[] = {"á", "č", "ď", "é", "ě", "í", "ň", "ó", "ř", "š", "ť", "ú", "ů", "ý", "ž",
                        "Á", "Č", "Ď", "É", "Ě", "Í", "Ň", "Ó", "Ř", "Š", "Ť", "Ú", "Ů", "Ý", "Ž"};
  const char* to[]   = {"a", "c", "d", "e", "e", "i", "n", "o", "r", "s", "t", "u", "u", "y", "z",
                        "A", "C", "D", "E", "E", "I", "N", "O", "R", "S", "T", "U", "U", "Y", "Z"};
  for (size_t i = 0; i < sizeof(from) / sizeof(from[0]); ++i) text.replace(from[i], to[i]);
  return text;
}

void showText(const String &title, const String &message) {
  const String displayMessage = asciiForOled(message);
  Serial.println(title + ": " + message);
  if (!oledReady) return;

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(title);
  display.drawLine(0, 11, 127, 11, SH110X_WHITE);

  // Jednoduché zalomení textu pro šířku 128 px (cca 21 znaků na řádek).
  int y = 18;
  String line;
  for (size_t i = 0; i < displayMessage.length() && y <= 55; ++i) {
    char c = displayMessage[i];
    if (c == '\n' || line.length() >= 21) {
      display.setCursor(0, y);
      display.println(line);
      line = "";
      y += 10;
      if (c == '\n') continue;
    }
    line += c;
  }
  if (line.length() && y <= 55) {
    display.setCursor(0, y);
    display.println(line);
  }
  display.display();
}

bool setupOled() {
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(OLED_ADDRESS, true)) {
    Serial.println("OLED nenalezen na 0x3C");
    return false;
  }
  oledReady = true;
  showText("Wit.ai STT", "OLED pripraven");
  return true;
}

bool setupMicrophone() {
  i2s_config_t config = {};
  config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 8;
  config.dma_buf_len = I2S_SAMPLES;
  config.use_apll = false;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = I2S_MIC_SCK;
  pins.ws_io_num = I2S_MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = I2S_MIC_SD;

  esp_err_t result = i2s_driver_install(MIC_PORT, &config, 0, nullptr);
  if (result != ESP_OK) {
    Serial.printf("I2S install chyba: %d\n", result);
    return false;
  }
  result = i2s_set_pin(MIC_PORT, &pins);
  if (result != ESP_OK) {
    Serial.printf("I2S piny chyba: %d\n", result);
    i2s_driver_uninstall(MIC_PORT);
    return false;
  }
  i2s_zero_dma_buffer(MIC_PORT);
  return true;
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  showText("Wi-Fi", "Pripojuji...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_TIMEOUT_MS) {
    delay(250);
  }
  if (WiFi.status() != WL_CONNECTED) {
    showText("Wi-Fi chyba", "Zkontroluj sit");
    return false;
  }
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  startWebServer();
  return true;
}

String readWitResponse(Client &client) {
  String result;
  uint32_t lastData = millis();
  while ((client.connected() || client.available()) && millis() - lastData < 10000) {
    while (client.available()) {
      result += (char)client.read();
      lastData = millis();
    }
    delay(2);
  }
  return result;
}

String lastTranscript(const String &response) {
  String transcript;
  int start = 0;
  while (start < response.length()) {
    int end = response.indexOf('\n', start);
    if (end < 0) end = response.length();
    String line = response.substring(start, end);
    line.trim();
    if (line.startsWith("{")) {
      StaticJsonDocument<1536> json;
      DeserializationError error = deserializeJson(json, line);
      if (!error && json["text"].is<const char*>()) transcript = json["text"].as<String>();
    }
    start = end + 1;
  }
  return transcript;
}

void recordAndSend() {
  if (!sdReady) {
    showText("microSD chyba", "Nelze nahravat");
    return;
  }
  // 1) Nejdřív vždy vznikne hotový WAV soubor na kartě.
  SD.remove("/last.wav");
  File recording = SD.open("/last.wav", FILE_WRITE);
  if (!recording) {
    showText("microSD chyba", "Nelze psat WAV");
    return;
  }
  uint8_t emptyHeader[44] = {};
  recording.write(emptyHeader, sizeof(emptyHeader));

  int32_t raw[I2S_SAMPLES];
  int16_t pcm[I2S_SAMPLES];
  size_t totalSamples = 0;
  uint32_t wavDataBytes = 0;
  const uint32_t recordingStarted = millis();
  showText("Nahravam", "Mluv 7 sekund");

  while (millis() - recordingStarted < MAX_RECORD_MS) {
    size_t bytesRead = 0;
    if (i2s_read(MIC_PORT, raw, sizeof(raw), &bytesRead, 250 / portTICK_PERIOD_MS) != ESP_OK) continue;
    const size_t count = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < count; ++i) pcm[i] = (int16_t)(raw[i] >> 16);
    const size_t pcmBytes = count * sizeof(int16_t);
    recording.write((const uint8_t *)pcm, pcmBytes);
    wavDataBytes += pcmBytes;
    totalSamples += count;
  }

  recording.seek(0);
  writeWavHeader(recording, wavDataBytes);
  recording.close();
  const uint32_t wavSize = wavDataBytes + 44;
  Serial.printf("Ulozeno /last.wav: %lu B\n", (unsigned long)wavSize);

  if (totalSamples < SAMPLE_RATE / 3) {
    showText("Prilis kratke", "Zkus to znovu");
    return;
  }

  // 2) Teprve po uložení se soubor z karty odešle do lokálního Whisper serveru.
  if (!connectWiFi()) return;
  WiFiClient client;
  showText("Odesilam lokalne", "Cekej...");
  if (!client.connect(LOCAL_STT_HOST, LOCAL_STT_PORT)) {
    showText("Local STT chyba", "PC server neni dostupny");
    return;
  }

  client.print("POST /transcribe HTTP/1.1\r\n");
  client.print("Host: ");
  client.print(LOCAL_STT_HOST);
  client.print("\r\nContent-Type: audio/wav\r\nContent-Length: ");
  client.print(wavSize);
  client.print("\r\nConnection: close\r\n\r\n");

  File upload = SD.open("/last.wav", FILE_READ);
  uint8_t uploadBuffer[1024];
  while (upload && upload.available()) {
    const size_t count = upload.read(uploadBuffer, sizeof(uploadBuffer));
    if (count == 0) break;
    client.write(uploadBuffer, count);
  }
  upload.close();

  showText("Zpracovavam", "Local Whisper...");
  String response = readWitResponse(client);
  client.stop();
  String text = lastTranscript(response);

  if (text.length()) {
    showText("Rozpoznano", text);
    Serial.println("Wit.ai odpoved: " + text);
  } else {
    Serial.println("Wit.ai raw odpoved:");
    Serial.println(response);
    showText("Nerozumim", "Zkus to znovu");
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  oledReady = setupOled();
  if (!setupMicrophone()) {
    showText("I2S chyba", "Zkontroluj INMP441");
    return;
  }
  // Wi-Fi i microSD se ověří hned při startu, ne až po stisku tlačítka.
  connectWiFi();
  showText(sdReady ? "Pripraven" : "microSD chyba", sdReady ? "Stiskni a mluv" : "Zkontroluj kartu");
}

void loop() {
  if (webServerStarted) webServer.handleClient();

  static bool wasPressed = false;
  const bool pressed = digitalRead(BUTTON_PIN) == LOW;
  if (pressed && !wasPressed) {
    delay(30); // odfiltrování zákmitů kontaktu
    if (digitalRead(BUTTON_PIN) == LOW) recordAndSend();
  }
  wasPressed = digitalRead(BUTTON_PIN) == LOW;
  delay(10);
}
