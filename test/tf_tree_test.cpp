#include "lazyrtui/tf_tree.hpp"
#include <gtest/gtest.h>
#include <string>

namespace lazyrtui {

TEST(TFTreeTest, CreateFramesAndRoots) {
  TFTree tree;
  EXPECT_TRUE(tree.update_transform("", "base_link", 1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 1.0));
  EXPECT_TRUE(tree.update_transform("base_link", "laser_link", 0.1, 0.0, 0.0, 0.0, 0.0, 0.0,
                                    1.0));
  auto roots = tree.get_roots();
  // With header.frame_id == "", base_link is the single root frame.
  ASSERT_EQ(roots.size(), 1u);
  EXPECT_EQ(roots.count("base_link"), 1u);
  EXPECT_EQ(roots.count(""), 0u);
  EXPECT_EQ(roots["base_link"]->children.count("laser_link"), 1u);

  auto snap = tree.snapshot();
  ASSERT_EQ(snap.roots.size(), 1u);
  EXPECT_EQ(snap.roots[0].frame_id, "base_link");
  ASSERT_EQ(snap.roots[0].children.size(), 1u);
  EXPECT_EQ(snap.roots[0].children[0].frame_id, "laser_link");
}

TEST(TFTreeTest, ReparentingRemovesOldParentChild) {
  TFTree tree;
  EXPECT_TRUE(tree.update_transform("a", "b", 0, 0, 0, 0, 0, 0, 1));
  EXPECT_TRUE(tree.update_transform("c", "b", 0, 0, 0, 0, 0, 0, 1));
  auto roots = tree.get_roots();
  ASSERT_EQ(roots.size(), 2u);  // "a" and "c" both become roots.
  EXPECT_EQ(roots["a"]->children.count("b"), 0u);
  EXPECT_EQ(roots["c"]->children.count("b"), 1u);
}

TEST(TFTreeTest, ReparentToEmptyRootCleansOldParent) {
  TFTree tree;
  EXPECT_TRUE(tree.update_transform("a", "b", 0, 0, 0, 0, 0, 0, 1));
  EXPECT_TRUE(tree.update_transform("", "b", 0, 0, 0, 0, 0, 0, 1));
  auto roots = tree.get_roots();
  // "a" and "b" are both roots. "b" is detached from "a".
  ASSERT_EQ(roots.size(), 2u);
  EXPECT_EQ(roots["a"]->children.count("b"), 0u);
  EXPECT_EQ(roots.count("b"), 1u);
  EXPECT_EQ(roots["b"]->parent_id, "");
}

TEST(TFTreeTest, EmptyChildFrameRejected) {
  TFTree tree;
  EXPECT_FALSE(tree.update_transform("base", "", 0, 0, 0, 0, 0, 0, 1));
  EXPECT_FALSE(tree.update_transform("", "", 0, 0, 0, 0, 0, 0, 1));
  EXPECT_TRUE(tree.get_roots().empty());
}

TEST(TFTreeTest, SelfParentingRejected) {
  TFTree tree;
  EXPECT_FALSE(tree.update_transform("a", "a", 0, 0, 0, 0, 0, 0, 1));
  EXPECT_TRUE(tree.get_roots().empty());
  EXPECT_EQ(tree.find_frame("a"), nullptr);
  auto snap = tree.snapshot();
  EXPECT_TRUE(snap.roots.empty());
}

TEST(TFTreeTest, UpdatingTransformPreservesChildren) {
  TFTree tree;
  EXPECT_TRUE(tree.update_transform("", "a", 1, 0, 0, 0, 0, 0, 1, 100));
  EXPECT_TRUE(tree.update_transform("a", "b", 0, 0, 0, 0, 0, 0, 1, 200));
  EXPECT_TRUE(tree.update_transform("", "a", 5, 0, 0, 0, 0, 0, 1, 300));
  auto roots = tree.get_roots();
  ASSERT_EQ(roots.size(), 1u);
  ASSERT_EQ(roots.count("a"), 1u);
  auto a = roots["a"];
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->translation.x, 5.0);
  EXPECT_EQ(a->last_update, 300.0);
  ASSERT_EQ(a->children.count("b"), 1u);
  EXPECT_EQ(a->children["b"]->translation.x, 0.0);
}

TEST(TFTreeTest, RepeatedUpdateRefreshesTimestamp) {
  TFTree tree;
  EXPECT_TRUE(tree.update_transform("", "a", 0, 0, 0, 0, 0, 0, 1, 100));
  EXPECT_TRUE(tree.update_transform("", "a", 0, 0, 0, 0, 0, 0, 1, 250));
  auto f = tree.find_frame("a");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->last_update, 250.0);
}

TEST(TFTreeTest, FindFrameReturnsKnownOrNull) {
  TFTree tree;
  EXPECT_TRUE(tree.update_transform("", "base_link", 1, 0, 0, 0, 0, 0, 1));
  auto f = tree.find_frame("base_link");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->frame_id, "base_link");
  EXPECT_EQ(f->parent_id, "");
  EXPECT_EQ(tree.find_frame("missing"), nullptr);
}

TEST(TFTreeTest, SnapshotDeepCopiesWithTimestamps) {
  TFTree tree;
  EXPECT_TRUE(tree.update_transform("", "base_link", 1, 2, 3, 0.1, 0.2, 0.3, 0.9, 100.0));
  EXPECT_TRUE(tree.update_transform("base_link", "laser", 0, 0, 0.5, 0, 0, 0, 1, 200.0));
  auto snap = tree.snapshot();
  ASSERT_EQ(snap.roots.size(), 1u);
  const auto &root_node = snap.roots[0];
  EXPECT_EQ(root_node.frame_id, "base_link");
  EXPECT_EQ(root_node.tx, 1.0);
  EXPECT_EQ(root_node.ty, 2.0);
  EXPECT_EQ(root_node.tz, 3.0);
  EXPECT_EQ(root_node.rx, 0.1);
  EXPECT_EQ(root_node.ry, 0.2);
  EXPECT_EQ(root_node.rz, 0.3);
  EXPECT_EQ(root_node.rw, 0.9);
  EXPECT_EQ(root_node.last_update, 100.0);
  ASSERT_EQ(root_node.children.size(), 1u);
  EXPECT_EQ(root_node.children[0].frame_id, "laser");
  EXPECT_EQ(root_node.children[0].last_update, 200.0);
}

TEST(TFTreeTest, SnapshotIsDeepCopyIndependentOfLiveTree) {
  TFTree tree;
  EXPECT_TRUE(tree.update_transform("", "a", 1, 0, 0, 0, 0, 0, 1, 100));
  auto snap = tree.snapshot();
  EXPECT_TRUE(tree.update_transform("a", "b", 0, 0, 0, 0, 0, 0, 1, 200));
  ASSERT_EQ(snap.roots.size(), 1u);
  EXPECT_EQ(snap.roots[0].frame_id, "a");
  EXPECT_TRUE(snap.roots[0].children.empty());  // No "b" in old snap.

  auto snap2 = tree.snapshot();
  ASSERT_EQ(snap2.roots.size(), 1u);
  EXPECT_EQ(snap2.roots[0].frame_id, "a");
  ASSERT_EQ(snap2.roots[0].children.size(), 1u);
  EXPECT_EQ(snap2.roots[0].children[0].frame_id, "b");
}

TEST(TFTreeTest, SnapshotTruncatesAtDepthCap) {
  TFTree tree;
  EXPECT_TRUE(tree.update_transform("", "n0", 0, 0, 0, 0, 0, 0, 1));
  for (int i = 1; i <= 70; ++i) {
    EXPECT_TRUE(tree.update_transform("n" + std::to_string(i - 1),
                                      "n" + std::to_string(i), 0, 0, 0, 0, 0, 0, 1));
  }
  auto snap = tree.snapshot();  // Must terminate; depth cap is 64.
  ASSERT_EQ(snap.roots.size(), 1u);
  EXPECT_EQ(snap.roots[0].frame_id, "n0");
  int depth = 0;
  const TFSnapshotNode *cur = &snap.roots[0];
  while (cur->children.size() == 1 && depth < 100) {
    cur = &cur->children[0];
    ++depth;
  }
  EXPECT_EQ(depth, 65);
  EXPECT_TRUE(cur->frame_id.empty());
  EXPECT_TRUE(cur->children.empty());
}

TEST(TFTreeTest, DirectTwoNodeCycleRejection) {
  TFTree tree;
  EXPECT_TRUE(tree.update_transform("a", "b", 1, 0, 0, 0, 0, 0, 1));
  EXPECT_FALSE(tree.update_transform("b", "a", 2, 0, 0, 0, 0, 0, 1));

  auto a = tree.find_frame("a");
  auto b = tree.find_frame("b");
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(a->parent_id, "");
  EXPECT_EQ(b->parent_id, "a");
  EXPECT_EQ(a->children.count("b"), 1u);
  EXPECT_EQ(b->children.count("a"), 0u);
}

TEST(TFTreeTest, MultiHopCycleRejection) {
  TFTree tree;
  EXPECT_TRUE(tree.update_transform("a", "b", 0, 0, 0, 0, 0, 0, 1));
  EXPECT_TRUE(tree.update_transform("b", "c", 0, 0, 0, 0, 0, 0, 1));
  EXPECT_TRUE(tree.update_transform("c", "d", 0, 0, 0, 0, 0, 0, 1));

  // Any attempt to make an ancestor the child of a descendant must be rejected.
  EXPECT_FALSE(tree.update_transform("d", "a", 0, 0, 0, 0, 0, 0, 1));
  EXPECT_FALSE(tree.update_transform("c", "a", 0, 0, 0, 0, 0, 0, 1));
  EXPECT_FALSE(tree.update_transform("d", "b", 0, 0, 0, 0, 0, 0, 1));

  // Non-cyclic extensions should succeed.
  EXPECT_TRUE(tree.update_transform("d", "e", 0, 0, 0, 0, 0, 0, 1));
  EXPECT_EQ(tree.find_frame("e")->parent_id, "d");
}

TEST(TFTreeTest, ClearRemovesAll) {
  TFTree tree;
  EXPECT_TRUE(tree.update_transform("a", "b", 0, 0, 0, 0, 0, 0, 1));
  tree.clear();
  EXPECT_TRUE(tree.get_roots().empty());
}

}  // namespace lazyrtui
