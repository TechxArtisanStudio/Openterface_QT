#include "RegularExpression.h"

RegularExpression::RegularExpression() {
    onRegex = QRegularExpression(QString("^(1|True|On)$"), QRegularExpression::CaseInsensitiveOption);
    offRegex = QRegularExpression(QString("^(0|False|Off)$"), QRegularExpression::CaseInsensitiveOption);
    sendEmbedRegex = QRegularExpression(QString(R"(\{Click\s*([^}]*)\})"), QRegularExpression::CaseInsensitiveOption);
    numberRegex = QRegularExpression(QString(R"(\d+)"));
    buttonRegex = QRegularExpression(QString(R"((?<![a-zA-Z])(right|R|middle|M|left|L)(?![a-zA-Z]))"), QRegularExpression::CaseInsensitiveOption);
    downUpRegex = QRegularExpression(QString(R"((?<![a-zA-Z])(down|D|Up|U)(?![a-zA-Z]))"), QRegularExpression::CaseInsensitiveOption);
    relativeRegex = QRegularExpression(QString(R"((?<![a-zA-Z])(rel|relative)(?![a-zA-Z]))"), QRegularExpression::CaseInsensitiveOption);
    braceKeyRegex = QRegularExpression(QString(R"(\{([^}]+)\})"), QRegularExpression::CaseInsensitiveOption);
    controlKeyRegex = QRegularExpression(QString(R"(([!^+#])((?:\{[^}]+\}|[^{])+))"));
}