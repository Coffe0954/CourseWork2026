#include "storage/Pager.hpp"
#include <stdexcept>

Pager::Pager(const std::string& filename) : filename(filename) {
    // Открываем для чтения и записи, в бинарном режиме. 
    // Если файла нет, std::ios::app создаст его.
    file.open(filename, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open database file.");
    }
}

Pager::~Pager() {
    if (file.is_open()) {
        file.close();
    }
}

void Pager::write_page(uint32_t page_id, const std::vector<uint8_t>& data) {
    if (data.size() != PAGE_SIZE) {
        throw std::invalid_argument("Data size must match PAGE_SIZE.");
    }
    file.clear(); // Очищаем флаги ошибок
    file.seekp(page_id * PAGE_SIZE, std::ios::beg);
    file.write(reinterpret_cast<const char*>(data.data()), PAGE_SIZE);
    file.flush();
}

std::vector<uint8_t> Pager::read_page(uint32_t page_id) {
    std::vector<uint8_t> data(PAGE_SIZE, 0);
    file.clear();
    file.seekg(page_id * PAGE_SIZE, std::ios::beg);
    file.read(reinterpret_cast<char*>(data.data()), PAGE_SIZE);
    return data;
}

uint32_t Pager::get_total_pages() {
    file.clear();
    file.seekg(0, std::ios::end);
    return file.tellg() / PAGE_SIZE;
}