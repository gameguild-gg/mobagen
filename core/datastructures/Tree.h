#ifndef MOBAGEN_TREE_H
#define MOBAGEN_TREE_H
#include "concepts.h"
#include <vector>

// binary tree
template <Sortable T> struct BinaryTree {
  struct Node {
    T value;
    Node* left = nullptr;
    Node* right = nullptr;
    explicit Node(T value) : value(value) {}
    Node(T value, Node* left, Node* right) : value(value), left(left), right(right) {}

    ~Node() {
      if (left) delete left;
      if (right) delete right;
    }
  };

  Node* root = nullptr;

  void Add(T value) {
    if (!root) {
      root = new Node(value);
      return;
    }
    Node* current = root;
    while (true) {
      if (value < current->value) {
        if (current->left != nullptr) {
          current = current->left;
        } else {
          current->left = new Node(value);
          break;
        }
      } else {
        if (current->right != nullptr) {
          current = current->right;
        } else {
          current->right = new Node(value);
          break;
        }
      }
    }
  }
};

#endif  // MOBAGEN_TREE_H
