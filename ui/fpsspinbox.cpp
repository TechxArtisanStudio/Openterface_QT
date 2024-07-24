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
