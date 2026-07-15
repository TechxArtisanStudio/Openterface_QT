/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#include "SystemKeyBlocker.h"

#include <QLoggingCategory>
#include <QKeyEvent>
#include <QMutex>
#include <QMutexLocker>

// ----- Windows -----
#ifdef Q_OS_WIN
#  include <windows.h>
#endif

// ----- Linux -----
#ifdef Q_OS_LINUX
#  include <X11/Xlib.h>
#  include <X11/keysym.h>
#  include <X11/XKBlib.h>
#endif

Q_LOGGING_CATEGORY(log_syskey, "opf.systemkey")

namespace {
    // Guard to protect the lazy singleton
    QBasicMutex g_instanceMutex;
    SystemKeyBlocker *g_instance = nullptr;
}

// ============================================================================
//  Singleton
// ============================================================================

SystemKeyBlocker &SystemKeyBlocker::instance()
{
    if (Q_UNLIKELY(!g_instance)) {
        QMutexLocker lock(&g_instanceMutex);
        if (!g_instance) {
            g_instance = new SystemKeyBlocker;
        }
    }
    return *g_instance;
}

// ============================================================================
//  Constructor / Destructor
// ============================================================================

SystemKeyBlocker::SystemKeyBlocker(QObject *parent)
    : QObject(parent)
{
}

SystemKeyBlocker::~SystemKeyBlocker()
{
    stop();
}

// ============================================================================
//  Public API
// ============================================================================

bool SystemKeyBlocker::start(quintptr nativeParentHwnd)
{
    if (m_active) {
        qCInfo(log_syskey) << "Already active; restarting capture";
        stop();
    }

    if (startImpl(nativeParentHwnd)) {
        m_active = true;
        qCInfo(log_syskey) << "Full keyboard capture started";
        emit captureStateChanged(true);
        return true;
    }

    qCWarning(log_syskey) << "Failed to start keyboard capture";
    emit captureStateChanged(false);
    return false;
}

void SystemKeyBlocker::stop()
{
    if (!m_active) {
        return;
    }

    stopImpl();
    m_active = false;
    qCInfo(log_syskey) << "Full keyboard capture stopped";
    emit captureStateChanged(false);
}

// ============================================================================
//  nativeToQtKey — platform VK / keysym → Qt key code
// ============================================================================

#ifdef Q_OS_WIN

int SystemKeyBlocker::nativeToQtKey(quint32 nativeVk, bool /*extended*/) const
{
    switch (nativeVk) {
    case VK_LWIN:     // fall through
    case VK_RWIN:     return Qt::Key_Meta;
    case VK_SNAPSHOT: return Qt::Key_Print;
    case VK_ESCAPE:   return Qt::Key_Escape;
    case VK_TAB:      return Qt::Key_Tab;
    case VK_RETURN:   return Qt::Key_Return;
    case VK_SPACE:    return Qt::Key_Space;
    case VK_BACK:     return Qt::Key_Backspace;
    case VK_DELETE:   return Qt::Key_Delete;
    case VK_INSERT:   return Qt::Key_Insert;
    case VK_HOME:     return Qt::Key_Home;
    case VK_END:      return Qt::Key_End;
    case VK_PRIOR:    return Qt::Key_PageUp;
    case VK_NEXT:     return Qt::Key_PageDown;
    case VK_LEFT:     return Qt::Key_Left;
    case VK_RIGHT:    return Qt::Key_Right;
    case VK_UP:       return Qt::Key_Up;
    case VK_DOWN:     return Qt::Key_Down;

    // Modifiers
    case VK_LSHIFT:   // fall through
    case VK_RSHIFT:   return Qt::Key_Shift;
    case VK_LCONTROL: // fall through
    case VK_RCONTROL: return Qt::Key_Control;
    case VK_LMENU:    // fall through
    case VK_RMENU:    return Qt::Key_Alt;

    // Function keys
    case VK_F1:  return Qt::Key_F1;
    case VK_F2:  return Qt::Key_F2;
    case VK_F3:  return Qt::Key_F3;
    case VK_F4:  return Qt::Key_F4;
    case VK_F5:  return Qt::Key_F5;
    case VK_F6:  return Qt::Key_F6;
    case VK_F7:  return Qt::Key_F7;
    case VK_F8:  return Qt::Key_F8;
    case VK_F9:  return Qt::Key_F9;
    case VK_F10: return Qt::Key_F10;
    case VK_F11: return Qt::Key_F11;
    case VK_F12: return Qt::Key_F12;

    // Lock keys
    case VK_CAPITAL:  return Qt::Key_CapsLock;
    case VK_NUMLOCK:  return Qt::Key_NumLock;
    case VK_SCROLL:   return Qt::Key_ScrollLock;

    // Numeric keypad
    case VK_NUMPAD0:  return Qt::Key_0;
    case VK_NUMPAD1:  return Qt::Key_1;
    case VK_NUMPAD2:  return Qt::Key_2;
    case VK_NUMPAD3:  return Qt::Key_3;
    case VK_NUMPAD4:  return Qt::Key_4;
    case VK_NUMPAD5:  return Qt::Key_5;
    case VK_NUMPAD6:  return Qt::Key_6;
    case VK_NUMPAD7:  return Qt::Key_7;
    case VK_NUMPAD8:  return Qt::Key_8;
    case VK_NUMPAD9:  return Qt::Key_9;
    case VK_MULTIPLY: return Qt::Key_Asterisk;
    case VK_ADD:      return Qt::Key_Plus;
    case VK_SUBTRACT: return Qt::Key_Minus;
    case VK_DECIMAL:  return Qt::Key_Period;
    case VK_DIVIDE:   return Qt::Key_Slash;

    // Context menu
    case VK_APPS:     return Qt::Key_Menu;

    // Volume / media
    case VK_VOLUME_MUTE: return Qt::Key_VolumeMute;
    case VK_VOLUME_DOWN: return Qt::Key_VolumeDown;
    case VK_VOLUME_UP:   return Qt::Key_VolumeUp;
    case VK_MEDIA_NEXT_TRACK: return Qt::Key_MediaNext;
    case VK_MEDIA_PREV_TRACK: return Qt::Key_MediaPrevious;
    case VK_MEDIA_STOP:       return Qt::Key_MediaStop;
    case VK_MEDIA_PLAY_PAUSE: return Qt::Key_MediaPlay;

    case VK_PAUSE:    return Qt::Key_Pause;
    case VK_CLEAR:    return Qt::Key_Clear;

    // Symbol keys (OEM keys)
    case VK_OEM_1:      return Qt::Key_Semicolon;      // ;:
    case VK_OEM_PLUS:   return Qt::Key_Plus;           // =+
    case VK_OEM_COMMA:  return Qt::Key_Comma;          // ,<
    case VK_OEM_MINUS:  return Qt::Key_Minus;          // -_
    case VK_OEM_PERIOD: return Qt::Key_Period;         // .>
    case VK_OEM_2:      return Qt::Key_Slash;          // /?
    case VK_OEM_3:      return Qt::Key_QuoteLeft;      // `~
    case VK_OEM_4:      return Qt::Key_BracketLeft;    // [{
    case VK_OEM_5:      return Qt::Key_Backslash;      // \|
    case VK_OEM_6:      return Qt::Key_BracketRight;   // ]}
    case VK_OEM_7:      return Qt::Key_Apostrophe;     // '"

    default:
        // For alphabetic and numeric keys the VK code directly maps
        // to the ASCII char, which Qt::Key uses for A-Z / 0-9.
        if (nativeVk >= 'A' && nativeVk <= 'Z')
            return static_cast<int>(nativeVk);
        if (nativeVk >= '0' && nativeVk <= '9')
            return static_cast<int>(nativeVk);
        return static_cast<int>(nativeVk);   // pass through raw VK
    }
}

#endif // Q_OS_WIN


#ifdef Q_OS_LINUX

int SystemKeyBlocker::nativeToQtKey(quint32 nativeVk, bool /*extended*/) const
{
    // nativeVk is an X11 keysym (e.g. 0xFF08 for XK_BackSpace)
    switch (nativeVk) {
    case XK_Super_L: // fall through
    case XK_Super_R: return Qt::Key_Meta;
    case XK_Meta_L:  // fall through
    case XK_Meta_R:  return Qt::Key_Meta;
    case XK_Print:   return Qt::Key_Print;
    case XK_Escape:  return Qt::Key_Escape;
    case XK_Tab:     return Qt::Key_Tab;
    case XK_ISO_Left_Tab: return Qt::Key_Tab;
    case XK_Return:  return Qt::Key_Return;
    case XK_KP_Enter: return Qt::Key_Enter;

    case XK_BackSpace: return Qt::Key_Backspace;

    case XK_Insert:    return Qt::Key_Insert;
    case XK_KP_Insert: return Qt::Key_Insert;
    case XK_Delete:    return Qt::Key_Delete;
    case XK_KP_Delete: return Qt::Key_Delete;

    case XK_Home:    return Qt::Key_Home;
    case XK_KP_Home: return Qt::Key_Home;
    case XK_End:     return Qt::Key_End;
    case XK_KP_End:  return Qt::Key_End;

    case XK_Page_Up:   return Qt::Key_PageUp;
    case XK_KP_Page_Up: return Qt::Key_PageUp;
    case XK_Page_Down:   return Qt::Key_PageDown;
    case XK_KP_Page_Down: return Qt::Key_PageDown;

    case XK_Left:  // fall through
    case XK_KP_Left:  return Qt::Key_Left;
    case XK_Right: // fall through
    case XK_KP_Right: return Qt::Key_Right;
    case XK_Up:    // fall through
    case XK_KP_Up:    return Qt::Key_Up;
    case XK_Down:  // fall through
    case XK_KP_Down:  return Qt::Key_Down;

    // Modifiers
    case XK_Shift_L: // fall through
    case XK_Shift_R: return Qt::Key_Shift;
    case XK_Control_L: // fall through
    case XK_Control_R: return Qt::Key_Control;
    case XK_Alt_L:   // fall through
    case XK_Alt_R:   return Qt::Key_Alt;

    // Function keys
    case XK_F1:  return Qt::Key_F1;
    case XK_F2:  return Qt::Key_F2;
    case XK_F3:  return Qt::Key_F3;
    case XK_F4:  return Qt::Key_F4;
    case XK_F5:  return Qt::Key_F5;
    case XK_F6:  return Qt::Key_F6;
    case XK_F7:  return Qt::Key_F7;
    case XK_F8:  return Qt::Key_F8;
    case XK_F9:  return Qt::Key_F9;
    case XK_F10: return Qt::Key_F10;
    case XK_F11: return Qt::Key_F11;
    case XK_F12: return Qt::Key_F12;

    // Lock keys
    case XK_Caps_Lock:   return Qt::Key_CapsLock;
    case XK_Num_Lock:    return Qt::Key_NumLock;
    case XK_Scroll_Lock: return Qt::Key_ScrollLock;

    // Space
    case XK_space: return Qt::Key_Space;
    case XK_KP_Space: return Qt::Key_Space;

    // Menu
    case XK_Menu: return Qt::Key_Menu;

    // Pause / Break
    case XK_Pause:
    case XK_Break: return Qt::Key_Pause;

    default:
        // ASCII range 0x20 - 0x7E maps directly
        if (nativeVk >= 0x20 && nativeVk <= 0x7E) {
            // X11 keysym uses lowercase for letters (0x61-0x7A), but Qt uses uppercase (0x41-0x5A)
            // Convert lowercase to uppercase to match Qt key codes
            if (nativeVk >= 'a' && nativeVk <= 'z')
                return static_cast<int>(nativeVk - 32);
            return static_cast<int>(nativeVk);
        }
        return static_cast<int>(nativeVk);
    }
}

#endif // Q_OS_LINUX