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


#include "Lexer.h"
#include <cctype>
#include <stdexcept>



Lexer::Lexer() : currentIndex(0) {}

void Lexer::setSource(const std::string& source) {
    this->source = source;
    currentIndex = 0;
}

char Lexer::currentChar() {
    return currentIndex < source.size() ? source[currentIndex] : '\0';
}

void Lexer::advance() {
    if (currentIndex < source.size()) {
        currentIndex++;
    }
}

std::vector<Token> Lexer::tokenize() {
    if (source.empty()) {
        throw std::runtime_error("Source is not set.");
    }
    std::vector<Token> tokens;
    Token token;
    do {
        token = nextToken();
        tokens.push_back(token);
    } while (token.type != AHKTokenType::ENDOFFILE);
    return tokens;
}

Token Lexer::nextToken() {
    while (std::isspace(currentChar())) {
        if (currentChar() == '\n') {
            advance();
            return {AHKTokenType::NEWLINE, "\\n"};
        }
        advance();
        return {AHKTokenType::WHITESPACE, " "};
    }

    if (std::isalpha(currentChar())) {
        return identifier();
    }

    if (std::isdigit(currentChar())) {
        return number();
    }

    for (const auto& op : operators) {
        if (source.substr(currentIndex, op.length()) == op) {
            advance();
            return {AHKTokenType::OPERATOR, op};
        }
    }

    if (currentChar() == '\0') {
        return {AHKTokenType::ENDOFFILE, ""};
    }

    return symbol();
}

Token Lexer::identifier() {
    std::string result;
    while (std::isalnum(currentChar())) {
        result += currentChar();
        advance();
    }

    if (keywords.find(result) != keywords.end()) {
        return {AHKTokenType::KEYWORD, result};
    }

    if (mouse_keyboard.find(result) != mouse_keyboard.end()){
        return {AHKTokenType::COMMAND, result};
    }

    return {AHKTokenType::IDENTIFIER, result};
}

Token Lexer::number() {
    std::string result;
    bool hasDecimalPoint = false;
    while (std::isdigit(currentChar()) || (currentChar() == '.' && !hasDecimalPoint)) {
        if (currentChar() == '.') {
            hasDecimalPoint = true;
        }
        result += currentChar();
        advance();
    }
    return {hasDecimalPoint ? AHKTokenType::FLOAT : AHKTokenType::INTEGER, result};
}

Token Lexer::symbol() {
    char current = currentChar();
    advance();
    return {AHKTokenType::SYMBOL, std::string(1, current)};
}
