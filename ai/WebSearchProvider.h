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

#ifndef WEB_SEARCH_PROVIDER_H
#define WEB_SEARCH_PROVIDER_H

#include <QObject>
#include <QString>

/**
 * @brief Abstract base class for web search providers.
 *
 * Subclasses implement specific search APIs (DuckDuckGo, Wikipedia, Exa, etc.)
 * and are managed by WebSearchManager which handles fallback chains.
 */
class WebSearchProvider : public QObject
{
    Q_OBJECT

public:
    explicit WebSearchProvider(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~WebSearchProvider() = default;

    /// Human-readable provider name (e.g., "DuckDuckGo", "Exa AI")
    virtual QString name() const = 0;

    /// Unique identifier for settings/storage (e.g., "duckduckgo", "exa")
    virtual QString id() const = 0;

    /// Whether this provider requires an API key to function
    virtual bool requiresApiKey() const = 0;

    /// Check if provider is properly configured (API key set if required)
    virtual bool isConfigured() const = 0;

    /// Perform a web search and return results as formatted text
    /// Returns error message starting with "web_search:" on failure
    virtual QString search(const QString &query) const = 0;

    /// Short description for UI display
    virtual QString description() const = 0;

signals:
    /// Emitted when a provider produces a log message
    void logMessage(const QString &message);
};

#endif // WEB_SEARCH_PROVIDER_H
