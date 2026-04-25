#pragma once
#include <string>
#include <vector>
#include "index/BStarTree.hpp"

enum class ColumnType { INT, STRING };

struct Column {
    std::string name;
    ColumnType type;
    bool is_indexed;
    bool not_null;
};

class Table {
public:
    std::string table_name;
    std::vector<Column> columns;
    
    //для каждой индексированной колонки создается свое B*-дерево
    //например: std::map<string, BStarTree<int>> int_indices;
};