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

#ifndef SHORTCUTMANAGER_H
#define SHORTCUTMANAGER_H

#include <QObject>
#include <QShortcut>
#include <QAction>
#include <QList>
#include <QMap>
#include <QKeySequence>

/**
 * @brief Unified manager for all application shortcuts.
 *
 * Provides centralized control for enabling/disabling all shortcuts in the application.
 * Uses QShortcut::setEnabled() for stable enable/disable control without losing signal connections.
 *
 * Architecture:
 * - All shortcuts are managed as QShortcut objects
 * - QAction shortcuts are converted to corresponding QShortcut objects
 * - Enable/disable is controlled via QShortcut::setEnabled() for stability
 * - Both standalone QShortcut and QAction shortcuts are stored in a single list
 *   and processed uniformly in enableAll()/disableAll()
 */
class ShortcutManager : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Get the singleton instance.
     * @return Reference to the global ShortcutManager instance.
     */
    static ShortcutManager& instance();

    /**
     * @brief Register a QShortcut for unified control.
     * @param shortcut Pointer to the shortcut to register.
     *
     * The shortcut will be included in enableAll()/disableAll() operations.
     * Passing nullptr is safe and will be ignored.
     */
    void registerShortcut(QShortcut *shortcut);

    /**
     * @brief Register a QAction for unified control.
     * @param action Pointer to the action to register.
     * @param parent Parent object for the created QShortcut (usually MainWindow).
     *
     * This creates a corresponding QShortcut for the action and manages it.
     * The action's menu item remains clickable even when shortcut is disabled.
     * Passing nullptr for action is safe and will be ignored.
     */
    void registerAction(QAction *action, QObject *parent);

    /**
     * @brief Enable all registered shortcuts and actions.
     */
    void enableAll();

    /**
     * @brief Disable all registered shortcuts and actions.
     */
    void disableAll();

    /**
     * @brief Toggle the enabled state of all shortcuts.
     */
    void toggleAll();

    /**
     * @brief Check if shortcuts are currently enabled.
     * @return true if shortcuts are enabled, false if disabled.
     */
    bool isEnabled() const;

    /**
     * @brief Get the total number of registered shortcuts (standalone + action shortcuts).
     * @return Total number of managed QShortcut objects.
     */
    int shortcutCount() const { return m_shortcuts.size(); }

signals:
    /**
     * @brief Emitted when the enabled state changes.
     * @param enabled The new enabled state.
     */
    void shortcutsStateChanged(bool enabled);

private:
    explicit ShortcutManager(QObject *parent = nullptr);
    ~ShortcutManager() override = default;

    // Prevent copying
    ShortcutManager(const ShortcutManager&) = delete;
    ShortcutManager& operator=(const ShortcutManager&) = delete;

    QList<QShortcut*> m_shortcuts;  // All managed shortcuts (standalone + action shortcuts)
    QMap<QAction*, QShortcut*> m_actionShortcuts;  // Mapping from QAction to its QShortcut (for deduplication)
    bool m_enabled = true;
};

#endif // SHORTCUTMANAGER_H
