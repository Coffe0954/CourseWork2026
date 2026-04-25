#pragma once
#include <string>
#include <vector>

enum class TokenType {
    KEYWORD, IDENTIFIER, STRING_LITERAL, NUMBER_LITERAL,
    OPERATOR, PUNCTUATION, END_OF_FILE, UNKNOWN
};

struct Token {
    TokenType type;
    std::string value;
};

class Lexer {
public:
    Lexer(const std::string& input);
    std::vector<Token> tokenize();

private:
    std::string input;
    size_t pos;

    char peek() const;
    char advance();
    void skip_whitespace();
    Token read_identifier();
    Token read_string();
    Token read_number();
};
