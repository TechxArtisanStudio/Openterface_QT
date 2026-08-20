#include "logcategoryregistry.h"
#include <QMutexLocker>
#include <algorithm>

LogCategoryRegistry& LogCategoryRegistry::instance()
{
    static LogCategoryRegistry instance;
    return instance;
}

void LogCategoryRegistry::registerCategory(const QString& name)
{
    QMutexLocker locker(&m_mutex);
    if (!m_categories.contains(name)) {
        m_categories.append(name);
        m_categories.sort();
    }
}

QStringList LogCategoryRegistry::allCategories() const
{
    QMutexLocker locker(&m_mutex);
    return m_categories;
}

QStringList LogCategoryRegistry::categoriesByGroup(const QString& group) const
{
    QMutexLocker locker(&m_mutex);
    QStringList result;
    for (const QString& cat : m_categories) {
        if (cat == group || cat.startsWith(group + ".")) {
            result.append(cat);
        }
    }
    return result;
}
