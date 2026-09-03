#-------------------------------------------------
#
# Project created by QtCreator 2024-04-02T19:21:24
#
#-------------------------------------------------

TARGET = openterfaceQT
TEMPLATE = app

MOC_DIR = moc
OBJECTS_DIR = obj
RCC_DIR = rcc

QT       += core gui gui-private multimedia multimediawidgets serialport concurrent svg svgwidgets network opengl openglwidgets xml dbus httpserver
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

# Speed up incremental rebuilds with ccache (install: sudo dnf install ccache / apt install ccache)
exists(/usr/bin/ccache) {
    QMAKE_CXX = ccache $$QMAKE_CXX
}

INCLUDEPATH += $$PWD

SOURCES += main.cpp \
    device/DeviceInfo.cpp \
    device/DeviceManager.cpp \
    device/DeviceLifecycleManager.cpp \
    device/HotplugMonitor.cpp \
    device/HotplugDebounceManager.cpp \
    device/platform/AbstractPlatformDeviceManager.cpp \
    device/platform/DeviceFactory.cpp \
    host/HostManager.cpp \
    host/audiomanager.cpp \
    host/audiothread.cpp \
    host/cameramanager.cpp \
    host/usbcontrol.cpp \
    host/UsbPortResetter.cpp \
    host/multimediabackend.cpp \
    host/imagecapturer.cpp \
    host/backend/qtmultimediabackendhandler.cpp \
    host/backend/qtbackendhandler.cpp \
    host/backend/ffmpegbackendhandler.cpp \
    host/backend/ffmpeg/capturethread.cpp \
    host/backend/ffmpeg/ffmpeg_hardware_accelerator.cpp \
    host/backend/ffmpeg/ffmpeg_device_manager.cpp \
    host/backend/ffmpeg/ffmpeg_frame_processor.cpp \
    host/backend/ffmpeg/ffmpeg_recorder.cpp \
    host/backend/ffmpeg/ffmpeg_device_validator.cpp \
    host/backend/ffmpeg/ffmpeg_hotplug_handler.cpp \
    host/backend/ffmpeg/ffmpeg_capture_manager.cpp \
    host/backend/ffmpeg/ffmpeg_amd_detector.cpp \
    host/backend/ffmpeg_compat_shim.c \
    regex/RegularExpression.cpp \
    scripts/KeyboardMouse.cpp \
    scripts/Lexer.cpp \
    scripts/Parser.cpp \
    scripts/semanticAnalyzer.cpp \
    scripts/scriptEditor.cpp \
    scripts/scriptRunner.cpp \
    scripts/scriptExecutor.cpp \
    serial/SerialPortManager.cpp \
    serial/SerialCommandCoordinator.cpp \
    serial/SerialStateManager.cpp \
    serial/SerialStatistics.cpp \
    serial/FactoryResetManager.cpp \
    serial/chipstrategy/CH9329Strategy.cpp \
    serial/chipstrategy/CH32V208Strategy.cpp \
    serial/chipstrategy/ChipStrategyFactory.cpp \
    serial/protocol/SerialProtocol.cpp \
    serial/watchdog/ConnectionWatchdog.cpp \
    serial/serial_hotplug_handler.cpp \
    server/tcpServer.cpp \
    server/tcpResponse.cpp \
    server/mcp/mcpServer.cpp \
    server/mcp/mcpProtocol.cpp \
    server/mcp/mcpToolHandler.cpp \
    server/mcp/mcpSseTransport.cpp \
    server/mcp/screenAnalyzer.cpp \
    target/KeyboardLayouts.cpp \
    target/KeyboardManager.cpp \
    target/MouseManager.cpp \
    target/mouseeventdto.cpp \
    video/videohid.cpp \
    video/videohid_register.cpp \
    video/videohid_eeprom.cpp \
    video/videohidchip.cpp \
    video/firmwarewriter.cpp \
    video/firmwarereader.cpp \
    video/firmwareoperationmanager.cpp \
    wch/WCHUSBTransport.cpp \
    wch/WCHFlasher.cpp \
    wch/WCHHexParser.cpp \
    wch/WCHDevice.cpp \
    wch/WCHProtocol.cpp \
    video/detection/ChipDetector.cpp \
    video/firmware/FirmwareNetworkClient.cpp \
    ui/TaskManager.cpp \
    ui/globalsetting.cpp \
    ui/inputhandler.cpp \
    ui/loghandler.cpp \
    ui/mainwindow.cpp \
    ui/videopane.cpp \
    ui/languagemanager.cpp \
    ui/screensavermanager.cpp \
    ui/screenscale.cpp \
    ui/coordinator/devicecoordinator.cpp \
    ui/coordinator/menucoordinator.cpp \
    ui/coordinator/windowlayoutcoordinator.cpp \
    ui/help/helppane.cpp \
    ui/help/versioninfomanager.cpp \
    ui/advance/scripttool.cpp \
    ui/advance/serialportdebugdialog.cpp \
    ui/advance/DeviceSelectorDialog.cpp \
    ui/advance/devicediagnosticsdialog.cpp \
    ui/advance/diagnostics/diagnosticsmanager.cpp \
    ui/advance/diagnostics/LogWriter.cpp \
    ui/advance/envdialog.cpp \
    ui/advance/firmwareupdatedialog.cpp \
    ui/advance/firmwaremanagerdialog.cpp \
    ui/advance/renamedisplaydialog.cpp \
    ui/advance/updatedisplaysettingsdialog.cpp \
    ui/advance/edid/edidutils.cpp \
    ui/advance/edid/firmwareutils.cpp \
    ui/advance/edid/edidresolutionparser.cpp \
    ui/advance/edid/resolutionmodel.cpp \
    ui/advance/edid/edidprocessor.cpp \
    ui/advance/recordingsettingsdialog.cpp \
    ui/advance/diagnostics/SupportEmailDialog.cpp \
    ui/advance/wchflash/WCHFlashWorker.cpp \
    ui/advance/wchflash/WCHFlashDialog.cpp \
    ui/advance/keyboardmapeditor.cpp \
    ui/initializer/mainwindowinitializer.cpp \
    ui/statusbar/statusbarmanager.cpp \
    ui/statusbar/statuswidget.cpp \
    ui/cornerwidget/cornerwidgetmanager.cpp \
    ui/windowcontrolmanager.cpp \
    ui/toolbar/toggleswitch.cpp \
    ui/toolbar/toolbarmanager.cpp \
    ui/recording/recordingcontroller.cpp \
    ui/splashscreen.cpp \
    ui/preferences/cameraadjust.cpp \
    ui/preferences/fpsspinbox.cpp \
    ui/preferences/settingdialog.cpp \
    ui/preferences/logpage.cpp \
    ui/preferences/preferencepagebase.cpp \
    ui/preferences/videopage.cpp \
    ui/preferences/audiopage.cpp \
    ui/preferences/mcppage.cpp \
    ui/preferences/firmwarepage.cpp \
    ui/preferences/edidconfigpage.cpp \
    ui/preferences/controlchipfirmwarepage.cpp \
    ui/preferences/targetcontrolpage.cpp \
    ui/floatingwindow/floatingwindow.cpp \
    ui/customkey/customkeymanager.cpp \
    ui/customkey/customkeydialog.cpp \
    ui/customkey/virtualkeyboardpage.cpp \
    SysKeyBlocker/SystemKeyBlocker.cpp \
    log/logcategoryregistry.cpp \
    ai/ChatAgentTypes.cpp \
    ai/ChatApiClient.cpp \
    ai/ChatConversationBuilder.cpp \
    ai/ChatGuideMode.cpp \
    ai/ChatInputRouter.cpp \
    ai/ChatManager.cpp \
    ai/ChatPersistence.cpp \
    ai/ChatScreenCapture.cpp \
    ai/SharedToolExecutor.cpp \
    ai/ChatSkillManager.cpp \
    ai/ChatToolExecution.cpp \
    ai/ChatTracing.cpp \
    ai/WebSearchManager.cpp \
    ai/WebSearchProviders.cpp \
    ui/chat/ChatBubbleWidget.cpp \
    ui/chat/ChatInputWidget.cpp \
    ui/chat/ChatPlanCardWidget.cpp \
    ui/chat/ChatSettingsPage.cpp \
    ui/chat/ToolsSettingsPage.cpp \
    ui/chat/ChatSkillBar.cpp \
    ui/chat/ChatEmptyStateWidget.cpp \
    ui/chat/ChatTraceDialog.cpp \
    ui/chat/ChatWindow.cpp \
    ui/hotplug/HotplugTestWizard.cpp \
    ui/hotplug/HotplugTestDialog.cpp

# Platform-specific backend handlers (exclude on Windows)
!win32 {
    SOURCES += host/backend/gstreamerbackendhandler.cpp \
               host/backend/gstreamer/sinkselector.cpp \
               host/backend/gstreamer/queueconfigurator.cpp \
               host/backend/gstreamer/videooverlaymanager.cpp \
               host/backend/gstreamer/pipelinebuilder.cpp \
               host/backend/gstreamer/pipelinefactory.cpp \
               host/backend/gstreamer/gstreamerhelpers.cpp \
               host/backend/gstreamer/inprocessgstrunner.cpp \
               host/backend/gstreamer/externalgstrunner.cpp \
               host/backend/gstreamer/recordingmanager.cpp
    HEADERS += host/backend/gstreamerbackendhandler.h \
               host/backend/gstreamer/sinkselector.h \
               host/backend/gstreamer/queueconfigurator.h \
               host/backend/gstreamer/videooverlaymanager.h \
               host/backend/gstreamer/pipelinebuilder.h \
               host/backend/gstreamer/pipelinefactory.h \
               host/backend/gstreamer/gstreamerhelpers.h \
               host/backend/gstreamer/inprocessgstrunner.h \
               host/backend/gstreamer/externalgstrunner.h \
               host/backend/gstreamer/recordingmanager.h
}


HEADERS  += \
    global.h \
    device/DeviceInfo.h \
    device/DeviceManager.h \
    device/DeviceSession.h \
    device/DeviceLifecycleManager.h \
    device/HotplugMonitor.h \
    device/HotplugDebounceManager.h \
    device/platform/AbstractPlatformDeviceManager.h \
    device/platform/DeviceFactory.h \
    device/platform/windows/WinDeviceEnumerator.h \
    device/platform/windows/IDeviceEnumerator.h \
    host/HostManager.h \
    host/audiomanager.h \
    host/audiothread.h \
    host/cameramanager.h \
    host/usbcontrol.h \
    host/UsbPortResetter.h \
    host/multimediabackend.h \
    host/imagecapturer.h \
    host/backend/qtmultimediabackendhandler.h \
    host/backend/qtbackendhandler.h \
    host/backend/ffmpegbackendhandler.h \
    host/backend/ffmpeg/capturethread.h \
    host/backend/ffmpeg/ffmpeg_hardware_accelerator.h \
    host/backend/ffmpeg/ffmpeg_device_manager.h \
    host/backend/ffmpeg/ffmpeg_frame_processor.h \
    host/backend/ffmpeg/ffmpeg_amd_detector.h \
    host/backend/ffmpeg/ffmpeg_recorder.h \
    host/backend/ffmpeg/ffmpeg_device_validator.h \
    host/backend/ffmpeg/ffmpeg_hotplug_handler.h \
    host/backend/ffmpeg/ffmpeg_capture_manager.h \
    host/backend/ffmpeg/ffmpeg_amd_detector.h \
    host/backend/ffmpeg/icapture_frame_reader.h \
    host/backend/ffmpeg/ffmpegutils.h \
    regex/RegularExpression.h \
    scripts/KeyboardMouse.h \
    scripts/SendKeyMaps.h \
    scripts/Lexer.h \
    scripts/Parser.h \
    scripts/semanticAnalyzer.h \
    scripts/scriptEditor.h \
    scripts/scriptRunner.h \
    scripts/scriptExecutor.h \
    serial/SerialPortManager.h \
    serial/SerialCommandCoordinator.h \
    serial/SerialStateManager.h \
    serial/SerialStatistics.h \
    serial/FactoryResetManager.h \
    serial/ch9329.h \
    serial/chipstrategy/IChipStrategy.h \
    serial/chipstrategy/CH9329Strategy.h \
    serial/chipstrategy/CH32V208Strategy.h \
    serial/chipstrategy/ChipStrategyFactory.h \
    serial/protocol/SerialProtocol.h \
    serial/watchdog/ConnectionWatchdog.h \
    serial/serial_hotplug_handler.h \
    server/tcpServer.h \
    server/tcpResponse.h \
    server/mcp/mcpServer.h \
    server/mcp/mcpProtocol.h \
    server/mcp/mcpToolHandler.h \
    server/mcp/mcpConstants.h \
    server/mcp/mcpSseTransport.h \
    server/mcp/screenAnalyzer.h \
    target/KeyboardLayouts.h \
    target/KeyboardManager.h \
    target/MouseManager.h \
    target/Keymapping.h \
    target/HIDScancodeReference.h \
    target/mouseeventdto.h \
    resources/version.h \
    video/videohid.h \
    video/firmwarewriter.h \
    video/firmwarereader.h \
    video/firmwareoperationmanager.h \
    video/ms2109.h \
    video/ms2109s.h \
    video/ms2130s.h \
    video/platformhidadapter.h \
    video/detection/ChipDetector.h \
    video/firmware/FirmwareNetworkClient.h \
    video/transport/IHIDTransport.h \
    ui/TaskManager.h \
    ui/globalsetting.h \
    ui/inputhandler.h \
    ui/loghandler.h \
    ui/mainwindow.h \
    ui/splashscreen.h \
    ui/videopane.h \
    ui/statusevents.h \
    ui/languagemanager.h \
    ui/screensavermanager.h \
    ui/screenscale.h \
    ui/coordinator/devicecoordinator.h \
    ui/coordinator/menucoordinator.h \
    ui/coordinator/windowlayoutcoordinator.h \
    ui/help/helppane.h \
    ui/help/versioninfomanager.h \
    ui/advance/scripttool.h \
    ui/advance/serialportdebugdialog.h \
    ui/advance/DeviceSelectorDialog.h \
    ui/advance/devicediagnosticsdialog.h \
    ui/advance/diagnostics/diagnosticsmanager.h \
    ui/advance/diagnostics/diagnostics_constants.h \
    ui/advance/diagnostics/LogWriter.h \
    ui/advance/envdialog.h \
    ui/advance/firmwareupdatedialog.h \
    ui/advance/firmwaremanagerdialog.h \
    ui/advance/renamedisplaydialog.h \
    ui/advance/updatedisplaysettingsdialog.h \
    ui/advance/edid/edidutils.h \
    ui/advance/edid/firmwareutils.h \
    ui/advance/edid/edidresolutionparser.h \
    ui/advance/edid/resolutionmodel.h \
    ui/advance/edid/edidprocessor.h \
    ui/advance/recordingsettingsdialog.h \
    ui/advance/diagnostics/SupportEmailDialog.h \
    ui/advance/wchflash/WCHFlashWorker.h \
    ui/advance/wchflash/WCHFlashDialog.h \
    ui/advance/keyboardmapeditor.h \
    ui/initializer/mainwindowinitializer.h \
    ui/statusbar/statusbarmanager.h \
    ui/statusbar/statuswidget.h \
    ui/cornerwidget/cornerwidgetmanager.h \
    ui/windowcontrolmanager.h \
    ui/toolbar/toggleswitch.h \
    ui/toolbar/toolbarmanager.h \
    ui/recording/recordingcontroller.h \
    ui/preferences/cameraadjust.h \
    ui/preferences/fpsspinbox.h \
    ui/preferences/settingdialog.h \
    ui/preferences/logpage.h \
    ui/preferences/preferencepagebase.h \
    ui/preferences/targetcontrolpage.h \
    ui/preferences/videopage.h \
    ui/preferences/audiopage.h \
    ui/preferences/mcppage.h \
    ui/preferences/firmwarepage.h \
    ui/preferences/edidconfigpage.h \
    ui/preferences/controlchipfirmwarepage.h \
    ui/floatingwindow/floatingwindow.h \
    ui/customkey/customkeymanager.h \
    ui/customkey/customkeydialog.h \
    ui/customkey/virtualkeyboardpage.h \
    SysKeyBlocker/SystemKeyBlocker.h \
    log/logcategoryregistry.h \
    log/opflogging.h \
    ai/ChatAgentTypes.h \
    ai/ChatApiClient.h \
    ai/ChatConversationBuilder.h \
    ai/ChatGuideMode.h \
    ai/ChatInputRouter.h \
    ai/ChatManager.h \
    ai/ChatPersistence.h \
    ai/ChatScreenCapture.h \
    ai/SharedToolExecutor.h \
    ai/ChatSkillManager.h \
    ai/ChatToolExecution.h \
    ai/ChatTracing.h \
    ai/ChatTypes.h \
    ai/WebSearchManager.h \
    ai/WebSearchProvider.h \
    ai/WebSearchProviders.h \
    ui/chat/ChatBubbleWidget.h \
    ui/chat/ChatInputWidget.h \
    ui/chat/ChatPlanCardWidget.h \
    ui/chat/ChatSettingsPage.h \
    ui/chat/ToolsSettingsPage.h \
    ui/chat/ChatSkillBar.h \
    ui/chat/ChatEmptyStateWidget.h \
    ui/chat/ChatTraceDialog.h \
    ui/chat/ChatWindow.h \
    ui/chat/QuickReplyWidget.h \
    ui/hotplug/HotplugTestWizard.h \
    ui/hotplug/HotplugTestDialog.h

FORMS    += \
    ui/mainwindow.ui \
    ui/preferences/settingdialog.ui \
    ui/advance/envdialog.ui

RESOURCES += \
    ui/mainwindow.qrc \
    config/keyboards/keyboard_layouts.qrc \
    config/languages/language.qrc \
    config/customkeys/customkeys.qrc


# Link against the HID library
win32:LIBS += -lhid
win32:LIBS += -lsetupapi
win32:LIBS += -lcfgmgr32
win32:LIBS += -lole32

win32 {
    # Add Windows-specific device manager
    SOURCES += device/platform/WindowsDeviceManager.cpp \
        device/platform/windows/WinDeviceEnumerator.cpp \
        device/platform/windows/discoverers/BaseDeviceDiscoverer.cpp \
        device/platform/windows/discoverers/BotherDeviceDiscoverer.cpp \
        device/platform/windows/discoverers/Generation3Discoverer.cpp \
        device/platform/windows/discoverers/DeviceDiscoveryManager.cpp \
        video/transport/WindowsHIDTransport.cpp \
        SysKeyBlocker/SystemKeyBlocker_win.cpp \
        host/backend/mf/mfbackendhandler.cpp \
        host/backend/mf/mf_capture_manager.cpp \
        host/backend/mf/mf_device_enumerator.cpp \
        host/backend/mf/mf_frame_processor.cpp
    HEADERS += device/platform/WindowsDeviceManager.h \
        video/transport/WindowsHIDTransport.h \
        device/platform/windows/discoverers/IDeviceDiscoverer.h \
        device/platform/windows/discoverers/BaseDeviceDiscoverer.h \
        device/platform/windows/discoverers/BotherDeviceDiscoverer.h \
        device/platform/windows/discoverers/Generation3Discoverer.h \
        device/platform/windows/discoverers/DeviceDiscoveryManager.h \
        host/backend/mf/mfbackendhandler.h \
        host/backend/mf/mf_capture_manager.h \
        host/backend/mf/mf_device_enumerator.h \
        host/backend/mf/mf_frame_processor.h
    
    INCLUDEPATH += $$PWD/lib
    # FFmpeg prefix. Resolution order:
    #   1. qmake arg:       qmake "FFMPEG_PREFIX=D:/ffmpeg"
    #   2. env var:         set FFMPEG_PREFIX=D:\ffmpeg
    #   3. default:         C:/ffmpeg-static (or C:/ffmpeg-shared with CONFIG+=shared_ffmpeg)
    # Usage:  qmake "CONFIG+=shared_ffmpeg" "FFMPEG_PREFIX=C:/ffmpeg-shared"
    # Mirrors cmake -DUSE_SHARED_FFMPEG=ON -DFFMPEG_PREFIX=...
    contains(CONFIG, shared_ffmpeg): FF_MODE = shared
    else: FF_MODE = static
    isEmpty(FFMPEG_PREFIX) {
        FFMPEG_PREFIX = $$(FFMPEG_PREFIX)
    }
    isEmpty(FFMPEG_PREFIX) {
        equals(FF_MODE, shared): FFMPEG_PREFIX = C:/ffmpeg-shared
        else: FFMPEG_PREFIX = C:/ffmpeg-static
    }
    message("Using FFmpeg prefix: $$FFMPEG_PREFIX (mode: $$FF_MODE)")
    INCLUDEPATH += $$FFMPEG_PREFIX/include

    LIBS += -L$$PWD/lib -llibusb-1.0 -loleaut32 -lwinpthread

    # MSYS2/MinGW sysroot for FFmpeg transitive deps (zlib, bz2, lzma, mfx, winpthread, iconv).
    # Resolution order: 1) qmake arg MSYS_PREFIX  2) env MSYS_PREFIX  3) C:/msys64
    isEmpty(MSYS_PREFIX) {
        MSYS_PREFIX = $$(MSYS_PREFIX)
    }
    isEmpty(MSYS_PREFIX) {
        MSYS_PREFIX = C:/msys64
    }
    MSYS_LIB_DIR = $$MSYS_PREFIX/mingw64/lib
    MFX_MSYS = $$MSYS_LIB_DIR/libmfx.a
    message("Using MSYS2 lib dir: $$MSYS_LIB_DIR")
    LIBS += -L$$MSYS_LIB_DIR

    equals(FF_MODE, shared) {
        # ---- Shared FFmpeg (import libs from $$FFMPEG_PREFIX/lib) ----
        # Mirrors cmake/FFmpeg.cmake: link .dll.a import libs + Windows system deps.
        SHARED_FF_LIB_DIR = $$FFMPEG_PREFIX/lib
        LIBS += -L$$SHARED_FF_LIB_DIR
        LIBS += -lavdevice -lavfilter -lavformat -lavcodec -lswresample -lswscale -lavutil

        # Windows system libs required by shared FFmpeg (same set as CMake HWACCEL_LIBRARIES)
        LIBS += -lws2_32 -lsecur32 -lbcrypt -lole32 -lstrmiids
        LIBS += -lvfw32 -lgdi32 -lshlwapi -lwinmm -luuid -loleaut32

        # Intel QSV (libmfx) - prefer ffmpeg-static copy, fallback to MSYS2
        exists($$FFMPEG_PREFIX/lib/libmfx.a) {
            message("Using libmfx from $$FFMPEG_PREFIX/lib/libmfx.a")
            LIBS += $$FFMPEG_PREFIX/lib/libmfx.a
        } else:exists($$MFX_MSYS) {
            message("Using libmfx from $$MFX_MSYS")
            LIBS += $$MFX_MSYS
        } else {
            message("libmfx not found - QSV support disabled")
        }

        # Shared FFmpeg transitive deps (zlib, bz2, lzma, winpthread)
        LIBS += -lz -lbz2 -llzma -lwinpthread
        DEFINES += HAVE_FFMPEG

        # libjpeg-turbo (optional, matches CMake HAVE_LIBJPEG_TURBO detection)
        exists($$FFMPEG_PREFIX/lib/libturbojpeg.a) {
            LIBS += $$FFMPEG_PREFIX/lib/libturbojpeg.a
            DEFINES += HAVE_LIBJPEG_TURBO
            message("Using libturbojpeg from $$FFMPEG_PREFIX/lib/libturbojpeg.a")
        } else {
            exists($$MSYS_LIB_DIR/libturbojpeg.a) {
                LIBS += $$MSYS_LIB_DIR/libturbojpeg.a
                DEFINES += HAVE_LIBJPEG_TURBO
            }
        }
    } else {
        # ---- Static FFmpeg (whole-archive from $$FFMPEG_PREFIX/lib) ----
        # Mirrors cmake link_ffmpeg_libraries() static branch.
        # IMPORTANT: do NOT use --start-group/--end-group — the linker loop
        # pulls objects repeatedly and corrupts init tables (0xC0000409).
        # IMPORTANT: FFmpeg .a files MUST come BEFORE the system libs that
        # satisfy their undefined refs (left-to-right resolution).
        FF_LIB_DIR = $$FFMPEG_PREFIX/lib
        LIBS += -L$$FF_LIB_DIR
        LIBS += -Wl,--whole-archive $$FF_LIB_DIR/libavdevice.a -Wl,--no-whole-archive
        LIBS += $$FF_LIB_DIR/libavfilter.a \
                $$FF_LIB_DIR/libavformat.a \
                $$FF_LIB_DIR/libavcodec.a \
                $$FF_LIB_DIR/libswresample.a \
                $$FF_LIB_DIR/libswscale.a \
                $$FF_LIB_DIR/libavutil.a
        exists($$FF_LIB_DIR/libjpeg.a):        LIBS += $$FF_LIB_DIR/libjpeg.a
        exists($$FF_LIB_DIR/libturbojpeg.a):   LIBS += $$FF_LIB_DIR/libturbojpeg.a
        exists($$FF_LIB_DIR/libpostproc.a):    LIBS += $$FF_LIB_DIR/libpostproc.a
        # Third-party / MSYS2 deps
        exists($$MSYS_LIB_DIR/libz.a):          LIBS += $$MSYS_LIB_DIR/libz.a
        exists($$MSYS_LIB_DIR/libbz2.a):        LIBS += $$MSYS_LIB_DIR/libbz2.a
        exists($$MSYS_LIB_DIR/liblzma.a):       LIBS += $$MSYS_LIB_DIR/liblzma.a
        exists($$MSYS_LIB_DIR/libwinpthread.a): LIBS += $$MSYS_LIB_DIR/libwinpthread.a
        exists($$MSYS_LIB_DIR/libmfx.a):        LIBS += $$MSYS_LIB_DIR/libmfx.a
        exists($$MSYS_LIB_DIR/libiconv.a):      LIBS += $$MSYS_LIB_DIR/libiconv.a
        # System libs (resolve refs from FFmpeg archives above)
        LIBS += -lgdi32 -lvfw32 -lshlwapi \
                -lws2_32 -lbcrypt -lsecur32 -lwinmm -luuid \
                -lole32 -loleaut32 -lstrmiids \
                -lmfuuid -lmfplat -lmf -lmfreadwrite -lwmcodecdspuuid \
                -lwinpthread

        DEFINES += HAVE_FFMPEG
    }

    # Media Foundation backend is always built on Windows (independent of FFmpeg mode).
    LIBS += -lmf -lmfplat -lmfreadwrite -lmfuuid -lshlwapi -lole32 -loleaut32

    RESOURCES += driver/windows/drivers.qrc
}

unix {
    # Add Linux-specific sources if any
    SOURCES += device/platform/LinuxDeviceManager.cpp \
               video/transport/LinuxHIDTransport.cpp \
               SysKeyBlocker/SystemKeyBlocker_x11.cpp
    HEADERS += device/platform/LinuxDeviceManager.h \
               video/transport/LinuxHIDTransport.h

    INCLUDEPATH += /usr/include
    # -lusb-1.0 is provided by PKGCONFIG below; keep -lturbojpeg (TurboJPEG API, distinct from -ljpeg)
    LIBS += -lX11 -lgstapp-1.0 -lturbojpeg

    # On non-mac Unix systems enable pkg-config based dependencies
    unix:!macx {
        CONFIG += link_pkgconfig
        # libjpeg and libusb-1.0 provide -ljpeg (-lturbojpeg) and -lusb-1.0 respectively
        PKGCONFIG += libudev gstreamer-1.0 gstreamer-video-1.0 libavformat libavcodec libavutil libswscale libavdevice libjpeg libusb-1.0
        DEFINES += HAVE_LIBUDEV HAVE_GSTREAMER HAVE_FFMPEG HAVE_LIBJPEG_TURBO HAVE_LIBUSB
        # Add VA-API libraries AFTER FFmpeg pkg-config libraries to ensure proper linking order
        LIBS += -lva -lva-drm -lva-x11

        # Optional: Tesseract OCR for screen_to_markdown
        TESSERACT_PKG = $$system(pkg-config --exists tesseract && echo yes)
        equals(TESSERACT_PKG, yes) {
            PKGCONFIG += tesseract lept
            DEFINES += HAVE_TESSERACT
            message("Tesseract OCR found - enabling screen_to_markdown feature")
        } else {
            message("Tesseract OCR not found - screen_to_markdown feature will be disabled")
        }

        # Optional: OpenCV for visual button detection in screen_to_markdown
        OPENCV4_PKG = $$system(pkg-config --exists opencv4 && echo yes)
        OPENCV_PKG = $$system(pkg-config --exists opencv && echo yes)
        !isEmpty(OPENCV4_PKG) {
            OPENCV_VERSION = $$system(pkg-config --modversion opencv4)
            PKGCONFIG += opencv4
            DEFINES += HAVE_OPENCV
            message("OpenCV found ($$OPENCV_VERSION) - enabling visual button detection")
        } else:!isEmpty(OPENCV_PKG) {
            OPENCV_VERSION = $$system(pkg-config --modversion opencv)
            PKGCONFIG += opencv4 opencv
            DEFINES += HAVE_OPENCV
            message("OpenCV found ($$OPENCV_VERSION) - enabling visual button detection")
        } else {
            message("OpenCV not found - visual button detection disabled. Install with: sudo dnf install opencv-devel")
        }
    }

    RESOURCES += driver/linux/drivers.qrc

    # Post-link: copy launcher script and strip the release binary
    QMAKE_POST_LINK = $$quote($$QMAKE_COPY $$quote($$PWD/build-script/openterfaceQT-local-launcher.sh) $$quote($$OUT_PWD/openterfaceQT-launcher.sh))
    exists(/usr/bin/strip) {
        QMAKE_POST_LINK += && strip $$quote($$OUT_PWD/$$TARGET)
    }
}

# Set platform-specific installation paths
win32 {
    target.path = $$(PROGRAMFILES)/openterfaceQT
} else {
    target.path = /usr/local/bin
}

# INSTALLS += target

RC_FILE = openterfaceQT.rc

TRANSLATIONS += config/languages/openterface_en.ts \
                config/languages/openterface_fr.ts \
                config/languages/openterface_da.ts \
                config/languages/openterface_ja.ts \
                config/languages/openterface_se.ts \
                config/languages/openterface_de.ts \
                config/languages/openterface_zh.ts \
                config/languages/openterface_es.ts \
                config/languages/openterface_it.ts \
                config/languages/openterface_ko.ts \
                config/languages/openterface_pt.ts \
                config/languages/openterface_ru.ts
