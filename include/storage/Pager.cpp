#include "storage/Pager.hpp"
#include <iostream>
#include <filesystem>

Pager::Pager(const std::string& filename) : filename(filename) {
    file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        file.clear();
        file.open(filename, std::ios::out | std::ios::binary);
        file.close();
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }
}

Pager::~Pager() {
    if (file.is_open()) {
        file.close();
    }
}

void Pager::write_page(uint32_t page_id, const std::vector<uint8_t>& data) {
    if (data.size() > PAGE_SIZE) {
        throw std::runtime_error("Data exceeds page size");
    }
    file.seekp(page_id * PAGE_SIZE);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    if (data.size() < PAGE_SIZE) {
        std::vector<uint8_t> padding(PAGE_SIZE - data.size(), 0);
        file.write(reinterpret_cast<const char*>(padding.data()), padding.size());
    }
    file.flush();
}

std::vector<uint8_t> Pager::read_page(uint32_t page_id) {
    file.seekg(page_id * PAGE_SIZE);
    std::vector<uint8_t> data(PAGE_SIZE);
    file.read(reinterpret_cast<char*>(data.data()), PAGE_SIZE);
    // If we read less than PAGE_SIZE, it might be a new page or EOF, but we expect full pages for simplicity
    return data;
}

uint32_t Pager::get_total_pages() {
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    return static_cast<uint32_t>(size / PAGE_SIZE);
}
