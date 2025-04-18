# Static Build Notes for OpenTerface QT

## Static Plugin Imports

When using static linking, dynamically loaded plugins are disabled. You need to import all required static plugins in your application code. Include the following code in your main.cpp file:

```cpp
#include <QtPlugin>

#if defined(QT_STATIC)
// Platform plugin
Q_IMPORT_PLUGIN(QXcbIntegrationPlugin)

// Image formats
Q_IMPORT_PLUGIN(QJpegPlugin)
Q_IMPORT_PLUGIN(QGifPlugin)
Q_IMPORT_PLUGIN(QPngPlugin)
Q_IMPORT_PLUGIN(QSvgPlugin)

// Media plugins if needed
Q_IMPORT_PLUGIN(QFFmpegMediaPlugin)

// Add any other required plugins
#endif
```

Additionally, you'll need to add the corresponding plugin libraries to your CMake/qmake configuration:

```cmake
# For CMake
target_link_libraries(your_target PRIVATE
    Qt6::QXcbIntegrationPlugin
    Qt6::QJpegPlugin
    Qt6::QGifPlugin
    Qt6::QPngPlugin
    Qt6::QSvgPlugin
    # Add other required plugins
)
```

## X11 Accessibility Bridge

The X11 Accessibility Bridge is currently disabled in the static build because D-Bus or AT-SPI is missing. If accessibility features are required, add D-Bus support by:

1. Installing D-Bus development packages:
   ```
   sudo apt-get install libdbus-1-dev libdbus-glib-1-dev
   ```

2. Enable D-Bus in the Qt build:
   ```
   -DFEATURE_dbus=ON
   ```
