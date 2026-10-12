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

#ifndef WEB_SEARCH_MANAGER_H
#define WEB_SEARCH_MANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>

class WebSearchProvider;

/**
 * @brief Manages web search providers and executes searches with fallback chain.
 *
 * Singleton that:
 *   - Maintains registry of available search providers
 *   - Reads provider configuration from GlobalSetting
 *   - Executes searches through configured providers in priority order
 *   - Falls back to next provider if current one fails
 */
class WebSearchManager : public QObject
{
    Q_OBJECT

public:
    static WebSearchManager &instance();

    /// Get list of all available providers
    QList<WebSearchProvider*> availableProviders() const;

    /// Get provider by ID (returns nullptr if not found)
    WebSearchProvider* providerById(const QString &id) const;

    /// Get the ordered list of configured provider IDs
    QStringList configuredProviderIds() const;

    /// Set the ordered list of configured provider IDs
    void setConfiguredProviderIds(const QStringList &ids);

    /// Execute a web search using the configured fallback chain
    /// Returns results from the first successful provider
    QString search(const QString &query) const;

    /// Check if any provider is configured and ready to use
    bool hasConfiguredProvider() const;

signals:
    /// Emitted when a search produces a log message
    void logMessage(const QString &message);

    /// Emitted when provider configuration changes
    void providersChanged();

private:
    explicit WebSearchManager(QObject *parent = nullptr);
    ~WebSearchManager();

    void initializeProviders();

    QList<WebSearchProvider*> m_providers;  // All available providers
    mutable QStringList m_configuredIds;     // Cached configured provider IDs
};

#endif // WEB_SEARCH_MANAGER_H
