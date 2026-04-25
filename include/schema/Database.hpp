#pragma once
#include <string>
#include <map>
#include <memory>
#include "schema/Table.hpp"

class Database {
public:
    std::string db_name;
    std::map<std::string, std::shared_ptr<Table>> tables;

    Database(const std::string& name);
    
    void create_table(const std::string& name, const std::vector<Column>& columns);
    void drop_table(const std::string& name);
};

class DatabaseManager {
public:
    std::map<std::string, std::shared_ptr<Database>> databases;
    std::shared_ptr<Database> current_db;

    void create_database(const std::string& name);
    void drop_database(const std::string& name);
    void use_database(const std::string& name);
};
