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
#include <QGuiApplication>
#include <QApplication>
#include <QWidget>
#include <QWindow>
#include <QWindowList>
#include <QCoreApplication>
#include <QAbstractNativeEventFilter>
#include <QProcess>
#include <QTimer>

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <xcb/xcb.h>

#include <unistd.h>
#include "../log/opflogging.h"

OPF_LOGGING_CATEGORY(log_syskey_x11, "opf.systemkey.x11")

/* ============================================================================
 *  GNOME System Shortcut Blocker
 *
 *  In GNOME Wayland environment, Mutter intercepts system shortcuts
 *  (Super, Alt+Tab, Alt+Space, etc.)
 *  This prevents XGrabKeyboard from capturing these keys.
 *
 *  Solution:
 *  1. When starting keyboard capture, temporarily disable all conflicting
 *     GNOME system shortcuts
 *  2. Save the original shortcut settings
 *  3. When stopping keyboard capture, automatically restore original settings
 *
 *  Uses QProcess::startDetached() to be fully non-blocking, avoiding
 *  main thread deadlock.
 *
 *  NOTE: With the current X11KeyCaptureFilter approach (no XGrabKeyboard),
 *  this class is no longer called. It is kept for potential future use.
 * ============================================================================ */

// List of GNOME shortcuts to disable
struct GnomeShortcutEntry {
    const char* schema;
    const char* key;
    QString originalValue;
    bool disabled;
};

static QList<GnomeShortcutEntry*> g_shortcuts;

// Initialize the shortcuts to be disabled
static void initShortcuts() {
    if (!g_shortcuts.isEmpty()) return;

    // Mutter overlay-key (Super key)
    g_shortcuts.append(new GnomeShortcutEntry{
        "org.gnome.mutter", "overlay-key", "", false
    });

    // Alt+Tab window switching
    g_shortcuts.append(new GnomeShortcutEntry{
        "org.gnome.desktop.wm.keybindings", "switch-applications", "", false
    });
    g_shortcuts.append(new GnomeShortcutEntry{
        "org.gnome.desktop.wm.keybindings", "switch-applications-backward", "", false
    });
    g_shortcuts.append(new GnomeShortcutEntry{
        "org.gnome.desktop.wm.keybindings", "switch-windows", "", false
    });
    g_shortcuts.append(new GnomeShortcutEntry{
        "org.gnome.desktop.wm.keybindings", "switch-windows-backward", "", false
    });

    // Alt+Space window menu
    g_shortcuts.append(new GnomeShortcutEntry{
        "org.gnome.desktop.wm.keybindings", "activate-window-menu", "", false
    });

    // Super+L lock screen
    g_shortcuts.append(new GnomeShortcutEntry{
        "org.gnome.settings-daemon.plugins.media-keys", "screensaver", "", false
    });

    // PrintScreen screenshot
    g_shortcuts.append(new GnomeShortcutEntry{
        "org.gnome.settings-daemon.plugins.media-keys", "screenshot", "", false
    });
    g_shortcuts.append(new GnomeShortcutEntry{
        "org.gnome.settings-daemon.plugins.media-keys", "area-screenshot", "", false
    });
    g_shortcuts.append(new GnomeShortcutEntry{
        "org.gnome.settings-daemon.plugins.media-keys", "window-screenshot", "", false
    });

    // Super+number to launch favorite app
    for (int i = 1; i <= 9; ++i) {
        g_shortcuts.append(new GnomeShortcutEntry{
            "org.gnome.shell.keybindings",
            QString("switch-to-application-%1").arg(i).toUtf8().constData(),
            "", false
        });
    }

    // Ctrl+Alt+T terminal (on some systems)
    g_shortcuts.append(new GnomeShortcutEntry{
        "org.gnome.settings-daemon.plugins.media-keys", "terminal", "", false
    });
}

class GnomeShortcutBlocker {
public:
    static GnomeShortcutBlocker& instance() {
        static GnomeShortcutBlocker mgr;
        return mgr;
    }

    /**
     * Disable all conflicting GNOME shortcuts, saving original values.
     * Uses startDetached to be fully non-blocking.
     */
    void disableAll() {
        if (m_disabled) {
            return;  // Already disabled
        }

        // Detect if running in GNOME environment
        if (!isGnomeEnvironment()) {
            qCDebug(log_syskey_x11) << "Not GNOME environment, skipping shortcut management";
            m_disabled = true;
            return;
        }

        initShortcuts();

        // Disable all shortcuts
        for (auto* entry : g_shortcuts) {
            if (entry->disabled) continue;

            // Get original value
            QProcess process;
            process.start("gsettings", {"get", entry->schema, entry->key});
            if (process.waitForFinished(500)) {
                QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
                entry->originalValue = output;

                // If already has a value, disable it
                if (!output.isEmpty() && output != "''" && output != "@as []" && output != "[]") {
                    QString emptyValue;
                    if (output.startsWith("@as")) {
                        emptyValue = "@as []";
                    } else if (output.startsWith("[")) {
                        emptyValue = "[]";  // Array type: clear with []
                    } else {
                        emptyValue = "''";  // String type: clear with ''
                    }
                    QStringList args = {"set", entry->schema, entry->key, emptyValue};
                    QProcess::startDetached("gsettings", args);
                    entry->disabled = true;
                    qCInfo(log_syskey_x11) << "Disabled" << entry->schema << entry->key
                                           << "(original:" << output << ", empty:" << emptyValue << ")";
                }
            }
        }

        m_disabled = true;
        qCInfo(log_syskey_x11) << "Disabled all conflicting GNOME system shortcuts";
    }

    /**
     * Restore all GNOME shortcuts to their original values.
     * Uses startDetached to be fully non-blocking.
     */
    void restoreAll() {
        if (!m_disabled) {
            return;  // Not disabled, nothing to restore
        }

        // Restore all shortcuts
        for (auto* entry : g_shortcuts) {
            if (!entry->disabled) continue;
            if (entry->originalValue.isEmpty() || entry->originalValue == "''" || entry->originalValue == "@as []") {
                entry->disabled = false;
                continue;  // Original value is already empty, nothing to restore
            }

            // Restore original value
            QStringList args = {"set", entry->schema, entry->key, entry->originalValue};
            QProcess::startDetached("gsettings", args);
            entry->disabled = false;
            qCInfo(log_syskey_x11) << "Restored" << entry->schema << entry->key
                                   << "->" << entry->originalValue;
        }

        m_disabled = false;
        qCInfo(log_syskey_x11) << "Restored all GNOME system shortcuts";
    }

    /**
     * Detect if running in GNOME environment.
     */
    bool isGnomeEnvironment() {
        QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
        return desktop.contains("GNOME", Qt::CaseInsensitive);
    }

private:
    GnomeShortcutBlocker() = default;
    ~GnomeShortcutBlocker() {
        // Ensure restoration on destruction (synchronous, since this is program exit)
        if (m_disabled && isGnomeEnvironment()) {
            for (auto* entry : g_shortcuts) {
                if (!entry->disabled) continue;
                if (entry->originalValue.isEmpty() || entry->originalValue == "''" || entry->originalValue == "@as []") {
                    continue;
                }

                QProcess process;
                process.start("gsettings", {"set", entry->schema, entry->key, entry->originalValue});
                process.waitForFinished(500);
                entry->disabled = false;
            }
        }

        // Free memory
        qDeleteAll(g_shortcuts);
        g_shortcuts.clear();
    }

    GnomeShortcutBlocker(const GnomeShortcutBlocker&) = delete;
    GnomeShortcutBlocker& operator=(const GnomeShortcutBlocker&) = delete;

    bool m_disabled = false;
};

/* ============================================================================
 *  X11KeyCaptureFilter — Passive keyboard capture via QAbstractNativeEventFilter
 *
 *  Mirrors the Windows WH_KEYBOARD_LL approach:
 *  - Installs on Qt's own XCB connection (no separate Display, no XGrabKeyboard)
 *  - Only intercepts keyboard events when our window has focus
 *  - Always forwards via keyCaptured signal
 *  - When swallow is ON: consumes event (return true → Qt doesn't process)
 *  - When swallow is OFF: lets event pass (return false → Qt processes normally)
 *
 *  No XGrabKeyboard → no global keyboard grab → no focus stealing, no lag.
 *  No separate Display connection, no polling timer, no synchronous X calls.
 *
 *  No Q_OBJECT — no .moc file needed.
 * ============================================================================ */

class SystemKeyBlocker::X11KeyCaptureFilter : public QAbstractNativeEventFilter
{
public:
    X11KeyCaptureFilter(SystemKeyBlocker *blocker, ::Window tlw);
    ~X11KeyCaptureFilter() override;

    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           qintptr *result) override;

    void resetModifierState() { m_modState = ModifierKeyState{}; }

private:
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

    SystemKeyBlocker *m_blocker = nullptr;
    ::Window          m_tlw     = 0;
    Display          *m_dpy     = nullptr;   // Cached X display for keysym conversion
    ModifierKeyState  m_modState;
};

SystemKeyBlocker::X11KeyCaptureFilter::X11KeyCaptureFilter(SystemKeyBlocker *blocker, ::Window tlw)
    : m_blocker(blocker), m_tlw(tlw)
{
    // Open a display connection for keysym conversion (cached for the lifetime of the filter)
    m_dpy = XOpenDisplay(nullptr);
    if (!m_dpy) {
        qCWarning(log_syskey_x11) << "Cannot open X11 display for keysym conversion";
    }
    qCInfo(log_syskey_x11) << "X11KeyCaptureFilter initialized: window=" << m_tlw;
}

SystemKeyBlocker::X11KeyCaptureFilter::~X11KeyCaptureFilter()
{
    if (m_dpy) {
        XCloseDisplay(m_dpy);
        m_dpy = nullptr;
    }
}

bool SystemKeyBlocker::X11KeyCaptureFilter::nativeEventFilter(
    const QByteArray &eventType, void *message, qintptr * /*result*/)
{
    if (eventType != QByteArrayLiteral("xcb_generic_event_t"))
        return false;

    xcb_generic_event_t *event = static_cast<xcb_generic_event_t *>(message);
    uint responseType = event->response_type & ~0x80;

    // Only handle keyboard events
    if (responseType != XCB_KEY_PRESS && responseType != XCB_KEY_RELEASE)
        return false;

    // Only swallow keys when focus is inside the designated focus target (VideoPane).
    // This allows dialogs (e.g. Preferences) to receive keyboard input even when
    // SystemBlocker is enabled.
    if (!m_blocker->focusTarget())
        return false;
    QWidget *fw = QApplication::focusWidget();
    if (!fw || !m_blocker->focusTarget()->isAncestorOf(fw))
        return false;

    xcb_key_press_event_t *ke = reinterpret_cast<xcb_key_press_event_t *>(event);
    const bool isDown = (responseType == XCB_KEY_PRESS);

    // Convert keycode to keysym (using cached display connection)
    if (!m_dpy) return false;
    KeySym keysym = XkbKeycodeToKeysym(m_dpy, ke->detail, 0, 0);

    qCDebug(log_syskey_x11) << "Key" << (isDown ? "PRESS" : "RELEASE")
                            << "keycode:" << ke->detail << "keysym:" << keysym;

    // Track modifier state
    switch (keysym) {
        case XK_Shift_L:   m_modState.lShift = isDown; break;
        case XK_Shift_R:   m_modState.rShift = isDown; break;
        case XK_Control_L: m_modState.lCtrl  = isDown; break;
        case XK_Control_R: m_modState.rCtrl  = isDown; break;
        case XK_Alt_L:     m_modState.lAlt   = isDown; break;
        case XK_Alt_R:     m_modState.rAlt   = isDown; break;
        case XK_Super_L:   m_modState.lSuper = isDown; break;
        case XK_Super_R:   m_modState.rSuper = isDown; break;
        case XK_Meta_L:    m_modState.lSuper = isDown; break;
        case XK_Meta_R:    m_modState.rSuper = isDown; break;
        default: break;
    }

    // Build modifier mask
    int modifiers = 0;
    if (m_modState.lShift || m_modState.rShift) modifiers |= Qt::ShiftModifier;
    if (m_modState.lCtrl  || m_modState.rCtrl)  modifiers |= Qt::ControlModifier;
    if (m_modState.lAlt   || m_modState.rAlt)   modifiers |= Qt::AltModifier;
    if (m_modState.lSuper || m_modState.rSuper) modifiers |= Qt::MetaModifier;

    // Translate to Qt key code and emit
    const int qtKey = m_blocker->nativeToQtKey(keysym, false);

    emit m_blocker->keyCaptured(qtKey, modifiers, isDown, static_cast<quint32>(keysym));

    // Swallow if enabled (local OS won't see the key)
    // When OFF, let Qt process the event normally (local OS also sees it)
    return m_blocker->isSwallowEnabled();
}

/* ============================================================================
 *  startImpl / stopImpl
 * ============================================================================ */

bool SystemKeyBlocker::startImpl(quintptr /*nativeParentHwnd*/)
{
    qCInfo(log_syskey) << "=== startImpl() called ===";

    // Get our visible top-level window ID for the capture filter
    ::Window tlw = 0;
    const QWindowList topLevels = QGuiApplication::topLevelWindows();
    for (QWindow *w : topLevels) {
        if (w->isVisible()) {
            tlw = static_cast<::Window>(w->winId());
            break;
        }
    }

    if (!tlw) {
        qCWarning(log_syskey) << "No visible top-level window found, cannot start capture";
        return false;
    }

    // Strategy: Use QAbstractNativeEventFilter on Qt's own XCB connection.
    // This is equivalent to the Windows WH_KEYBOARD_LL approach:
    // - No global keyboard grab (no XGrabKeyboard)
    // - Only intercepts when our window has focus
    // - No separate Display connection, no polling timer, no synchronous X calls
    qCInfo(log_syskey) << "Creating X11KeyCaptureFilter (passive capture, no XGrabKeyboard)...";
    m_x11Filter = new X11KeyCaptureFilter(this, tlw);

    qCInfo(log_syskey) << "Installing native event filter on Qt's connection...";
    QCoreApplication::instance()->installNativeEventFilter(m_x11Filter);

    qCInfo(log_syskey) << "X11 keyboard capture started (passive filter, no grab)";
    return true;
}

void SystemKeyBlocker::stopImpl()
{
    qCInfo(log_syskey) << "Stopping key capture";

    if (m_x11Filter) {
        qCInfo(log_syskey) << "Removing native event filter...";
        QCoreApplication::instance()->removeNativeEventFilter(m_x11Filter);
        delete m_x11Filter;
        m_x11Filter = nullptr;
    }

    qCInfo(log_syskey) << "X11 keyboard capture stopped";
}
