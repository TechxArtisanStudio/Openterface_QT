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

#ifndef WEB_SEARCH_PROVIDERS_H
#define WEB_SEARCH_PROVIDERS_H

#include "WebSearchProvider.h"

/**
 * @brief DuckDuckGo Instant Answer API provider (free, no API key required)
 */
class DuckDuckGoProvider : public WebSearchProvider
{
    Q_OBJECT
public:
    explicit DuckDuckGoProvider(QObject *parent = nullptr) : WebSearchProvider(parent) {}

    QString name() const override { return "DuckDuckGo"; }
    QString id() const override { return "duckduckgo"; }
    bool requiresApiKey() const override { return false; }
    bool isConfigured() const override { return true; }
    QString description() const override { return "Free search API for well-known topics"; }
    QString search(const QString &query) const override;
};

/**
 * @brief Wikipedia API provider (free, no API key required)
 */
class WikipediaProvider : public WebSearchProvider
{
    Q_OBJECT
public:
    explicit WikipediaProvider(QObject *parent = nullptr) : WebSearchProvider(parent) {}

    QString name() const override { return "Wikipedia"; }
    QString id() const override { return "wikipedia"; }
    bool requiresApiKey() const override { return false; }
    bool isConfigured() const override { return true; }
    QString description() const override { return "Wikipedia article summaries"; }
    QString search(const QString &query) const override;
};

/**
 * @brief Exa AI search provider via MCP (free anonymous access, or API key for paid tier)
 * Uses MCP (Model Context Protocol) endpoint at https://mcp.exa.ai/mcp
 * Works without API key for anonymous access with rate limiting.
 * Returns high-quality results with full content and highlights.
 */
class ExaProvider : public WebSearchProvider
{
    Q_OBJECT
public:
    explicit ExaProvider(QObject *parent = nullptr) : WebSearchProvider(parent) {}

    QString name() const override { return "Exa AI"; }
    QString id() const override { return "exa"; }
    bool requiresApiKey() const override { return false; }  // Free anonymous access available
    bool isConfigured() const override { return true; }  // Always available (anonymous or with key)
    QString description() const override { return "AI-optimized semantic search via MCP (free anonymous access)"; }
    QString search(const QString &query) const override;
};

/**
 * @brief Parallel AI search provider via MCP (free anonymous access, or API key for paid tier)
 * Uses MCP (Model Context Protocol) endpoint at https://search.parallel.ai/mcp
 * Works without API key for anonymous access with rate limiting.
 */
class ParallelProvider : public WebSearchProvider
{
    Q_OBJECT
public:
    explicit ParallelProvider(QObject *parent = nullptr) : WebSearchProvider(parent) {}

    QString name() const override { return "Parallel"; }
    QString id() const override { return "parallel"; }
    bool requiresApiKey() const override { return false; }  // Free anonymous access available
    bool isConfigured() const override { return true; }  // Always available (anonymous or with key)
    QString description() const override { return "AI-optimized web search via MCP (free anonymous access)"; }
    QString search(const QString &query) const override;
};

#endif // WEB_SEARCH_PROVIDERS_H
