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

#include "menucoordinator.h"
#include "ui/languagemanager.h"
#include "serial/SerialPortManager.h"
#include <QMessageBox>
#include <QPushButton>
#include <QDebug>

Q_LOGGING_CATEGORY(log_ui_menucoordinator, "opf.ui.menucoordinator")

MenuCoordinator::MenuCoordinator(QMenu *languageMenu,
                                 QMenu *baudrateMenu,
                                 LanguageManager *languageManager,
                                 QWidget *parentWidget,
                                 QObject *parent)
    : QObject(parent)
    , m_languageMenu(languageMenu)
    , m_baudrateMenu(baudrateMenu)
    , m_languageManager(languageManager)
    , m_parentWidget(parentWidget)
    , m_languageGroup(nullptr)
{
    qCDebug(log_ui_menucoordinator) << "MenuCoordinator created";
}

MenuCoordinator::~MenuCoordinator()
{
    qCDebug(log_ui_menucoordinator) << "MenuCoordinator destroyed";
    
    // Clean up action group (actions are owned by menu and will be cleaned up there)
    if (m_languageGroup) {
        delete m_languageGroup;
        m_languageGroup = nullptr;
    }
}

void MenuCoordinator::setupLanguageMenu()
{
    if (!m_languageMenu || !m_languageManager) {
        qCWarning(log_ui_menucoordinator) << "Language menu or manager not initialized";
        return;
    }
    
    qCDebug(log_ui_menucoordinator) << "Setting up language menu";
    
    // Clear existing language actions
    m_languageMenu->clear();
    
    // Get available languages
    QStringList languages = m_languageManager->availableLanguages();
    for (const QString &lang : languages) {
        qCDebug(log_ui_menucoordinator) << "Available language:" << lang;
    }
    
    // Fallback list if no languages are found
    if (languages.isEmpty()) {
        languages << "en" << "fr" << "de" << "da" << "ja" << "se";
        qCDebug(log_ui_menucoordinator) << "Using fallback language list";
    }
    
    // Create action group for exclusive selection
    if (m_languageGroup) {
        delete m_languageGroup;
    }
    m_languageGroup = new QActionGroup(this);
    m_languageGroup->setExclusive(true);
    
    // Language display names
    QMap<QString, QString> languageNames = {
        {"en", "English"},
        {"fr", "Français"},
        {"de", "German"},
        {"da", "Danish"},
        {"ja", "Japanese"},
        {"se", "Swedish"},
        {"zh", "中文"}
    };
    
    // Create menu actions for each language
    QString currentLanguage = m_languageManager->currentLanguage();
    for (const QString &lang : languages) {
        QString displayName = languageNames.value(lang, lang);
        QAction *action = new QAction(displayName, this);
        action->setCheckable(true);
        action->setData(lang);
        
        // Mark current language as checked
        if (lang == currentLanguage) {
            action->setChecked(true);
            qCDebug(log_ui_menucoordinator) << "Marked current language:" << lang;
        }
        
        m_languageMenu->addAction(action);
        m_languageGroup->addAction(action);
    }
    
    // Connect to language selection
    connect(m_languageGroup, &QActionGroup::triggered, this, &MenuCoordinator::onLanguageSelected);
    
    qCDebug(log_ui_menucoordinator) << "Language menu setup complete with" << languages.size() << "languages";
}

void MenuCoordinator::updateBaudrateMenu(int baudrate)
{
    if (!m_baudrateMenu) {
        qCWarning(log_ui_menucoordinator) << "Baudrate menu not initialized";
        return;
    }
    
    qCDebug(log_ui_menucoordinator) << "Updating baudrate menu, target baudrate:" << baudrate;
    
    QList<QAction*> actions = m_baudrateMenu->actions();
    for (QAction* action : actions) {
        if (baudrate == 0) {
            // Clear all selections
            action->setChecked(false);
        } else {
            // Check if this action matches the target baudrate
            bool ok;
            int actionBaudrate = action->text().toInt(&ok);
            if (ok && actionBaudrate == baudrate) {
                action->setChecked(true);
                qCDebug(log_ui_menucoordinator) << "Checked baudrate:" << baudrate;
            } else {
                action->setChecked(false);
            }
        }
    }
}

void MenuCoordinator::showArmBaudratePerformanceRecommendation(int currentBaudrate)
{
    if (!m_parentWidget) {
        qCWarning(log_ui_menucoordinator) << "No parent widget for dialog";
        return;
    }
    
    // Get the actual current baudrate from SerialPortManager
    int actualCurrentBaudrate = SerialPortManager::getInstance().getCurrentBaudrate();
    
    qCDebug(log_ui_menucoordinator) << "Showing ARM baudrate recommendation, current:" 
                                    << actualCurrentBaudrate;
    
    QMessageBox msgBox(m_parentWidget);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setWindowTitle("Performance Recommendation");
    
    QPushButton* actionButton = nullptr;
    QPushButton* stayButton = nullptr;
    
    if (actualCurrentBaudrate == 9600) {
        // Offer to switch to higher baudrate
        msgBox.setText("You are running on an ARM architecture with 9600 baudrate.\n\n"
                      "You can switch to 115200 baudrate for potentially faster communication, "
                      "but it may use more CPU resources.");
        actionButton = msgBox.addButton("Switch to 115200", QMessageBox::AcceptRole);
        stayButton = msgBox.addButton("Stay in 9600", QMessageBox::RejectRole);
    } else {
        // Recommend lower baudrate for better performance
        msgBox.setText(QString("You are running on an ARM architecture with %1 baudrate.\n\n"
                              "For better performance and lower CPU usage, we recommend using 9600 baudrate instead.")
                              .arg(actualCurrentBaudrate));
        actionButton = msgBox.addButton("Switch to 9600", QMessageBox::AcceptRole);
        stayButton = msgBox.addButton(QString("Stay in %1").arg(actualCurrentBaudrate), 
                                     QMessageBox::RejectRole);
    }
    
    msgBox.setDefaultButton(stayButton);
    msgBox.exec();
    
    if (msgBox.clickedButton() == actionButton) {
        int targetBaudrate = (actualCurrentBaudrate == 9600) ? 115200 : 9600;
        qCInfo(log_ui_menucoordinator) << "User accepted baudrate recommendation, switching to:" 
                                       << targetBaudrate;
        
        SerialPortManager::getInstance().setUserSelectedBaudrate(targetBaudrate);
        showBaudrateChangeMessage(targetBaudrate);
        emit baudrateChanged(targetBaudrate);
    } else {
        qCDebug(log_ui_menucoordinator) << "User declined baudrate recommendation";
    }
}

void MenuCoordinator::onLanguageSelected(QAction *action)
{
    QString language = action->data().toString();
    qCDebug(log_ui_menucoordinator) << "Language selected from menu:" << language;
    
    if (m_languageManager) {
        m_languageManager->switchLanguage(language);
        emit languageChanged(language);
    }
}

void MenuCoordinator::onBaudrateMenuTriggered(QAction *action)
{
    bool ok;
    int baudrate = action->text().toInt(&ok);
    
    if (!ok) {
        qCWarning(log_ui_menucoordinator) << "Invalid baudrate selected from menu:" << action->text();
        return;
    }
    
    qCDebug(log_ui_menucoordinator) << "User selected baudrate from menu:" << baudrate;
    
    // Handle factory reset for 9600 baudrate
    if (baudrate == 9600) {
        SerialPortManager::getInstance().factoryResetHipChip();
    } else {
        SerialPortManager::getInstance().setUserSelectedBaudrate(baudrate);
    }
    
    // Show message about device reconnection
    showBaudrateChangeMessage(baudrate);
    
    emit baudrateChanged(baudrate);
}

void MenuCoordinator::showBaudrateChangeMessage(int baudrate)
{
    if (!m_parentWidget) {
        qCWarning(log_ui_menucoordinator) << "No parent widget for message box";
        return;
    }
    
    QMessageBox msgBox(m_parentWidget);
    msgBox.setIcon(QMessageBox::Information);
    msgBox.setWindowTitle("Baudrate Changed");
    msgBox.setText(QString("Baudrate has been changed to %1.\n\n"
                          "Please unplug and replug the device to make the new baudrate setting effective.")
                          .arg(baudrate));
    msgBox.addButton(QMessageBox::Ok);
    msgBox.exec();
}
