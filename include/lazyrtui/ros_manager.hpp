#pragma once
#include "lazyrtui/tf_tree.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lazyrtui {

struct NodeInfo {
  std::string name;
  std::string ns;
};

struct TopicInfo {
  std::string name;
  std::vector<std::string> types;
};

struct ServiceInfo {
  std::string name;
  std::vector<std::string> types;
};

struct ActionInfo {
  std::string name;
  std::vector<std::string> types;
};

struct NodeDetail {
  std::string name;
  std::string ns;
  std::vector<std::pair<std::string, std::string>> publishers;  // (topic, type)
  std::vector<std::pair<std::string, std::string>> subscribers; // (topic, type)
  std::vector<std::pair<std::string, std::string>> services; // (service, type)
};

struct TopicDetail {
  std::string name;
  std::string type;
  int publisher_count = 0;
  int subscriber_count = 0;
};

using TopicCallback = std::function<void(const std::string &topic,
                                         const std::string &serialized_msg)>;
using ServiceCallback = std::function<void(
    bool success, const std::string &response_json, double elapsed_ms)>;
using ActionFeedbackCallback = std::function<void(
    const std::string &feedback_json)>;
using ActionResultCallback = std::function<void(
    bool success, int8_t status, const std::string &result_json, double elapsed_ms)>;
using ActionCancelCallback = std::function<void(
    bool success, const std::string &response_json)>;

class ROS2Manager {
public:
  ROS2Manager(const std::string &node_name = "lazy_rtui_node");
  ~ROS2Manager();

  bool start(int argc = 0, char **argv = nullptr);
  void stop();
  bool is_connected() const;

  // Topology discovery
  std::vector<NodeInfo> get_nodes();
  std::vector<TopicInfo> get_topics();
  std::vector<ServiceInfo> get_services();
  std::vector<ActionInfo> get_actions();

  // Detail queries
  NodeDetail get_node_info(const std::string &name, const std::string &ns);
  TopicDetail get_topic_info(const std::string &topic_name);
  std::string get_service_request_json(const std::string &service_name,
                                       const std::string &type_str);
  std::string get_action_goal_json(const std::string &action_name,
                                   const std::string &type_str);

  // Dynamic subscription
  bool subscribe_topic(const std::string &topic, const std::string &type_str,
                       TopicCallback callback);
  void unsubscribe_topic(const std::string &topic);
  bool is_topic_subscribed(const std::string &topic) const;

  // Service call
  void call_service_async(const std::string &service_name,
                          const std::string &type_str,
                          const std::string &request_json,
                          ServiceCallback callback);

  // Action goal
  void send_action_goal_async(const std::string &action_name,
                              const std::string &type_str,
                              const std::string &goal_json,
                              ActionFeedbackCallback feedback_cb,
                              ActionResultCallback result_cb);
  void cancel_action_goal_async(const std::string &action_name,
                                ActionCancelCallback callback = nullptr);

  // Interface tree
  std::map<std::string, std::vector<std::string>> get_interfaces_tree();
  std::string get_interface_detail(const std::string &type_str);

  // TF
  TFTree &get_tf_tree();

private:
  void spin_loop();
  void setup_tf_subscribers();
  void execute_service_call(const std::string& service_name, const std::string& type_str,
                            const std::string& request_json, ServiceCallback callback);
  std::string serialize_msg_to_json(/* generic msg */) const;

  std::string node_name_;
  std::atomic<bool> connected_{false};
  std::atomic<bool> stop_requested_{false};
  std::unique_ptr<std::thread> spin_thread_;
  TFTree tf_tree_;

  mutable std::mutex mutex_;
  // ROS 2 node and subscriptions stored as void* / shared_ptr to avoid
  // requiring rclcpp headers in this header. We use PIMPL-like approach.
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace lazyrtui
