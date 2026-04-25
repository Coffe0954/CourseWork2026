#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>

template <typename K>
struct BStarNode {
    bool is_leaf;
    std::vector<K> keys;
    std::vector<uint64_t> offsets; // For leaves: data offsets. For internal: child node IDs.
    uint32_t id;
    uint32_t parent_id = 0xFFFFFFFF;
};

template <typename K>
class BStarTree {
public:
    BStarTree() : root_id(0), next_node_id(1) {
        BStarNode<K> root;
        root.is_leaf = true;
        root.id = 0;
        nodes[0] = root;
    }

    uint64_t search(K key) {
        return search_recursive(root_id, key);
    }

    void insert(K key, uint64_t offset) {
        insert_recursive(root_id, key, offset);
    }

private:
    uint32_t root_id;
    uint32_t next_node_id;
    std::map<uint32_t, BStarNode<K>> nodes;
    const size_t MAX_KEYS = 4;

    uint64_t search_recursive(uint32_t node_id, K key) {
        BStarNode<K>& node = nodes[node_id];
        auto it = std::lower_bound(node.keys.begin(), node.keys.end(), key);
        int idx = std::distance(node.keys.begin(), it);

        if (node.is_leaf) {
            if (it != node.keys.end() && *it == key) {
                return node.offsets[idx];
            }
            return 0;
        } else {
            // In internal nodes, if key < keys[0], follow offsets[0]
            // If keys[0] <= key < keys[1], follow offsets[1]
            // etc.
            // Wait, my current idx from lower_bound gives the first key >= key.
            // If key == keys[idx], we should go to offsets[idx+1]? 
            // Standard B-tree: child i contains keys in range [key[i-1], key[i])
            // So if key < keys[0], follow offsets[0].
            // If keys[0] <= key < keys[1], follow offsets[1].
            // lower_bound(key) gives it to keys[0] if key <= keys[0].
            
            // Let's use a simpler convention:
            // internal keys[i] is the separator between child i and child i+1.
            // child 0: keys < keys[0]
            // child 1: keys >= keys[0] and keys < keys[1]
            // ...
            // child n: keys >= keys[n-1]
            
            if (it == node.keys.end()) {
                return search_recursive(node.offsets.back(), key);
            } else if (*it == key) {
                return search_recursive(node.offsets[idx + 1], key);
            } else {
                return search_recursive(node.offsets[idx], key);
            }
        }
    }

    void insert_recursive(uint32_t node_id, K key, uint64_t offset) {
        BStarNode<K>& node = nodes[node_id];
        if (node.is_leaf) {
            auto it = std::lower_bound(node.keys.begin(), node.keys.end(), key);
            int idx = std::distance(node.keys.begin(), it);
            node.keys.insert(it, key);
            node.offsets.insert(node.offsets.begin() + idx, offset);

            if (node.keys.size() > MAX_KEYS) {
                split_node(node_id);
            }
        } else {
            auto it = std::lower_bound(node.keys.begin(), node.keys.end(), key);
            int idx = std::distance(node.keys.begin(), it);
            uint32_t child_id;
            if (it == node.keys.end()) {
                child_id = node.offsets.back();
            } else if (*it == key) {
                child_id = node.offsets[idx+1];
            } else {
                child_id = node.offsets[idx];
            }
            insert_recursive(child_id, key, offset);
        }
    }

    void split_node(uint32_t node_id) {
        BStarNode<K> node = nodes[node_id];
        int mid = node.keys.size() / 2;
        K mid_key = node.keys[mid];

        BStarNode<K> new_node;
        new_node.id = next_node_id++;
        new_node.is_leaf = node.is_leaf;
        new_node.parent_id = node.parent_id;

        if (node.is_leaf) {
            new_node.keys.assign(node.keys.begin() + mid, node.keys.end());
            new_node.offsets.assign(node.offsets.begin() + mid, node.offsets.end());
            node.keys.erase(node.keys.begin() + mid, node.keys.end());
            node.offsets.erase(node.offsets.begin() + mid, node.offsets.end());
        } else {
            new_node.keys.assign(node.keys.begin() + mid + 1, node.keys.end());
            new_node.offsets.assign(node.offsets.begin() + mid + 1, node.offsets.end());
            node.keys.erase(node.keys.begin() + mid, node.keys.end());
            node.offsets.erase(node.offsets.begin() + mid + 1, node.offsets.end());
            
            for(auto cid : new_node.offsets) {
                nodes[cid].parent_id = new_node.id;
            }
        }

        nodes[node.id] = node;
        nodes[new_node.id] = new_node;

        if (node_id == root_id) {
            BStarNode<K> new_root;
            new_root.id = next_node_id++;
            new_root.is_leaf = false;
            new_root.keys.push_back(mid_key);
            new_root.offsets.push_back(node.id);
            new_root.offsets.push_back(new_node.id);
            root_id = new_root.id;
            nodes[root_id] = new_root;
            nodes[node.id].parent_id = root_id;
            nodes[new_node.id].parent_id = root_id;
        } else {
            insert_into_parent(node.parent_id, mid_key, new_node.id);
        }
    }

    void insert_into_parent(uint32_t parent_id, K key, uint32_t child_id) {
        BStarNode<K> parent = nodes[parent_id];
        auto it = std::lower_bound(parent.keys.begin(), parent.keys.end(), key);
        int idx = std::distance(parent.keys.begin(), it);
        parent.keys.insert(it, key);
        parent.offsets.insert(parent.offsets.begin() + idx + 1, child_id);

        nodes[parent_id] = parent;

        if (parent.keys.size() > MAX_KEYS) {
            split_node(parent_id);
        }
    }
};
