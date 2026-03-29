![MeshCute WiFi Menu](images/meshcute.jpg)

# MeshCute

A multi-scanner tool for the **M5Stack Cardputer Adv** featuring Bluetooth Low Energy (BLE), WiFi, and LoRa packet scanning. The LoRa scanner operates in **receive-only mode** and is configured for Swiss radio regulations (BAKOM/OFCOM).

---

## Why

It is meant to be taken to Meshtastic or Meshcore meetings and diagnose problems immediately. (Is my device visible in Bluetooth / WiFi / Lora?)

## Hardware Requirements

- **M5Stack Cardputer Adv** (ESP32-S3, Stamp-S3A module)
- **Cap LoRa-1262** (SX1262 + ATGM336H) — required for the LoRa scanner. The BLE and WiFi scanners work without this accessory.
- A LoRa antenna connected to the Cap's RP-SMA port (never power the SX1262 without an antenna attached)

## Software Requirements

- **Arduino IDE** 2.x (or 1.8.19+)
- **Board Manager:** ESP32 by Espressif Systems
- **Libraries:**
  - `M5Cardputer` — install from GitHub: https://github.com/m5stack/M5Cardputer
  - `RadioLib` — install from the Arduino Library Manager

## Flashing

> **Important:** The default partition scheme is too small for this sketch.  
> Before compiling, change the following setting in the Arduino IDE:
>
> **Tools → Partition Scheme → "Huge APP (3MB No OTA/1MB SPIFFS)"**
>
> Without this change the build will fail with a "text section exceeds available space" error.

Other recommended board settings:

| Setting              | Value                      |
|----------------------|----------------------------|
| Board                | ESP32S3 Dev Module         |
| USB CDC On Boot      | Enabled                    |
| Upload Speed         | 1500000                    |
| Partition Scheme     | Huge APP (3MB No OTA/1MB SPIFFS) |

To enter download mode: set the power switch to **OFF**, press and hold the **G0** button on the back, switch power to **ON**, then release G0.

Never forget that by flashing you overwrite your old software!

---

## Usage

### Navigation Keys

The Cardputer keyboard has no dedicated arrow keys. MeshCuter uses the following keys instead:

| Key         | Function                            |
|-------------|-------------------------------------|
| `1` `2` `3` | Select scanner from main menu      |
| `;`         | Scroll up                           |
| `.`         | Scroll down                         |
| `/`         | Show extra column (device type, encryption, SNR) |
| `,`         | Hide extra column                   |
| `0`         | Return to main menu                 |
| Backspace   | Return to main menu                 |

### Main Menu

The main menu shows three options:

```
MeshCuter v1.0
─────────────────────
1 - Bluetooth Scanner
2 - WiFi Scanner
3 - LoRa Scanner (RX)
```

Press the corresponding number key to enter a scanner.

### 1 — Bluetooth Scanner

Performs continuous BLE advertising scans using the ESP32-S3's built-in Bluetooth radio. Active scanning is enabled, which means the device sends scan requests to get device names from scan responses.

**Default columns:**

| Column | Description                                      |
|--------|--------------------------------------------------|
| #      | Row number (sorted by signal strength)           |
| dBm    | Received signal strength (RSSI)                  |
| Name   | Device name if available, short MAC otherwise    |

**Extra column** (press `/`):

| Column | Description                                      |
|--------|--------------------------------------------------|
| Type   | Device classification (Phone, Audio, Wearable, Tracker, HID, Computer, Mesh, Light, etc.) |

Devices are classified using BLE appearance values, service UUIDs, and name heuristics. Many BLE devices (especially Apple devices using random addresses) do not broadcast a name — these are shown with a partial MAC address in parentheses.

Devices remain in the list once discovered. The list is sorted by signal strength (strongest first) and updates every few seconds.

Known issues: Sometimes problems with name displays and name resolutions.

### 2 — WiFi Scanner

Scans for nearby WiFi access points using the ESP32-S3's built-in WiFi radio. Hidden networks are included in the scan.

**Default columns:**

| Column | Description                                      |
|--------|--------------------------------------------------|
| #      | Row number (sorted by signal strength)           |
| dBm    | Received signal strength (RSSI)                  |
| SSID   | Network name (or `<hidden>` for hidden networks) |

**Extra column** (press `/`):

| Column | Description                                      |
|--------|--------------------------------------------------|
| Enc/Ch | Encryption type (OPEN, WEP, WPA, WPA2, WPA3) and channel number |

Networks remain in the list once discovered and are re-sorted on each scan cycle.

Known issues: This works quite nice so far.

### 3 — LoRa Scanner (RX Only)

Listens for LoRa packets on the MeshCore Switzerland community frequency. **This scanner only receives — it never transmits.**

**Default Swiss configuration (MeshCore CH community):**

| Parameter        | Value         |
|------------------|---------------|
| Frequency        | 869.618 MHz   |
| Bandwidth        | 62.5 kHz      |
| Spreading Factor | SF8           |
| Coding Rate      | 8             |
| Sync Word        | 0x34          |

This frequency lies within the SRD High Power sub-band (869.4–869.65 MHz) as defined by BAKOM/OFCOM and ETSI EN 300 220. Since MeshCuter only receives, duty cycle limits do not apply.

**Default columns:**

| Column | Description                                      |
|--------|--------------------------------------------------|
| #      | Packet ID (incremental)                          |
| dBm    | Received signal strength (RSSI)                  |
| Data   | First 16 bytes of payload in hexadecimal         |

**Extra column** (press `/`):

| Column | Description                                      |
|--------|--------------------------------------------------|
| SNR/Len | Signal-to-noise ratio (dB) and packet length in bytes |

If no packets are received, a "Listening..." screen is shown with elapsed time and the active LoRa parameters. This is normal when no MeshCore nodes are transmitting nearby.

If the Cap LoRa-1262 is not attached or not detected, an error screen is displayed. Check the Serial Monitor (115200 baud) for the specific error code.

Known issues: It might be that no LoRa devices are shown. Working on it.

---

## How This Project Was Made

MeshCuter was developed collaboratively with **Claude** (Anthropic's AI assistant). The code, architecture, and documentation were created through an iterative conversation between the author and Claude, with the author testing on real hardware and providing feedback.
(Further human development is planned.)

---

## In the future

I would wish for a smaller software size, more options and especially the possibility to set own radio options outside of Switzerland.

## Author

```
Name:    Dennis Eichardt, Switzerland
Contact: dennis@eichardt.net
GitHub:  https://github.com/MadScientistCH
```

---

## License

This project is licensed under the **GNU General Public License v3.0 (GPLv3)**.  
See the [LICENSE](LICENSE) file for the full license text.

## Disclaimer

THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT.

IN NO EVENT SHALL THE AUTHORS, CONTRIBUTORS, OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**The user is solely responsible for compliance with all applicable local, national, and international radio regulations.** While this software is configured for Swiss BAKOM/OFCOM regulations and operates in receive-only mode, it is the user's responsibility to verify that their use of this software and associated hardware complies with the laws of their jurisdiction.

The authors assume no liability for any damage to hardware, interference with radio communications, legal consequences, or any other damages resulting from the use of this software.

## Dedicated

Dedicated to Mesh Stamm, every first Saturday each month in OBI Schönbühl, Switzerland. <3