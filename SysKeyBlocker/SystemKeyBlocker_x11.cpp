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

Q_LOGGING_CATEGORY(log_syskey_x11, "opf.systemkey.x11")

/* ============================================================================
 *  Modifier key state tracking
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
 *  X11KeyGrabber — Grab the entire keyboard via XGrabKeyboard()
 *
 *  Uses a SEPARATE X11 Display connection for the grab. This is critical:
 *  events from XGrabKeyboard are delivered to the grabbing client's
 *  connection. We use a QTimer to poll for events on the grab connection
 *  and forward them to the target machine.
 *
 *  We also install a QAbstractNativeEventFilter on Qt's connection to
 *  watch for FocusIn/FocusOut on our window, so we can re-grab when
 *  focus returns (the grab is lost on focus-out).
 *
 *  No Q_OBJECT — no .moc file needed.
 * ============================================================================ */

class SystemKeyBlocker::X11KeyGrabber : public QAbstractNativeEventFilter
{
public:
    X11KeyGrabber(SystemKeyBlocker *blocker);
    ~X11KeyGrabber() override;

    bool grabKeyboard();
    void ungrabKeyboard();
    bool isGrabbed() const { return m_grabbed; }

    // QAbstractNativeEventFilter — track focus changes on Qt's connection
    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           qintptr *result) override;

    // Called by timer to process grabbed events
    void processEvents();

private:
    void handleKeyEvent(quint32 keysym, quint32 state, bool isPress);

    SystemKeyBlocker *m_blocker = nullptr;
    Display         *m_grabDpy = nullptr;   // Separate X connection for grab
    ::Window         m_tlw     = 0;         // Our window ID
    QTimer          *m_pollTimer = nullptr;  // Polls grab connection for events
    bool             m_grabbed  = false;
};

SystemKeyBlocker::X11KeyGrabber::X11KeyGrabber(SystemKeyBlocker *blocker)
    : m_blocker(blocker)
{
    // Open a SEPARATE X display connection for grabbing.
    // Events from XGrabKeyboard will be delivered to this connection.
    m_grabDpy = XOpenDisplay(nullptr);
    if (!m_grabDpy) {
        qCWarning(log_syskey_x11) << "Cannot open X11 display for grab";
        return;
    }

    // Get our visible top-level window ID
    const QWindowList topLevels = QGuiApplication::topLevelWindows();
    for (QWindow *w : topLevels) {
        if (w->isVisible()) {
            m_tlw = static_cast<::Window>(w->winId());
            break;
        }
    }

    if (!m_tlw) {
        qCWarning(log_syskey_x11) << "No visible top-level window found";
        return;
    }

    // Use a QTimer to poll the grab connection for events.
    // 5ms interval = ~200Hz polling, low latency for key events.
    m_pollTimer = new QTimer();
    m_pollTimer->setInterval(5);
    QObject::connect(m_pollTimer, &QTimer::timeout, [this]() {
        processEvents();
    });

    qCInfo(log_syskey_x11) << "X11KeyGrabber initialized: window=" << m_tlw;
}

SystemKeyBlocker::X11KeyGrabber::~X11KeyGrabber()
{
    if (m_grabbed)
        ungrabKeyboard();

    if (m_pollTimer) {
        m_pollTimer->stop();
        delete m_pollTimer;
        m_pollTimer = nullptr;
    }

    if (m_grabDpy) {
        XCloseDisplay(m_grabDpy);
        m_grabDpy = nullptr;
    }
}

bool SystemKeyBlocker::X11KeyGrabber::grabKeyboard()
{
    if (m_grabbed || !m_grabDpy || !m_tlw) {
        qCInfo(log_syskey_x11) << "grabKeyboard() skipped: m_grabbed=" << m_grabbed
                               << "m_grabDpy=" << m_grabDpy << "m_tlw=" << m_tlw;
        return m_grabbed;
    }

    qCInfo(log_syskey_x11) << "Attempting XGrabKeyboard on window" << m_tlw;

    // XGrabKeyboard: active grab on our separate display connection.
    // - owner_events=False: ALL key events go to the grab window (m_tlw),
    //   regardless of which window actually has focus. This is critical:
    //   with True, events go to the window on our connection that has focus,
    //   but since we're on a separate connection, that's unreliable.
    // - GrabModeAsync: key/mouse events continue normally (no freeze)
    //
    // With owner_events=False, X server delivers ALL key events to m_tlw
    // on our grab connection. This blocks Alt+Tab completely because the
    // WM never sees the Alt key at all.
    int status = XGrabKeyboard(
        m_grabDpy,
        m_tlw,
        False,          // owner_events = False (critical!)
        GrabModeAsync,  // pointer_mode
        GrabModeAsync,  // keyboard_mode
        CurrentTime
    );

    if (status == GrabSuccess) {
        m_grabbed = true;
        XFlush(m_grabDpy);
        // Start polling for grabbed events
        if (m_pollTimer)
            m_pollTimer->start();
        qCInfo(log_syskey_x11) << "Keyboard grabbed successfully on window" << m_tlw
                                << "(owner_events=False)";
    } else {
        const char *reason = "Unknown";
        switch (status) {
            case AlreadyGrabbed:  reason = "AlreadyGrabbed";  break;
            case GrabNotViewable: reason = "GrabNotViewable"; break;
            case GrabFrozen:      reason = "GrabFrozen";      break;
            case GrabInvalidTime: reason = "GrabInvalidTime"; break;
        }
        qCWarning(log_syskey_x11) << "XGrabKeyboard failed, status:" << status
                                   << "(" << reason << ")";
    }

    return m_grabbed;
}

void SystemKeyBlocker::X11KeyGrabber::ungrabKeyboard()
{
    if (!m_grabDpy || !m_tlw) {
        qCInfo(log_syskey_x11) << "ungrabKeyboard() skipped: m_grabDpy=" << m_grabDpy
                               << "m_tlw=" << m_tlw;
        return;
    }

    qCInfo(log_syskey_x11) << "Calling XUngrabKeyboard on window" << m_tlw << "m_grabbed=" << m_grabbed;

    // Stop polling
    if (m_pollTimer)
        m_pollTimer->stop();

    XUngrabKeyboard(m_grabDpy, CurrentTime);
    XFlush(m_grabDpy);
    m_grabbed = false;
    qCInfo(log_syskey_x11) << "Keyboard ungrabbed";
}

void SystemKeyBlocker::X11KeyGrabber::processEvents()
{
    if (!m_grabDpy) {
        qCWarning(log_syskey_x11) << "processEvents() called but m_grabDpy is null!";
        return;
    }

    // Don't return early if !m_grabbed - X server might still be delivering events
    // even during re-grab attempts. We want to process any pending events.

    // Process all pending events on our grab connection
    int eventCount = 0;
    int keyEventCount = 0;
    while (m_grabDpy && XPending(m_grabDpy) > 0) {
        XEvent ev;
        XNextEvent(m_grabDpy, &ev);
        eventCount++;

        if (ev.type == KeyPress || ev.type == KeyRelease) {
            keyEventCount++;
            const XKeyEvent &ke = ev.xkey;
            const bool isDown = (ev.type == KeyPress);

            // Convert keycode to keysym
            KeySym ks = XkbKeycodeToKeysym(m_grabDpy, ke.keycode, 0, 0);

            qCInfo(log_syskey_x11) << "✓ Key event on grab connection:" << (isDown ? "PRESS" : "RELEASE")
                                   << "keycode:" << ke.keycode << "keysym:" << ks
                                   << "state:" << Qt::hex << ke.state;

            handleKeyEvent(static_cast<quint32>(ks),
                           static_cast<quint32>(ke.state),
                           isDown);
        } else if (ev.type == FocusIn || ev.type == FocusOut) {
            // Focus events on grab connection - check if we lost the grab
            const XFocusChangeEvent &fe = ev.xfocus;
            qCInfo(log_syskey_x11) << "Focus event on GRAB connection:"
                                   << (ev.type == FocusIn ? "FocusIn" : "FocusOut")
                                   << "window:" << fe.window
                                   << "mode:" << fe.mode
                                   << "detail:" << fe.detail;

            // When grab is lost due to FocusOut with NotifyWhileGrabbed,
            // we need to mark as ungrabbed and schedule a re-grab
            // But DON'T stop the timer - keep processing events
            if (ev.type == FocusOut && fe.mode == NotifyWhileGrabbed && m_grabbed) {
                qCWarning(log_syskey_x11) << "✗ Keyboard grab LOST! Scheduling re-grab in 100ms...";
                m_grabbed = false;
                // Keep timer running - don't stop it, so we continue processing events
                // Schedule re-grab after a short delay
                QTimer::singleShot(100, [this]() {
                    qCInfo(log_syskey_x11) << "Attempting to re-grab keyboard...";
                    grabKeyboard();
                });
            }
        } else {
            qCDebug(log_syskey_x11) << "Other event on grab connection, type:" << ev.type;
        }
    }

    if (eventCount > 0) {
        qCInfo(log_syskey_x11) << "processEvents() processed" << eventCount << "total events,"
                               << keyEventCount << "key events";
    }
}

void SystemKeyBlocker::X11KeyGrabber::handleKeyEvent(quint32 keysym, quint32 /*state*/, bool isDown)
{
    qCInfo(log_syskey_x11) << "handleKeyEvent() called: keysym=" << keysym << "isDown=" << isDown;

    if (!m_blocker) {
        qCWarning(log_syskey_x11) << "ERROR: m_blocker is null!";
        return;
    }

    // Track modifier state
    // Use hardcoded X11 keysym values to ensure correct matching
    // XK_Shift_L=0xFFE1, XK_Shift_R=0xFFE2
    // XK_Control_L=0xFFE3, XK_Control_R=0xFFE4
    // XK_Alt_L=0xFFE9, XK_Alt_R=0xFFEA
    // XK_Super_L=0xFFEB, XK_Super_R=0xFFEC
    // XK_Meta_L=0xFFE7, XK_Meta_R=0xFFE8
    qCInfo(log_syskey_x11) << "Before tracking: lAlt=" << g_modifierState.lAlt << "rAlt=" << g_modifierState.rAlt
                           << "lSuper=" << g_modifierState.lSuper << "rSuper=" << g_modifierState.rSuper;

    switch (keysym) {
        case 0xFFE1:    g_modifierState.lShift = isDown; break; // XK_Shift_L
        case 0xFFE2:    g_modifierState.rShift = isDown; break; // XK_Shift_R
        case 0xFFE3:    g_modifierState.lCtrl  = isDown; break; // XK_Control_L
        case 0xFFE4:    g_modifierState.rCtrl  = isDown; break; // XK_Control_R
        case 0xFFE9:    g_modifierState.lAlt   = isDown; qCInfo(log_syskey_x11) << "Setting lAlt=" << isDown; break; // XK_Alt_L
        case 0xFFEA:    g_modifierState.rAlt   = isDown; qCInfo(log_syskey_x11) << "Setting rAlt=" << isDown; break; // XK_Alt_R
        case 0xFFEB:    g_modifierState.lSuper = isDown; break; // XK_Super_L
        case 0xFFEC:    g_modifierState.rSuper = isDown; break; // XK_Super_R
        case 0xFFE7:    g_modifierState.lSuper = isDown; break; // XK_Meta_L
        case 0xFFE8:    g_modifierState.rSuper = isDown; break; // XK_Meta_R
        default: break;
    }

    qCInfo(log_syskey_x11) << "After tracking: lAlt=" << g_modifierState.lAlt << "rAlt=" << g_modifierState.rAlt
                           << "lSuper=" << g_modifierState.lSuper << "rSuper=" << g_modifierState.rSuper;

    // Build modifier mask
    int modifiers = 0;
    if (g_modifierState.lShift || g_modifierState.rShift) modifiers |= Qt::ShiftModifier;
    if (g_modifierState.lCtrl  || g_modifierState.rCtrl)  modifiers |= Qt::ControlModifier;
    if (g_modifierState.lAlt   || g_modifierState.rAlt)   modifiers |= Qt::AltModifier;
    if (g_modifierState.lSuper || g_modifierState.rSuper) modifiers |= Qt::MetaModifier;

    // Translate to Qt key code and emit
    const int qtKey = m_blocker->nativeToQtKey(keysym, false);

    qCInfo(log_syskey_x11) << "✓ Emitting keyCaptured signal: qtKey=" << qtKey
                           << "modifiers=" << Qt::hex << modifiers
                           << "nativeVk=" << keysym
                           << "lAlt=" << g_modifierState.lAlt
                           << "rAlt=" << g_modifierState.rAlt
                           << "lSuper=" << g_modifierState.lSuper
                           << "rSuper=" << g_modifierState.rSuper;

    emit m_blocker->keyCaptured(qtKey, modifiers, isDown, keysym);
    qCInfo(log_syskey_x11) << "✓ Signal emitted successfully";
}

bool SystemKeyBlocker::X11KeyGrabber::nativeEventFilter(
    const QByteArray &eventType, void * /*message*/, qintptr * /*result*/)
{
    if (eventType != QByteArrayLiteral("xcb_generic_event_t"))
        return false;

    // We intentionally do NOT manage the grab based on focus events.
    // When XGrabKeyboard is called, the X server sends FocusOut events on ALL
    // connections, including Qt's connection. These grab-induced focus events
    // would incorrectly trigger ungrab, creating a grab-ungrab loop.
    //
    // Instead, the grab stays active as long as the feature is enabled.
    // The user can disable the feature in settings to release the grab.
    //
    // This is the correct behavior for a KVM app: all keys should be captured
    // when the feature is enabled, regardless of focus state.

    return false;  // Never swallow — let Qt handle normally
}

/* ============================================================================
 *  startImpl / stopImpl
 * ============================================================================ */

bool SystemKeyBlocker::startImpl(quintptr /*nativeParentHwnd*/)
{
    qCInfo(log_syskey) << "=== startImpl() called ===";
    qCInfo(log_syskey) << "Resetting modifier state";

    g_modifierState = ModifierKeyState{};

    // In Wayland + GNOME environment, Mutter intercepts system shortcuts
    // (Super, Alt+Tab, Alt+Space, etc.)
    // Temporarily disable all conflicting shortcuts so XGrabKeyboard can capture them.
    qCInfo(log_syskey) << "Disabling GNOME system shortcuts (Wayland workaround)...";
    GnomeShortcutBlocker::instance().disableAll();

    // Strategy: Use X11 XGrabKeyboard to capture all keyboard input
    // This grabs the entire keyboard before it reaches the desktop environment
    // Note: On Wayland, XGrabKeyboard may not capture all system keys due to
    // compositor security restrictions. For full Wayland support, a different
    // approach would be needed (e.g., compositor-specific protocols).

    // Create X11 grabber
    qCInfo(log_syskey) << "Creating X11KeyGrabber...";
    m_x11Grabber = new X11KeyGrabber(this);

    // Install the grabber's native event filter on Qt's connection
    qCInfo(log_syskey) << "Installing native event filter...";
    QCoreApplication::instance()->installNativeEventFilter(m_x11Grabber);

    // Grab the entire keyboard
    qCInfo(log_syskey) << "Attempting initial keyboard grab...";
    if (!m_x11Grabber->grabKeyboard()) {
        qCWarning(log_syskey) << "Failed to grab keyboard on startup";
        // Restore shortcuts on failure
        GnomeShortcutBlocker::instance().restoreAll();
        return false;
    }

    qCInfo(log_syskey) << "Initial keyboard grab succeeded (X11 backend)";
    return true;
}

void SystemKeyBlocker::stopImpl()
{
    qCInfo(log_syskey) << "Stopping key capture";

    // Stop X11 grabber if active
    if (m_x11Grabber) {
        qCInfo(log_syskey) << "Stopping X11 grabber...";
        // 1) Ungrab keyboard FIRST
        if (m_x11Grabber) {
            m_x11Grabber->ungrabKeyboard();
        }

        // 2) Remove event filter
        if (m_x11Grabber) {
            QCoreApplication::instance()->removeNativeEventFilter(m_x11Grabber);
        }

        // 3) Delete the grabber (closes its Display connection)
        if (m_x11Grabber) {
            delete m_x11Grabber;
            m_x11Grabber = nullptr;
        }
    }

    // Restore GNOME system shortcuts (allow Super, Alt+Tab, Alt+Space etc. to control host normally)
    qCInfo(log_syskey) << "Restoring GNOME system shortcuts...";
    GnomeShortcutBlocker::instance().restoreAll();

    g_modifierState = ModifierKeyState{};
}
