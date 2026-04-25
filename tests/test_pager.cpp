#include "storage/Pager.hpp"
#include <cassert>
#include <iostream>
#include <vector>
#include <filesystem>

void test_pager() {
    std::string test_file = "test_db.bin";
    if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
    }

    {
        Pager pager(test_file);
        std::vector<uint8_t> data(4096, 0xAB);
        pager.write_page(0, data);
        
        std::vector<uint8_t> data2(4096, 0xCD);
        pager.write_page(1, data2);
        
        assert(pager.get_total_pages() == 2);
    }

    {
        Pager pager(test_file);
        assert(pager.get_total_pages() == 2);
        
        std::vector<uint8_t> read_data = pager.read_page(0);
        assert(read_data[0] == 0xAB);
        assert(read_data[4095] == 0xAB);
        
        std::vector<uint8_t> read_data2 = pager.read_page(1);
        assert(read_data2[0] == 0xCD);
        assert(read_data2[4095] == 0xCD);
    }

    std::filesystem::remove(test_file);
    std::cout << "Pager tests passed!" << std::endl;
}

int main() {
    try {
        test_pager();
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
