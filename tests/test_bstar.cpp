#include "index/BStarTree.hpp"
#include <cassert>
#include <iostream>

void test_bstar_tree() {
    BStarTree<int> tree;

    // Test basic insertion and search
    std::cout << "Inserting 10, 20, 5" << std::endl;
    tree.insert(10, 100);
    tree.insert(20, 200);
    tree.insert(5, 50);

    assert(tree.search(10) == 100);
    assert(tree.search(20) == 200);
    assert(tree.search(5) == 50);
    assert(tree.search(15) == 0);

    // Test splitting
    std::cout << "Inserting 30, 40 (split)" << std::endl;
    tree.insert(30, 300);
    tree.insert(40, 400); // Should trigger split

    std::cout << "Searching after split" << std::endl;
    std::cout << "Search 10: " << tree.search(10) << std::endl;
    std::cout << "Search 20: " << tree.search(20) << std::endl;
    std::cout << "Search 30: " << tree.search(30) << std::endl;
    std::cout << "Search 40: " << tree.search(40) << std::endl;
    std::cout << "Search 5: " << tree.search(5) << std::endl;

    assert(tree.search(10) == 100);
    assert(tree.search(40) == 400);
    assert(tree.search(5) == 50);

    std::cout << "Inserting more" << std::endl;
    tree.insert(50, 500);
    tree.insert(60, 600);

    assert(tree.search(60) == 600);

    std::cout << "BStarTree tests passed!" << std::endl;
}

int main() {
    try {
        test_bstar_tree();
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
