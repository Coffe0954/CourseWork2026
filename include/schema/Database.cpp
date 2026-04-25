#include "schema/Database.hpp"
#include <stdexcept>
#include <filesystem>

Database::Database(const std::string& name) : db_name(name) {}

void Database::create_table(const std::string& name, const std::vector<Column>& columns) {
    if (tables.find(name) != tables.end()) {
        throw std::runtime_error("Table already exists: " + name);
    }
    tables[name] = std::make_shared<Table>(name, columns);
}

void Database::drop_table(const std::string& name) {
    if (tables.find(name) == tables.end()) {
        throw std::runtime_error("Table not found: " + name);
    }
    std::filesystem::remove(name + ".db");
    tables.erase(name);
}

void DatabaseManager::create_database(const std::string& name) {
    if (databases.find(name) != databases.end()) {
        throw std::runtime_error("Database already exists: " + name);
    }
    databases[name] = std::make_shared<Database>(name);
}

void DatabaseManager::drop_database(const std::string& name) {
    if (databases.find(name) == databases.end()) {
        throw std::runtime_error("Database not found: " + name);
    }
    if (current_db && current_db->db_name == name) {
        current_db = nullptr;
    }
    // In a real system, we'd delete files here
    databases.erase(name);
}

void DatabaseManager::use_database(const std::string& name) {
    if (databases.find(name) == databases.end()) {
        throw std::runtime_error("Database not found: " + name);
    }
    current_db = databases[name];
}
