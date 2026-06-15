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

#ifndef SYSTEMKEYBLOCKER_H
#define SYSTEMKEYBLOCKER_H

#include <QObject>

#ifdef Q_OS_WIN
#  ifndef WINVER
#    define WINVER 0x0601
#  endif
#  include <windows.h>
#endif // Q_OS_WIN

/**
 * @brief SystemKeyBlocker — Global keyboard capture toggle
 *
 * When active, intercepts ALL keystrokes at the OS level (Win/Super,
 * PrintScreen, Alt+Tab, and any other system keys).  The OS no longer
 * processes any key event: everything is forwarded to the target machine
 * through the keyCaptured signal.
 *
 * When stopped, the system returns to normal behaviour.
 *
 * One on/off switch — no per-key configuration.
 *
 * Platform backends:
 *   - Windows   : SetWindowsHookEx(WH_KEYBOARD_LL)
 *   - Linux X11 : XGrabKey on top-level window + QAbstractNativeEventFilter
 *   - Wayland   : not yet supported (hard compositor restrictions)
 *
 * @code
 *   // Start capture (VideoPane gains focus)
 *   SystemKeyBlocker::instance().start();
 *
 *   // Route all captured keys to KeyboardManager
 *   connect(&SystemKeyBlocker::instance(), &SystemKeyBlocker::keyCaptured,
 *           [](int key, int mod, bool down, quint32 nativeVk) {
 *               KeyboardManager::instance().handleKeyboardAction(
 *                   key, mod, down, nativeVk);
 *           });
 *
 *   // Stop capture (VideoPane loses focus)
 *   SystemKeyBlocker::instance().stop();
 * @endcode
 *
 * Reference: VirtualBox UIKeyboardHandler.cpp
 *   https://github.com/vbox/vbox/blob/main/src/VBox/Frontends/VirtualBox/src/runtime/UIKeyboardHandler.cpp
 */
class SystemKeyBlocker : public QObject
{
    Q_OBJECT

public:
    /* ---- singleton ---- */
    static SystemKeyBlocker& instance();

    /* ---- lifecycle ---- */

    /**
     * Start full keyboard capture.
     *
     * Installs a platform-level hook / event filter that intercepts every
     * key event.  If already active the previous hook is torn down first.
     *
     * @param nativeParentHwnd  Platform-native window handle.
     *        On Windows this is the HWND of the VideoPane (winId()).
     *        On Linux it is ignored (X11 connection is discovered automatically).
     * @return true on success, false on failure (log contains details).
     */
    bool start(quintptr nativeParentHwnd = 0);

    /// Stop full keyboard capture.  All hooks are released, the OS
    /// resumes its normal keyboard handling.
    void stop();

    /// Whether the capture hook is currently installed.
    bool isActive() const { return m_active; }

signals:
    /**
     * Emitted for every keystroke while capture is active.
     *
     * @param qtKeyCode  Qt key code (e.g. Qt::Key_Meta, Qt::Key_Print).
     * @param modifiers  OR-ed Qt modifiers (Shift/Ctrl/Alt/Meta).
     * @param isKeyDown  true = press, false = release.
     * @param nativeVk   Platform-native virtual key / keysym.
     */
    void keyCaptured(int qtKeyCode, int modifiers, bool isKeyDown, quint32 nativeVk);

    /// Emitted whenever the capture state changes.
    void captureStateChanged(bool active);

private:
    explicit SystemKeyBlocker(QObject *parent = nullptr);
    ~SystemKeyBlocker() override;

    SystemKeyBlocker(const SystemKeyBlocker&) = delete;
    SystemKeyBlocker& operator=(const SystemKeyBlocker&) = delete;

    /* ---- platform impl (conditional compilation) ---- */
    bool startImpl(quintptr nativeParentHwnd);
    void stopImpl();

    /// Translate a platform native VK / keysym to a Qt key code
    int  nativeToQtKey(quint32 nativeVk, bool extended) const;

    /* ---- state ---- */
    bool m_active = false;

    /* ---- Windows ---- */
#ifdef Q_OS_WIN
    void       *m_hHook      = nullptr;   // HHOOK
    quintptr    m_hookedHwnd = 0;         // HWND

    static SystemKeyBlocker *s_self;      // needed by the static hook proc
    static LRESULT CALLBACK lowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
#endif // Q_OS_WIN

    /* ---- Linux X11 ---- */
#ifdef Q_OS_LINUX
    class X11KeyCaptureFilter;
    class X11KeyGrabber;
    X11KeyCaptureFilter *m_x11Filter  = nullptr;
    X11KeyGrabber       *m_x11Grabber = nullptr;
#endif // Q_OS_LINUX
};

#endif // SYSTEMKEYBLOCKER_H