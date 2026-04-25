#pragma once
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <stdexcept>

class Pager {
public:
    static const size_t PAGE_SIZE = 4096;

    Pager(const std::string& filename);
    ~Pager();

    void write_page(uint32_t page_id, const std::vector<uint8_t>& data);
    std::vector<uint8_t> read_page(uint32_t page_id);

    uint32_t get_total_pages();

private:
    std::fstream file;
    std::string filename;
};
