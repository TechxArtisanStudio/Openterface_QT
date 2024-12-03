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
	"SetKeyDelay", "SetNumLockState", "SetScrollLockState", "SetStoreCapsLockMode"
};


#endif // TOKEN_H
