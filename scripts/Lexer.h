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
