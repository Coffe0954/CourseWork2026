#pragma once
#include <vector>
#include <cstdint>

//структура заголовка узла на диске
struct NodeHeader {
    bool is_leaf;
    uint32_t parent_id;
    uint32_t left_sibling_id;  //для B*
    uint32_t right_sibling_id; //для B*
    uint16_t keys_count;
};

template <typename K>
class BStarTree {
public:
    //поиск возвращает смещение данных в файле (offset)
    uint64_t search(K key);
    
    //вставка: если узел полон, сначала пробуем перераспределить ключи 
    //между соседями, и только если они тоже полны — делаем сплит (2 в 3)
    void insert(K key, uint64_t offset);

private:
    void redistribute(uint32_t node_id, uint32_t sibling_id);
    void split_two_into_three(uint32_t node_id, uint32_t sibling_id);
};