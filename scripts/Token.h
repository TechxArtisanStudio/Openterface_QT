#ifndef TOKEN_H
#define TOKEN_H

#include <string>

enum class AHKTokenType {
    Identifier,
    Number,
    Symbol,
    Click,
    Send,
    If,
    EndOfFile,
    NewLine,
    Unknown
};

struct Token {
    AHKTokenType type;
    std::string value;
};

#endif // TOKEN_H