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

#include "ShortcutManager.h"
#include "globalsetting.h"
#include <QApplication>
#include <QDebug>

ShortcutManager& ShortcutManager::instance()
{
    static ShortcutManager instance;
    return instance;
}

ShortcutManager::ShortcutManager(QObject *parent)
    : QObject(parent)
{
    // Initialize enabled state from GlobalSetting
    m_enabled = GlobalSetting::instance().getShortcutsEnabled();
}

void ShortcutManager::registerShortcut(QShortcut *shortcut)
{
    if (!shortcut) {
        return;
    }

    // Avoid duplicate registration
    if (!m_shortcuts.contains(shortcut)) {
        m_shortcuts.append(shortcut);
        // Apply current enabled state
        if (!m_enabled) {
            shortcut->setEnabled(false);
        }
        qDebug() << "ShortcutManager: Registered QShortcut, key:" << shortcut->key().toString()
                 << "| context:" << shortcut->context()
                 << "| enabled:" << shortcut->isEnabled()
                 << "| manager state:" << m_enabled;
    }
}

void ShortcutManager::registerAction(QAction *action, QObject *parent)
{
    if (!action) {
        return;
    }

    // Avoid duplicate registration
    if (m_actionShortcuts.contains(action)) {
        return;
    }

    // Get the original shortcut from the action
    QKeySequence originalShortcut = action->shortcut();
    if (originalShortcut.isEmpty()) {
        qDebug() << "ShortcutManager: Action" << action->text() << "has no shortcut, skipping";
        return;
    }

    // Store original text for menu display
    QString originalText = action->text();

    // Create a QShortcut for this action
    QShortcut *shortcut = new QShortcut(originalShortcut, parent);

    // Set application-wide context so shortcut works regardless of focus
    shortcut->setContext(Qt::ApplicationShortcut);

    // Connect the shortcut's activated signal to the action's trigger
    connect(shortcut, &QShortcut::activated, action, &QAction::trigger);

    // Store the mapping
    m_actionShortcuts[action] = shortcut;
    m_shortcuts.append(shortcut);

    // Apply current enabled state
    if (!m_enabled) {
        shortcut->setEnabled(false);
    }

    // Remove QAction's shortcut to avoid conflicts with QShortcut
    // But keep the shortcut text in menu by appending it to the action text
    action->setShortcut(QKeySequence());
    action->setText(originalText + "\t" + originalShortcut.toString());

    qDebug() << "ShortcutManager: Registered action" << action->text()
             << "| key:" << originalShortcut.toString()
             << "| context:" << shortcut->context()
             << "| enabled:" << shortcut->isEnabled()
             << "| manager state:" << m_enabled;
}

void ShortcutManager::enableAll()
{
    if (m_enabled) {
        return; // Already enabled
    }

    m_enabled = true;

    for (QShortcut *shortcut : m_shortcuts) {
        if (shortcut) {
            shortcut->setEnabled(true);
        } else {
            qWarning() << "ShortcutManager::enableAll(): nullptr in m_shortcuts";
        }
    }

    qInfo() << "ShortcutManager: enabled" << m_shortcuts.size() << "shortcuts";
    emit shortcutsStateChanged(true);
}

void ShortcutManager::disableAll()
{
    if (!m_enabled) {
        return; // Already disabled
    }

    m_enabled = false;

    for (QShortcut *shortcut : m_shortcuts) {
        if (shortcut) {
            shortcut->setEnabled(false);
        } else {
            qWarning() << "ShortcutManager::disableAll(): nullptr in m_shortcuts";
        }
    }

    qInfo() << "ShortcutManager: disabled" << m_shortcuts.size() << "shortcuts";
    emit shortcutsStateChanged(false);
}

void ShortcutManager::toggleAll()
{
    if (m_enabled) {
        disableAll();
    } else {
        enableAll();
    }
}

bool ShortcutManager::isEnabled() const
{
    return m_enabled;
}
