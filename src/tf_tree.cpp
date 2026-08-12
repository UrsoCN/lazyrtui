#include "lazyrtui/tf_tree.hpp"

namespace lazyrtui {

void TFTree::update_transform(const std::string &parent,
                              const std::string &child, double tx, double ty,
                              double tz, double rx, double ry, double rz,
                              double rw, double timestamp) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Get or create parent
  auto parent_it = frames_.find(parent);
  if (parent_it == frames_.end()) {
    auto new_parent = std::make_shared<TFTreeNode>();
    new_parent->frame_id = parent;
    parent_it = frames_.emplace(parent, new_parent).first;
  }

  // Get or create child
  auto child_it = frames_.find(child);
  if (child_it == frames_.end()) {
    auto new_child = std::make_shared<TFTreeNode>();
    new_child->frame_id = child;
    child_it = frames_.emplace(child, new_child).first;
  }

  auto &child_node = child_it->second;
  // Re-parenting: remove the child from its previous parent's children map so
  // stale branch pointers don't survive a parent change.
  if (!child_node->parent_id.empty() && child_node->parent_id != parent) {
    auto old_parent_it = frames_.find(child_node->parent_id);
    if (old_parent_it != frames_.end()) {
      old_parent_it->second->children.erase(child);
    }
  }
  child_node->parent_id = parent;
  child_node->translation.x = tx;
  child_node->translation.y = ty;
  child_node->translation.z = tz;
  child_node->rotation.x = rx;
  child_node->rotation.y = ry;
  child_node->rotation.z = rz;
  child_node->rotation.w = rw;
  child_node->last_update = timestamp;

  // Add child to parent's children map
  parent_it->second->children[child] = child_node;
}

std::map<std::string, std::shared_ptr<TFTreeNode>> TFTree::get_roots() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::map<std::string, std::shared_ptr<TFTreeNode>> roots;
  for (const auto &[frame_id, node] : frames_) {
    if (node->parent_id.empty()) {
      roots[frame_id] = node;
    }
  }
  return roots;
}

std::shared_ptr<TFTreeNode>
TFTree::find_frame(const std::string &frame_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = frames_.find(frame_id);
  if (it != frames_.end()) {
    return it->second;
  }
  return nullptr;
}

void TFTree::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  frames_.clear();
}

} // namespace lazyrtui
