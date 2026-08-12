#include "lazyrtui/tf_tree.hpp"
#include <gtest/gtest.h>
#include <string>

namespace lazyrtui {

TEST(TFTreeTest, CreateFramesAndRoots) {
  TFTree tree;
  tree.update_transform("", "base_link", 1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 1.0);
  tree.update_transform("base_link", "laser_link", 0.1, 0.0, 0.0, 0.0, 0.0, 0.0,
                        1.0);
  auto roots = tree.get_roots();
  // A frame whose parent_id is empty counts as a root.
  // update_transform("", ...) creates the literal "" frame AND gives
  // base_link parent_id "" — so both are roots (real-TF "root publishes
  // with header.frame_id == \"\"" case).
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
  // Issue #7: old parent cleaned.
  EXPECT_EQ(roots["a"]->children.count("b"), 0u);
  EXPECT_EQ(roots["c"]->children.count("b"), 1u);
}

TEST(TFTreeTest, ReparentToEmptyRootCleansOldParent) {
  TFTree tree;
  tree.update_transform("a", "b", 0, 0, 0, 0, 0, 0, 1);
  tree.update_transform("", "b", 0, 0, 0, 0, 0, 0, 1);
  auto roots = tree.get_roots();
  // "a", "" and "b" all carry parent_id == "" -> all three are roots (the
  // documented root semantics: update_transform("", X) makes both "" and X
  // root-eligible). "b" being both a root and a child of "" is the known quirk.
  ASSERT_EQ(roots.size(), 3u);
  EXPECT_EQ(roots["a"]->children.count("b"), 0u);  // Old parent cleaned.
  EXPECT_EQ(roots[""]->children.count("b"), 1u);   // New parent owns it.
  EXPECT_EQ(roots[""]->children["b"]->parent_id, "");
}

TEST(TFTreeTest, EmptyChildFrameIsParentedToBase) {
  TFTree tree;
  // Real TF quirk: a publisher may emit header.frame_id == "" with a non-empty
  // parent chain. The "" frame then becomes a normal child, not a root.
  tree.update_transform("base", "", 0, 0, 0, 0, 0, 0, 1);
  auto roots = tree.get_roots();
  ASSERT_EQ(roots.size(), 1u);
  EXPECT_EQ(roots.count("base"), 1u);
  EXPECT_EQ(roots["base"]->children.count(""), 1u);
  EXPECT_EQ(roots["base"]->children[""]->parent_id, "base");
}

TEST(TFTreeTest, SelfParentingOrphansFrame) {
  TFTree tree;
  tree.update_transform("a", "a", 0, 0, 0, 0, 0, 0, 1);
  // parent_id == "a" is non-empty, so "a" is not a root and nothing reaches it.
  EXPECT_TRUE(tree.get_roots().empty());
  auto f = tree.find_frame("a");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->parent_id, "a");
  auto snap = tree.snapshot();  // Must terminate with no roots.
  EXPECT_TRUE(snap.roots.empty());
}

TEST(TFTreeTest, UpdatingTransformPreservesChildren) {
  TFTree tree;
  tree.update_transform("", "a", 1, 0, 0, 0, 0, 0, 1, 100);
  tree.update_transform("a", "b", 0, 0, 0, 0, 0, 0, 1, 200);
  tree.update_transform("", "a", 5, 0, 0, 0, 0, 0, 1, 300);
  auto roots = tree.get_roots();
  ASSERT_EQ(roots.count(""), 1u);
  auto a = roots[""]->children["a"];
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->translation.x, 5.0);
  EXPECT_EQ(a->last_update, 300.0);
  ASSERT_EQ(a->children.count("b"), 1u);  // Child preserved across update.
  EXPECT_EQ(a->children["b"]->translation.x, 0.0);
}

TEST(TFTreeTest, RepeatedUpdateRefreshesTimestamp) {
  TFTree tree;
  tree.update_transform("", "a", 0, 0, 0, 0, 0, 0, 1, 100);
  tree.update_transform("", "a", 0, 0, 0, 0, 0, 0, 1, 250);
  auto f = tree.find_frame("a");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->last_update, 250.0);
}

TEST(TFTreeTest, FindFrameReturnsKnownOrNull) {
  TFTree tree;
  tree.update_transform("", "base_link", 1, 0, 0, 0, 0, 0, 1);
  auto f = tree.find_frame("base_link");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->frame_id, "base_link");
  EXPECT_EQ(f->parent_id, "");
  EXPECT_EQ(tree.find_frame("missing"), nullptr);
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

TEST(TFTreeTest, SnapshotIsDeepCopyIndependentOfLiveTree) {
  TFTree tree;
  tree.update_transform("", "a", 1, 0, 0, 0, 0, 0, 1, 100);
  auto snap = tree.snapshot();
  // Mutate the live tree after snapshotting.
  tree.update_transform("a", "b", 0, 0, 0, 0, 0, 0, 1, 200);
  // "" is a root; "a" (parent_id == "") is also a root but is skipped as an
  // empty stub because the shared visited set already traversed it under "".
  // Navigate the "" root's subtree, which carries the real data.
  ASSERT_EQ(snap.roots.size(), 2u);
  const TFSnapshotNode *root_node = nullptr;
  for (const auto &r : snap.roots) {
    if (r.frame_id == "" && !r.children.empty()) {
      root_node = &r;
      break;
    }
  }
  ASSERT_NE(root_node, nullptr);
  ASSERT_EQ(root_node->children.size(), 1u);
  EXPECT_EQ(root_node->children[0].frame_id, "a");
  EXPECT_TRUE(root_node->children[0].children.empty());  // No "b" in old snap.
  // The live tree now exposes the new branch under the "" root's child "a".
  auto snap2 = tree.snapshot();
  const TFSnapshotNode *root2 = nullptr;
  for (const auto &r : snap2.roots) {
    if (r.frame_id == "" && !r.children.empty()) {
      root2 = &r;
      break;
    }
  }
  ASSERT_NE(root2, nullptr);
  ASSERT_EQ(root2->children.size(), 1u);
  EXPECT_EQ(root2->children[0].frame_id, "a");
  ASSERT_EQ(root2->children[0].children.size(), 1u);
  EXPECT_EQ(root2->children[0].children[0].frame_id, "b");
}

TEST(TFTreeTest, SnapshotTruncatesAtDepthCap) {
  TFTree tree;
  tree.update_transform("", "n0", 0, 0, 0, 0, 0, 0, 1);
  for (int i = 1; i <= 70; ++i) {
    tree.update_transform("n" + std::to_string(i - 1),
                          "n" + std::to_string(i), 0, 0, 0, 0, 0, 0, 1);
  }
  auto snap = tree.snapshot();  // Must terminate; depth cap is 64.
  // "" is a root carrying the full chain; "n0" (parent_id == "") is a root
  // that reduces to an empty stub under the shared visited set. Walk the ""
  // root's chain.
  const TFSnapshotNode *root_node = nullptr;
  for (const auto &r : snap.roots) {
    if (r.frame_id == "" && !r.children.empty()) {
      root_node = &r;
      break;
    }
  }
  ASSERT_NE(root_node, nullptr);
  int depth = 0;
  const TFSnapshotNode *cur = root_node;
  while (cur->children.size() == 1 && depth < 100) {
    cur = &cur->children[0];
    ++depth;
  }
  // Data nodes at depths 0..64 are copied (65 nodes: "" + n0..n63); the
  // depth-65 child (n64) is an empty stub (copy_node returns before filling).
  EXPECT_EQ(depth, 65);
  EXPECT_TRUE(cur->frame_id.empty());
  EXPECT_TRUE(cur->children.empty());
}

TEST(TFTreeTest, SnapshotTerminatesOnCycle) {
  TFTree tree;
  // "" -> a makes a.root-eligible (parent_id == ""). Re-parenting "" under a
  // then forms a root-reachable cycle: a is a root, a.children contains "",
  // "" still lists a as its child.
  tree.update_transform("", "a", 0, 0, 0, 0, 0, 0, 1);
  tree.update_transform("a", "", 0, 0, 0, 0, 0, 0, 1);
  auto snap = tree.snapshot();  // Must terminate (visited set + depth cap).
  ASSERT_FALSE(snap.roots.empty());
  const TFSnapshotNode *a = nullptr;
  for (const auto &r : snap.roots) {
    if (r.frame_id == "a") {
      a = &r;
      break;
    }
  }
  ASSERT_NE(a, nullptr);
  ASSERT_EQ(a->children.size(), 1u);
  EXPECT_EQ(a->children[0].frame_id, "");
  // The back-edge into "a" is blocked by the visited set: the "" node never
  // re-expands a full "a" node (an empty stub may remain in its children).
  for (const auto &c : a->children[0].children) {
    EXPECT_NE(c.frame_id, "a");
  }
}

TEST(TFTreeTest, BackEdgeCycleTerminates) {
  TFTree tree;
  tree.update_transform("", "a", 0, 0, 0, 0, 0, 0, 1);
  tree.update_transform("a", "b", 0, 0, 0, 0, 0, 0, 1);
  tree.update_transform("b", "a", 0, 0, 0, 0, 0, 0, 1);  // a back-edge -> cycle
  auto snap = tree.snapshot();
  ASSERT_EQ(snap.roots.size(), 1u);  // "" is the only root (a/b have parents).
  ASSERT_EQ(snap.roots[0].children.size(), 1u);
  EXPECT_EQ(snap.roots[0].children[0].frame_id, "a");
  ASSERT_EQ(snap.roots[0].children[0].children.size(), 1u);
  EXPECT_EQ(snap.roots[0].children[0].children[0].frame_id, "b");
  // Re-entering "a" from "b" is blocked by the visited set: only an empty stub.
  ASSERT_EQ(snap.roots[0].children[0].children[0].children.size(), 1u);
  EXPECT_TRUE(
      snap.roots[0].children[0].children[0].children[0].frame_id.empty());
}

TEST(TFTreeTest, ClearRemovesAll) {
  TFTree tree;
  tree.update_transform("a", "b", 0, 0, 0, 0, 0, 0, 1);
  tree.clear();
  EXPECT_TRUE(tree.get_roots().empty());
}

}  // namespace lazyrtui
