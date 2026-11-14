#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace bbtree {

// An in-memory B-tree ordered map. MinDegree controls the minimum number of
// children in a non-root internal node; each node holds at most 2*T-1 entries.
// References returned by get are invalidated by any mutation.
template <class Key, class Value, std::size_t MinDegree = 16,
          class Compare = std::less<Key>>
class BTreeMap {
  static_assert(MinDegree >= 2, "B-tree minimum degree must be at least two");

  using Entry = std::pair<Key, Value>;

  struct Node {
    explicit Node(bool is_leaf) : leaf(is_leaf) {
      entries.reserve(2 * MinDegree - 1);
      if (!leaf) children.reserve(2 * MinDegree);
    }

    bool leaf;
    std::vector<Entry> entries;
    std::vector<std::unique_ptr<Node>> children;
  };

 public:
  BTreeMap() = default;
  explicit BTreeMap(Compare compare) : compare_(std::move(compare)) {}

  BTreeMap(const BTreeMap&) = delete;
  BTreeMap& operator=(const BTreeMap&) = delete;
  BTreeMap(BTreeMap&&) = default;
  BTreeMap& operator=(BTreeMap&&) = default;

  std::size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  const Value* get(const Key& key) const {
    auto* node = root_.get();
    while (node) {
      const auto index = position(*node, key);
      if (index < node->entries.size() && equal(node->entries[index].first, key)) {
        return &node->entries[index].second;
      }
      if (node->leaf) return nullptr;
      node = node->children[index].get();
    }
    return nullptr;
  }

  Value* get_mut(const Key& key) {
    return const_cast<Value*>(std::as_const(*this).get(key));
  }

  // Returns true for a new key, false when an existing value was replaced.
  bool insert_or_assign(Key key, Value value) {
    if (!root_) root_ = std::make_unique<Node>(true);
    if (root_->entries.size() == max_keys()) {
      auto new_root = std::make_unique<Node>(false);
      new_root->children.push_back(std::move(root_));
      split_child(*new_root, 0);
      root_ = std::move(new_root);
    }

    auto* node = root_.get();
    while (true) {
      auto index = position(*node, key);
      if (index < node->entries.size() && equal(node->entries[index].first, key)) {
        node->entries[index].second = std::move(value);
        return false;
      }
      if (node->leaf) {
        node->entries.insert(node->entries.begin() + index,
                             Entry{std::move(key), std::move(value)});
        ++size_;
        return true;
      }
      if (node->children[index]->entries.size() == max_keys()) {
        split_child(*node, index);
        if (equal(node->entries[index].first, key)) {
          node->entries[index].second = std::move(value);
          return false;
        }
        if (compare_(node->entries[index].first, key)) ++index;
      }
      node = node->children[index].get();
    }
  }

  bool erase(const Key& key) {
    if (!root_) return false;
    const auto removed = erase_from(*root_, key);
    if (removed) --size_;
    if (root_->entries.empty()) {
      if (root_->leaf) root_.reset();
      else root_ = std::move(root_->children.front());
    }
    return removed;
  }

  template <class Visitor>
  void for_each(Visitor&& visitor) const {
    if (root_) visit(*root_, visitor);
  }

  // Visits at most limit entries, starting at the first key >= start.
  template <class Visitor>
  void for_each_from(const Key& start, std::size_t limit, Visitor&& visitor) const {
    if (root_ && limit != 0) visit_from(*root_, start, limit, visitor);
  }

 private:
  static constexpr std::size_t max_keys() { return 2 * MinDegree - 1; }

  bool equal(const Key& left, const Key& right) const {
    return !compare_(left, right) && !compare_(right, left);
  }

  std::size_t position(const Node& node, const Key& key) const {
    const auto it = std::lower_bound(
        node.entries.begin(), node.entries.end(), key,
        [this](const Entry& entry, const Key& target) {
          return compare_(entry.first, target);
        });
    return static_cast<std::size_t>(it - node.entries.begin());
  }

  void split_child(Node& parent, std::size_t index) {
    auto& left = *parent.children[index];
    auto right = std::make_unique<Node>(left.leaf);
    right->entries.insert(right->entries.end(),
                          std::make_move_iterator(left.entries.begin() + MinDegree),
                          std::make_move_iterator(left.entries.end()));
    auto middle = std::move(left.entries[MinDegree - 1]);
    left.entries.erase(left.entries.begin() + MinDegree - 1, left.entries.end());
    if (!left.leaf) {
      right->children.insert(right->children.end(),
                             std::make_move_iterator(left.children.begin() + MinDegree),
                             std::make_move_iterator(left.children.end()));
      left.children.resize(MinDegree);
    }
    parent.entries.insert(parent.entries.begin() + index, std::move(middle));
    parent.children.insert(parent.children.begin() + index + 1, std::move(right));
  }

  void merge_children(Node& parent, std::size_t index) {
    auto& left = *parent.children[index];
    auto& right = *parent.children[index + 1];
    left.entries.push_back(std::move(parent.entries[index]));
    left.entries.insert(left.entries.end(),
                        std::make_move_iterator(right.entries.begin()),
                        std::make_move_iterator(right.entries.end()));
    if (!left.leaf) {
      left.children.insert(left.children.end(),
                           std::make_move_iterator(right.children.begin()),
                           std::make_move_iterator(right.children.end()));
    }
    parent.entries.erase(parent.entries.begin() + index);
    parent.children.erase(parent.children.begin() + index + 1);
  }

  void borrow_from_left(Node& parent, std::size_t index) {
    auto& child = *parent.children[index];
    auto& sibling = *parent.children[index - 1];
    child.entries.insert(child.entries.begin(), std::move(parent.entries[index - 1]));
    parent.entries[index - 1] = std::move(sibling.entries.back());
    sibling.entries.pop_back();
    if (!child.leaf) {
      child.children.insert(child.children.begin(), std::move(sibling.children.back()));
      sibling.children.pop_back();
    }
  }

  void borrow_from_right(Node& parent, std::size_t index) {
    auto& child = *parent.children[index];
    auto& sibling = *parent.children[index + 1];
    child.entries.push_back(std::move(parent.entries[index]));
    parent.entries[index] = std::move(sibling.entries.front());
    sibling.entries.erase(sibling.entries.begin());
    if (!child.leaf) {
      child.children.push_back(std::move(sibling.children.front()));
      sibling.children.erase(sibling.children.begin());
    }
  }

  bool erase_from(Node& node, const Key& key) {
    auto index = position(node, key);
    if (index < node.entries.size() && equal(node.entries[index].first, key)) {
      if (node.leaf) {
        node.entries.erase(node.entries.begin() + index);
        return true;
      }
      auto& left = *node.children[index];
      auto& right = *node.children[index + 1];
      if (left.entries.size() >= MinDegree) {
        auto* predecessor = &left;
        while (!predecessor->leaf) predecessor = predecessor->children.back().get();
        auto replacement = predecessor->entries.back();
        node.entries[index] = replacement;
        return erase_from(left, replacement.first);
      }
      if (right.entries.size() >= MinDegree) {
        auto* successor = &right;
        while (!successor->leaf) successor = successor->children.front().get();
        auto replacement = successor->entries.front();
        node.entries[index] = replacement;
        return erase_from(right, replacement.first);
      }
      merge_children(node, index);
      return erase_from(*node.children[index], key);
    }
    if (node.leaf) return false;

    if (node.children[index]->entries.size() == MinDegree - 1) {
      if (index > 0 && node.children[index - 1]->entries.size() >= MinDegree) {
        borrow_from_left(node, index);
      } else if (index < node.entries.size() &&
                 node.children[index + 1]->entries.size() >= MinDegree) {
        borrow_from_right(node, index);
      } else if (index < node.entries.size()) {
        merge_children(node, index);
      } else {
        merge_children(node, index - 1);
        --index;
      }
    }
    return erase_from(*node.children[index], key);
  }

  template <class Visitor>
  static void visit(const Node& node, Visitor& visitor) {
    for (std::size_t index = 0; index < node.entries.size(); ++index) {
      if (!node.leaf) visit(*node.children[index], visitor);
      visitor(node.entries[index].first, node.entries[index].second);
    }
    if (!node.leaf) visit(*node.children.back(), visitor);
  }

  template <class Visitor>
  static void visit_limited(const Node& node, std::size_t& remaining,
                            Visitor& visitor) {
    for (std::size_t index = 0; index < node.entries.size() && remaining != 0;
         ++index) {
      if (!node.leaf) visit_limited(*node.children[index], remaining, visitor);
      if (remaining == 0) return;
      visitor(node.entries[index].first, node.entries[index].second);
      --remaining;
    }
    if (!node.leaf && remaining != 0) {
      visit_limited(*node.children.back(), remaining, visitor);
    }
  }

  template <class Visitor>
  void visit_from(const Node& node, const Key& start, std::size_t& remaining,
                  Visitor& visitor) const {
    auto index = position(node, start);
    if (!node.leaf) visit_from(*node.children[index], start, remaining, visitor);
    for (; index < node.entries.size() && remaining != 0; ++index) {
      visitor(node.entries[index].first, node.entries[index].second);
      --remaining;
      if (!node.leaf && remaining != 0) {
        visit_limited(*node.children[index + 1], remaining, visitor);
      }
    }
  }

  std::unique_ptr<Node> root_;
  std::size_t size_ = 0;
  Compare compare_{};
};

}  // namespace bbtree
