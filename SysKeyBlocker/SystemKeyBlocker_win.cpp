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

#include <QWidget>
#include <windows.h>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QAbstractEventDispatcher>

Q_LOGGING_CATEGORY(log_syskey_win, "opf.systemkey.win")

/* ============================================================================
 *  Static self-pointer
 *
 *  WH_KEYBOARD_LL hook proc is a C-style callback — no `this`.  We store
 *  the current active instance so the hook can reach it.
 * ============================================================================ */
SystemKeyBlocker *SystemKeyBlocker::s_self = nullptr;

/* ============================================================================
 *  Modifier key state tracking
 *
 *  Since we swallow key events, GetAsyncKeyState won't work correctly for
 *  modifier keys that we've intercepted. We maintain our own state.
 * ============================================================================ */
struct ModifierKeyState {
    bool lShift = false;
    bool rShift = false;
    bool lCtrl = false;
    bool rCtrl = false;
    bool lAlt = false;
    bool rAlt = false;
    bool lWin = false;
    bool rWin = false;
};
static ModifierKeyState g_modifierState;

/* ============================================================================
 *  startImpl / stopImpl
 * ============================================================================ */

bool SystemKeyBlocker::startImpl(quintptr nativeParentHwnd)
{
    Q_ASSERT(!s_self || s_self == this);
    s_self = this;
    m_hookedHwnd = nativeParentHwnd;

    HINSTANCE hInst = GetModuleHandle(nullptr);
    HHOOK hook = SetWindowsHookEx(
        WH_KEYBOARD_LL,
        lowLevelKeyboardProc,
        hInst,
        0                    // hMod = 0 and dwThreadId = 0 → global hook
    );

    if (!hook) {
        DWORD err = GetLastError();
        qCCritical(log_syskey_win)
            << "SetWindowsHookEx(WH_KEYBOARD_LL) failed, error =" << err;
        s_self = nullptr;
        return false;
    }

    m_hHook = hook;
    qCInfo(log_syskey_win) << "Low-level keyboard hook installed (HWND:"
                           << nativeParentHwnd << ")";
    return true;
}

void SystemKeyBlocker::stopImpl()
{
    if (m_hHook) {
        UnhookWindowsHookEx(static_cast<HHOOK>(m_hHook));
        m_hHook = nullptr;
        qCInfo(log_syskey_win) << "Low-level keyboard hook removed";
    }
    s_self = nullptr;
}

/* ============================================================================
 *  Low-level keyboard hook callback
 *
 *  IMPORTANT — runs on a **system thread**, not the Qt event thread.
 *  - Must return within ~10 ms or the system will unhook us.
 *  - We do the absolute minimum here: classify the event, emit the signal
 *    (Qt marshals it across threads), and return.
 *  - Key-down events are swallowed (return 1).  Key-up events are passed
 *    through to avoid stuck keys in the OS.  Same strategy as VirtualBox.
 * ============================================================================ */

LRESULT CALLBACK SystemKeyBlocker::lowLevelKeyboardProc(
    int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode != HC_ACTION || !s_self || !s_self->m_active) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // Only swallow keys when focus is inside the designated focus target (VideoPane).
    // This allows dialogs (e.g. Preferences) to receive keyboard input even when
    // SystemBlocker is enabled.
    if (!s_self->m_focusTarget) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }
    HWND focusHwnd = GetFocus();
    HWND targetHwnd = reinterpret_cast<HWND>(s_self->m_focusTarget->winId());
    if (focusHwnd != targetHwnd && !IsChild(targetHwnd, focusHwnd)) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    const auto *kb = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
    const bool isKeyDown   = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool isExtended  = (kb->flags & LLKHF_EXTENDED) != 0;
    const bool wasInjected = (kb->flags & LLKHF_INJECTED)  != 0;
    const quint32 vk       = kb->vkCode;

    // Let injected events pass through — they were generated by another
    // application (e.g. on-screen keyboard, automation tool), not by
    // physical keystrokes the user intends for the target.
    if (wasInjected) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // ---- Update modifier state tracking ----
    switch (vk) {
        case VK_LSHIFT:    g_modifierState.lShift = isKeyDown; break;
        case VK_RSHIFT:    g_modifierState.rShift = isKeyDown; break;
        case VK_LCONTROL:  g_modifierState.lCtrl = isKeyDown;  break;
        case VK_RCONTROL:  g_modifierState.rCtrl = isKeyDown;  break;
        case VK_LMENU:     g_modifierState.lAlt = isKeyDown;   break;
        case VK_RMENU:     g_modifierState.rAlt = isKeyDown;   break;
        case VK_LWIN:      g_modifierState.lWin = isKeyDown;   break;
        case VK_RWIN:      g_modifierState.rWin = isKeyDown;   break;
    }

    // ---- Build modifier mask from our tracked state ----
    int modifiers = 0;
    if (g_modifierState.lShift || g_modifierState.rShift) modifiers |= Qt::ShiftModifier;
    if (g_modifierState.lCtrl || g_modifierState.rCtrl)   modifiers |= Qt::ControlModifier;
    if (g_modifierState.lAlt || g_modifierState.rAlt)     modifiers |= Qt::AltModifier;
    if (g_modifierState.lWin || g_modifierState.rWin)     modifiers |= Qt::MetaModifier;

    // ---- Translate to Qt key code ----
    const int qtKey = s_self->nativeToQtKey(vk, isExtended);

    // ---- Emit signal (cross-thread dispatch by Qt) ----
    emit s_self->keyCaptured(qtKey, modifiers, isKeyDown, vk);

    // ---- Conditional swallow based on m_swallowEnabled ----
    // When swallowEnabled (Blocker ON): swallow ALL key events (both down and up).
    //   The local OS won't see any keystrokes — everything goes to the target.
    // When !swallowEnabled (Blocker OFF): pass through to local OS.
    //   Keys are STILL forwarded to the target via the keyCaptured signal above,
    //   but the local OS also processes them normally.
    if (s_self->m_swallowEnabled) {
        return 1;  // swallow — local OS won't see the key
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);  // pass through
}

