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


#ifndef TOKEN_H
#define TOKEN_H

#include <string>
#include <vector>
#include <set>
#include <iostream>
#include <cctype>
#include <algorithm>
#include <sstream>


enum class AHKTokenType {
    KEYWORD,
	IDENTIFIER,
	OBJECT,
	INTEGER,
	FLOAT,
	STRING,
	OPERATOR,
	SYMBOL,
	VARIABLE,
	COMMENT,
	COMMAND,
	WHITESPACE,
	FUNCTION,
	INVALID,
	NEWLINE,
	ENDOFFILE
};


struct Token {
    AHKTokenType type;
    std::string value;
};

const std::set<std::string> keywords = {
	"If", "Else", "Loop", "While", "For", "Try", "Catch", "Finally", "Throw",
	"Switch", "Return", "Goto", "Continue", "Until"
};

const std::vector<std::string> operators = {
	":=", "=", "+=", "-=", "*=", "/=", ".=", "|=", "&=", "^=", ">>=", "<<=", "+", "++", "-", "--", "*", "**", "/", "//",
	">", "<", "!", "==", "!=", "<>", "|", "||", "&", "&&",
    "%", ".", "()",
};

const std::set<std::string> mouse_keyboard = {
	"BlockInput", "Click", "ControlClick", "ControlSend", "CoordMode","GetKeyName", "GetKeySC", "GetKeyState",
	"GetKeyVK", "List of Keys", "KeyHistory", "KeyWait", "Input", "InputHook", "MouseClick", "MouseClickDrag",
	"MouseGetPos", "MouseMove", "Send", "SendLevel", "SendMode", "SetCapsLockState", "SetDefaultMouseSpeed",
	"SetKeyDelay", "SetNumLockState", "SetScrollLockState", "SetStoreCapsLockMode", "Sleep", "FullScreenCapture","AreaScreenCapture"
};


#endif // TOKEN_H
