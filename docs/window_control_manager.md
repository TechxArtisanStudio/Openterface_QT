# Window Control Manager

## Overview

The `WindowControlManager` is a generic, reusable component for managing window behaviors in Qt applications. It provides a clean, maintainable solution for common window control patterns such as auto-hiding toolbars, edge detection, and window state management.

## Features

### 1. Auto-Hide Toolbar
- Automatically hides the toolbar after a configurable delay when the window is maximized
- Toolbar reappears when user moves mouse to the top edge of the window
- Prevents hiding when menus are open
- Smooth animations for show/hide transitions

### 2. Edge Detection
- Detects when mouse hovers at the top edge of the window
- Configurable threshold (pixels from edge)
- Emits signals for custom behavior

### 3. Window State Management
- Tracks maximized, fullscreen, and normal window states
- Automatically adjusts behavior based on window state
- Proper cleanup when window is restored to normal size

### 4. Event Filtering
- Non-intrusive event filtering for mouse tracking
- Automatic installation/removal of event filters
- No conflicts with existing event handlers

## Usage

### Basic Setup

```cpp
#include "ui/windowcontrolmanager.h"

// In your MainWindow constructor
m_windowControlManager = new WindowControlManager(this, toolbarManager->getToolbar(), this);

// Enable auto-hide with default settings
m_windowControlManager->setAutoHideEnabled(true);

// Connect to signals
connect(m_windowControlManager, &WindowControlManager::toolbarVisibilityChanged,
        this, &MainWindow::onToolbarVisibilityChanged);
```

### Configuration

```cpp
// Set auto-hide delay (milliseconds)
m_windowControlManager->setAutoHideDelay(10000);  // 10 seconds

// Set edge detection threshold (pixels from top edge)
m_windowControlManager->setEdgeDetectionThreshold(5);  // 5 pixels

// Set animation duration (milliseconds)
m_windowControlManager->setAnimationDuration(300);  // 300ms
```

### Manual Control

```cpp
// Manually show/hide toolbar
m_windowControlManager->showToolbar();
m_windowControlManager->hideToolbar();
m_windowControlManager->toggleToolbar();

// Query state
bool isHidden = !m_windowControlManager->isToolbarVisible();
bool isMaximized = m_windowControlManager->isMaximized();
bool isAutoHideEnabled = m_windowControlManager->isAutoHideEnabled();
```

### Signals

```cpp
// Emitted when toolbar visibility changes
connect(manager, &WindowControlManager::toolbarVisibilityChanged, 
        this, [](bool visible) {
    qDebug() << "Toolbar visible:" << visible;
});

// Emitted when auto-hide is triggered
connect(manager, &WindowControlManager::autoHideTriggered,
        this, []() {
    qDebug() << "Toolbar auto-hidden";
});

// Emitted when mouse hovers at top edge
connect(manager, &WindowControlManager::edgeHoverDetected,
        this, []() {
    qDebug() << "Mouse at top edge";
});
```

## Architecture

### Class Structure

```
WindowControlManager
├── Configuration
│   ├── Auto-hide settings (delay, enabled)
│   ├── Edge detection (threshold)
│   └── Animation settings (duration)
├── State Management
│   ├── Window states (maximized, fullscreen, normal)
│   ├── Toolbar state (visible, hidden, auto-hidden)
│   └── Mouse tracking (position, edge detection)
├── Timers
│   ├── Auto-hide timer (single-shot)
│   └── Edge check timer (repeating)
└── Event Handling
    ├── Event filter for mouse tracking
    ├── Window state change detection
    └── Menu detection (prevent hide during menu use)
```

### Behavior Flow

1. **Window Maximized**
   ```
   User maximizes window
   → WindowControlManager detects state change
   → Start auto-hide timer (default: 10 seconds)
   → Start edge detection timer
   → After delay, hide toolbar
   → Start monitoring mouse position
   ```

2. **Mouse at Top Edge**
   ```
   User moves mouse to top edge
   → Edge detection triggered
   → Show toolbar with animation
   → Reset auto-hide timer
   → Hide again after inactivity
   ```

3. **Window Restored**
   ```
   User restores window to normal
   → Stop all timers
   → Show toolbar if it was auto-hidden
   → Remove event filters
   ```

## Integration Example

### MainWindow.h
```cpp
#include "ui/windowcontrolmanager.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
    
private:
    WindowControlManager *m_windowControlManager;
    
private slots:
    void onToolbarVisibilityChanged(bool visible);
};
```

### MainWindow.cpp
```cpp
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    // Setup toolbar
    QToolBar *toolbar = new QToolBar(this);
    addToolBar(Qt::TopToolBarArea, toolbar);
    
    // Create window control manager
    m_windowControlManager = new WindowControlManager(this, toolbar, this);
    m_windowControlManager->setAutoHideEnabled(true);
    m_windowControlManager->setAutoHideDelay(10000);
    m_windowControlManager->setEdgeDetectionThreshold(5);
    
    // Connect signals
    connect(m_windowControlManager, &WindowControlManager::toolbarVisibilityChanged,
            this, &MainWindow::onToolbarVisibilityChanged);
}

void MainWindow::onToolbarVisibilityChanged(bool visible) {
    // Adjust layout when toolbar visibility changes
    // For example, resize video pane or adjust content area
    qDebug() << "Toolbar visibility changed:" << visible;
}

MainWindow::~MainWindow() {
    if (m_windowControlManager) {
        m_windowControlManager->setAutoHideEnabled(false);
        delete m_windowControlManager;
    }
}
```

## Benefits

### 1. **Reusability**
- Can be used in any Qt application
- Not tied to specific UI implementation
- Easy to integrate into existing projects

### 2. **Maintainability**
- Centralized window behavior logic
- Clear separation of concerns
- Easy to modify or extend

### 3. **Configurability**
- All timing and thresholds are configurable
- Enable/disable features independently
- Runtime configuration changes

### 4. **Safety**
- Proper cleanup in destructor
- Event filter management
- Prevents conflicts with existing code

## Best Practices

### 1. Initialization Order
Always create the WindowControlManager **after** the toolbar:
```cpp
// Correct
addToolBar(toolbar);
m_windowControlManager = new WindowControlManager(this, toolbar, this);

// Wrong - toolbar might not exist yet
m_windowControlManager = new WindowControlManager(this, toolbar, this);
addToolBar(toolbar);
```

### 2. Cleanup
Always disable auto-hide before destroying:
```cpp
if (m_windowControlManager) {
    m_windowControlManager->setAutoHideEnabled(false);
    delete m_windowControlManager;
}
```

### 3. Signal Connections
Connect signals before enabling auto-hide:
```cpp
// Correct
connect(manager, &WindowControlManager::toolbarVisibilityChanged, ...);
manager->setAutoHideEnabled(true);

// Less ideal - might miss initial events
manager->setAutoHideEnabled(true);
connect(manager, &WindowControlManager::toolbarVisibilityChanged, ...);
```

## Advanced Usage

### Custom Animation
Currently, the manager uses simple show/hide. To add custom animations:

```cpp
// Subclass WindowControlManager
class AnimatedWindowControlManager : public WindowControlManager {
protected:
    void animateToolbarShow() override {
        // Add slide-down animation
        QPropertyAnimation *anim = new QPropertyAnimation(m_toolbar, "pos");
        anim->setDuration(m_animationDuration);
        anim->setStartValue(QPoint(0, -m_toolbar->height()));
        anim->setEndValue(QPoint(0, 0));
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
};
```

### Extending Functionality
Add new window behaviors by extending the class:

```cpp
class ExtendedWindowControlManager : public WindowControlManager {
public:
    void setFullscreenOnDoubleClick(bool enabled);
    void setAutoMinimizeOnIdle(bool enabled);
    
private:
    void handleDoubleClick();
    void handleIdleTimeout();
};
```

## Troubleshooting

### Issue: Toolbar not hiding
**Solution**: Check if a menu is open - the manager prevents hiding during menu use.

### Issue: Toolbar shows but immediately hides
**Solution**: Increase the auto-hide delay or check edge threshold.

### Issue: Mouse detection not working
**Solution**: Ensure auto-hide is enabled and window is maximized.

## Future Enhancements

Potential improvements for future versions:

1. **Smooth Animations**
   - Add slide animations for toolbar show/hide
   - Configurable easing curves

2. **Multiple Toolbars**
   - Support managing multiple toolbars simultaneously
   - Independent or synchronized hiding

3. **Customizable Triggers**
   - Keyboard shortcuts to toggle toolbar
   - Other edge detection positions (bottom, sides)

4. **Persistence**
   - Save/restore user preferences
   - Remember toolbar state across sessions

5. **Visual Feedback**
   - Show visual hint when mouse approaches edge
   - Fade effects for smoother transitions

## License

This component is part of the Openterface Mini KVM App QT version and is licensed under the GNU General Public License version 3.
