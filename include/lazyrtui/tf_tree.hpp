#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace lazyrtui {

struct TFTreeNode {
  std::string frame_id;
  std::string parent_id;
  std::map<std::string, std::shared_ptr<TFTreeNode>> children;
  struct {
    double x = 0, y = 0, z = 0;
  } translation;
  struct {
    double x = 0, y = 0, z = 0, w = 1;
  } rotation;
  double last_update = 0.0;
};

// Render-safe value snapshot of the frame tree: a deep copy taken under the
// tree mutex, safe to traverse without locking (the executor thread may keep
// mutating the live tree).
struct TFSnapshotNode {
  std::string frame_id;
  std::string parent_id;
  double tx = 0, ty = 0, tz = 0;
  double rx = 0, ry = 0, rz = 0, rw = 1;
  double last_update = 0.0;
  std::vector<TFSnapshotNode> children;
};

struct TFSnapshot {
  std::vector<TFSnapshotNode> roots;
};

class TFTree {
public:
  void update_transform(const std::string &parent, const std::string &child,
                        double tx, double ty, double tz, double rx, double ry,
                        double rz, double rw, double timestamp = 0.0);
  std::map<std::string, std::shared_ptr<TFTreeNode>> get_roots() const;
  std::shared_ptr<TFTreeNode> find_frame(const std::string &frame_id) const;
  TFSnapshot snapshot() const;
  void clear();

private:
  mutable std::mutex mutex_;
  std::map<std::string, std::shared_ptr<TFTreeNode>> frames_;
};

} // namespace lazyrtui
