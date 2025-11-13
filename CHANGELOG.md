# Changelog

This document outlines the major changes between the original F1 Tracker code and the enhanced version (`F1_Tracker_3Color.ino`).

## Removed Features

### Web Server & Dashboard
- **Removed:** Full web server implementation with HTML dashboard
- **Removed:** mDNS support (`f1tracker.local` hostname)
- **Removed:** Online dashboard showing:
  - Season calendar
  - All season rounds
  - Pole positions
  - Driver points
  - Constructor standings
- **Reason:** Focus shifted to e-paper display only, reducing code complexity and memory usage

### OTA (Over-The-Air) Updates
- **Removed:** OTA update server functionality
- **Removed:** Ability to update firmware via web interface or `f1tracker.local`
- **Reason:** Simplified deployment model, updates now require USB connection

## Added Features

### Intelligent Update Scheduling
- **Added:** Wall-clock aligned update scheduler
- **Added:** Dynamic update frequency based on race proximity:
  - >48 hours before race: Updates every 6 hours
  - 24-48 hours before race: Updates every 2 hours
  - <24 hours before race: Updates every 30 minutes (to catch starting grid)
  - Race window (6h before to 6h after): Updates every 30 minutes
  - After race: Returns to 2-hour updates
- **Benefit:** Reduces unnecessary API calls while ensuring timely updates during race events

### Multi-Level Caching System
- **Added:** Intelligent API response caching with TTL (Time To Live):
  - Calendar cache: 1 hour TTL
  - Standings cache: 1 hour TTL (invalidated when race finishes)
  - Qualifying cache: 24 hours TTL (never changes once posted)
  - Results cache: 1 hour TTL
  - Availability cache: Separate caching for qualifying/results availability
- **Benefit:** Reduces API calls by ~99%, improving reliability and reducing power consumption

### Smart Display Modes
- **Added:** Automatic display mode switching:
  - Normal mode: Shows driver standings, constructor standings, and last race podium
  - Race approaching (within 18h): Switches to starting grid view
  - Race in progress: Shows "Race in Progress!" and current race grid
  - Post-race: Shows results podium when available
- **Added:** "Awaiting Results" display when next race is approaching

### Enhanced Race State Detection
- **Added:** Real-time race state detection:
  - Detects when race is in progress (0 to +4 hours from start)
  - Detects race window (6 hours before to 6 hours after)
  - Detects when next race is approaching (within 18 hours)
- **Added:** Automatic podium clearing during race events

### Power Management
- **Added:** WiFi power management:
  - WiFi disconnects after updates to save power
  - WiFi reconnects automatically when needed
  - Display hibernates between updates
- **Benefit:** Significantly reduced power consumption for battery-powered applications

### Improved Time Handling
- **Added:** Local timezone conversion for race times
- **Added:** Countdown timer showing days/hours until next race
- **Added:** Formatted local date/time display (MM/DD format, 12-hour time)
- **Added:** Persistent race epoch storage in Preferences

### Better Error Handling
- **Added:** HTTP retry logic with exponential backoff
- **Added:** Graceful degradation on API failures
- **Added:** Cache validation and fallback mechanisms
- **Added:** WiFi reconnection logic with automatic restart on failure

## Code Improvements

### Memory Optimization
- **Reduced:** JSON buffer from 16KB to 12KB (saves 4KB RAM)
- **Added:** String pre-allocation to reduce fragmentation
- **Added:** PROGMEM storage for API templates and logo data
- **Result:** More efficient memory usage, better stability

### Code Organization
- **Improved:** Modular function structure
- **Added:** Template functions for cached API calls
- **Improved:** Clear separation of concerns (display, API, caching, scheduling)
- **Added:** Comprehensive serial logging for debugging

### Hardware Changes

#### Pin Mapping (ESP32-C3)
- **Changed:** Pin assignments optimized for ESP32-C3 Mini:
  - BUSY: GPIO 4 (was GPIO 4)
  - RST: GPIO 5 (was GPIO 16)
  - DC: GPIO 7 (was GPIO 17)
  - CS: GPIO 3 (was GPIO 5)
  - SCK: GPIO 6 (was GPIO 18)
  - MOSI: GPIO 10 (was GPIO 23)
  - MISO: Removed (not needed for display)

#### Board Support
- **Changed:** Optimized for ESP32-C3 (from ESP32)
- **Changed:** Partition scheme recommendation: "Huge APP (3MB No OTA)"
- **Benefit:** Better power efficiency, smaller form factor support

## API Changes

### Endpoint Updates
- **Changed:** API base URL now uses `api.jolpi.ca/ergast/f1/` proxy
- **Added:** Dynamic year detection for API calls
- **Improved:** API URL building with PROGMEM templates

### Data Fetching
- **Added:** JSON filtering to reduce payload size
- **Added:** Cached API responses to reduce network traffic
- **Improved:** Error handling and retry logic

## Display Improvements

### Layout Enhancements
- **Added:** Red divider line in header
- **Improved:** Text alignment and positioning
- **Added:** Better podium visualization with proper spacing
- **Added:** "Race in Progress!" indicator
- **Improved:** Starting grid display formatting

### Font and Rendering
- **Maintained:** U8g2 font rendering system
- **Improved:** Text width calculations for better alignment
- **Added:** UTF-8 safe substring function for driver name abbreviations

## Configuration Changes

### WiFi Setup
- **Changed:** WiFi AP SSID from "F1 display" to "F1Tracker-Setup"
- **Maintained:** Password "formula1"
- **Added:** Visual setup instructions on display during configuration
- **Improved:** WiFiManager integration with callback for display updates

### Timezone
- **Changed:** Default timezone from BST (UK) to EST/EDT (USA)
- **Maintained:** Easy timezone configuration via `MY_TZ` define

## Performance Improvements

- **Reduced:** API calls by ~99% through intelligent caching
- **Reduced:** Power consumption through WiFi and display hibernation
- **Improved:** Update reliability through retry logic and error handling
- **Optimized:** Memory usage through buffer reduction and string management
- **Faster:** Display updates through optimized drawing routines

## Migration Notes

### For Users Upgrading from Original Code

1. **No Web Interface:** The web dashboard is no longer available. All information is displayed on the e-paper screen only.

2. **No OTA Updates:** Firmware updates must be done via USB connection to the ESP32-C3.

3. **Pin Changes:** If migrating from ESP32 to ESP32-C3, update your wiring according to the new pin mapping.

4. **Partition Scheme:** Use "Huge APP (3MB No OTA)" partition scheme for best results.

5. **WiFi Setup:** The WiFi setup process remains similar, but the AP name has changed to "F1Tracker-Setup".

6. **Update Frequency:** The device now updates automatically based on race timing. Manual update intervals are no longer configurable (but can be modified in code if needed).

## Summary

The enhanced version focuses on creating a reliable, power-efficient e-paper display solution. By removing the web server and OTA functionality, the codebase is simpler, uses less memory, and consumes less power. The addition of intelligent scheduling and caching ensures the display stays up-to-date during important race events while minimizing unnecessary updates during off-seasons.

