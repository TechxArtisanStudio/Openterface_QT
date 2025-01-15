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


#include "fpsspinbox.h"

FpsSpinBox::FpsSpinBox(QWidget *parent)
    : QSpinBox(parent)
{
}

void FpsSpinBox::setValidValues(const std::set<int> &values)
{
    validValues = values;
    if (!validValues.empty()) {
        setRange(*validValues.begin(), *validValues.rbegin());
    }
}

void FpsSpinBox::stepBy(int steps)
{
    if (validValues.empty()) {
        QSpinBox::stepBy(steps);
        return;
    }

    int currentValue = value();
    auto it = validValues.find(currentValue);

    if (steps > 0) { // Incrementing
        if (it != validValues.end()) ++it;
        if (it != validValues.end()) setValue(*it);
    } else if (steps < 0) { // Decrementing
        if (it == validValues.begin() || it == validValues.end()) {
            // If it's the first element or not found, do nothing special
        } else {
            --it;
            setValue(*it);
        }
    }
}
