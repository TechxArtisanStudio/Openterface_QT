#include "Parser.h"

Parser::Parser(const std::vector<Token>& tokens) : tokens(tokens), currentIndex(0) {}

Token Parser::currentToken() {
    return currentIndex < tokens.size() ? tokens[currentIndex] : Token{AHKTokenType::ENDOFFILE, ""};
}

void Parser::advance() {
    if (currentIndex < tokens.size()) {
        currentIndex++;
    }
}

std::unique_ptr<ASTNode> Parser::parse() {
    // Create a root node to hold all statements
    auto root = std::make_unique<StatementListNode>();
    
    // Parse statements until we reach the end of file
    while (currentToken().type != AHKTokenType::ENDOFFILE) {
        auto statement = parseStatement();
        if (statement) {
            root->addStatement(std::move(statement));
        }
        
        // Skip any newline tokens between statements
        while (currentToken().type == AHKTokenType::NEWLINE) {
            advance();
        }
    }
    
    return root;
}

std::unique_ptr<ASTNode> Parser::parseExpression() {
    // Implement expression parsing logic
    return std::make_unique<ExpressionNode>();
}

std::unique_ptr<ASTNode> Parser::parseStatement() {
    // Skip any leading newlines
    while (currentToken().type == AHKTokenType::NEWLINE) {
        advance();
    }
    
    if (currentToken().type == AHKTokenType::COMMAND) {
        if (currentToken().value == "Click"){
            return parseClickStatement();
        }

    }
    // Add other statement types here
    
    // Skip unknown statements until newline
    while (currentToken().type != AHKTokenType::NEWLINE &&
           currentToken().type != AHKTokenType::ENDOFFILE) {
        advance();
    }
    
    return nullptr;
}

std::unique_ptr<ASTNode> Parser::parseClickStatement() {
    advance(); // Move past the 'Click' token

    std::vector<std::string> options;
    while (currentToken().type == AHKTokenType::INTEGER
            || currentToken().type == AHKTokenType::IDENTIFIER
            || currentToken().type == AHKTokenType::SYMBOL) {
        options.push_back(currentToken().value);
        advance();
    }

    return std::make_unique<ClickStatementNode>(options);
}
