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
    } while (token.type != AHKTokenType::EndOfFile);
    return tokens;
}

Token Lexer::nextToken() {
    while (std::isspace(currentChar())) {
        if (currentChar() == '\n') {
            advance();
            return {AHKTokenType::NewLine, "\\n"};
        }
        advance();
    }

    if (std::isalpha(currentChar())) {
        return identifier();
    }

    if (std::isdigit(currentChar())) {
        return number();
    }

    if (currentChar() == '\0') {
        return {AHKTokenType::EndOfFile, ""};
    }

    return symbol();
}

Token Lexer::identifier() {
    std::string result;
    while (std::isalnum(currentChar())) {
        result += currentChar();
        advance();
    }

    if (result == "Click") {
        return {AHKTokenType::Click, result};
    } else if (result == "Send") {
        return {AHKTokenType::Send, result};
    } else if (result == "If") {
        return {AHKTokenType::If, result};
    }

    return {AHKTokenType::Identifier, result};
}

Token Lexer::number() {
    std::string result;
    while (std::isdigit(currentChar())) {
        result += currentChar();
        advance();
    }
    return {AHKTokenType::Number, result};
}

Token Lexer::symbol() {
    char current = currentChar();
    advance();
    return {AHKTokenType::Symbol, std::string(1, current)};
}
