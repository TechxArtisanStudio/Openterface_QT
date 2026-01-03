# Phase 3: Window Layout & Geometry Logic Extraction

## Overview
Phase 3 extracts window layout, geometry calculations, and video pane positioning logic from MainWindow into a dedicated WindowLayoutCoordinator class. This reduces MainWindow by approximately **350 lines** and improves maintainability by separating spatial concerns from business logic.

## Implementation Date
- **Started**: October 5, 2025
- **Completed**: October 5, 2025

## Goals
1. Extract window resize and geometry logic
2. Separate aspect ratio calculations
3. Centralize fullscreen management
4. Consolidate zoom operations
5. Implement video pane animation with toolbar awareness

## Files Created

### 1. `ui/coordinator/windowlayoutcoordinator.h` (234 lines)
**Purpose**: Class declaration for window layout coordination

**Key Components**:
- Public methods for resize, fullscreen, zoom operations
- Signal emissions for layout changes
- Private helper methods for aspect ratio calculations
- Member variables for window state tracking

**Public Interface**:
```cpp
void doResize();                    // Main resize orchestration
void checkInitSize();               // Initial window sizing
void fullScreen();                  // Toggle fullscreen mode
bool isFullScreenMode() const;      // Query fullscreen state
void zoomIn();                      // Zoom in video pane
void zoomOut();                     // Zoom out video pane
void zoomReduction();               // Reset zoom to fit
void calculateVideoPosition();      // Center window on screen
void animateVideoPane();            // Animate video pane with toolbar
void setToolbarManager(ToolbarManager*); // Set toolbar for animation
```

**Signals**:
- `layoutChanged(QSize)` - Emitted when window size changes
- `fullscreenChanged(bool)` - Emitted when fullscreen state changes
- `zoomChanged(double)` - Emitted when zoom level changes

### 2. `ui/coordinator/windowlayoutcoordinator.cpp` (424 lines)
**Purpose**: Implementation of window layout logic

**Key Methods**:

#### `doResize()`
- Orchestrates main resize operation
- Checks screen bounds
- Delegates to specialized resize handlers
- Updates global window state

#### `handleScreenBoundsResize()`
- Handles resize when exceeding screen limits
- Maintains aspect ratio while fitting to screen
- Adjusts video pane and main window sizes

#### `handleAspectRatioResize()`
- Resizes based on aspect ratio
- Special handling for maximized windows
- Supports portrait and landscape orientations
- Fills available space in maximized mode

#### `checkInitSize()`
- Sets initial window size to 2/3 screen height
- Maintains 16:9 aspect ratio
- Centers window on screen

#### `fullScreen()`
- Toggles between fullscreen and normal mode
- Hides/shows status bar appropriately
- Repositions video pane to center
- Emits fullscreenChanged signal

#### `zoomIn()` / `zoomOut()` / `zoomReduction()`
- Delegates to VideoPane zoom functionality
- Emits zoomChanged signals
- Maintains proper zoom constraints

#### `calculateVideoPosition()`
- Performs resize to update layout
- Centers window on current screen
- Useful after resolution changes

#### `animateVideoPane()` (Fully Implemented)
- **New**: Full implementation with ToolbarManager awareness
- Calculates video pane size based on toolbar visibility
- Handles maximized vs normal window states
- Creates smooth 150ms animations with OutCubic easing
- Includes safety checks for window destruction
- Centers video pane horizontally when window is wider
- Uses QPropertyAnimation for smooth transitions

## Files Modified

### 1. `ui/mainwindow.h`
**Changes**:
- Removed method declarations: `doResize()`, `handleScreenBoundsResize()`, `handleAspectRatioResize()`, `fullScreen()`, `isFullScreenMode()`, `onZoomIn()`, `onZoomOut()`, `onZoomReduction()`, `checkInitSize()`, `calculate_video_position()`, `animateVideoPane()`, `centerVideoPane()`
- Removed member variables: `fullScreenState`, `oldWindowState`, `factorScale`, `currentRatioType`
- Added: `#include "ui/coordinator/windowlayoutcoordinator.h"`
- Added: `WindowLayoutCoordinator *m_windowLayoutCoordinator;`

**Lines Removed**: ~60 lines

### 2. `ui/mainwindow.cpp`
**Changes**:
- Removed method implementations (~350 lines total):
  - `fullScreen()` (29 lines)
  - `onZoomIn()` (8 lines)
  - `onZoomOut()` (7 lines)
  - `onZoomReduction()` (8 lines)
  - `checkInitSize()` (30 lines)
  - `doResize()` (55 lines)
  - `handleScreenBoundsResize()` (35 lines)
  - `handleAspectRatioResize()` (63 lines)
  - `calculate_video_position()` (20 lines)
  - `animateVideoPane()` (95 lines)
  - `isFullScreenMode()` (4 lines)

- Modified coordinator initialization:
  - Moved `m_windowLayoutCoordinator` initialization to early in constructor (before `checkInitSize()` call)
  - Added `setToolbarManager()` call after toolbar initialization

- Updated method calls to use coordinator:
  - `checkInitSize()` → `m_windowLayoutCoordinator->checkInitSize()`
  - `doResize()` → `m_windowLayoutCoordinator->doResize()`
  - `calculateVideoPosition()` → `m_windowLayoutCoordinator->calculateVideoPosition()`
  - `fullScreen()` → `m_windowLayoutCoordinator->fullScreen()`
  - `isFullScreenMode()` → `m_windowLayoutCoordinator->isFullScreenMode()`
  - `zoomIn/Out/Reduction()` → Replaced with lambda calls to coordinator

- Connected zoom buttons to coordinator via lambdas
- Updated `resizeEvent()` to use coordinator
- Fixed initialization order to prevent segmentation fault

**Lines Removed**: ~350 lines
**Lines Added**: ~30 lines (coordinator calls and lambdas)
**Net Reduction**: ~320 lines

### 3. `cmake/SourceFiles.cmake`
**Changes**:
```cmake
set(UI_COORDINATOR_SOURCES
    ui/coordinator/devicecoordinator.cpp ui/coordinator/devicecoordinator.h
    ui/coordinator/menucoordinator.cpp ui/coordinator/menucoordinator.h
    ui/coordinator/windowlayoutcoordinator.cpp ui/coordinator/windowlayoutcoordinator.h
)
```

### 4. `openterfaceQT.pro`
**Changes**:
```qmake
SOURCES += ui/coordinator/windowlayoutcoordinator.cpp
HEADERS += ui/coordinator/windowlayoutcoordinator.h
```

## Architecture Design

### Separation of Concerns

**WindowLayoutCoordinator** (Geometry & Layout):
- Spatial calculations and positioning
- Aspect ratio maintenance
- Screen bounds checking
- Video pane sizing and positioning
- Animation of layout changes

**WindowControlManager** (Behavior & Interaction):
- Toolbar auto-hide behavior
- Mouse edge detection
- Timer-based interactions
- Window state tracking for UI behavior

**MainWindow** (Orchestration):
- Creates and owns both coordinators
- Connects them via signals
- Delegates layout operations to WindowLayoutCoordinator
- Delegates UI behavior to WindowControlManager

### Coordinator Integration

```cpp
// In MainWindow constructor:
m_windowLayoutCoordinator = new WindowLayoutCoordinator(
    this, videoPane, menuBar(), statusBar(), this);
    
// Set toolbar manager for animation coordination
m_windowLayoutCoordinator->setToolbarManager(toolbarManager);

// Connect toolbar visibility to layout animations
connect(m_windowControlManager, &WindowControlManager::toolbarVisibilityChanged,
        this, &MainWindow::onToolbarVisibilityChanged);
```

### Animation Flow

1. **Toolbar visibility changes** (WindowControlManager)
2. **MainWindow receives signal** → `onToolbarVisibilityChanged()`
3. **Calls coordinator** → `m_windowLayoutCoordinator->animateVideoPane()`
4. **Coordinator animates** with toolbar awareness
5. **Emits signals** for further coordination if needed

## Critical Bug Fixes

### Issue: Segmentation Fault on Startup
**Problem**: Application crashed at "Init camera..." log line with segmentation fault (exit code 134)

**Root Cause**: `m_windowLayoutCoordinator` was being used (via `checkInitSize()`) before it was initialized in the constructor.

**Solution**: Moved coordinator initialization to immediately after `ui->setupUi(this)`, ensuring it's available before first use.

**Code Change**:
```cpp
// BEFORE (line ~347): checkInitSize() called here
// AFTER (line ~141): m_windowLayoutCoordinator initialized here
```

This moved the initialization ~200 lines earlier in the constructor execution order.

## Benefits

### 1. Code Organization
- Window layout logic now centralized in one coordinator
- Clear separation between layout calculations and UI behavior
- Easier to locate and modify geometry-related code

### 2. Maintainability
- Reduced MainWindow complexity (~320 lines removed)
- Single responsibility: WindowLayoutCoordinator only handles layout
- Easier to test layout logic in isolation

### 3. Reusability
- WindowLayoutCoordinator can be reused for other windows
- Animation logic can be refined without touching MainWindow
- Toolbar coordination is now explicit and clear

### 4. Extensibility
- Easy to add new layout modes or aspect ratios
- Animation improvements localized to one file
- Window state management centralized

## Testing Checklist

- [x] Window resize behavior
- [x] Fullscreen toggle
- [x] Zoom in/out/reduction
- [x] Maximized window behavior
- [x] Aspect ratio maintenance
- [x] Initial window sizing
- [x] Window centering after resolution change
- [x] Video pane animation with toolbar visibility
- [ ] Animation smoothness with toolbar show/hide
- [ ] Multi-monitor support
- [ ] Different aspect ratios (portrait/landscape)
- [ ] Screen bounds on small displays

## Metrics

### Code Reduction
- **MainWindow.h**: -60 lines
- **MainWindow.cpp**: -320 lines (net)
- **Total Reduction**: -380 lines from MainWindow
- **New Code**: +658 lines in WindowLayoutCoordinator
- **Net Project**: +278 lines (investment in organization)

### Complexity Reduction
- **MainWindow methods**: -11 methods
- **WindowLayoutCoordinator methods**: +10 public methods, +2 private helpers
- **Cyclomatic complexity**: Reduced in MainWindow

### Phase Statistics
- **Phase 1 (DeviceCoordinator)**: -330 lines from MainWindow
- **Phase 2 (MenuCoordinator)**: -200 lines from MainWindow  
- **Phase 3 (WindowLayoutCoordinator)**: -380 lines from MainWindow
- **Total Reduction So Far**: -910 lines from MainWindow (~40% reduction)

## Known Limitations

1. **Animation Complexity**: `animateVideoPane()` still has complex conditional logic that could be refactored further
2. **Toolbar Dependency**: Coordinator depends on ToolbarManager being set via setter (not constructor injection)
3. **Global State**: Still uses GlobalVar and GlobalSetting singletons for some state
4. **Maximized Window Logic**: Special handling for maximized windows adds complexity

## Future Improvements

1. **Extract Animation Strategy**: Consider separate animation strategy classes for different window states
2. **Reduce Singleton Usage**: Pass necessary values through coordinator interface instead of global access
3. **Add Unit Tests**: Create unit tests for geometry calculations
4. **Metrics/Telemetry**: Add metrics for layout performance and animation frame rates
5. **Configuration**: Allow animation duration and easing curves to be configurable

## Related Documentation
- Phase 1: [Device Coordinator](phase1_device_coordinator.md)
- Phase 2: [Menu Coordinator](phase2_menu_coordinator.md)
- Architecture: [Window Control Manager](../window_control_manager.md)

## Dependencies
```
WindowLayoutCoordinator depends on:
  - VideoPane (for resizing and positioning)
  - GlobalSetting (for screen ratio settings)
  - GlobalVar (for capture dimensions and window state)
  - ToolbarManager (for animation coordination)
  - QMainWindow, QMenuBar, QStatusBar (for window metrics)
```

## Conclusion

Phase 3 successfully extracts window layout logic into a dedicated coordinator while maintaining proper coordination with WindowControlManager for toolbar-related behavior. The separation of concerns improves code organization and maintainability, with the full implementation of `animateVideoPane()` providing smooth transitions when toolbar visibility changes.

The critical initialization order bug was identified and fixed, ensuring stable application startup. The coordinator pattern continues to prove valuable for organizing MainWindow's extensive responsibilities.
