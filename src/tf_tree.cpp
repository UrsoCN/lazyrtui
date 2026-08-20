#include "lazyrtui/tf_tree.hpp"

#include <functional>
#include <set>

namespace lazyrtui {

bool TFTree::update_transform(const std::string &parent,
                              const std::string &child, double tx, double ty,
                              double tz, double rx, double ry, double rz,
                              double rw, double timestamp) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Cycle detection:
  // 1. Direct self-loop (parent == child with non-empty frame)
  if (parent == child && !parent.empty()) {
    return false;
  }

  // 2. Ancestor cycle: check if `parent` is currently a descendant of `child`.
  // If `child` is already in the ancestor chain of `parent`, making `parent`
  // the parent of `child` would create a cycle (child -> ... -> parent -> child).
  if (!parent.empty() && !child.empty()) {
    std::string ancestor = parent;
    std::set<std::string> seen;
    while (!ancestor.empty() && seen.insert(ancestor).second) {
      if (ancestor == child) {
        return false; // Cycle rejected!
      }
      auto it = frames_.find(ancestor);
      if (it == frames_.end()) {
        break;
      }
      ancestor = it->second->parent_id;
    }
  }

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
  // stale branch pointers don't survive a parent change. The previous parent
  // frame may not exist yet (a frame created as a parent defaults to an empty
  // parent_id) — the lookup guards that case. This also cleans re-parenting
  // away from the literal "" root frame (parent_id ""), which the old
  // !parent_id.empty() guard skipped, leaving a duplicated subtree in
  // snapshots.
  if (child_node->parent_id != parent) {
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
  return true;
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

TFSnapshot TFTree::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  TFSnapshot out;
  // Cycle guard: update_transform permits loops.
  std::set<std::string> visited;
  std::function<void(const std::shared_ptr<TFTreeNode> &, TFSnapshotNode &,
                     int)>
      copy_node;
  copy_node = [&](const std::shared_ptr<TFTreeNode> &src, TFSnapshotNode &dst,
                  int depth) {
    if (!src || depth > 64 || !visited.insert(src->frame_id).second) {
      return;
    }
    dst.frame_id = src->frame_id;
    dst.parent_id = src->parent_id;
    dst.tx = src->translation.x;
    dst.ty = src->translation.y;
    dst.tz = src->translation.z;
    dst.rx = src->rotation.x;
    dst.ry = src->rotation.y;
    dst.rz = src->rotation.z;
    dst.rw = src->rotation.w;
    dst.last_update = src->last_update;
    for (const auto &[child_id, child] : src->children) {
      (void)child_id;
      dst.children.emplace_back();
      copy_node(child, dst.children.back(), depth + 1);
    }
  };
  for (const auto &[frame_id, node] : frames_) {
    (void)frame_id;
    if (node->parent_id.empty()) {  // Roots: frames without a parent.
      out.roots.emplace_back();
      copy_node(node, out.roots.back(), 0);
    }
  }
  return out;
}

void TFTree::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  frames_.clear();
}

} // namespace lazyrtui
