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

#ifndef MENUCOORDINATOR_H
#define MENUCOORDINATOR_H

#include <QObject>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QString>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_ui_menucoordinator)

class LanguageManager;
class SerialPortManager;
class QWidget;

/**
 * @brief Coordinates menu management for the application
 * 
 * This class handles all menu-related operations including:
 * - Language menu setup and switching
 * - Baudrate menu management and selection
 * - Menu action groups and state management
 * - User notifications for menu changes
 */
class MenuCoordinator : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Construct a new Menu Coordinator
     * @param languageMenu Pointer to the language menu
     * @param baudrateMenu Pointer to the baudrate menu
     * @param languageManager Pointer to language manager
     * @param parentWidget Parent widget for message boxes
     * @param parent Parent QObject
     */
    explicit MenuCoordinator(QMenu *languageMenu,
                            QMenu *baudrateMenu,
                            LanguageManager *languageManager,
                            QWidget *parentWidget,
                            QObject *parent = nullptr);
    
    /**
     * @brief Destructor
     */
    ~MenuCoordinator();

    /**
     * @brief Initialize language menu with available languages
     * Sets up the menu structure and marks current language
     */
    void setupLanguageMenu();
    
    /**
     * @brief Update baudrate menu to reflect current baudrate
     * @param baudrate The baudrate to mark as selected (0 to clear selection)
     */
    void updateBaudrateMenu(int baudrate);
    
    /**
     * @brief Show ARM baudrate performance recommendation dialog
     * @param currentBaudrate Current baudrate setting
     * 
     * Recommends 9600 for better performance on ARM, or offers 115200 for faster communication
     */
    void showArmBaudratePerformanceRecommendation(int currentBaudrate);

signals:
    /**
     * @brief Emitted when language is changed through menu
     * @param language The new language code (e.g., "en", "fr")
     */
    void languageChanged(const QString &language);
    
    /**
     * @brief Emitted when baudrate is changed through menu
     * @param baudrate The new baudrate value
     */
    void baudrateChanged(int baudrate);

private slots:
    /**
     * @brief Handle language selection from menu
     * @param action The menu action that was triggered
     */
    void onLanguageSelected(QAction *action);
    
    /**
     * @brief Handle baudrate selection from menu
     * @param action The menu action that was triggered
     */
    void onBaudrateMenuTriggered(QAction *action);

private:
    // Member variables
    QMenu *m_languageMenu;              ///< Pointer to language menu (not owned)
    QMenu *m_baudrateMenu;              ///< Pointer to baudrate menu (not owned)
    LanguageManager *m_languageManager; ///< Pointer to language manager (not owned)
    QWidget *m_parentWidget;            ///< Parent widget for dialogs (not owned)
    QActionGroup *m_languageGroup;      ///< Action group for language menu
    
    /**
     * @brief Show message box about baudrate change requiring device reconnection
     * @param baudrate The new baudrate value
     */
    void showBaudrateChangeMessage(int baudrate);
};

#endif // MENUCOORDINATOR_H
