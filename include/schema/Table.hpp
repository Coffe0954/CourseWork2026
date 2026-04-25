#pragma once
#include <string>
#include <vector>
#include <variant>
#include <memory>
#include "index/BStarTree.hpp"
#include "storage/Pager.hpp"

enum class ColumnType { INT, STRING };

struct Column {
    std::string name;
    ColumnType type;
    bool is_indexed;
    bool not_null;
};

using Value = std::variant<std::monostate, int, std::string>;

class Table {
public:
    std::string table_name;
    std::vector<Column> columns;
    std::unique_ptr<Pager> pager;

    // Simplified indexing for now: using in-memory BStarTree
    // In a real system, these would be persisted too.
    std::vector<std::unique_ptr<BStarTree<int>>> int_indices;
    std::vector<std::unique_ptr<BStarTree<std::string>>> string_indices;

    Table(const std::string& name, const std::vector<Column>& cols);
    
    void insert_row(const std::vector<Value>& values);
    std::vector<std::vector<Value>> select_rows(const std::string& condition = "");

    std::vector<uint8_t> serialize_row(const std::vector<Value>& values);
    std::vector<Value> deserialize_row(const std::vector<uint8_t>& data);
};
