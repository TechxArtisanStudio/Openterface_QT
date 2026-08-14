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

#include "preferencepagebase.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDialog>

PreferencePageBase::PreferencePageBase(QWidget *parent)
    : QWidget(parent)
{
}

void PreferencePageBase::createButtonBar(QVBoxLayout *parentLayout)
{
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_applyButton = new QPushButton(tr("Apply"), this);
    m_revertButton = new QPushButton(tr("Revert"), this);
    m_cancelButton = new QPushButton(tr("Cancel"), this);

    m_applyButton->setObjectName("applyButton");
    m_revertButton->setObjectName("revertButton");
    m_cancelButton->setObjectName("cancelButton");

    m_applyButton->setFixedSize(80, 30);
    m_revertButton->setFixedSize(80, 30);
    m_cancelButton->setFixedSize(80, 30);

    buttonLayout->addWidget(m_applyButton);
    buttonLayout->addWidget(m_revertButton);
    buttonLayout->addWidget(m_cancelButton);

    parentLayout->addLayout(buttonLayout);

    // Connect Apply: apply settings, capture new snapshot, clear dirty
    connect(m_applyButton, &QPushButton::clicked, this, [this]() {
        applySettings();
        captureSnapshot();
        clearDirty();
    });

    // Connect Revert: restore from snapshot, mark dirty so user sees Apply is orange
    connect(m_revertButton, &QPushButton::clicked, this, [this]() {
        revertToSnapshot();
        markDirty();
    });

    // Connect Cancel: close the dialog
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        QDialog *dlg = qobject_cast<QDialog*>(window());
        if (dlg) dlg->reject();
    });

    updateButtonStyles();
}

void PreferencePageBase::markDirty()
{
    if (!m_isDirty) {
        m_isDirty = true;
        updateButtonStyles();
        emit dirtyChanged(true);
    }
}

void PreferencePageBase::clearDirty()
{
    if (m_isDirty) {
        m_isDirty = false;
        updateButtonStyles();
        emit dirtyChanged(false);
    }
}

void PreferencePageBase::updateButtonStyles()
{
    if (m_applyButton) {
        if (m_isDirty) {
            m_applyButton->setStyleSheet(dirtyApplyButtonStyle());
        } else {
            m_applyButton->setStyleSheet(defaultButtonStyle());
        }
    }
    if (m_revertButton) {
        m_revertButton->setStyleSheet(defaultButtonStyle());
    }
    if (m_cancelButton) {
        m_cancelButton->setStyleSheet(defaultButtonStyle());
    }
}

QString PreferencePageBase::defaultButtonStyle()
{
    return QStringLiteral(
        "QPushButton {"
        "  background-color: #ffffff;"
        "  border: 1px solid #cccccc;"
        "  border-radius: 4px;"
        "  padding: 4px 16px;"
        ""
        ""
        "  color: #333333;"
        "}"
        "QPushButton:hover {"
        "  background-color: #f0f0f0;"
        "  border-color: #999999;"
        "}"
        "QPushButton:pressed {"
        "  background-color: #e0e0e0;"
        "}"
    );
}

QString PreferencePageBase::dirtyApplyButtonStyle()
{
    return QStringLiteral(
        "QPushButton#applyButton {"
        "  background-color: #ff8c00;"
        "  border: 1px solid #e07000;"
        "  color: white;"
        "  font-weight: bold;"
        "}"
        "QPushButton#applyButton:hover {"
        "  background-color: #ff9920;"
        "}"
        "QPushButton#applyButton:pressed {"
        "  background-color: #e07000;"
        "}"
    );
}
