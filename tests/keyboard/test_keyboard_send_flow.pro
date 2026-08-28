# test_keyboard_send_flow.pro
# 键盘发送流程集成测试
#
# 测试两条键盘路径：
# 路径1 (SystemKeyBlocker OFF): QKeyEvent → VideoPane → InputHandler → HostManager → SerialPortManager
# 路径2 (SystemKeyBlocker ON):  keyCaptured → VideoPane::handleCapturedKey → InputHandler → HostManager → SerialPortManager

TEMPLATE = app
TARGET = test_keyboard_send_flow
CONFIG += qt test c++17

QT += core gui gui-private widgets testlib serialport concurrent multimedia multimediawidgets svg svgwidgets network opengl openglwidgets xml dbus httpserver

# 项目根目录
PROJECT_ROOT = $$PWD/../..

INCLUDEPATH += $$PROJECT_ROOT

# 测试源文件
SOURCES += test_keyboard_send_flow.cpp

# 链接主项目的所有 object 文件
# 使用 qmake 的 wildcard 函数
MAIN_OBJ_DIR = $$PROJECT_ROOT/build/obj
MAIN_RCC_DIR = $$PROJECT_ROOT/build/rcc

# 收集所有 object 文件（排除 main.o 因为测试有自己的 QTEST_MAIN）
OBJECTS = $$files($$MAIN_OBJ_DIR/*.o, true)
OBJECTS -= $$MAIN_OBJ_DIR/main.o

# 排除 moc 文件中与测试无关的（可选，保留也可以）

# RCC 生成的 object 文件
# 注意：rcc 目录下是 .cpp 文件，需要先编译
# 这里直接用 obj 目录下已经编译好的 qrc 文件
# 如果 build/rcc 下没有 .o 文件，需要手动编译

# X11 库（SystemKeyBlocker 需要）
LIBS += -lX11 -lxkbcommon

# GStreamer
LIBS += -lgstapp-1.0 -lgstvideo-1.0 -lgstbase-1.0 -lgstreamer-1.0 -lgobject-2.0 -lglib-2.0

# FFmpeg
LIBS += -lavformat -lavcodec -lavutil -lswscale -lavdevice

# 其他依赖
LIBS += -ljpeg -lturbojpeg -lusb-1.0 -ludev -ltesseract -larchive -lcurl -lleptonica

# OpenGL
LIBS += -lGLX -lOpenGL

# pthread
LIBS += -lpthread
