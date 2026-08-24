/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
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

#ifndef CHAT_SKILL_MANAGER_H
#define CHAT_SKILL_MANAGER_H

#include <QObject>
#include <QList>
#include "ChatTypes.h"

/**
 * @brief Loads and manages AI chat skills from JSON files.
 *
 * Ported from MacOS SkillManager.swift.
 * Skills are stored as JSON files in the user-accessible Skills folder.
 */
class ChatSkillManager : public QObject
{
    Q_OBJECT

public:
    static ChatSkillManager &instance();

    /// All currently loaded skills in display order
    QList<ChatSkill> skills() const { return m_skills; }

    /// Get the folder where skill JSON files live
    static QString skillsFolder();

    /// Re-read skills from disk
    void reload();

    /// Find a skill by ID
    ChatSkill skillById(const QString &id) const;

signals:
    /// Emitted when skills are reloaded
    void skillsChanged();

private:
    explicit ChatSkillManager(QObject *parent = nullptr);

    void seedAndLoad();
    QList<ChatSkill> loadFromFolder(const QString &folderPath) const;
    QList<ChatSkill> builtInSkills() const;

    QList<ChatSkill> m_skills;
};

#endif // CHAT_SKILL_MANAGER_H
