#ifndef PARSER_H
#define PARSER_H

#include <vector>
#include "Token.h"
#include "AST.h"

class Parser {
public:
    Parser(const std::vector<Token>& tokens);
    std::unique_ptr<ASTNode> parse();

private:
    const std::vector<Token>& tokens;
    size_t currentIndex;

    Token currentToken();
    void advance();
    std::unique_ptr<ASTNode> parseExpression();
    std::unique_ptr<ASTNode> parseStatement();
    std::unique_ptr<ASTNode> parseClickStatement();
    // Add more parsing methods as needed
};

#endif // PARSER_H
