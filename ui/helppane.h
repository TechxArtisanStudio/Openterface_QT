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

#ifndef HELPPANE_H
#define HELPPANE_H

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>

class HelpPane : public QWidget
{
    Q_OBJECT

public:
    explicit HelpPane(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;  // Add this line

private:
    QVBoxLayout *layout;
    QLabel *titleLabel;
    QLabel *contentLabel;
};

#endif // HELPPANE_H
