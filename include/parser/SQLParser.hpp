#pragma once
#include <string>
#include <vector>
#include "parser/Lexer.hpp"
#include "schema/Database.hpp"

class SQLParser {
public:
    SQLParser(const std::string& input, DatabaseManager& db_manager);
    void execute();

private:
    std::vector<Token> tokens;
    size_t pos;
    DatabaseManager& db_manager;

    Token peek() const;
    Token advance();
    bool match(TokenType type, const std::string& value = "");
    void expect(TokenType type, const std::string& value = "");

    void parse_create();
    void parse_drop();
    void parse_use();
    void parse_insert();
    void parse_update();
    void parse_delete();
    void parse_select();
};
