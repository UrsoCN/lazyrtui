#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>

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

class TFTree {
public:
  void update_transform(const std::string &parent, const std::string &child,
                        double tx, double ty, double tz, double rx, double ry,
                        double rz, double rw, double timestamp = 0.0);
  std::map<std::string, std::shared_ptr<TFTreeNode>> get_roots() const;
  std::shared_ptr<TFTreeNode> find_frame(const std::string &frame_id) const;
  void clear();

private:
  mutable std::mutex mutex_;
  std::map<std::string, std::shared_ptr<TFTreeNode>> frames_;
};

} // namespace lazyrtui
