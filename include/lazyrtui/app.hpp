#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "lazyrtui/ros_manager.hpp"
#include "lazyrtui/config_loader.hpp"
#include "lazyrtui/python_plugin_engine.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

namespace lazyrtui {

// ros_manager.hpp is included above for NodeDetail/TopicDetail only;
// ROS2Manager itself remains used via shared_ptr from app.cpp.

// Immutable snapshot of every dataset the FTXUI render path reads. Writers
// (refresh thread, ROS callbacks, UI-thread mutations) publish a fresh
// snapshot under data_mutex_; Render() lambdas read the latest snapshot
// lock-free, so rendering never blocks on the data mutex or graph queries.
struct UiSnapshot {
  std::vector<std::string> nodes_list;
  std::vector<std::string> topics_menu_labels;
  std::vector<std::string> services_list;
  std::vector<std::string> actions_list;
  std::set<std::string> subscribed_topics;
  std::map<std::string, std::vector<std::string>> topic_messages;
  // Cached during refresh so Render() never issues ROS graph queries.
  std::map<std::string, NodeDetail> node_details;    // key: "/ns/name"
  std::map<std::string, TopicDetail> topic_details;  // key: topic name
  std::map<std::string, std::vector<std::string>> interfaces_tree;  // pkg -> iface list
};

// ConstStringListRef adapter over an atomically-published immutable list, so
// FTXUI Menu components render lock-free from the UI thread.
class SnapshotStringList : public ftxui::ConstStringListRef::Adapter {
 public:
  size_t size() const override {
    auto list = std::atomic_load(&list_);
    return list ? list->size() : 0;
  }
  std::string_view operator[](size_t i) const override {
    auto list = std::atomic_load(&list_);
    if (list && i < list->size()) {
      scratch_ = (*list)[i];  // Copy while `list_` still holds the data alive.
      return scratch_;
    }
    return "";
  }
  void publish(std::shared_ptr<const std::vector<std::string>> list) {
    std::atomic_store(&list_, std::move(list));
  }

 private:
  std::shared_ptr<const std::vector<std::string>> list_;
  // Only touched from the UI thread inside operator[] (Menu renders
  // synchronously), so a single scratch slot is safe against concurrent
  // publish() calls.
  mutable std::string scratch_;
};

class LazyRTUIApp {
public:
  LazyRTUIApp(std::shared_ptr<ROS2Manager> ros_mgr, const Config &config);
  ~LazyRTUIApp();
  void run();

  PythonPluginEngine *python_plugin_engine() {
    return python_plugin_engine_.get();
  }

private:
  // Tab component builders
  ftxui::Component make_nodes_tab();
  ftxui::Component make_topics_tab();
  ftxui::Component make_services_tab();
  ftxui::Component make_actions_tab();
  ftxui::Component make_interfaces_tab();
  ftxui::Component make_bags_tab();
  ftxui::Component make_tf_tab();
  ftxui::Component make_about_tab();

  void toggle_topic_subscription(int index);

  // UI state
  int selected_tab_ = 0;
  std::vector<std::string> tab_names_;
  bool show_help_ = false;

  // Data refresh
  void refresh_data();
  void start_refresh_timer();
  void stop_refresh_timer();
  // Copies the working datasets into a new immutable snapshot and publishes
  // it plus the menu lists. Call while holding data_mutex_.
  void publish_snapshot();

  std::shared_ptr<ROS2Manager> ros_mgr_;
  Config config_;

  // Screen pointer for PostEvent
  ftxui::ScreenInteractive *screen_ = nullptr;

  // Refresh timer
  std::atomic<bool> refresh_running_{false};
  std::mutex refresh_mutex_;            // Guards refresh_cv_/refresh_running_.
  std::condition_variable refresh_cv_;  // Interruptible sleep for the refresh loop.
  std::unique_ptr<std::thread> refresh_thread_;

  // Double-buffered UI data: writers mutate working copies under
  // data_mutex_, then publish an immutable snapshot Render() reads lock-free.
  std::shared_ptr<const UiSnapshot> ui_snapshot_;  // atomically published

  // Menu adapters (bound once at construction; read snapshots lock-free).
  std::shared_ptr<SnapshotStringList> nodes_menu_;
  std::shared_ptr<SnapshotStringList> topics_menu_;
  std::shared_ptr<SnapshotStringList> services_menu_;
  std::shared_ptr<SnapshotStringList> actions_menu_;
  std::shared_ptr<SnapshotStringList> interfaces_pkg_menu_;
  std::shared_ptr<SnapshotStringList> interfaces_item_menu_;

  // Data cached for UI
  mutable std::mutex data_mutex_;
  std::vector<std::string> nodes_list_;
  std::vector<std::string> topics_list_;
  std::vector<std::string> topics_menu_labels_;
  std::vector<std::string> services_list_;
  std::vector<std::string> actions_list_;
  std::map<std::string, NodeDetail> node_details_;    // key: "/ns/name"
  std::map<std::string, TopicDetail> topic_details_;  // key: topic name
  std::map<std::string, std::vector<std::string>> interfaces_tree_;  // working copy

  // Node tab state
  int selected_node_ = 0;
  int node_pane_focus_ = 0; // 0=left, 1=right

  // Topic tab state (Multi-topic subscription support)
  int selected_topic_ = 0;
  int topic_pane_focus_ = 0;
  std::set<std::string> subscribed_topics_;
  std::map<std::string, std::vector<std::string>> topic_messages_map_;

  // Service tab state
  int selected_service_ = 0;
  int service_pane_focus_ = 0;
  std::string service_request_json_ = "{}";
  std::string service_response_;

  // Action tab state
  int selected_action_ = 0;
  int action_pane_focus_ = 0;
  std::string action_goal_json_ = "{}";
  std::string action_response_;

  // Interfaces tab state
  int selected_interface_pkg_ = 0;
  int selected_interface_item_ = 0;
  int interface_pane_focus_ = 0;
  std::string interface_detail_;  // UI-thread only (fetched on selection)

  // TF tab state
  int selected_tf_ = 0;
  int tf_pane_focus_ = 0;

  // Python Topic Plugin Engine
  std::unique_ptr<PythonPluginEngine> python_plugin_engine_;
};

} // namespace lazyrtui
