#include "lazyrtui/tf_tree.hpp"
#include <gtest/gtest.h>

namespace lazyrtui {

TEST(TFTreeTest, CreateFramesAndRoots) {
  TFTree tree;
  tree.update_transform("", "base_link", 1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 1.0);
  tree.update_transform("base_link", "laser_link", 0.1, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0);
  auto roots = tree.get_roots();
  // A frame whose parent_id is empty counts as a root. update_transform("", ...)
  // creates the literal "" frame AND gives base_link parent_id "" — so both
  // are roots (the real-TF "root frame publishes with header.frame_id == \"\"" case).
  ASSERT_EQ(roots.size(), 2u);
  EXPECT_EQ(roots.count(""), 1u);
  EXPECT_EQ(roots.count("base_link"), 1u);
  EXPECT_EQ(roots[""]->children.count("base_link"), 1u);
  EXPECT_EQ(roots["base_link"]->children.count("laser_link"), 1u);
}

TEST(TFTreeTest, ReparentingRemovesOldParentChild) {
  TFTree tree;
  tree.update_transform("a", "b", 0, 0, 0, 0, 0, 0, 1);
  tree.update_transform("c", "b", 0, 0, 0, 0, 0, 0, 1);
  auto roots = tree.get_roots();
  ASSERT_EQ(roots.size(), 2u);  // "a" and "c" both become roots.
  EXPECT_EQ(roots["a"]->children.count("b"), 0u);  // Issue #7: old parent cleaned.
  EXPECT_EQ(roots["c"]->children.count("b"), 1u);
}

TEST(TFTreeTest, SnapshotDeepCopiesWithTimestamps) {
  TFTree tree;
  tree.update_transform("", "base_link", 1, 2, 3, 0, 0, 0, 1, 100.0);
  tree.update_transform("base_link", "laser", 0, 0, 0.5, 0, 0, 0, 1, 200.0);
  auto snap = tree.snapshot();
  // Same root semantics as get_roots(): the literal "" frame and base_link.
  ASSERT_EQ(snap.roots.size(), 2u);
  const TFSnapshotNode *root_node = nullptr;
  for (const auto &r : snap.roots) {
    if (r.frame_id == "") {
      root_node = &r;
      break;
    }
  }
  ASSERT_NE(root_node, nullptr);
  ASSERT_EQ(root_node->children.size(), 1u);
  const auto &child = root_node->children[0];
  EXPECT_EQ(child.frame_id, "base_link");
  EXPECT_EQ(child.tx, 1.0);
  EXPECT_EQ(child.last_update, 100.0);
  ASSERT_EQ(child.children.size(), 1u);
  EXPECT_EQ(child.children[0].frame_id, "laser");
  EXPECT_EQ(child.children[0].last_update, 200.0);
}

TEST(TFTreeTest, ClearRemovesAll) {
  TFTree tree;
  tree.update_transform("a", "b", 0, 0, 0, 0, 0, 0, 1);
  tree.clear();
  EXPECT_TRUE(tree.get_roots().empty());
}

}  // namespace lazyrtui
