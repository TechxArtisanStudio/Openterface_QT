/*
 * ========================================================================== *
 *                                                                            *
 *    This file is part of the Openterface Mini KVM App QT version            *
 *                                                                            *
 *    Copyright (C) 2024   <info@openterface.com>                             *
 *                                                                            *
 *    This program is free software: you can redistribute it and/or modify    *
 *    it under the terms of the GNU General Public License version 3.         *
 *                                                                            *
 * ========================================================================== *
 */

#ifndef PREFERENCEPAGEBASE_H
#define PREFERENCEPAGEBASE_H

#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>

/**
 * @brief Base class for preference pages that have Apply/Revert/Cancel buttons.
 *
 * Provides common button creation, styling (white with shadow, orange when dirty),
 * and dirty-state tracking. Subclasses implement captureSnapshot(), applySettings(),
 * and revertToSnapshot().
 */
class PreferencePageBase : public QWidget
{
    Q_OBJECT

public:
    explicit PreferencePageBase(QWidget *parent = nullptr);

    /// Save current UI state to snapshot (for revert)
    virtual void captureSnapshot() = 0;
    /// Apply current UI state to persistent settings
    virtual void applySettings() = 0;
    /// Restore UI state from snapshot
    virtual void revertToSnapshot() = 0;

    bool isDirty() const { return m_isDirty; }
    void clearDirty();

signals:
    void dirtyChanged(bool dirty);

protected:
    QPushButton *m_applyButton = nullptr;
    QPushButton *m_revertButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    /// Create Apply/Revert/Cancel button bar and add to the given layout.
    /// Call this at the end of setupUI().
    void createButtonBar(QVBoxLayout *parentLayout);

    /// Mark the page as dirty (modified since last snapshot).
    void markDirty();


    /// Update button styles based on dirty state.
    void updateButtonStyles();

private:
    bool m_isDirty = false;

    static QString defaultButtonStyle();
    static QString dirtyApplyButtonStyle();
};

#endif // PREFERENCEPAGEBASE_H
