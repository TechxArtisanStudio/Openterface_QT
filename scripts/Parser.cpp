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


#include "Parser.h"
#include <QDebug>


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
        return parseCommandStatement();
    }
    // Add other statement types here
    
    // Skip unknown statements until newline
    while (currentToken().type != AHKTokenType::NEWLINE &&
           currentToken().type != AHKTokenType::ENDOFFILE) {
        advance();
    }
    
    return nullptr;
}

std::unique_ptr<ASTNode> Parser::parseCommandStatement() {
    QString tmp = QString::fromStdString(currentToken().value);
    advance(); // Move past the COMMAND token
    
    std::vector<std::string> options;
    while (currentToken().type != AHKTokenType::NEWLINE &&
           currentToken().type != AHKTokenType::ENDOFFILE) {
        options.push_back(currentToken().value);
        advance();
    }
    auto commandStatementNode = std::make_unique<CommandStatementNode>(options);
    commandStatementNode->setCommandName(tmp);
    return commandStatementNode;
}
