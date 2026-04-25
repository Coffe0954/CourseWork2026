#include "schema/Table.hpp"
#include <cstring>
#include <iostream>

Table::Table(const std::string& name, const std::vector<Column>& cols) 
    : table_name(name), columns(cols) {
    pager = std::make_unique<Pager>(name + ".db");
    
    for (const auto& col : columns) {
        if (col.is_indexed) {
            if (col.type == ColumnType::INT) {
                int_indices.push_back(std::make_unique<BStarTree<int>>());
            } else {
                string_indices.push_back(std::make_unique<BStarTree<std::string>>());
            }
        }
    }
}

std::vector<uint8_t> Table::serialize_row(const std::vector<Value>& values) {
    std::vector<uint8_t> data;
    for (size_t i = 0; i < columns.size(); ++i) {
        if (std::holds_alternative<std::monostate>(values[i])) {
            // Null handle (simplified: just skip or mark)
            data.push_back(0); // Flag for null
        } else {
            data.push_back(1); // Flag for not null
            if (columns[i].type == ColumnType::INT) {
                int val = std::get<int>(values[i]);
                uint8_t buf[4];
                std::memcpy(buf, &val, 4);
                for (int j = 0; j < 4; ++j) data.push_back(buf[j]);
            } else {
                std::string val = std::get<std::string>(values[i]);
                uint32_t len = val.length();
                uint8_t buf[4];
                std::memcpy(buf, &len, 4);
                for (int j = 0; j < 4; ++j) data.push_back(buf[j]);
                for (char c : val) data.push_back(static_cast<uint8_t>(c));
            }
        }
    }
    return data;
}

std::vector<Value> Table::deserialize_row(const std::vector<uint8_t>& data) {
    std::vector<Value> values;
    size_t pos = 0;
    for (size_t i = 0; i < columns.size(); ++i) {
        if (data[pos++] == 0) {
            values.push_back(std::monostate{});
        } else {
            if (columns[i].type == ColumnType::INT) {
                int val;
                std::memcpy(&val, &data[pos], 4);
                pos += 4;
                values.push_back(val);
            } else {
                uint32_t len;
                std::memcpy(&len, &data[pos], 4);
                pos += 4;
                std::string val(reinterpret_cast<const char*>(&data[pos]), len);
                pos += len;
                values.push_back(val);
            }
        }
    }
    return values;
}

void Table::insert_row(const std::vector<Value>& values) {
    std::vector<uint8_t> row_data = serialize_row(values);
    uint32_t page_id = pager->get_total_pages();
    pager->write_page(page_id, row_data);
    
    uint64_t offset = static_cast<uint64_t>(page_id) * Pager::PAGE_SIZE;

    // Update indices
    int int_idx = 0;
    int str_idx = 0;
    for (size_t i = 0; i < columns.size(); ++i) {
        if (columns[i].is_indexed) {
            if (columns[i].type == ColumnType::INT) {
                int_indices[int_idx++]->insert(std::get<int>(values[i]), offset);
            } else {
                string_indices[str_idx++]->insert(std::get<std::string>(values[i]), offset);
            }
        }
    }
}

std::vector<std::vector<Value>> Table::select_rows(const std::string& condition) {
    std::vector<std::vector<Value>> results;
    uint32_t total_pages = pager->get_total_pages();
    for (uint32_t i = 0; i < total_pages; ++i) {
        std::vector<uint8_t> data = pager->read_page(i);
        // This is very simplified: each row is in its own page.
        results.push_back(deserialize_row(data));
    }
    return results;
}
