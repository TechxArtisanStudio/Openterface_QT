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
#include <QDebug>
#include <QGraphicsDropShadowEffect>

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

<<<<<<< HEAD
    // Add subtle drop shadow to each button for depth and visibility.
    // Especially useful on Linux where buttons can otherwise blend into the panel.
    auto addShadow = [](QPushButton* btn) {
        auto* effect = new QGraphicsDropShadowEffect(btn);
        effect->setBlurRadius(8);
        effect->setOffset(0, 2);
        effect->setColor(QColor(0, 0, 0, 60)); // ~24% opacity black
        btn->setGraphicsEffect(effect);
    };
    addShadow(m_applyButton);
    addShadow(m_revertButton);
    addShadow(m_cancelButton);

=======
>>>>>>> 1b08b3c (fix(SystemKeyBlocker): include QWidget header for proper functionality (#577))
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
        checkDirtyState();
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

void PreferencePageBase::checkDirtyState()
{
    bool matches = valuesMatchSnapshot();
    if (matches && m_isDirty) {
        clearDirty();
    } else if (!matches && !m_isDirty) {
        markDirty();
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
<<<<<<< HEAD
    // Flat palette-aware style consistent with the global QPushButton style
    // in main.cpp. The buttons also have a QGraphicsDropShadowEffect applied
    // per-widget in createButtonBar() for extra visual separation.
    return QStringLiteral(
        "QPushButton {"
        "  background-color: palette(button);"
        "  border: 1px solid palette(dark);"
        "  border-radius: 6px;"
        "  padding: 4px 16px;"
        "  color: palette(buttonText);"
        "}"
        "QPushButton:hover { background-color: palette(midlight); }"
        "QPushButton:pressed { background-color: palette(mid); }"
        "QPushButton:disabled {"
        "  color: palette(mid);"
        "  background-color: palette(button);"
        "  border: 1px solid palette(dark);"
=======
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
>>>>>>> 1b08b3c (fix(SystemKeyBlocker): include QWidget header for proper functionality (#577))
        "}"
    );
}

QString PreferencePageBase::dirtyApplyButtonStyle()
{
<<<<<<< HEAD
    // Canonical brand orange (consistent with firmwarepage and other prominent CTAs).
    // White text on orange guarantees readability in both light and dark themes.
    return QStringLiteral(
        "QPushButton#applyButton {"
        "  background-color: #e8841a;"
        "  border: 1px solid #c46e14;"
=======
    return QStringLiteral(
        "QPushButton#applyButton {"
        "  background-color: #ff8c00;"
        "  border: 1px solid #e07000;"
>>>>>>> 1b08b3c (fix(SystemKeyBlocker): include QWidget header for proper functionality (#577))
        "  color: white;"
        "  font-weight: bold;"
        "}"
        "QPushButton#applyButton:hover {"
<<<<<<< HEAD
        "  background-color: #f59330;"
        "}"
        "QPushButton#applyButton:pressed {"
        "  background-color: #c46e14;"
=======
        "  background-color: #ff9920;"
        "}"
        "QPushButton#applyButton:pressed {"
        "  background-color: #e07000;"
>>>>>>> 1b08b3c (fix(SystemKeyBlocker): include QWidget header for proper functionality (#577))
        "}"
    );
}
