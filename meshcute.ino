/*
 * MeshCuter - Multi-Scanner for M5 Cardputer Adv
 * ================================================
 * BLE Scanner, WiFi Scanner, LoRa Scanner (RX only)
 *
 * Hardware: M5 Cardputer Adv (ESP32-S3) + Cap LoRa-1262 (SX1262)
 * Display:  1.14" ST7789V2, 240x135px
 * Keyboard: 56-key, no dedicated arrow keys
 *
 * Navigation:
 *   Menu:    Press 1/2/3 to select scanner
 *   Scanner: ; = scroll up, . = scroll down
 *            / = show extra col, , = hide extra col
 *            0 or Backspace = back to menu
 *
 * LoRa: RECEIVE ONLY - Swiss BAKOM/OFCOM compliant
 *   869.618 MHz / BW 62.5 kHz / SF8 / CR 8
 *   (MeshCore CH community settings, SRD High Power band)
 *
 * Libraries:
 *   - M5Cardputer  (github.com/m5stack/M5Cardputer)
 *   - RadioLib     (Arduino Library Manager)
 *
 * Board: ESP32S3 Dev Module, USB CDC On Boot: Enabled
 * Partition: "Huge APP (3MB No OTA/1MB SPIFFS)"
 *
 */

#include <M5Cardputer.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <WiFi.h>
#include <RadioLib.h>
#include "types.h"

// ═══════════════════════════════════════════════════════
//  HARDWARE CONFIG
// ═══════════════════════════════════════════════════════

// Cap LoRa-1262 SPI pins (MOSI=G14, MISO=G39, SCK=G40 auto-mapped)
#define LORA_NSS   5
#define LORA_IRQ   4
#define LORA_RST   3
#define LORA_BUSY  6

// ═══════════════════════════════════════════════════════
//  SWISS LoRa CONFIG - RX ONLY, NO TRANSMISSION!
// ═══════════════════════════════════════════════════════
// MeshCore CH: 869.618 MHz, "Narrow" preset
// SRD High Power band (869.4-869.65 MHz), max 500 mW ERP, 10% DC
#define LORA_FREQ           869.618f
#define LORA_BW             62.5f     // kHz
#define LORA_SF             8
#define LORA_CR             8   
#define LORA_SYNC_WORD      0x34
#define LORA_PREAMBLE       20
#define LORA_TX_POWER       10        // needed for init, never used (RX only)

// ═══════════════════════════════════════════════════════
//  DISPLAY CONSTANTS
// ═══════════════════════════════════════════════════════
#define SCREEN_W   240
#define SCREEN_H   135
#define HEADER_H   14
#define ROW_H      11
#define STATUS_H   12
#define TABLE_Y    (HEADER_H + 12)
#define FONT_SIZE  1

// ═══════════════════════════════════════════════════════
//  STATE
// ═══════════════════════════════════════════════════════
enum Screen { MENU, BT_SCAN, WIFI_SCAN, LORA_SCAN };
Screen currentScreen = MENU;

int scrollPos = 0;
bool showExtraCol = false;
bool needsRedraw = true;
unsigned long lastPeriodicRedraw = 0;

// ─── BLE ───
struct BTDevice {
    String address;
    String name;
    int rssi;
    String devType;
    unsigned long lastSeen;
};

static const int MAX_BT = 128;
BTDevice btDevices[MAX_BT];
int btCount = 0;
BLEScan* pBLEScan = nullptr;
bool btScanning = false;
bool btScanRunning = false;
unsigned long btLastScanStart = 0;

// ─── WiFi ───
struct WifiNet {
    String bssid;
    String ssid;
    int rssi;
    String encType;
    int channel;
    unsigned long lastSeen;
};

static const int MAX_WIFI = 128;
WifiNet wifiNets[MAX_WIFI];
int wifiCount = 0;
bool wifiScanning = false;
bool wifiScanPending = false;
unsigned long wifiLastScanTime = 0;

// ─── LoRa ───
struct LoRaPacket {
    int id;
    int rssi;
    float snr;
    int len;
    String dataHex;
    unsigned long timestamp;
};

static const int MAX_LORA = 64;
LoRaPacket loraPkts[MAX_LORA];
int loraCount = 0;
bool loraListening = false;
bool loraInitFailed = false;
unsigned long loraStartTime = 0;

SX1262 radio = new Module(LORA_NSS, LORA_IRQ, LORA_RST, LORA_BUSY);
volatile bool loraRxFlag = false;
int loraPacketCounter = 0;

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void loraRxISR(void) {
    loraRxFlag = true;
}

// ═══════════════════════════════════════════════════════
//  UTILITIES
// ═══════════════════════════════════════════════════════

String truncStr(const String& s, int maxChars) {
    if ((int)s.length() <= maxChars) return s;
    return s.substring(0, maxChars - 1) + "~";
}

String encTypeStr(wifi_auth_mode_t auth) {
    switch (auth) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2E";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        default:                        return "?";
    }
}

// ═══════════════════════════════════════════════════════
//  SORT (insertion sort, descending RSSI = strongest first)
// ═══════════════════════════════════════════════════════

void sortBTDevices() {
    for (int i = 1; i < btCount; i++) {
        BTDevice key = btDevices[i];
        int j = i - 1;
        while (j >= 0 && btDevices[j].rssi < key.rssi) {
            btDevices[j + 1] = btDevices[j]; j--;
        }
        btDevices[j + 1] = key;
    }
}

void sortWifiNets() {
    for (int i = 1; i < wifiCount; i++) {
        WifiNet key = wifiNets[i];
        int j = i - 1;
        while (j >= 0 && wifiNets[j].rssi < key.rssi) {
            wifiNets[j + 1] = wifiNets[j]; j--;
        }
        wifiNets[j + 1] = key;
    }
}

void sortLoraPkts() {
    for (int i = 1; i < loraCount; i++) {
        LoRaPacket key = loraPkts[i];
        int j = i - 1;
        while (j >= 0 && loraPkts[j].rssi < key.rssi) {
            loraPkts[j + 1] = loraPkts[j]; j--;
        }
        loraPkts[j + 1] = key;
    }
}

// ═══════════════════════════════════════════════════════
//  BLE: CLASSIFY + ADD/UPDATE
// ═══════════════════════════════════════════════════════

String classifyBLE(BLEAdvertisedDevice& dev, const String& name) {
    // 1) BLE Appearance
    if (dev.haveAppearance()) {
        uint16_t app = dev.getAppearance();
        if (app >= 0x0040 && app <= 0x007F) return "Phone";
        if (app >= 0x0080 && app <= 0x00BF) return "Computer";
        if (app >= 0x00C0 && app <= 0x00FF) return "Watch";
        if (app >= 0x0180 && app <= 0x01BF) return "Remote";
        if (app >= 0x0480 && app <= 0x04BF) return "Light";
        if (app >= 0x0840 && app <= 0x087F) return "Keyboard";
        if (app >= 0x0880 && app <= 0x08BF) return "Mouse";
        if (app >= 0x08C0 && app <= 0x08FF) return "Gamepad";
        if (app >= 0x0C40 && app <= 0x0C7F) return "HRM";
        if (app >= 0x0C80 && app <= 0x0CBF) return "BloodP";
        if (app >= 0x0D00 && app <= 0x0D3F) return "Scale";
        return "BLE Dev";
    }

    // 2) Service UUIDs
    if (dev.haveServiceUUID()) {
        String uuid = String(dev.getServiceUUID().toString().c_str());
        if (uuid.indexOf("180d") >= 0) return "HRM";
        if (uuid.indexOf("180f") >= 0) return "Battery";
        if (uuid.indexOf("1812") >= 0) return "HID";
        if (uuid.indexOf("180a") >= 0) return "DevInfo";
        if (uuid.indexOf("fee0") >= 0) return "Wearable";
    }

    // 3) Name heuristics
    if (name.length() > 0) {
        String nl = name;
        nl.toLowerCase();
        if (nl.indexOf("iphone") >= 0 || nl.indexOf("pixel") >= 0 ||
            nl.indexOf("galaxy") >= 0 || nl.indexOf("samsung") >= 0 ||
            nl.indexOf("phone") >= 0 || nl.indexOf("oneplus") >= 0 ||
            nl.indexOf("huawei") >= 0 || nl.indexOf("xiaomi") >= 0)
            return "Phone";
        if (nl.indexOf("watch") >= 0 || nl.indexOf("band") >= 0 ||
            nl.indexOf("fitbit") >= 0 || nl.indexOf("garmin") >= 0 ||
            nl.indexOf("amazfit") >= 0)
            return "Wearable";
        if (nl.indexOf("buds") >= 0 || nl.indexOf("airpod") >= 0 ||
            nl.indexOf("headph") >= 0 || nl.indexOf("speaker") >= 0 ||
            nl.indexOf("jbl") >= 0 || nl.indexOf("bose") >= 0 ||
            nl.indexOf("sony") >= 0 || nl.indexOf("beats") >= 0 ||
            nl.indexOf("jabra") >= 0 || nl.indexOf("audio") >= 0 ||
            nl.indexOf("soundcore") >= 0)
            return "Audio";
        if (nl.indexOf("tv") >= 0 || nl.indexOf("chromecast") >= 0 ||
            nl.indexOf("fire") >= 0 || nl.indexOf("roku") >= 0)
            return "Media";
        if (nl.indexOf("keyboard") >= 0) return "Keyboard";
        if (nl.indexOf("mouse") >= 0 || nl.indexOf("trackpad") >= 0) return "Mouse";
        if (nl.indexOf("laptop") >= 0 || nl.indexOf("macbook") >= 0 ||
            nl.indexOf("surface") >= 0 || nl.indexOf("ipad") >= 0)
            return "Computer";
        if (nl.indexOf("mesh") >= 0 || nl.indexOf("lora") >= 0 ||
            nl.indexOf("meshtastic") >= 0)
            return "Mesh";
        if (nl.indexOf("tile") >= 0 || nl.indexOf("airtag") >= 0 ||
            nl.indexOf("tracker") >= 0 || nl.indexOf("chipolo") >= 0)
            return "Tracker";
        if (nl.indexOf("lamp") >= 0 || nl.indexOf("bulb") >= 0 ||
            nl.indexOf("light") >= 0 || nl.indexOf("hue") >= 0)
            return "Light";
        if (nl.indexOf("printer") >= 0) return "Printer";
        return "Named";
    }

    return "";
}

void bleAddOrUpdate(BLEAdvertisedDevice& dev) {
    String addr = String(dev.getAddress().toString().c_str());
    String name = dev.haveName() ? String(dev.getName().c_str()) : "";
    int rssi = dev.getRSSI();
    String devType = classifyBLE(dev, name);

    // Update existing
    for (int i = 0; i < btCount; i++) {
        if (btDevices[i].address == addr) {
            btDevices[i].rssi = rssi;
            btDevices[i].lastSeen = millis();
            // Only overwrite name/type if we got better info
            if (name.length() > 0 && btDevices[i].name.length() == 0)
                btDevices[i].name = name;
            if (devType.length() > 0 && btDevices[i].devType.length() == 0)
                btDevices[i].devType = devType;
            return;
        }
    }

    // Add new
    if (btCount < MAX_BT) {
        btDevices[btCount].address = addr;
        btDevices[btCount].name = name;
        btDevices[btCount].rssi = rssi;
        btDevices[btCount].devType = devType;
        btDevices[btCount].lastSeen = millis();
        btCount++;
    }
}

// BLE callbacks (called during scan, non-blocking)
class BTScanCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        bleAddOrUpdate(advertisedDevice);
    }
};

// Async scan-complete callback
void btScanComplete(BLEScanResults results) {
    btScanRunning = false;
    sortBTDevices();
    needsRedraw = true;
}

// ═══════════════════════════════════════════════════════
//  WiFi: ADD/UPDATE
// ═══════════════════════════════════════════════════════

void wifiAddOrUpdate(int idx) {
    String bssid = WiFi.BSSIDstr(idx);
    String ssid = WiFi.SSID(idx);
    int rssi = WiFi.RSSI(idx);
    String enc = encTypeStr(WiFi.encryptionType(idx));
    int ch = WiFi.channel(idx);

    for (int i = 0; i < wifiCount; i++) {
        if (wifiNets[i].bssid == bssid) {
            wifiNets[i].rssi = rssi;
            wifiNets[i].lastSeen = millis();
            if (ssid.length() > 0) wifiNets[i].ssid = ssid;
            wifiNets[i].encType = enc;
            wifiNets[i].channel = ch;
            return;
        }
    }

    if (wifiCount < MAX_WIFI) {
        wifiNets[wifiCount].bssid = bssid;
        wifiNets[wifiCount].ssid = ssid.length() > 0 ? ssid : "<hidden>";
        wifiNets[wifiCount].rssi = rssi;
        wifiNets[wifiCount].encType = enc;
        wifiNets[wifiCount].channel = ch;
        wifiNets[wifiCount].lastSeen = millis();
        wifiCount++;
    }
}

// ═══════════════════════════════════════════════════════
//  LoRa: INIT (RX ONLY!)
// ═══════════════════════════════════════════════════════

bool loraInit() {
    Serial.print("[LoRa] Init SX1262... ");

    // begin(freq, bw, sf, cr, syncWord, power, preamble)
    int state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                            LORA_SYNC_WORD, LORA_TX_POWER, LORA_PREAMBLE);

    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("FAIL (%d), retry with TCXO 1.8V... ", state);
        // Some SX1262 modules need explicit TCXO voltage
        state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                            LORA_SYNC_WORD, LORA_TX_POWER, LORA_PREAMBLE,
                            1.8f, false);
        if (state != RADIOLIB_ERR_NONE) {
            Serial.printf("FAIL (%d)\n", state);
            return false;
        }
    }
    Serial.println("OK");

    // Disable CRC to capture any raw packet (promiscuous)
    radio.setCRC(0);

    // Start continuous receive
    radio.setDio1Action(loraRxISR);
    state = radio.startReceive();
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[LoRa] startReceive FAIL (%d)\n", state);
        return false;
    }

    loraListening = true;
    loraStartTime = millis();
    Serial.printf("[LoRa] RX active: %.3f MHz / BW %.1f kHz / SF%d / CR 4/%d\n",
                  LORA_FREQ, LORA_BW, LORA_SF, LORA_CR);
    return true;
}

void loraHandleRx() {
    if (!loraRxFlag) return;
    loraRxFlag = false;

    int len = radio.getPacketLength();
    if (len <= 0 || len > 255) {
        radio.startReceive();
        return;
    }

    uint8_t buf[256];
    int state = radio.readData(buf, len);
    if (state != RADIOLIB_ERR_NONE) {
        radio.startReceive();
        return;
    }

    float rssi = radio.getRSSI();
    float snr = radio.getSNR();

    // Hex string (first 16 bytes)
    String hex = "";
    int showLen = len > 16 ? 16 : len;
    for (int i = 0; i < showLen; i++) {
        if (buf[i] < 0x10) hex += "0";
        hex += String(buf[i], HEX);
    }
    if (len > 16) hex += "..";
    hex.toUpperCase();

    if (loraCount < MAX_LORA) {
        loraPkts[loraCount].id = ++loraPacketCounter;
        loraPkts[loraCount].rssi = (int)rssi;
        loraPkts[loraCount].snr = snr;
        loraPkts[loraCount].len = len;
        loraPkts[loraCount].dataHex = hex;
        loraPkts[loraCount].timestamp = millis();
        loraCount++;
        sortLoraPkts();
        needsRedraw = true;
    }

    radio.startReceive();
}

// ═══════════════════════════════════════════════════════
//  STOP RADIOS
// ═══════════════════════════════════════════════════════

void stopBLE() {
    if (pBLEScan) {
        pBLEScan->stop();
        pBLEScan->clearResults();
    }
    btScanning = false;
    btScanRunning = false;
    BLEDevice::deinit(false);
}

void stopWiFi() {
    WiFi.scanDelete();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiScanning = false;
    wifiScanPending = false;
}

void stopLoRa() {
    if (loraListening) {
        radio.standby();
        loraListening = false;
    }
}

void stopAllRadios() { stopBLE(); stopWiFi(); stopLoRa(); }

// ═══════════════════════════════════════════════════════
//  DRAW HELPERS
// ═══════════════════════════════════════════════════════

void drawHeader(const char* title) {
    M5Cardputer.Display.fillRect(0, 0, SCREEN_W, HEADER_H, TFT_NAVY);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    M5Cardputer.Display.setTextSize(FONT_SIZE);
    M5Cardputer.Display.setCursor(4, 2);
    M5Cardputer.Display.print(title);
}

void drawStatusBar(const char* text) {
    int y = SCREEN_H - STATUS_H;
    M5Cardputer.Display.fillRect(0, y, SCREEN_W, STATUS_H, TFT_DARKGREY);
    M5Cardputer.Display.setTextColor(TFT_LIGHTGREY, TFT_DARKGREY);
    M5Cardputer.Display.setTextSize(FONT_SIZE);
    M5Cardputer.Display.setCursor(4, y + 1);
    M5Cardputer.Display.print(text);
}

void drawMenu() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    drawHeader("MeshCuter v1.0");

    M5Cardputer.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5Cardputer.Display.setTextSize(FONT_SIZE);

    int y = 28, lineH = 16;
    M5Cardputer.Display.setCursor(20, y);
    M5Cardputer.Display.print("1 - Bluetooth Scanner");
    M5Cardputer.Display.setCursor(20, y + lineH);
    M5Cardputer.Display.print("2 - WiFi Scanner");
    M5Cardputer.Display.setCursor(20, y + lineH * 2);
    M5Cardputer.Display.print("3 - LoRa Scanner (RX)");

    M5Cardputer.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5Cardputer.Display.setCursor(20, y + lineH * 3 + 8);
    M5Cardputer.Display.print("Swiss BAKOM compliant");

    drawStatusBar(";.=scroll /,=cols 0=back");
}

// Column layouts
ColLayout getColsBT() {
    ColLayout c;
    if (showExtraCol) {
        c.id_x=0; c.id_w=3; c.rssi_x=22; c.rssi_w=4;
        c.name_x=52; c.name_w=15; c.extra_x=146; c.extra_w=10;
    } else {
        c.id_x=0; c.id_w=3; c.rssi_x=22; c.rssi_w=4;
        c.name_x=52; c.name_w=28; c.extra_x=0; c.extra_w=0;
    }
    return c;
}

ColLayout getColsWiFi() {
    ColLayout c;
    if (showExtraCol) {
        c.id_x=0; c.id_w=3; c.rssi_x=22; c.rssi_w=4;
        c.name_x=52; c.name_w=12; c.extra_x=130; c.extra_w=14;
    } else {
        c.id_x=0; c.id_w=3; c.rssi_x=22; c.rssi_w=4;
        c.name_x=52; c.name_w=28; c.extra_x=0; c.extra_w=0;
    }
    return c;
}

ColLayout getColsLoRa() {
    ColLayout c;
    if (showExtraCol) {
        c.id_x=0; c.id_w=3; c.rssi_x=22; c.rssi_w=4;
        c.name_x=52; c.name_w=10; c.extra_x=120; c.extra_w=14;
    } else {
        c.id_x=0; c.id_w=3; c.rssi_x=22; c.rssi_w=4;
        c.name_x=52; c.name_w=28; c.extra_x=0; c.extra_w=0;
    }
    return c;
}

void drawColHeaders(const char* c1, const char* c2, const char* c3,
                    const char* c4, ColLayout& cl) {
    int y = HEADER_H + 1;
    M5Cardputer.Display.fillRect(0, y, SCREEN_W, 10, TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5Cardputer.Display.setCursor(cl.id_x + 2, y);
    M5Cardputer.Display.print(c1);
    M5Cardputer.Display.setCursor(cl.rssi_x, y);
    M5Cardputer.Display.print(c2);
    M5Cardputer.Display.setCursor(cl.name_x, y);
    M5Cardputer.Display.print(c3);
    if (showExtraCol && c4) {
        M5Cardputer.Display.setCursor(cl.extra_x, y);
        M5Cardputer.Display.print(c4);
    }
}

// ═══════════════════════════════════════════════════════
//  DRAW SCANNERS
// ═══════════════════════════════════════════════════════

void drawBTScanner() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    char hdr[48];
    snprintf(hdr, sizeof(hdr), "BLE Scanner [%d devices]", btCount);
    drawHeader(hdr);

    ColLayout cl = getColsBT();
    drawColHeaders("#", "dBm", "Name", "Type", cl);

    int maxRows = (SCREEN_H - TABLE_Y - STATUS_H) / ROW_H;

    if (btCount == 0) {
        M5Cardputer.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5Cardputer.Display.setCursor(30, TABLE_Y + 20);
        M5Cardputer.Display.print("Scanning for BLE devices...");
    }

    for (int i = 0; i < maxRows && (scrollPos + i) < btCount; i++) {
        int idx = scrollPos + i;
        int y = TABLE_Y + i * ROW_H;
        uint16_t bg = (i % 2 == 1) ? 0x0841 : TFT_BLACK;
        if (bg != TFT_BLACK)
            M5Cardputer.Display.fillRect(0, y, SCREEN_W, ROW_H, bg);
        M5Cardputer.Display.setTextColor(TFT_GREEN, bg);

        M5Cardputer.Display.setCursor(cl.id_x + 2, y + 1);
        M5Cardputer.Display.printf("%d", idx + 1);

        M5Cardputer.Display.setCursor(cl.rssi_x, y + 1);
        M5Cardputer.Display.printf("%d", btDevices[idx].rssi);

        // Name column: show name if available, otherwise short MAC
        M5Cardputer.Display.setCursor(cl.name_x, y + 1);
        String dispName;
        if (btDevices[idx].name.length() > 0) {
            dispName = btDevices[idx].name;
        } else {
            // Short MAC: last 3 octets in parens
            String a = btDevices[idx].address;
            if (a.length() > 8)
                dispName = "(" + a.substring(a.length() - 8) + ")";
            else
                dispName = "(" + a + ")";
        }
        int maxN = showExtraCol ? 15 : 28;
        M5Cardputer.Display.print(truncStr(dispName, maxN));

        if (showExtraCol) {
            M5Cardputer.Display.setCursor(cl.extra_x, y + 1);
            String dt = btDevices[idx].devType;
            M5Cardputer.Display.print(truncStr(dt.length() > 0 ? dt : "?", 10));
        }
    }

    char status[48];
    snprintf(status, sizeof(status), "%d/%d  ;.=scrl /,=col 0=back",
             btCount > 0 ? scrollPos + 1 : 0, btCount);
    drawStatusBar(status);
}

void drawWiFiScanner() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    char hdr[48];
    snprintf(hdr, sizeof(hdr), "WiFi Scanner [%d nets]", wifiCount);
    drawHeader(hdr);

    ColLayout cl = getColsWiFi();
    drawColHeaders("#", "dBm", "SSID", "Enc/Ch", cl);

    int maxRows = (SCREEN_H - TABLE_Y - STATUS_H) / ROW_H;

    if (wifiCount == 0) {
        M5Cardputer.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5Cardputer.Display.setCursor(30, TABLE_Y + 20);
        M5Cardputer.Display.print("Scanning for WiFi networks...");
    }

    for (int i = 0; i < maxRows && (scrollPos + i) < wifiCount; i++) {
        int idx = scrollPos + i;
        int y = TABLE_Y + i * ROW_H;
        uint16_t bg = (i % 2 == 1) ? 0x0841 : TFT_BLACK;
        if (bg != TFT_BLACK)
            M5Cardputer.Display.fillRect(0, y, SCREEN_W, ROW_H, bg);
        M5Cardputer.Display.setTextColor(TFT_CYAN, bg);

        M5Cardputer.Display.setCursor(cl.id_x + 2, y + 1);
        M5Cardputer.Display.printf("%d", idx + 1);

        M5Cardputer.Display.setCursor(cl.rssi_x, y + 1);
        M5Cardputer.Display.printf("%d", wifiNets[idx].rssi);

        M5Cardputer.Display.setCursor(cl.name_x, y + 1);
        int maxN = showExtraCol ? 12 : 28;
        M5Cardputer.Display.print(truncStr(wifiNets[idx].ssid, maxN));

        if (showExtraCol) {
            M5Cardputer.Display.setCursor(cl.extra_x, y + 1);
            char extra[16];
            snprintf(extra, sizeof(extra), "%s ch%d",
                     wifiNets[idx].encType.c_str(), wifiNets[idx].channel);
            M5Cardputer.Display.print(truncStr(String(extra), 14));
        }
    }

    char status[48];
    snprintf(status, sizeof(status), "%d/%d  ;.=scrl /,=col 0=back",
             wifiCount > 0 ? scrollPos + 1 : 0, wifiCount);
    drawStatusBar(status);
}

void drawLoRaScanner() {
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    char hdr[64];
    snprintf(hdr, sizeof(hdr), "LoRa RX %.3fMHz [%d pkts]", LORA_FREQ, loraCount);
    drawHeader(hdr);

    ColLayout cl = getColsLoRa();
    drawColHeaders("#", "dBm", "Data (hex)", "SNR/Len", cl);

    int maxRows = (SCREEN_H - TABLE_Y - STATUS_H) / ROW_H;

    // Error state
    if (loraInitFailed) {
        M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5Cardputer.Display.setCursor(10, TABLE_Y + 10);
        M5Cardputer.Display.print("SX1262 init failed!");
        M5Cardputer.Display.setCursor(10, TABLE_Y + 24);
        M5Cardputer.Display.print("Check Cap LoRa-1262");
        M5Cardputer.Display.setCursor(10, TABLE_Y + 38);
        M5Cardputer.Display.print("See Serial for error code");
        drawStatusBar("0 = back to menu");
        return;
    }

    // Listening but no packets yet
    if (loraCount == 0 && loraListening) {
        unsigned long elapsed = (millis() - loraStartTime) / 1000;
        M5Cardputer.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
        M5Cardputer.Display.setCursor(20, TABLE_Y + 8);
        M5Cardputer.Display.printf("Listening... %lus elapsed", elapsed);
        M5Cardputer.Display.setCursor(20, TABLE_Y + 22);
        M5Cardputer.Display.printf("BW:%.1fkHz SF:%d CR:4/%d",
                                   LORA_BW, LORA_SF, LORA_CR);
        M5Cardputer.Display.setCursor(20, TABLE_Y + 40);
        M5Cardputer.Display.print("No packets yet - normal if");
        M5Cardputer.Display.setCursor(20, TABLE_Y + 52);
        M5Cardputer.Display.print("no MeshCore nodes nearby");
    }

    for (int i = 0; i < maxRows && (scrollPos + i) < loraCount; i++) {
        int idx = scrollPos + i;
        int y = TABLE_Y + i * ROW_H;
        uint16_t bg = (i % 2 == 1) ? 0x0841 : TFT_BLACK;
        if (bg != TFT_BLACK)
            M5Cardputer.Display.fillRect(0, y, SCREEN_W, ROW_H, bg);
        M5Cardputer.Display.setTextColor(TFT_ORANGE, bg);

        M5Cardputer.Display.setCursor(cl.id_x + 2, y + 1);
        M5Cardputer.Display.printf("%d", loraPkts[idx].id);

        M5Cardputer.Display.setCursor(cl.rssi_x, y + 1);
        M5Cardputer.Display.printf("%d", loraPkts[idx].rssi);

        M5Cardputer.Display.setCursor(cl.name_x, y + 1);
        int maxH = showExtraCol ? 10 : 28;
        M5Cardputer.Display.print(truncStr(loraPkts[idx].dataHex, maxH));

        if (showExtraCol) {
            M5Cardputer.Display.setCursor(cl.extra_x, y + 1);
            char extra[20];
            snprintf(extra, sizeof(extra), "%.1f %dB",
                     loraPkts[idx].snr, loraPkts[idx].len);
            M5Cardputer.Display.print(truncStr(String(extra), 14));
        }
    }

    char status[60];
    snprintf(status, sizeof(status), "%d/%d  ;.=scrl /,=col 0=back",
             loraCount > 0 ? scrollPos + 1 : 0, loraCount);
    drawStatusBar(status);
}

// ═══════════════════════════════════════════════════════
//  ENTER / EXIT
// ═══════════════════════════════════════════════════════

void enterBTScanner() {
    currentScreen = BT_SCAN;
    scrollPos = 0;
    showExtraCol = false;
    needsRedraw = true;

    BLEDevice::init("MeshCuter");
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new BTScanCallbacks(), true);
    pBLEScan->setActiveScan(true);   // active = requests scan response (names!)
    pBLEScan->setInterval(80);
    pBLEScan->setWindow(79);         // window ~ interval = near-continuous
    btScanning = true;
    btScanRunning = false;
    btLastScanStart = 0;
}

void enterWiFiScanner() {
    currentScreen = WIFI_SCAN;
    scrollPos = 0;
    showExtraCol = false;
    needsRedraw = true;

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(50);
    wifiScanning = true;
    wifiScanPending = false;
    wifiLastScanTime = 0;
}

void enterLoRaScanner() {
    currentScreen = LORA_SCAN;
    scrollPos = 0;
    showExtraCol = false;
    loraInitFailed = false;
    needsRedraw = true;

    if (!loraInit()) {
        loraInitFailed = true;
    }
}

void exitToMenu() {
    stopAllRadios();
    currentScreen = MENU;
    scrollPos = 0;
    showExtraCol = false;
    needsRedraw = true;
}

// ═══════════════════════════════════════════════════════
//  KEYBOARD
// ═══════════════════════════════════════════════════════

void handleKeyboard() {
    if (!M5Cardputer.Keyboard.isChange()) return;
    if (!M5Cardputer.Keyboard.isPressed()) return;

    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

    if (status.del) {
        if (currentScreen != MENU) exitToMenu();
        return;
    }

    for (auto c : status.word) {
        switch (currentScreen) {
            case MENU:
                if (c == '1') enterBTScanner();
                else if (c == '2') enterWiFiScanner();
                else if (c == '3') enterLoRaScanner();
                break;

            case BT_SCAN:
            case WIFI_SCAN:
            case LORA_SCAN: {
                int maxItems = 0;
                if (currentScreen == BT_SCAN)   maxItems = btCount;
                if (currentScreen == WIFI_SCAN)  maxItems = wifiCount;
                if (currentScreen == LORA_SCAN)  maxItems = loraCount;

                if (c == '0') {
                    exitToMenu();
                } else if (c == ';') {
                    if (scrollPos > 0) { scrollPos--; needsRedraw = true; }
                } else if (c == '.') {
                    if (scrollPos < maxItems - 1) { scrollPos++; needsRedraw = true; }
                } else if (c == '/') {
                    if (!showExtraCol) { showExtraCol = true; needsRedraw = true; }
                } else if (c == ',') {
                    if (showExtraCol) { showExtraCol = false; needsRedraw = true; }
                }
                break;
            }
        }
    }
}

// ═══════════════════════════════════════════════════════
//  SCAN LOOPS (all non-blocking!)
// ═══════════════════════════════════════════════════════

void btScanLoop() {
    if (!btScanning) return;

    // Start new async scan when previous is done
    if (!btScanRunning && (millis() - btLastScanStart > 500)) {
        btLastScanStart = millis();
        btScanRunning = true;
        // 5 sec async scan, callback on complete, keep known devices
        pBLEScan->start(5, btScanComplete, false);
    }
}

void wifiScanLoop() {
    if (!wifiScanning) return;

    // Start async scan
    if (!wifiScanPending && (millis() - wifiLastScanTime > 5000)) {
        wifiLastScanTime = millis();
        WiFi.scanNetworks(true, true);  // async=true, showHidden=true
        wifiScanPending = true;
    }

    // Check async results
    if (wifiScanPending) {
        int n = WiFi.scanComplete();
        if (n >= 0) {
            for (int i = 0; i < n; i++) wifiAddOrUpdate(i);
            WiFi.scanDelete();
            sortWifiNets();
            wifiScanPending = false;
            needsRedraw = true;
        } else if (n == WIFI_SCAN_FAILED) {
            wifiScanPending = false;
        }
    }
}

void loraScanLoop() {
    if (!loraListening) return;
    loraHandleRx();
}

// ═══════════════════════════════════════════════════════
//  SETUP & LOOP
// ═══════════════════════════════════════════════════════

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setTextSize(FONT_SIZE);
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    Serial.begin(115200);
    Serial.println("\n=== MeshCuter v1.0 ===");
    Serial.println("Swiss BAKOM/OFCOM compliant");
    Serial.printf("LoRa: %.3f MHz / BW %.1f kHz / SF%d / CR 4/%d\n",
                  LORA_FREQ, LORA_BW, LORA_SF, LORA_CR);
    Serial.println("LoRa: RX ONLY - no TX!\n");

    currentScreen = MENU;
    needsRedraw = true;
}

void loop() {
    M5Cardputer.update();
    handleKeyboard();

    switch (currentScreen) {
        case BT_SCAN:   btScanLoop();   break;
        case WIFI_SCAN:  wifiScanLoop(); break;
        case LORA_SCAN:  loraScanLoop(); break;
        default: break;
    }

    if (needsRedraw) {
        needsRedraw = false;
        lastPeriodicRedraw = millis();
        switch (currentScreen) {
            case MENU:       drawMenu();         break;
            case BT_SCAN:    drawBTScanner();    break;
            case WIFI_SCAN:  drawWiFiScanner();  break;
            case LORA_SCAN:  drawLoRaScanner();  break;
        }
    }

    // Periodic redraw for "Listening..." timer and "Scanning..." text
    if ((currentScreen == LORA_SCAN && loraCount == 0 && loraListening) ||
        (currentScreen == BT_SCAN && btCount == 0)) {
        if (millis() - lastPeriodicRedraw > 2000) {
            needsRedraw = true;
        }
    }

    delay(10);
}
