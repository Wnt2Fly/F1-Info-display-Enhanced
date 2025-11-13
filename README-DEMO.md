# F1 Tracker - Demo Mode

A demonstration version of the F1 Tracker that automatically cycles through all display modes. This is designed for **taking photos** of the device in different states and is **NOT intended for production use**.

## ⚠️ Important Notice

**This is a TEMPORARY demo file to verify your setup is working**

- **DO NOT** use this in production
- **DO NOT** leave this running permanently
- Use `F1-Info-display-Enhanced.ino` for normal operation

## Purpose

This demo file automatically cycles through all 4 display modes every 7 seconds, making it easy to photograph the device showing:
- Normal operation mode
- Race approaching mode
- Race in progress mode
- Post-race mode

## Features

### Automatic Mode Cycling
- **4 Display Modes** that cycle automatically
- **7-second intervals** between mode changes (configurable)
- **Real API data** from Ergast F1 API
- **Continuous loop** until power off or code change

### Display Modes

#### Mode 0: Normal Operation
- **Top 10 Drivers** standings with points
- **Top 5 Constructors** standings
- **Last Race Podium** with winner names
- **Next Race** information with countdown

#### Mode 1: Race Approaching (Within 18h)
- **Starting Grid** (qualifying positions)
- **Top 5 Constructors** standings
- **"Awaiting Results"** podium placeholder
- **Next Race** information

#### Mode 2: Race In Progress
- **"Race in Progress!"** indicator
- **Starting Grid** (current race)
- **Top 5 Constructors** standings
- **Empty Podium** (no results yet)

#### Mode 3: Post-Race
- **Top 10 Drivers** standings with points
- **Top 5 Constructors** standings
- **Last Race Podium** with winner names
- **Next Race** information

## Hardware Requirements

Same as the main F1 Tracker:
- **ESP32-C3 Mini** (or compatible ESP32-C3 board)
- **3-Color E-Paper Display** - GxEPD2_290_C90c (296x128 pixels)
- **SPI Connections** - See pin mapping below

### Pin Connections

| Display Pin | ESP32-C3 Pin | Function |
|------------|-------------|----------|
| BUSY       | GPIO 4      | Busy signal |
| RST        | GPIO 5      | Reset |
| DC         | GPIO 7      | Data/Command |
| CS         | GPIO 3      | Chip Select |
| SCK        | GPIO 6      | SPI Clock |
| MOSI       | GPIO 10     | SPI Data |

## Installation

### 1. Prerequisites

Same requirements as the main project:
- **Arduino IDE** (1.8.x or 2.x)
- **ESP32 board support** installed
- **Required libraries:**
  - ArduinoJson (v7.x)
  - GxEPD2
  - U8g2_for_Adafruit_GFX
  - WiFiManager

### 2. Upload Demo Code

1. Open `F1-Info-display-Enhanced-Demo.ino` in Arduino IDE
2. Select your ESP32-C3 board
3. Select partition scheme: **"Huge APP (3MB No OTA)"**
4. Connect ESP32-C3 via USB
5. Select correct COM port
6. Click **Upload**

### 3. First Run

On first boot:
1. Device creates WiFi AP: `F1Tracker-Setup` (password: `formula1`)
2. Connect to this network from your phone/computer
3. Select your home WiFi network in the captive portal
4. Device connects and fetches F1 data
5. Demo mode starts automatically

## Usage

### Starting the Demo

1. **Upload the code** to your ESP32-C3
2. **Wait for WiFi connection** (first time setup may be needed)
3. **Data loads** - you'll see "DEMO MODE - Cycling through all modes..."
4. **Demo starts** - modes cycle automatically every 7 seconds

### Taking Photos

1. **Position your camera** ready to photograph the display
2. **Wait for the mode** you want to capture
3. **Take the photo** - you have 7 seconds per mode
4. **Wait for next mode** or restart if needed

### Mode Timing

- Each mode displays for **7 seconds**
- Full cycle (4 modes) takes **28 seconds**
- Modes cycle in order: 0 → 1 → 2 → 3 → 0 (repeat)

### Stopping the Demo

- **Power off** the device, or
- **Upload different code** (e.g., the main F1 Tracker code)

## Configuration

### Changing Display Interval

To change how long each mode displays, edit line 26 in the demo file:

```cpp
const int DEMO_DELAY_MS = 7000;  // Change this value (in milliseconds)
```

Examples:
- `5000` = 5 seconds per mode
- `10000` = 10 seconds per mode
- `15000` = 15 seconds per mode

### Changing Starting Mode

To start from a different mode, edit line 25:

```cpp
int demoMode = 0;  // 0=Normal, 1=Race Approaching, 2=Race In Progress, 3=Post-Race
```

## Serial Monitor Output

The demo provides detailed serial output:

```
=== F1 TRACKER DEMO MODE ===
Cycles through all display modes every 7 seconds

[WiFi] Starting WiFi setup...
[WiFi] Connected!
IP: 192.168.1.xxx

[Time] Synced!

[DEMO] Fetching calendar data...
[DATA] Last: R23 Abu Dhabi GP
[DATA] Next: R1 Bahrain GP

[DEMO] Data loaded. Starting mode cycle...

[DEMO] Switching to mode 0
[DEMO] Mode 0: Normal
[DEMO] Switching to mode 1
[DEMO] Mode 1: Race Approaching
[DEMO] Switching to mode 2
[DEMO] Mode 2: Race In Progress
[DEMO] Switching to mode 3
[DEMO] Mode 3: Post-Race
```

## Troubleshooting

### Display Not Updating

1. **Check Serial Monitor** - Look for error messages
2. **Verify WiFi Connection** - Device must be connected to internet
3. **Check API Access** - Verify API endpoint is accessible
4. **Wait for Data Load** - First data fetch may take 10-30 seconds

### Wrong Data Showing

- **API may not have data** for current/next race
- Demo uses **last completed race** for podium results
- Demo uses **next race** for starting grid (if available)
- If no qualifying data exists, grid will show "No grid data"

### Mode Not Changing

- **Check Serial Monitor** - Should show mode changes every 7 seconds
- **Verify code uploaded correctly** - Re-upload if needed
- **Check display refresh** - E-paper takes 15-30 seconds to refresh

### WiFi Issues

- **First time:** Connect to `F1Tracker-Setup` AP and configure
- **Subsequent runs:** Device remembers WiFi credentials
- **Reset WiFi:** Hold reset button or erase flash if needed

## Data Requirements

The demo needs internet access to fetch:
- **Race Calendar** - Last and next race information
- **Driver Standings** - Current championship standings
- **Constructor Standings** - Team championship standings
- **Qualifying Results** - Starting grid positions
- **Race Results** - Podium finishers

If any data is unavailable:
- Demo will still run but may show placeholder text
- Some modes may look incomplete
- Check Serial Monitor for specific errors

## Returning to Normal Operation

To switch back to the main F1 Tracker:

1. **Open** `F1-Info-display-Enhanced.ino` in Arduino IDE
2. **Upload** to your ESP32-C3
3. Device will resume normal operation with intelligent scheduling

## Technical Details

### Memory Usage
- Similar to main code: ~450KB flash, ~40-70KB RAM
- No caching in demo mode (fetches fresh data each cycle)
- More API calls than production code

### Power Consumption
- **Higher than production** - WiFi stays connected, display refreshes every 7 seconds
- **Not optimized** for battery operation
- **Intended for** short photography sessions

### API Calls
- Makes API calls for each mode change
- No caching (unlike production code)
- May hit rate limits if run for extended periods

## Limitations

- **Not optimized** for power consumption
- **No intelligent scheduling** - just cycles modes
- **No caching** - fetches data every cycle
- **Continuous operation** - doesn't hibernate
- **WiFi always on** - unlike production code

## Support

For issues:
1. Check **Serial Monitor** output for error messages
2. Verify **WiFi connection** is stable
3. Ensure **API endpoint** is accessible
4. Check **pin connections** are correct

## License

Same license as the main project. See `LICENSE` file for details.

---

**Remember:** This is a **DEMO ONLY** file. Use `F1-Info-display-Enhanced.ino` for normal operation! 🏎️

