#ifndef LOGCATEGORYREGISTRY_H
#define LOGCATEGORYREGISTRY_H

#include <QString>
#include <QStringList>
#include <QMutex>

class LogCategoryRegistry
{
public:
    static LogCategoryRegistry& instance();

    void registerCategory(const QString& name);
    QStringList allCategories() const;
    QStringList categoriesByGroup(const QString& group) const;

private:
    LogCategoryRegistry() = default;
    QStringList m_categories;
    mutable QMutex m_mutex;
};

#endif // LOGCATEGORYREGISTRY_H
