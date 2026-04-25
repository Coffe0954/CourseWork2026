#include <iostream>
#include <string>
#include <fstream>
#include "storage/Pager.hpp"

void run_interactive_mode() {
    std::string command;
    while (true) {
        std::cout << "dbms> ";
        if (!std::getline(std::cin, command) || command == "exit") break;
        //здесь вызывается SQLParser::parse(command)
    }
}

void run_batch_mode(const std::string& filename) {
    std::ifstream script(filename);
    std::string command;
    while (std::getline(script, command)) {
        //выполнение команд из файла
    }
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
        run_interactive_mode();
    } else if (argc == 2) {
        run_batch_mode(argv[1]);
    } else {
        std::cerr << "Usage: " << argv[0] << " [script.txt]" << std::endl;
        return 1;
    }
    return 0;
}