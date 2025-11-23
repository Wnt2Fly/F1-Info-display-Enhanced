# Changelog

All notable changes to the F1 Info Display Enhanced project will be documented in this file.

## [Unreleased]

_No new changes yet - all recent changes moved to v1.1.0 below_

## [1.1.0] - 2024-12-XX

### Added

#### Debug Mode
- **Interactive Debug Mode** - Complete time simulation system for testing all display modes
  - Set `DEBUG_MODE` to 1 to enable interactive testing
  - Time offset simulation allows testing scenarios using past races
  - All time calculations use simulated time while API calls use real time
  - Display shows `[DEBUG] Mode X` indicator in header when in debug mode
  - WiFi stays connected in debug mode for faster testing
  - Auto-refresh disabled by default in debug mode (only updates on manual command)

#### Interactive Debug Menu
- **Numbered Menu System** - Easy-to-use menu with numbered options (1-8)
  - Menu automatically displays on startup
  - Menu reappears after each command completes
  - Type numbers (1-8) or full commands - both work
- **Menu Options:**
  - `1` or `normal` - Jump to 3 days before race (Mode 1: Normal Operation)
  - `2` or `before` - Jump to 2 hours before race (Mode 2: Race Approaching)
  - `3` or `during` - Jump to race start time (Mode 3: Race In Progress)
  - `4` or `after` - Jump to 5 hours after race start (Mode 4: Post-Race)
  - `5` or `refresh` - Force immediate display update
  - `6` or `status` - Show current offset, real time, simulated time, and race state
  - `7` or `reset` - Reset to default offset (-30 days)
  - `8` or `autorefresh` - Toggle auto-refresh on/off
- **Time Adjustments** (also available):
  - `+1h`, `+2h`, `+6h` - Add hours to time offset
  - `-1h`, `-2h`, `-6h` - Subtract hours from time offset
  - `+1d`, `+2d`, `+7d` - Add days to time offset
  - `-1d`, `-2d`, `-7d` - Subtract days from time offset
  - `set <seconds>` - Set exact time offset
- **Menu Commands:**
  - `menu` or `help` or `?` - Show menu again

#### Display Mode Improvements
- **Mode 1: Normal Operation** - Driver standings, countdown, last race podium
- **Mode 2: Race Approaching** - Starting grid, "Awaiting Results" message
- **Mode 3: Race In Progress** - "Race in Progress!" indicator, starting grid, empty podium
- **Mode 4: Post-Race** - Driver standings (when results available), podium with winners

### Fixed

#### Scheduler Improvements
- **Race State Detection** - Fixed scheduler to continue updates during and after race
  - Added `lastRaceJustFinished` check that runs before checking `nextRaceEpoch`
  - Ensures 30-minute updates continue for 4 hours after race start
  - Handles case where race moves from "next" to "last" in API
  - Prevents display from getting stuck at last update time

#### Display Logic Fixes
- **Grid vs Standings Display** - Fixed logic for when to show starting grid vs driver standings
  - Grid shows: Before race (within 18h) OR during race until results available
  - Driver standings show: After results become available
  - Grid never shows after results are posted
- **"Awaiting Results" Display** - Fixed to show "Awaiting Results" instead of countdown when grid is displayed
  - Shows "Awaiting Results" in Next Race section when starting grid is active
  - Properly indicates race is approaching and results are pending
- **Mode 2 Podium Display** - Fixed to show blank podium with "Awaiting Results" instead of old race results
  - Mode 2 (Race Approaching) now correctly hides previous race podium
  - Shows empty podium boxes with "Awaiting Results" text
- **Mode 3 Grid Display** - Fixed to always show starting grid during race
  - Mode 3 (Race In Progress) now correctly displays grid regardless of qualifying data availability
  - Handles race moving from nextRound to lastRound in API
- **Mode 4 Detection** - Fixed "after" command to jump to 5 hours after race start
  - Ensures Mode 4 is correctly detected (requires >4h past race and not in race window)
  - Properly shows driver standings and podium with results

#### Serial Communication
- **Enhanced Serial Output** - Added progress messages for debug commands
  - Shows step-by-step progress during display refresh
  - Indicates when WiFi is connecting, calendar is fetching, and display is refreshing
  - Provides feedback that 15-30 second refresh time is normal for e-paper displays
- **Interactive Menu System** - Menu displays automatically and after each command
  - Shows numbered options for easy selection
  - Displays current auto-refresh status
  - Menu reappears after commands complete for continuous testing

### Changed

#### Time Management
- **Simulated Time System** - New `getSimulatedTime()` and `getSimulatedLocalTime()` functions
  - All race state detection uses simulated time in debug mode
  - API calls continue to use real time for correct data fetching
  - Time offset is configurable via `debugTimeOffsetSeconds` variable

#### Code Organization
- **Debug Mode Compilation** - All debug features compile out when `DEBUG_MODE = 0`
  - No performance impact in production mode
  - Clean separation between debug and production code
- **Code Simplification** - Removed redundant mode detection calls from display functions
  - DrawDrivers() and DrawLastRace() use simple timing checks
  - Mode detection only used in DrawTime() for header display
  - Reduced code complexity while maintaining functionality

### Technical Details

#### New Functions
- `getSimulatedTime()` - Returns real time + offset in debug mode
- `getSimulatedLocalTime()` - Populates timeinfo with simulated time
- `getCurrentDisplayMode()` - Returns current display mode (1-4) in debug mode
- `handleDebugCommands()` - Processes interactive serial commands
- `printDebugMenu()` - Displays interactive menu with numbered options

#### Modified Functions
- `DrawDrivers()` - Simplified logic using timing checks (grid before/during race, standings otherwise)
- `DrawLastRace()` - Fixed to show blank podium in Mode 2, results in Mode 4
- `DrawNextRace()` - Added check for grid display to show "Awaiting Results"
- `DrawTime()` - Shows `[DEBUG] Mode X` in header when debug mode enabled
- `shouldUpdateNow()` - Added `lastRaceJustFinished` check for post-race updates
- `disconnectWiFiIfIdle()` - Keeps WiFi connected in debug mode for faster testing
- `loop()` - Checks for debug commands every second (instead of 60s) for responsive interaction
- `setup()` - Added debug mode initialization and menu display

#### Constants and Variables
- `DEBUG_MODE` - Compile-time flag to enable/disable debug mode
- `debugTimeOffsetSeconds` - Runtime variable for time offset (default: -30 days)
- `debugAutoRefresh` - Runtime flag to enable/disable auto-refresh in debug mode (default: false)

## [1.0.0] - Initial Release

### Added
- Race calendar display
- Driver standings (top 10)
- Constructor standings (top 5)
- Starting grid display (when available)
- Race results podium
- Smart update scheduling based on race timing
- Multi-level caching system
- WiFi Manager for easy setup
- Timezone support (EST/EDT default)

---

**Note:** This changelog follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) format.

