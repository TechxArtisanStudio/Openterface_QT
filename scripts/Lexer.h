#ifndef LEXER_H
#define LEXER_H

#include <string>
#include <vector>
#include "Token.h"

class Lexer {
public:
    Lexer();
    void setSource(const std::string& source);
    std::vector<Token> tokenize();

private:
    std::string source;
    size_t currentIndex;
    char currentChar();

    void advance();
    Token nextToken();
    Token identifier();
    Token number();
    Token symbol();
};

#endif // LEXER_H
