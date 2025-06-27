#ifndef REGULAR_EXPRESSIONS_H
#define REGULAR_EXPRESSIONS_H

#include <QRegularExpression>
#include <QString>

class RegularExpression {
public:
    RegularExpression(const RegularExpression&) = delete;
    RegularExpression& operator=(const RegularExpression&) = delete;
    
    static RegularExpression& instance() {
        static RegularExpression instance;
        return instance;
    }

    QRegularExpression onRegex;
    QRegularExpression offRegex;
    QRegularExpression sendEmbedRegex;
    QRegularExpression numberRegex;
    QRegularExpression buttonRegex;
    QRegularExpression downUpRegex;
    QRegularExpression relativeRegex;
    QRegularExpression braceKeyRegex;
    QRegularExpression controlKeyRegex;

private:
    RegularExpression();
    ~RegularExpression() = default;
};

#endif // REGULAR_EXPRESSIONS_H