#include "parser/SQLParser.hpp"
#include <iostream>
#include <stdexcept>

SQLParser::SQLParser(const std::string& input, DatabaseManager& db_manager) 
    : db_manager(db_manager), pos(0) {
    Lexer lexer(input);
    tokens = lexer.tokenize();
}

Token SQLParser::peek() const {
    if (pos >= tokens.size()) return {TokenType::END_OF_FILE, ""};
    return tokens[pos];
}

Token SQLParser::advance() {
    if (pos >= tokens.size()) return {TokenType::END_OF_FILE, ""};
    return tokens[pos++];
}

bool SQLParser::match(TokenType type, const std::string& value) {
    if (peek().type == type && (value == "" || peek().value == value)) {
        advance();
        return true;
    }
    return false;
}

void SQLParser::expect(TokenType type, const std::string& value) {
    if (!match(type, value)) {
        throw std::runtime_error("Unexpected token: " + peek().value);
    }
}

void SQLParser::execute() {
    while (peek().type != TokenType::END_OF_FILE) {
        if (match(TokenType::KEYWORD, "CREATE")) {
            parse_create();
        } else if (match(TokenType::KEYWORD, "DROP")) {
            parse_drop();
        } else if (match(TokenType::KEYWORD, "USE")) {
            parse_use();
        } else if (match(TokenType::KEYWORD, "INSERT")) {
            parse_insert();
        } else if (match(TokenType::KEYWORD, "SELECT")) {
            parse_select();
        } else if (match(TokenType::KEYWORD, "UPDATE")) {
            parse_update();
        } else if (match(TokenType::KEYWORD, "DELETE")) {
            parse_delete();
        } else {
            throw std::runtime_error("Unknown command: " + peek().value);
        }
        expect(TokenType::PUNCTUATION, ";");
    }
}

void SQLParser::parse_create() {
    if (match(TokenType::KEYWORD, "DATABASE")) {
        std::string name = advance().value;
        db_manager.create_database(name);
        std::cout << "Database created: " << name << std::endl;
    } else if (match(TokenType::KEYWORD, "TABLE")) {
        std::string name = advance().value;
        expect(TokenType::PUNCTUATION, "(");
        std::vector<Column> columns;
        while (peek().value != ")") {
            std::string col_name = advance().value;
            std::string type_str = advance().value;
            ColumnType type = (type_str == "INT") ? ColumnType::INT : ColumnType::STRING;
            bool indexed = false;
            bool not_null = false;
            while (peek().type == TokenType::KEYWORD && (peek().value == "INDEXED" || peek().value == "NOT_NULL")) {
                if (advance().value == "INDEXED") indexed = true;
                else not_null = true;
            }
            columns.push_back({col_name, type, indexed, not_null});
            if (peek().value == ",") advance();
        }
        expect(TokenType::PUNCTUATION, ")");
        if (!db_manager.current_db) throw std::runtime_error("No database selected");
        db_manager.current_db->create_table(name, columns);
        std::cout << "Table created: " << name << std::endl;
    }
}

void SQLParser::parse_drop() {
    if (match(TokenType::KEYWORD, "DATABASE")) {
        std::string name = advance().value;
        db_manager.drop_database(name);
        std::cout << "Database dropped: " << name << std::endl;
    } else if (match(TokenType::KEYWORD, "TABLE")) {
        std::string name = advance().value;
        if (!db_manager.current_db) throw std::runtime_error("No database selected");
        db_manager.current_db->drop_table(name);
        std::cout << "Table dropped: " << name << std::endl;
    }
}

void SQLParser::parse_use() {
    std::string name = advance().value;
    db_manager.use_database(name);
    std::cout << "Using database: " << name << std::endl;
}

void SQLParser::parse_insert() {
    expect(TokenType::KEYWORD, "INTO");
    std::string table_name = advance().value;
    expect(TokenType::PUNCTUATION, "(");
    std::vector<std::string> col_names;
    while (peek().value != ")") {
        col_names.push_back(advance().value);
        if (peek().value == ",") advance();
    }
    expect(TokenType::PUNCTUATION, ")");
    expect(TokenType::KEYWORD, "VALUE"); // The PDF says VALUE or VALUES? PDF says VALUE.
    expect(TokenType::PUNCTUATION, "(");
    std::vector<Value> values;
    for (size_t i = 0; i < col_names.size(); ++i) {
        Token t = advance();
        if (t.type == TokenType::NUMBER_LITERAL) {
            values.push_back(std::stoi(t.value));
        } else if (t.type == TokenType::STRING_LITERAL) {
            values.push_back(t.value);
        }
        if (peek().value == ",") advance();
    }
    expect(TokenType::PUNCTUATION, ")");
    
    if (!db_manager.current_db) throw std::runtime_error("No database selected");
    auto table = db_manager.current_db->tables[table_name];
    if (!table) throw std::runtime_error("Table not found: " + table_name);
    
    // Mapping col_names to table columns
    std::vector<Value> full_row(table->columns.size(), std::monostate{});
    for (size_t i = 0; i < col_names.size(); ++i) {
        for (size_t j = 0; j < table->columns.size(); ++j) {
            if (table->columns[j].name == col_names[i]) {
                full_row[j] = values[i];
                break;
            }
        }
    }
    table->insert_row(full_row);
    std::cout << "Row inserted into " << table_name << std::endl;
}

void SQLParser::parse_select() {
    // SELECT *|([col_1] <AS [alias]>, ...) FROM [table_name] <WHERE condition>;
    bool select_all = false;
    if (match(TokenType::PUNCTUATION, "*")) {
        select_all = true;
    } else {
        // Parse columns, but for simplicity we'll just support * for now
        while (peek().value != "FROM") advance();
    }
    expect(TokenType::KEYWORD, "FROM");
    std::string table_name = advance().value;
    
    if (!db_manager.current_db) throw std::runtime_error("No database selected");
    auto table = db_manager.current_db->tables[table_name];
    if (!table) throw std::runtime_error("Table not found: " + table_name);

    auto rows = table->select_rows();
    
    // Output as JSON array of objects
    std::cout << "[" << std::endl;
    for (size_t i = 0; i < rows.size(); ++i) {
        std::cout << "  {";
        for (size_t j = 0; j < table->columns.size(); ++j) {
            std::cout << "\"" << table->columns[j].name << "\": ";
            if (std::holds_alternative<int>(rows[i][j])) {
                std::cout << std::get<int>(rows[i][j]);
            } else if (std::holds_alternative<std::string>(rows[i][j])) {
                std::cout << "\"" << std::get<std::string>(rows[i][j]) << "\"";
            } else {
                std::cout << "null";
            }
            if (j < table->columns.size() - 1) std::cout << ", ";
        }
        std::cout << "}";
        if (i < rows.size() - 1) std::cout << ",";
        std::cout << std::endl;
    }
    std::cout << "]" << std::endl;
}

void SQLParser::parse_update() {
    // Basic placeholder
    std::string table_name = advance().value;
    while (peek().value != ";") advance();
    std::cout << "UPDATE not fully implemented yet" << std::endl;
}

void SQLParser::parse_delete() {
    // Basic placeholder
    expect(TokenType::KEYWORD, "FROM");
    std::string table_name = advance().value;
    while (peek().value != ";") advance();
    std::cout << "DELETE not fully implemented yet" << std::endl;
}
