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
#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QWindow>
#include <QWindowList>

#include <xcb/xcb.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>

Q_LOGGING_CATEGORY(log_syskey_x11, "opf.systemkey.x11")

/* ============================================================================
 *  Modifier key state tracking
 *
 *  Since we swallow key events, the X server / Qt won't see them.
 *  We maintain our own modifier state — same approach as the Windows backend.
 * ============================================================================ */

struct ModifierKeyState {
    bool lShift = false;
    bool rShift = false;
    bool lCtrl  = false;
    bool rCtrl  = false;
    bool lAlt   = false;
    bool rAlt   = false;
    bool lSuper = false;
    bool rSuper = false;
};
static ModifierKeyState g_modifierState;

/* ============================================================================
 *  Safe X error handler for XGrabKey
 *
 *  XGrabKey may generate BadAccess if the WM already holds a conflicting
 *  grab on the same key+modifiers.  The default X error handler would
 *  terminate the application.  We install a temporary no-op handler so the
 *  error is silently swallowed.
 * ============================================================================ */

static int g_xGrabKeyErrorCode = 0;

static int xGrabKeyErrorHandler(Display * /*dpy*/, XErrorEvent *ev)
{
    g_xGrabKeyErrorCode = ev->error_code;
    return 0;   // swallowed
}

/* ============================================================================
 *  X11KeyGrabber — grabs system keys on our top-level window
 *
 *  On X11, the window manager typically grabs system keys (Super, Alt+Tab,
 *  etc.) on the root window with a passive grab.  If we also place a passive
 *  grab on the SAME window (root), the WM's grab wins (first-come-first-
 *  served).
 *
 *  However, the X server rule is: when multiple passive grabs exist on
 *  DIFFERENT windows in the ancestor chain, the grab on the *lowest*
 *  (closest-to-focused) window wins.  By placing our grab on our top-level
 *  window, our grab takes precedence when our app has focus.
 *
 *  owner_events=True: the KeyPress/Release events are still reported to the
 *  actually focused child (Qt sees them, our filter catches them).
 *
 *  Limitations:
 *    - If the WM has a grab on our top-level window itself (unusual), our
 *      grab may fail.  We log a warning and continue.
 *    - Ctrl+Alt+Del is intercepted by the kernel on Linux and cannot be
 *      grabbed by any X11 client.
 *    - Wayland is not supported (hard compositor restrictions).
 * ============================================================================ */

class SystemKeyBlocker::X11KeyGrabber
{
public:
    X11KeyGrabber();
    ~X11KeyGrabber();

    bool grabSystemKeys();
    void ungrabSystemKeys();

private:
    static QVector<int> systemKeysyms();

    Display    *m_dpy  = nullptr;   // borrowed — do not free
    ::Window    m_tlw  = 0;         // our top-level X11 window
    bool        m_grabbed = false;
};

// ---------------------------------------------------------------------------

X11KeyGrabber::X11KeyGrabber()
{
    // Obtain the X11 Display* from Qt's platform integration
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        m_dpy = reinterpret_cast<Display *>(
            app->platformNativeInterface()->nativeResourceForScreen("display", nullptr));
    }
    if (!m_dpy) {
        qCWarning(log_syskey_x11) << "X11KeyGrabber: cannot obtain X11 Display";
        return;
    }

    // Get our top-level window — grab on this window so our passive grab
    // takes precedence over the WM's grab on the root window.
    const QWindowList topLevels = QGuiApplication::topLevelWindows();
    for (QWindow *w : topLevels) {
        if (w->isVisible()) {
            m_tlw = static_cast<::Window>(w->winId());
            break;
        }
    }
    if (!m_tlw) {
        qCWarning(log_syskey_x11) << "X11KeyGrabber: no visible top-level window found";
    }
}

X11KeyGrabber::~X11KeyGrabber()
{
    if (m_grabbed)
        ungrabSystemKeys();
}

QVector<int> X11KeyGrabber::systemKeysyms()
{
    return {
        // Super / Meta keys (WM app launcher, overview, etc.)
        XK_Super_L, XK_Super_R,
        XK_Meta_L,  XK_Meta_R,

        // Keys commonly bound to system actions
        XK_Tab,         // Alt+Tab / Super+Tab
        XK_ISO_Left_Tab,
        XK_Escape,      // Super+Escape (activity overview)
        XK_Print,       // PrintScreen
        XK_Sys_Req,
        XK_Menu,        // Context menu key
        XK_Break,

        // Function keys (some DEs bind F-keys to system actions)
        XK_F1,  XK_F2,  XK_F3,  XK_F4,  XK_F5,  XK_F6,
        XK_F7,  XK_F8,  XK_F9,  XK_F10, XK_F11, XK_F12,

        // Navigation keys
        XK_Insert, XK_Delete,
        XK_Home, XK_End,
        XK_Page_Up, XK_Page_Down,

        // Volume / media
        XF86XK_AudioMute,
        XF86XK_AudioLowerVolume,
        XF86XK_AudioRaiseVolume,
        XF86XK_AudioPlay,
        XF86XK_AudioStop,
        XF86XK_AudioPrev,
        XF86XK_AudioNext,
    };
}

bool X11KeyGrabber::grabSystemKeys()
{
    if (!m_dpy || !m_tlw) {
        qCWarning(log_syskey_x11) << "X11KeyGrabber: cannot grab — no display or window";
        return false;
    }

    const QVector<int> keys = systemKeysyms();
    int failed = 0;

    // Install safe error handler — XGrabKey may return BadAccess if the WM
    // already owns a conflicting grab on the same key+modifiers.
    auto *oldHandler = XSetErrorHandler(xGrabKeyErrorHandler);

    for (int ks : keys) {
        g_xGrabKeyErrorCode = 0;

        KeyCode kc = XKeysymToKeycode(m_dpy, static_cast<KeySym>(ks));
        if (kc == 0)
            continue;   // keysym has no keycode on this keyboard — skip

        // AnyModifier = grab regardless of which other modifiers are held.
        // This catches Alt+Tab, Ctrl+Escape, Super+A, etc.
        // owner_events=True: events still delivered to focused child window.
        XGrabKey(m_dpy, kc, AnyModifier, m_tlw,
                 /*owner_events=*/True,
                 GrabModeAsync, GrabModeAsync);
        XSync(m_dpy, False);

        if (g_xGrabKeyErrorCode != 0) {
            ++failed;
        }
    }

    // Restore the previous error handler
    XSetErrorHandler(oldHandler);

    if (failed > 0) {
        qCWarning(log_syskey_x11)
            << "XGrabKey: failed for" << failed << "of" << keys.size()
            << "keys (WM may already own those grabs)";
    }

    m_grabbed = true;
    qCInfo(log_syskey_x11) << "System keys grabbed on TLW" << m_tlw
                           << "(" << (keys.size() - failed) << "/" << keys.size() << " succeeded)";
    return (failed < keys.size());   // succeed if at least some keys were grabbed
}

void X11KeyGrabber::ungrabSystemKeys()
{
    if (!m_dpy || !m_tlw)
        return;

    const QVector<int> keys = systemKeysyms();
    for (int ks : keys) {
        KeyCode kc = XKeysymToKeycode(m_dpy, static_cast<KeySym>(ks));
        if (kc == 0)
            continue;
        XUngrabKey(m_dpy, kc, AnyModifier, m_tlw);
    }
    XSync(m_dpy, False);
    m_grabbed = false;
    qCInfo(log_syskey_x11) << "System keys ungrabbed";
}

/* ============================================================================
 *  X11KeyCaptureFilter — QAbstractNativeEventFilter
 *
 *  Catches XCB key events delivered to our windows.  System keys are
 *  redirected to our window by X11KeyGrabber's XGrabKey, then we intercept
 *  them here before any Qt widget sees them.
 *
 *  BOTH key-press AND key-release events are swallowed (return true),
 *  matching the Windows low-level-hook behaviour.
 * ============================================================================ */

class SystemKeyBlocker::X11KeyCaptureFilter : public QAbstractNativeEventFilter
{
public:
    X11KeyCaptureFilter(SystemKeyBlocker *b) : blocker(b) {}

    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           long * /*result*/) override
    {
        if (eventType != QByteArrayLiteral("xcb_generic_event_t"))
            return false;

        if (!blocker || !blocker->isActive())
            return false;

        const auto *ev = static_cast<xcb_generic_event_t *>(message);
        const uint8_t type = ev->response_type & ~0x80;

        if (type != XCB_KEY_PRESS && type != XCB_KEY_RELEASE)
            return false;

        const auto *ke = static_cast<xcb_key_press_event_t *>(message);
        const bool isDown = (type == XCB_KEY_PRESS);

        // ---- Resolve keysym ----
        Display *dpy = nullptr;
        if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            dpy = reinterpret_cast<Display *>(
                app->platformNativeInterface()->nativeResourceForScreen("display", nullptr));
        }
        if (!dpy) {
            qCWarning(log_syskey_x11) << "Cannot get X11 Display — event dropped";
            return false;
        }

        KeySym ks = XkbKeycodeToKeysym(dpy, ke->detail, 0, 0);

        // ---- Update modifier state tracking (matches Windows approach) ----
        switch (ks) {
            case XK_Shift_L:    g_modifierState.lShift = isDown; break;
            case XK_Shift_R:    g_modifierState.rShift = isDown; break;
            case XK_Control_L:  g_modifierState.lCtrl  = isDown; break;
            case XK_Control_R:  g_modifierState.rCtrl  = isDown; break;
            case XK_Alt_L:      g_modifierState.lAlt   = isDown; break;
            case XK_Alt_R:      g_modifierState.rAlt   = isDown; break;
            case XK_Super_L:
            case XK_Meta_L:     g_modifierState.lSuper = isDown; break;
            case XK_Super_R:
            case XK_Meta_R:     g_modifierState.rSuper = isDown; break;
            default: break;
        }

        // ---- Build modifier mask from our tracked state ----
        int modifiers = 0;
        if (g_modifierState.lShift || g_modifierState.rShift) modifiers |= Qt::ShiftModifier;
        if (g_modifierState.lCtrl  || g_modifierState.rCtrl)  modifiers |= Qt::ControlModifier;
        if (g_modifierState.lAlt   || g_modifierState.rAlt)   modifiers |= Qt::AltModifier;
        if (g_modifierState.lSuper || g_modifierState.rSuper) modifiers |= Qt::MetaModifier;

        // ---- Translate to Qt key code and emit ----
        const int qtKey = blocker->nativeToQtKey(static_cast<quint32>(ks), false);
        emit blocker->keyCaptured(qtKey, modifiers, isDown, static_cast<quint32>(ks));

        // ---- Swallow ALL key events (both press and release) ----
        // Matching Windows behaviour: every keystroke goes to the target machine,
        // the host OS must not see any of them.
        return true;
    }

private:
    SystemKeyBlocker *blocker = nullptr;
};

/* ============================================================================
 *  startImpl / stopImpl
 * ============================================================================ */

bool SystemKeyBlocker::startImpl(quintptr /*nativeParentHwnd*/)
{
    qCInfo(log_syskey_x11) << "Starting X11 key capture";

    // Reset modifier state
    g_modifierState = ModifierKeyState{};

    // 1) Install the native event filter (catches all key events for our windows)
    m_x11Filter = new X11KeyCaptureFilter(this);
    QCoreApplication::instance()->installNativeEventFilter(m_x11Filter);

    // 2) Grab system keys on our top-level window so the WM doesn't see them
    m_x11Grabber = new X11KeyGrabber;
    m_x11Grabber->grabSystemKeys();

    return true;
}

void SystemKeyBlocker::stopImpl()
{
    // 1) Remove event filter
    if (m_x11Filter) {
        QCoreApplication::instance()->removeNativeEventFilter(m_x11Filter);
        delete m_x11Filter;
        m_x11Filter = nullptr;
    }

    // 2) Release system key grabs
    if (m_x11Grabber) {
        m_x11Grabber->ungrabSystemKeys();
        delete m_x11Grabber;
        m_x11Grabber = nullptr;
    }

    // Reset modifier state
    g_modifierState = ModifierKeyState{};

    qCInfo(log_syskey_x11) << "X11 key capture stopped";
}
