/*
 * LongRangeText - Node 1 firmware
 * ESP32-WROOM-32 + NRF24L01+ PA/LNA + SSD1306 128x64 OLED + 6-button T9 keypad
 *
 * Libraries: RF24 (TMRh20), Adafruit GFX Library, Adafruit SSD1306 (+ BusIO), Wire, SPI
 * Core:      ESP32 Arduino core 2.x or 3.x (LEDC API difference handled below)
 *
 * STATUS: NOT compiled or verified on physical hardware by the author.
 *         Record the library/core versions you actually test with in the README.
 *
 * Only Node 1 is implemented. End-to-end messaging needs a compatible Node 2.
 * A radio ACK means the peer RADIO received the packet. It does NOT mean it was displayed or read.
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <RF24.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <string.h>

// =====================================================================
// CONFIGURATION (everything you may want to change lives here)
// =====================================================================
#define FW_VERSION            "0.1.0"

// ---- Feature switches (1 = on, 0 = off) ----
#define ENABLE_BUZZER         1     // 0 disables all buzzer code paths
#define BUZZER_IS_ACTIVE      1     // 1 = active buzzer (DC on/off), 0 = passive (tone)
#define ENABLE_STATUS_LEDS    0     // set 1 once the red/blue LEDs are wired
#define ENABLE_HEARTBEAT      1     // periodic link-check packets
#define ENABLE_SERIAL_CMDS    1     // 'h' help, 's' stats, 'r' radio details

// ---- Node identity ----
static const uint8_t NODE_ID_SELF = 1;
static const uint8_t NODE_ID_PEER = 2;

// ---- Pins ----
static const uint8_t PIN_OLED_SDA = 21;
static const uint8_t PIN_OLED_SCL = 22;
static const uint8_t PIN_NRF_CE   = 4;
static const uint8_t PIN_NRF_CSN  = 5;
static const uint8_t PIN_NRF_SCK  = 18;
static const uint8_t PIN_NRF_MOSI = 23;
static const uint8_t PIN_NRF_MISO = 19;
static const uint8_t PIN_BUZZER   = 27;
static const uint8_t PIN_LED_RED  = 16;   // power / firmware OK
static const uint8_t PIN_LED_BLUE = 17;   // peer link OK
static const uint8_t BTN_PINS[6]  = {13, 14, 25, 26, 32, 33};  // B1..B6, to GND, INPUT_PULLUP
static const uint8_t BTN_SPACE    = 4;    // index of Button 5
static const uint8_t BTN_DEL_SEND = 5;    // index of Button 6

// ---- OLED ----
static const uint8_t OLED_W = 128;
static const uint8_t OLED_H = 64;
static const uint8_t OLED_ADDR_PRIMARY   = 0x3C;
static const uint8_t OLED_ADDR_SECONDARY = 0x3D;

// ---- Typing ----
static const uint32_t DEBOUNCE_MS          = 25;
static const uint32_t MULTITAP_TIMEOUT_MS  = 700;
static const uint32_t LONG_PRESS_MS        = 900;
static const uint8_t  MSG_MAX              = 28;   // chars per packet
static const char* const T9_SETS[4] = {"ABCDEF", "GHIJKLM", "NOPQRST", "UVWXYZ"};

// ---- Radio ----
static const uint8_t      RADIO_CHANNEL    = 76;               // 2476 MHz, within 2.4 GHz band
static const rf24_datarate_e RADIO_DATA_RATE = RF24_250KBPS;   // use RF24_1MBPS for clone chips
static const rf24_pa_dbm_e   RADIO_PA_LEVEL  = RF24_PA_MIN;    // raise only after bench tests
static const uint8_t      RADIO_RETRY_DELAY = 5;               // (5*250)+250 = 1500 us
static const uint8_t      RADIO_RETRY_COUNT = 15;
static const uint32_t     RADIO_SPI_HZ      = 4000000;         // conservative for jumper wires

// ---- Link / timing ----
static const uint32_t HEARTBEAT_INTERVAL_MS = 2000;
static const uint32_t PEER_TIMEOUT_MS       = 7000;   // blue LED/peer flag expires after this
static const uint32_t TYPING_QUIET_MS       = 500;    // no heartbeat TX right after key activity
static const uint32_t ACTION_SHOW_MS        = 2500;
static const uint32_t DISPLAY_REFRESH_MS    = 500;    // periodic redraw (cursor blink)
static const uint32_t DISPLAY_MIN_GAP_MS    = 60;

// ---- Passive buzzer tone ----
static const uint16_t BUZZER_TONE_HZ = 2400;

// =====================================================================
// PROTOCOL (see docs/RADIO_PROTOCOL.md)
// =====================================================================
static const uint8_t PROTOCOL_VERSION = 1;
enum PacketType : uint8_t { PKT_TEXT = 1, PKT_HEARTBEAT = 2 };

struct __attribute__((packed)) RadioPacket {
  uint8_t verType;   // hi nibble = version, lo nibble = type
  uint8_t nodeIds;   // hi nibble = sender, lo nibble = receiver
  uint8_t seq;
  char    text[MSG_MAX];   // printable ASCII, NUL padded (NUL-terminated if shorter than 28)
  uint8_t crc;       // CRC-8 (poly 0x07) over bytes 0..30
};
static_assert(sizeof(RadioPacket) == 32, "RadioPacket must be exactly 32 bytes");

// =====================================================================
// GLOBALS
// =====================================================================
Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1);
RF24 radio(PIN_NRF_CE, PIN_NRF_CSN, RADIO_SPI_HZ);

enum Action : uint8_t { ACT_READY, ACT_SENDING, ACT_SENT, ACT_NOACK, ACT_RECEIVED, ACT_NORADIO, ACT_EMPTY };

static bool     oledOk = false;
static bool     radioOk = false;
static bool     displayDirty = true;
static uint32_t lastRenderMs = 0;

// Draft (pending multi-tap char is stored as the last char of the draft)
static char     draft[MSG_MAX + 1] = {0};
static uint8_t  draftLen = 0;
static bool     pendingActive = false;
static int8_t   pendingBtn = -1;
static uint8_t  pendingIdx = 0;
static uint32_t pendingLastTapMs = 0;

// Received message
static char     rxMsg[MSG_MAX + 1] = {0};
static bool     haveRx = false;
static uint8_t  lastRxSeq = 0;
static bool     haveLastRxSeq = false;

// Link
static bool     peerSeen = false;
static uint32_t peerLastSeenMs = 0;
static bool     lastPeerOnline = false;
static uint32_t lastHeartbeatMs = 0;
static uint32_t lastKeyMs = 0;
static uint8_t  txSeq = 0;

// Action
static Action   action = ACT_READY;
static uint32_t actionSetMs = 0;

// Stats
static uint32_t statTxOk = 0, statTxFail = 0, statRxText = 0, statRxHb = 0, statRxBad = 0, statRxDup = 0;

// Buttons
struct ButtonState {
  bool     rawLast;
  bool     stable;
  uint32_t changedAtMs;
  uint32_t pressedAtMs;
  bool     longFired;
};
static ButtonState btn[6];

// Buzzer
static const uint16_t PAT_RX[]   = {90, 70, 90};
static const uint16_t PAT_OK[]   = {40};
static const uint16_t PAT_FAIL[] = {180, 90, 180};
static const uint16_t* buzPat = nullptr;
static uint8_t  buzLen = 0, buzIdx = 0;
static uint32_t buzStepStartMs = 0;
static bool     buzOn = false;

// =====================================================================
// UTILITIES
// =====================================================================
static uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t b = 0; b < 8; b++) {
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
  }
  return crc;
}

static void makeAddress(uint8_t id, uint8_t out[5]) {
  memcpy(out, "NODE", 4);
  out[4] = (uint8_t)('0' + id);
}

static void setAction(Action a) {
  action = a;
  actionSetMs = millis();
  displayDirty = true;
}

static const char* actionText(Action a) {
  switch (a) {
    case ACT_SENDING:  return "SENDING";
    case ACT_SENT:     return "SENT";
    case ACT_NOACK:    return "NO ACK";
    case ACT_RECEIVED: return "RECEIVED";
    case ACT_NORADIO:  return "NO RADIO";
    case ACT_EMPTY:    return "EMPTY";
    default:           return "READY";
  }
}

static bool peerOnline() {
  return peerSeen && (millis() - peerLastSeenMs) <= PEER_TIMEOUT_MS;
}

static bool isPrintable(char c) { return c >= 0x20 && c <= 0x7E; }

// =====================================================================
// BUZZER (non-blocking)
// =====================================================================
static void buzzerHw(bool on) {
#if ENABLE_BUZZER
  #if BUZZER_IS_ACTIVE
    digitalWrite(PIN_BUZZER, on ? HIGH : LOW);
  #else
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
      ledcWriteTone(PIN_BUZZER, on ? BUZZER_TONE_HZ : 0);
    #else
      ledcWriteTone(0, on ? BUZZER_TONE_HZ : 0);
    #endif
  #endif
#else
  (void)on;
#endif
}

static void buzzerInit() {
#if ENABLE_BUZZER
  #if BUZZER_IS_ACTIVE
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
  #else
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
      ledcAttach(PIN_BUZZER, 2000, 8);
    #else
      ledcSetup(0, 2000, 8);
      ledcAttachPin(PIN_BUZZER, 0);
    #endif
  #endif
  buzzerHw(false);
#endif
}

static void buzzerPlay(const uint16_t* pattern, uint8_t len) {
#if ENABLE_BUZZER
  buzPat = pattern; buzLen = len; buzIdx = 0;
  buzStepStartMs = millis();
  buzOn = true;
  buzzerHw(true);
#else
  (void)pattern; (void)len;
#endif
}

static void buzzerUpdate() {
#if ENABLE_BUZZER
  if (!buzPat) return;
  if (millis() - buzStepStartMs >= buzPat[buzIdx]) {
    buzIdx++;
    buzStepStartMs = millis();
    if (buzIdx >= buzLen) {
      buzPat = nullptr; buzOn = false; buzzerHw(false);
      return;
    }
    buzOn = !buzOn;          // pattern alternates ON, OFF, ON ...
    buzzerHw(buzOn);
  }
#endif
}

// =====================================================================
// LEDS
// =====================================================================
static void ledsInit() {
#if ENABLE_STATUS_LEDS
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_BLUE, LOW);
#endif
}

static void ledsUpdate(bool firmwareReady) {
#if ENABLE_STATUS_LEDS
  digitalWrite(PIN_LED_RED, firmwareReady ? HIGH : LOW);
  digitalWrite(PIN_LED_BLUE, peerOnline() ? HIGH : LOW);   // never just "radio initialized"
#else
  (void)firmwareReady;
#endif
}

// =====================================================================
// OLED
// =====================================================================
static bool oledProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static void oledInit() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);
  uint8_t addr = 0;
  if (oledProbe(OLED_ADDR_PRIMARY)) addr = OLED_ADDR_PRIMARY;
  else if (oledProbe(OLED_ADDR_SECONDARY)) addr = OLED_ADDR_SECONDARY;

  if (addr == 0) {
    Serial.println(F("[OLED] Not found at 0x3C or 0x3D. Check SDA/SCL/power. Continuing without display."));
    oledOk = false;
    return;
  }
  oledOk = display.begin(SSD1306_SWITCHCAPVCC, addr);
  Serial.printf("[OLED] %s at 0x%02X\n", oledOk ? "OK" : "begin() FAILED", addr);
  if (oledOk) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.display();
  }
}

static void showSplash() {
  if (!oledOk) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(7, 6);  display.print(F("LONG RANGE COMMS"));
  display.setCursor(40, 22); display.print(F("NODE "));  display.print(NODE_ID_SELF);
  display.setCursor(22, 38); display.print(F("Radio: "));
  display.print(radioOk ? F("OK") : F("FAIL"));
  display.setCursor(31, 52); display.print(F("v")); display.print(F(FW_VERSION));
  display.display();
}

static void drawChars(int16_t x, int16_t y, const char* s, uint8_t n) {
  for (uint8_t i = 0; i < n && s[i]; i++) display.drawChar(x + i * 6, y, s[i], SSD1306_WHITE, SSD1306_BLACK, 1);
}

static void render() {
  if (!oledOk) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Row 0: header (<= 21 chars)
  display.setCursor(0, 0);
  display.printf("NODE %u RF:%s PEER:%s", NODE_ID_SELF, radioOk ? "OK" : "ERR", peerOnline() ? "ON" : "--");

  // Rows 1-3: latest received message
  display.setCursor(0, 9);
  if (haveRx) display.printf("RX FROM NODE %u:", NODE_ID_PEER);
  else        display.print(F("RX: none yet"));
  if (haveRx) {
    size_t n = strlen(rxMsg);
    drawChars(0, 18, rxMsg, n > 21 ? 21 : (uint8_t)n);
    if (n > 21) drawChars(0, 27, rxMsg + 21, (uint8_t)(n - 21));
  }
  display.drawFastHLine(0, 35, OLED_W, SSD1306_WHITE);

  // Row 4: draft length + action
  display.setCursor(0, 37);
  display.printf("TX %u/%u %s", draftLen, MSG_MAX, actionText(action));

  // Rows 5-6: draft (pending multi-tap char shown inverted)
  for (uint8_t i = 0; i < draftLen; i++) {
    int16_t x = (i % 21) * 6;
    int16_t y = 46 + (i / 21) * 9;
    bool pend = pendingActive && (i == draftLen - 1);
    display.drawChar(x, y, draft[i], pend ? SSD1306_BLACK : SSD1306_WHITE,
                     pend ? SSD1306_WHITE : SSD1306_BLACK, 1);
  }
  if (!pendingActive && draftLen < MSG_MAX && ((millis() / 500) & 1)) {
    display.drawChar((draftLen % 21) * 6, 46 + (draftLen / 21) * 9, '_', SSD1306_WHITE, SSD1306_BLACK, 1);
  }

  display.display();
  lastRenderMs = millis();
  displayDirty = false;
}

static void renderIfNeeded() {
  uint32_t now = millis();
  bool periodic = (now - lastRenderMs) >= DISPLAY_REFRESH_MS;
  if ((displayDirty || periodic) && (now - lastRenderMs) >= DISPLAY_MIN_GAP_MS) render();
}

// =====================================================================
// RADIO
// =====================================================================
static bool radioInit() {
  SPI.begin(PIN_NRF_SCK, PIN_NRF_MISO, PIN_NRF_MOSI, PIN_NRF_CSN);
  if (!radio.begin(&SPI)) {
    Serial.println(F("[RADIO] begin() failed. Check wiring and 3.3 V supply."));
    return false;
  }
  if (!radio.isChipConnected()) {
    Serial.println(F("[RADIO] Chip not detected on SPI."));
    return false;
  }

  radio.setPALevel(RADIO_PA_LEVEL);
  radio.setDataRate(RADIO_DATA_RATE);
  radio.setChannel(RADIO_CHANNEL);
  radio.setAddressWidth(5);
  radio.setCRCLength(RF24_CRC_16);
  radio.setAutoAck(true);
  radio.setRetries(RADIO_RETRY_DELAY, RADIO_RETRY_COUNT);
  radio.setPayloadSize(sizeof(RadioPacket));

  uint8_t selfAddr[5], peerAddr[5];
  makeAddress(NODE_ID_SELF, selfAddr);
  makeAddress(NODE_ID_PEER, peerAddr);
  radio.openWritingPipe(peerAddr);
  radio.openReadingPipe(1, selfAddr);   // pipe 0 is reserved for TX auto-ACK
  radio.startListening();

  if (radio.getDataRate() != RADIO_DATA_RATE) {
    Serial.println(F("[RADIO] WARNING: data rate did not stick (clone chip?). Try RF24_1MBPS on both nodes."));
  }
  if (radio.getChannel() != RADIO_CHANNEL) {
    Serial.println(F("[RADIO] WARNING: channel readback mismatch."));
  }
  Serial.printf("[RADIO] OK ch=%u rate=%d addr self=NODE%u peer=NODE%u\n",
                RADIO_CHANNEL, (int)RADIO_DATA_RATE, NODE_ID_SELF, NODE_ID_PEER);
  return true;
}

static bool transmitPacket(uint8_t type, const char* text, uint8_t len) {
  if (!radioOk) return false;
  RadioPacket p;
  memset(&p, 0, sizeof(p));
  p.verType = (uint8_t)((PROTOCOL_VERSION << 4) | (type & 0x0F));
  p.nodeIds = (uint8_t)((NODE_ID_SELF << 4) | (NODE_ID_PEER & 0x0F));
  p.seq = txSeq++;
  if (text && len > 0) memcpy(p.text, text, len > MSG_MAX ? MSG_MAX : len);
  p.crc = crc8((const uint8_t*)&p, sizeof(p) - 1);

  radio.stopListening();
  bool ok = radio.write(&p, sizeof(p));
  if (!ok) radio.flush_tx();
  radio.startListening();
  return ok;
}

static void processPacket(const RadioPacket& p) {
  if (p.crc != crc8((const uint8_t*)&p, sizeof(p) - 1)) { statRxBad++; Serial.println(F("[RX] drop: bad CRC")); return; }

  uint8_t ver = p.verType >> 4, type = p.verType & 0x0F;
  uint8_t sender = p.nodeIds >> 4, receiver = p.nodeIds & 0x0F;
  if (ver != PROTOCOL_VERSION)  { statRxBad++; Serial.println(F("[RX] drop: bad version")); return; }
  if (receiver != NODE_ID_SELF) { statRxBad++; Serial.println(F("[RX] drop: wrong receiver")); return; }
  if (sender != NODE_ID_PEER)   { statRxBad++; Serial.println(F("[RX] drop: unknown sender")); return; }

  peerSeen = true;
  peerLastSeenMs = millis();

  if (type == PKT_HEARTBEAT) { statRxHb++; return; }
  if (type != PKT_TEXT)      { statRxBad++; Serial.println(F("[RX] drop: unknown type")); return; }

  // Text validation: bounded length, printable ASCII only
  size_t len = strnlen(p.text, MSG_MAX);
  if (len == 0) { statRxBad++; Serial.println(F("[RX] drop: empty text")); return; }
  for (size_t i = 0; i < len; i++) {
    if (!isPrintable(p.text[i])) { statRxBad++; Serial.println(F("[RX] drop: non-printable text")); return; }
  }

  char tmp[MSG_MAX + 1];
  memcpy(tmp, p.text, len);
  tmp[len] = '\0';

  if (haveLastRxSeq && p.seq == lastRxSeq && haveRx && strcmp(tmp, rxMsg) == 0) {
    statRxDup++; return;   // retransmission duplicate
  }
  lastRxSeq = p.seq; haveLastRxSeq = true;

  memcpy(rxMsg, tmp, len + 1);
  haveRx = true;
  statRxText++;
  setAction(ACT_RECEIVED);
  buzzerPlay(PAT_RX, sizeof(PAT_RX) / sizeof(PAT_RX[0]));
  Serial.printf("[RX] NODE%u seq=%u: \"%s\"\n", sender, p.seq, rxMsg);
}

static void radioReceiveLoop() {
  if (!radioOk) return;
  uint8_t guard = 0;
  while (guard++ < 4 && radio.available()) {
    RadioPacket p;
    radio.read(&p, sizeof(p));
    processPacket(p);
  }
}

static void heartbeatLoop() {
#if ENABLE_HEARTBEAT
  if (!radioOk) return;
  uint32_t now = millis();
  if (now - lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) return;
  if (now - lastKeyMs < TYPING_QUIET_MS) return;           // never block while the user is typing
  if (action == ACT_SENDING) return;
  lastHeartbeatMs = now;
  bool ok = transmitPacket(PKT_HEARTBEAT, nullptr, 0);
  // Radio ACK alone does NOT set peer-online; only a valid packet FROM Node 2 does.
  Serial.printf("[HB] tx %s\n", ok ? "acked" : "no ack");
#endif
}

// =====================================================================
// TYPING
// =====================================================================
static void commitPending() {
  pendingActive = false;
  pendingBtn = -1;
}

static void deleteLast() {
  if (draftLen > 0) {
    draftLen--;
    draft[draftLen] = '\0';
  }
  pendingActive = false;
  pendingBtn = -1;
  displayDirty = true;
}

static void onLetterButton(uint8_t b) {
  const char* set = T9_SETS[b];
  uint8_t n = (uint8_t)strlen(set);
  uint32_t now = millis();
  if (pendingActive && pendingBtn == b && (now - pendingLastTapMs) <= MULTITAP_TIMEOUT_MS && draftLen > 0) {
    pendingIdx = (uint8_t)((pendingIdx + 1) % n);
    draft[draftLen - 1] = set[pendingIdx];
  } else {
    commitPending();
    if (draftLen >= MSG_MAX) { buzzerPlay(PAT_FAIL, sizeof(PAT_FAIL) / sizeof(PAT_FAIL[0])); return; }
    pendingBtn = (int8_t)b; pendingIdx = 0; pendingActive = true;
    draft[draftLen++] = set[0];
    draft[draftLen] = '\0';
  }
  pendingLastTapMs = now;
  displayDirty = true;
}

static void onSpace() {
  commitPending();
  if (draftLen >= MSG_MAX) { buzzerPlay(PAT_FAIL, sizeof(PAT_FAIL) / sizeof(PAT_FAIL[0])); return; }
  draft[draftLen++] = ' ';
  draft[draftLen] = '\0';
  displayDirty = true;
}

static void typingTimeoutLoop() {
  if (pendingActive && (millis() - pendingLastTapMs) > MULTITAP_TIMEOUT_MS) {
    commitPending();
    displayDirty = true;
  }
}

static void sendDraft() {
  commitPending();
  if (draftLen == 0) { setAction(ACT_EMPTY); return; }
  if (!radioOk)      { setAction(ACT_NORADIO); buzzerPlay(PAT_FAIL, sizeof(PAT_FAIL) / sizeof(PAT_FAIL[0])); Serial.println(F("[TX] radio unavailable, draft kept")); return; }

  setAction(ACT_SENDING);
  render();                                    // show SENDING before the (short) blocking write
  bool ok = transmitPacket(PKT_TEXT, draft, draftLen);
  lastHeartbeatMs = millis();

  if (ok) {
    statTxOk++;
    Serial.printf("[TX] radio-ACKed: \"%s\"\n", draft);
    draft[0] = '\0'; draftLen = 0;
    setAction(ACT_SENT);
    buzzerPlay(PAT_OK, sizeof(PAT_OK) / sizeof(PAT_OK[0]));
  } else {
    statTxFail++;
    Serial.printf("[TX] NO ACK, draft kept: \"%s\"\n", draft);
    setAction(ACT_NOACK);
    buzzerPlay(PAT_FAIL, sizeof(PAT_FAIL) / sizeof(PAT_FAIL[0]));
  }
}

// =====================================================================
// BUTTONS (debounced, non-blocking)
// =====================================================================
static void onButtonPress(uint8_t i) {
  lastKeyMs = millis();
  if (i < 4)               onLetterButton(i);
  else if (i == BTN_SPACE) onSpace();
  // Button 6 acts on release (short) or hold (long)
}

static void onButtonRelease(uint8_t i) {
  lastKeyMs = millis();
  if (i == BTN_DEL_SEND && !btn[i].longFired) deleteLast();
}

static void buttonsInit() {
  for (uint8_t i = 0; i < 6; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
    btn[i].rawLast = HIGH;
    btn[i].stable = HIGH;
    btn[i].changedAtMs = millis();
    btn[i].pressedAtMs = 0;
    btn[i].longFired = false;
  }
}

static void buttonsUpdate() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < 6; i++) {
    bool raw = digitalRead(BTN_PINS[i]);
    if (raw != btn[i].rawLast) { btn[i].rawLast = raw; btn[i].changedAtMs = now; }
    if ((now - btn[i].changedAtMs) >= DEBOUNCE_MS && raw != btn[i].stable) {
      btn[i].stable = raw;
      if (raw == LOW) { btn[i].pressedAtMs = now; btn[i].longFired = false; onButtonPress(i); }
      else            { onButtonRelease(i); }
    }
    if (i == BTN_DEL_SEND && btn[i].stable == LOW && !btn[i].longFired &&
        (now - btn[i].pressedAtMs) >= LONG_PRESS_MS) {
      btn[i].longFired = true;
      sendDraft();
    }
  }
}

// =====================================================================
// SERIAL DIAGNOSTICS
// =====================================================================
static void serialCommands() {
#if ENABLE_SERIAL_CMDS
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'h') Serial.println(F("Commands: h=help  s=stats  r=radio details"));
    else if (c == 's') {
      Serial.printf("[STAT] txOk=%lu txFail=%lu rxText=%lu rxHb=%lu rxBad=%lu rxDup=%lu peer=%s radio=%s\n",
                    (unsigned long)statTxOk, (unsigned long)statTxFail, (unsigned long)statRxText,
                    (unsigned long)statRxHb, (unsigned long)statRxBad, (unsigned long)statRxDup,
                    peerOnline() ? "online" : "offline", radioOk ? "ok" : "fail");
    } else if (c == 'r') {
      if (radioOk) radio.printPrettyDetails(); else Serial.println(F("Radio not initialized"));
    }
  }
#endif
}

// =====================================================================
// ARDUINO ENTRY POINTS
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.println(F("=== LongRangeText Node 1 ==="));
  Serial.printf("Firmware v%s | self=NODE%u peer=NODE%u\n", FW_VERSION, NODE_ID_SELF, NODE_ID_PEER);

  buttonsInit();
  buzzerInit();
  ledsInit();
  oledInit();
  radioOk = radioInit();
  Serial.printf("[BOOT] Radio init: %s\n", radioOk ? "OK" : "FAILED (OLED and buttons still active)");

  showSplash();
  delay(1500);                         // setup-only delay, not in the main loop
  ledsUpdate(true);
  Serial.println(F("[BOOT] Ready. Type 'h' for serial commands."));
  displayDirty = true;
}

void loop() {
  buttonsUpdate();
  typingTimeoutLoop();
  radioReceiveLoop();
  heartbeatLoop();

  if (action != ACT_READY && (millis() - actionSetMs) > ACTION_SHOW_MS) {
    action = ACT_READY;
    displayDirty = true;
  }
  bool online = peerOnline();
  if (online != lastPeerOnline) {
    lastPeerOnline = online;
    displayDirty = true;
    Serial.printf("[LINK] peer %s\n", online ? "ONLINE (valid packet from Node 2)" : "OFFLINE (timeout)");
  }

  ledsUpdate(true);
  buzzerUpdate();
  serialCommands();
  renderIfNeeded();
}
