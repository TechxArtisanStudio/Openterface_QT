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

#include "WebSearchManager.h"
#include "WebSearchProviders.h"
#include "../ui/globalsetting.h"
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(log_ai_chat)

WebSearchManager::WebSearchManager(QObject *parent)
    : QObject(parent)
{
    initializeProviders();
}

WebSearchManager::~WebSearchManager()
{
    qDeleteAll(m_providers);
    m_providers.clear();
}

WebSearchManager &WebSearchManager::instance()
{
    static WebSearchManager instance;
    return instance;
}

void WebSearchManager::initializeProviders()
{
    // Create all available providers
    m_providers.append(new DuckDuckGoProvider(this));
    m_providers.append(new WikipediaProvider(this));
    m_providers.append(new ExaProvider(this));
    m_providers.append(new ParallelProvider(this));
}

QList<WebSearchProvider*> WebSearchManager::availableProviders() const
{
    return m_providers;
}

WebSearchProvider* WebSearchManager::providerById(const QString &id) const
{
    for (auto *provider : m_providers) {
        if (provider->id() == id) {
            return provider;
        }
    }
    return nullptr;
}

QStringList WebSearchManager::configuredProviderIds() const
{
    // Read from GlobalSetting
    QStringList ids = GlobalSetting::instance().getChatWebSearchProviders();

    // If not configured, return default order
    if (ids.isEmpty()) {
        // Default: Exa (best quality with full content), then Parallel, then DuckDuckGo, then Wikipedia
        return QStringList{"exa", "parallel", "duckduckgo", "wikipedia"};
    }

    return ids;
}

void WebSearchManager::setConfiguredProviderIds(const QStringList &ids)
{
    GlobalSetting::instance().setChatWebSearchProviders(ids);
    m_configuredIds = ids;  // Update cache
    emit providersChanged();
}

QString WebSearchManager::search(const QString &query) const
{
    if (query.isEmpty()) {
        return "web_search: missing query argument";
    }

    QStringList providerIds = configuredProviderIds();
    QString lastError;

    // Try each configured provider in order
    for (const QString &id : providerIds) {
        WebSearchProvider *provider = providerById(id);
        if (!provider) {
            qCWarning(log_ai_chat) << "WebSearchManager: unknown provider:" << id;
            continue;
        }

        // Skip unconfigured providers (missing API key)
        if (!provider->isConfigured()) {
            continue;
        }

        QString result = provider->search(query);

        // If we see an SSL error, return it immediately - no point trying other providers
        if (result.contains("SSL/TLS error")) {
            qCWarning(log_ai_chat) << "WebSearchManager: SSL error from" << provider->name();
            return result;
        }

        // Check if search was successful (not an error)
        if (!result.startsWith("web_search: no results found") &&
            !result.startsWith("web_search: failed") &&
            !result.startsWith("web_search: error") &&
            !result.contains("API error") &&
            !result.contains("not configured")) {
            qCDebug(log_ai_chat) << "WebSearchManager: success with" << provider->name();
            // Prepend source info to result
            return QString("[%1]\n%2").arg(provider->name(), result);
        }

        lastError = result;
    }

    // All providers failed
    qCWarning(log_ai_chat) << "WebSearchManager: all providers failed for query:" << query;

    // If we have a specific error (not generic "no results"), return it
    if (!lastError.isEmpty() && !lastError.startsWith("web_search: no results found")) {
        return lastError;
    }

    return "web_search: no results found (all providers failed)";
}

bool WebSearchManager::hasConfiguredProvider() const
{
    QStringList providerIds = configuredProviderIds();

    for (const QString &id : providerIds) {
        WebSearchProvider *provider = providerById(id);
        if (provider && provider->isConfigured()) {
            return true;
        }
    }

    return false;
}
