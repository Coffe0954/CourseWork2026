#include "parser/Lexer.hpp"
#include <cctype>
#include <algorithm>
#include <set>

Lexer::Lexer(const std::string& input) : input(input), pos(0) {}

char Lexer::peek() const {
    if (pos >= input.length()) return '\0';
    return input[pos];
}

char Lexer::advance() {
    if (pos >= input.length()) return '\0';
    return input[pos++];
}

void Lexer::skip_whitespace() {
    while (std::isspace(peek())) advance();
}

static const std::set<std::string> KEYWORDS = {
    "CREATE", "DATABASE", "DROP", "TABLE", "USE", "INSERT", "INTO", "VALUE", "VALUES",
    "UPDATE", "SET", "WHERE", "DELETE", "FROM", "SELECT", "AS", "NOT_NULL", "INDEXED",
    "INT", "STRING", "BETWEEN", "AND", "LIKE"
};

Token Lexer::read_identifier() {
    std::string value;
    while (std::isalnum(peek()) || peek() == '_') {
        value += advance();
    }
    std::string upper_value = value;
    std::transform(upper_value.begin(), upper_value.end(), upper_value.begin(), ::toupper);
    
    if (KEYWORDS.count(upper_value)) {
        return {TokenType::KEYWORD, upper_value};
    }
    return {TokenType::IDENTIFIER, value};
}

Token Lexer::read_string() {
    advance(); // Skip "
    std::string value;
    while (peek() != '"' && peek() != '\0') {
        value += advance();
    }
    if (peek() == '"') advance();
    return {TokenType::STRING_LITERAL, value};
}

Token Lexer::read_number() {
    std::string value;
    while (std::isdigit(peek())) {
        value += advance();
    }
    return {TokenType::NUMBER_LITERAL, value};
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (pos < input.length()) {
        skip_whitespace();
        if (pos >= input.length()) break;

        char c = peek();
        if (std::isalpha(c) || c == '_') {
            tokens.push_back(read_identifier());
        } else if (std::isdigit(c)) {
            tokens.push_back(read_number());
        } else if (c == '"') {
            tokens.push_back(read_string());
        } else if (c == ';' || c == ',' || c == '(' || c == ')' || c == '*') {
            tokens.push_back({TokenType::PUNCTUATION, std::string(1, advance())});
        } else if (c == '=' || c == '!' || c == '<' || c == '>') {
            std::string op(1, advance());
            if (peek() == '=') op += advance();
            tokens.push_back({TokenType::OPERATOR, op});
        } else {
            tokens.push_back({TokenType::UNKNOWN, std::string(1, advance())});
        }
    }
    tokens.push_back({TokenType::END_OF_FILE, ""});
    return tokens;
}
