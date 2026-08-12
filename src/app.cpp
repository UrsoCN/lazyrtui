#include "lazyrtui/app.hpp"
#include "lazyrtui/config_loader.hpp"
#include "lazyrtui/ftxui_converter.hpp"
#include "lazyrtui/python_plugin_engine.hpp"
#include "lazyrtui/ros_manager.hpp"
#include "lazyrtui/tf_tree.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/screen.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

#ifndef LAZYRTUI_VERSION
#define LAZYRTUI_VERSION "0.1.0"
#endif

using namespace ftxui;

namespace lazyrtui {

LazyRTUIApp::LazyRTUIApp(std::shared_ptr<ROS2Manager> ros_mgr,
                         const Config &config)
    : ros_mgr_(std::move(ros_mgr)), config_(config) {
  nodes_menu_ = std::make_shared<SnapshotStringList>();
  topics_menu_ = std::make_shared<SnapshotStringList>();
  services_menu_ = std::make_shared<SnapshotStringList>();
  actions_menu_ = std::make_shared<SnapshotStringList>();
  interfaces_pkg_menu_ = std::make_shared<SnapshotStringList>();
  interfaces_item_menu_ = std::make_shared<SnapshotStringList>();

  tab_names_ = {"1:Nodes",      "2:Topics", "3:Services", "4:Actions",
                "5:Interfaces", "6:Bags",   "7:TF",       "8:About"};

  // Initial data to populate menus
  nodes_list_ = {"/turtlesim", "/teleop_turtle"};
  topics_list_ = {"/turtle1/cmd_vel [geometry_msgs/msg/Twist]",
                  "/turtle1/pose [turtlesim/msg/Pose]"};
  topics_menu_labels_ = {"[ ] /turtle1/cmd_vel [geometry_msgs/msg/Twist]",
                         "[ ] /turtle1/pose [turtlesim/msg/Pose]"};
  services_list_ = {"/clear [std_srvs/srv/Empty]",
                    "/spawn [turtlesim/srv/Spawn]"};
  actions_list_ = {
      "/turtle1/rotate_absolute [turtlesim/action/RotateAbsolute]"};

  // Initialize Python Topic Plugin Engine
  python_plugin_engine_ = std::make_unique<PythonPluginEngine>();
  const char *home = std::getenv("HOME");
  if (home) {
    python_plugin_engine_->load_plugins_from_dir(std::string(home) +
                                                 "/.config/lazyrtui/plugins");
  }
  python_plugin_engine_->load_plugins_from_dir("./config/plugins");
  python_plugin_engine_->load_plugins_from_dir("./plugins");

  const char *plugin_path = std::getenv("LAZYRTUI_PLUGIN_PATH");
  if (plugin_path) {
    std::string path_str(plugin_path);
    size_t pos = 0;
    while ((pos = path_str.find(':')) != std::string::npos) {
      std::string dir = path_str.substr(0, pos);
      if (!dir.empty())
        python_plugin_engine_->load_plugins_from_dir(dir);
      path_str.erase(0, pos + 1);
    }
    if (!path_str.empty())
      python_plugin_engine_->load_plugins_from_dir(path_str);
  }

  if (!python_plugin_engine_->loaded_plugins().empty()) {
    std::cerr << "[lazyrtui] Loaded "
              << python_plugin_engine_->loaded_plugins().size()
              << " Python topic plugin(s):\n";
    for (const auto &p : python_plugin_engine_->loaded_plugins()) {
      std::cerr << "  - " << p.name << " (" << p.file_path << ")\n";
    }
  }

  // Publish the initial (static/demo) data so the UI renders before the first
  // refresh cycle completes. Single-threaded construction: no lock needed.
  publish_snapshot();
}

void LazyRTUIApp::publish_snapshot() {
  // data_mutex_ must be held by the caller (except during construction).
  auto snap = std::make_shared<UiSnapshot>();
  snap->nodes_list = nodes_list_;
  snap->topics_menu_labels = topics_menu_labels_;
  snap->services_list = services_list_;
  snap->actions_list = actions_list_;
  snap->subscribed_topics = subscribed_topics_;
  snap->topic_messages = topic_messages_map_;
  snap->node_details = node_details_;
  snap->topic_details = topic_details_;
  snap->interfaces_tree = interfaces_tree_;
  std::atomic_store(&ui_snapshot_,
                    std::shared_ptr<const UiSnapshot>(std::move(snap)));

  nodes_menu_->publish(std::make_shared<const std::vector<std::string>>(nodes_list_));
  topics_menu_->publish(
      std::make_shared<const std::vector<std::string>>(topics_menu_labels_));
  services_menu_->publish(
      std::make_shared<const std::vector<std::string>>(services_list_));
  actions_menu_->publish(
      std::make_shared<const std::vector<std::string>>(actions_list_));
  {
    std::vector<std::string> pkg_names;
    for (const auto& [pkg, ifaces] : interfaces_tree_) {
      (void)ifaces;
      pkg_names.push_back(pkg);
    }
    interfaces_pkg_menu_->publish(
        std::make_shared<const std::vector<std::string>>(pkg_names));
  }
}

LazyRTUIApp::~LazyRTUIApp() {
  stop_refresh_timer();
  if (ros_mgr_) {
    for (const auto &topic : subscribed_topics_) {
      ros_mgr_->unsubscribe_topic(topic);
    }
  }
}

void LazyRTUIApp::toggle_topic_subscription(int index) {
  std::lock_guard<std::mutex> lock(data_mutex_);
  if (index < 0 || index >= (int)topics_list_.size())
    return;

  std::string full_str = topics_list_[index];
  size_t pos = full_str.find(" [");
  if (pos == std::string::npos)
    return;

  std::string topic_name = full_str.substr(0, pos);
  std::string type_str = full_str.substr(pos + 2);
  if (!type_str.empty() && type_str.back() == ']')
    type_str.pop_back();

  if (subscribed_topics_.find(topic_name) != subscribed_topics_.end()) {
    // Unsubscribe
    subscribed_topics_.erase(topic_name);
    topic_messages_map_.erase(topic_name);
    if (ros_mgr_) {
      ros_mgr_->unsubscribe_topic(topic_name);
    }
  } else {
    // Subscribe
    subscribed_topics_.insert(topic_name);
    if (ros_mgr_) {
      ros_mgr_->subscribe_topic(
          topic_name, type_str,
          [this](const std::string &topic, const std::string &msg) {
            std::lock_guard<std::mutex> lock(data_mutex_);
            auto &msgs = topic_messages_map_[topic];
            if (msgs.size() >= 20) {
              msgs.erase(msgs.begin());
            }
            msgs.push_back(msg);
            publish_snapshot();
            if (screen_) {
              screen_->PostEvent(Event::Custom);
            }
          });
    }
  }

  // Refresh menu labels
  topics_menu_labels_.clear();
  for (const auto &t_str : topics_list_) {
    size_t p = t_str.find(" [");
    std::string t_name = (p != std::string::npos) ? t_str.substr(0, p) : t_str;
    bool is_sub = (subscribed_topics_.find(t_name) != subscribed_topics_.end());
    topics_menu_labels_.push_back(std::string(is_sub ? "[x] " : "[ ] ") +
                                  t_str);
  }
  publish_snapshot();
}

void LazyRTUIApp::start_refresh_timer() {
  if (config_.ui.auto_refresh_interval_ms <= 0)
    return;
  refresh_running_ = true;
  refresh_thread_ = std::make_unique<std::thread>([this]() {
    const auto interval =
        std::chrono::milliseconds(config_.ui.auto_refresh_interval_ms);
    while (true) {
      std::unique_lock<std::mutex> lock(refresh_mutex_);
      // Sleep interruptibly: stop_refresh_timer() wakes us immediately.
      refresh_cv_.wait_for(lock, interval,
                           [this]() { return !refresh_running_.load(); });
      if (!refresh_running_.load()) {
        break;
      }
      lock.unlock();

      refresh_data();
      if (screen_) {
        screen_->PostEvent(Event::Custom);
      }
    }
  });
}

void LazyRTUIApp::stop_refresh_timer() {
  {
    std::lock_guard<std::mutex> lock(refresh_mutex_);
    refresh_running_ = false;
  }
  refresh_cv_.notify_all();
  if (refresh_thread_ && refresh_thread_->joinable()) {
    refresh_thread_->join();
  }
}

void LazyRTUIApp::refresh_data() {
  std::lock_guard<std::mutex> lock(data_mutex_);
  if (!ros_mgr_ || !ros_mgr_->is_connected())
    return;

  try {
    auto nodes = ros_mgr_->get_nodes();
    std::vector<std::string> new_nodes;
    for (const auto &n : nodes) {
      new_nodes.push_back((n.ns == "/" ? "" : n.ns) + "/" + n.name);
    }
    if (new_nodes != nodes_list_)
      nodes_list_ = std::move(new_nodes);

    auto topics = ros_mgr_->get_topics();
    std::vector<std::string> new_topics;
    std::vector<std::string> new_menu_labels;
    for (const auto &t : topics) {
      std::string type_str = t.types.empty() ? "" : t.types.front();
      std::string item_str = t.name + " [" + type_str + "]";
      new_topics.push_back(item_str);
      bool is_sub =
          (subscribed_topics_.find(t.name) != subscribed_topics_.end());
      new_menu_labels.push_back(std::string(is_sub ? "[x] " : "[ ] ") +
                                item_str);
    }
    if (new_topics != topics_list_) {
      topics_list_ = std::move(new_topics);
      topics_menu_labels_ = std::move(new_menu_labels);
    }

    auto services = ros_mgr_->get_services();
    std::vector<std::string> new_services;
    for (const auto &s : services) {
      std::string type_str = s.types.empty() ? "" : s.types.front();
      new_services.push_back(s.name + " [" + type_str + "]");
    }
    if (new_services != services_list_)
      services_list_ = std::move(new_services);

    auto actions = ros_mgr_->get_actions();
    std::vector<std::string> new_actions;
    for (const auto &a : actions) {
      std::string type_str = a.types.empty() ? "" : a.types.front();
      new_actions.push_back(a.name + " [" + type_str + "]");
    }
    if (new_actions != actions_list_)
      actions_list_ = std::move(new_actions);

    // Cache graph-derived details so Render() never issues graph queries.
    node_details_.clear();
    for (const auto &n : nodes) {
      node_details_[n.ns + (n.ns == "/" ? "" : "/") + n.name] =
          ros_mgr_->get_node_info(n.name, n.ns);
    }
    topic_details_.clear();
    for (const auto &topic : subscribed_topics_) {
      topic_details_[topic] = ros_mgr_->get_topic_info(topic);
    }
    interfaces_tree_ = ros_mgr_->get_interfaces_tree();
  } catch (...) {
    // Silently handle exceptions during refresh
  }

  publish_snapshot();
}

Component LazyRTUIApp::make_nodes_tab() {
  auto menu = Menu(ConstStringListRef(nodes_menu_.get()), &selected_node_);

  auto left_pane = Renderer(menu, [this, menu]() {
    return window(text("Nodes"), menu->Render()) |
           (node_pane_focus_ == 0 ? borderLight : borderEmpty);
  });

  auto right_pane = Renderer([this]() {
    auto snap = std::atomic_load(&ui_snapshot_);
    std::string selected = "None";
    if (snap && selected_node_ >= 0 &&
        selected_node_ < (int)snap->nodes_list.size()) {
      selected = snap->nodes_list[selected_node_];
    }

    Elements items;
    if (selected != "None" && snap) {
      auto it = snap->node_details.find(selected);
      if (it == snap->node_details.end()) {
        items.push_back(text("Waiting for node detail...") | dim);
      } else {
        const auto &detail = it->second;

        items.push_back(text("Publishers:") | bold);
        if (detail.publishers.empty()) {
          items.push_back(text("  (None)") | dim);
        } else {
          for (const auto &[t, type] : detail.publishers) {
            items.push_back(text("  - " + t + " [" + type + "]"));
          }
        }
        items.push_back(separator());

        items.push_back(text("Subscribers:") | bold);
        if (detail.subscribers.empty()) {
          items.push_back(text("  (None)") | dim);
        } else {
          for (const auto &[t, type] : detail.subscribers) {
            items.push_back(text("  - " + t + " [" + type + "]"));
          }
        }
        items.push_back(separator());

        items.push_back(text("Services:") | bold);
        if (detail.services.empty()) {
          items.push_back(text("  (None)") | dim);
        } else {
          for (const auto &[s, type] : detail.services) {
            items.push_back(text("  - " + s + " [" + type + "]"));
          }
        }
      }
    } else {
      items.push_back(text("No node selected"));
    }

    return window(text("Node Details: " + selected), vbox(items)) |
           (node_pane_focus_ == 1 ? borderLight : borderEmpty);
  });

  auto container =
      Container::Horizontal({left_pane, right_pane}, &node_pane_focus_);

  return Renderer(container, [left_pane, right_pane]() {
    return hbox({left_pane->Render() | size(WIDTH, GREATER_THAN, 30),
                 right_pane->Render() | flex});
  });
}

Component LazyRTUIApp::make_topics_tab() {
  auto menu = Menu(ConstStringListRef(topics_menu_.get()), &selected_topic_);

  auto left_pane = Renderer(menu, [this, menu]() {
    return window(text("Topics (Space/'e' to Toggle)"), menu->Render()) |
           (topic_pane_focus_ == 0 ? borderLight : borderEmpty);
  });

  auto right_pane = Renderer([this]() {
    auto snap = std::atomic_load(&ui_snapshot_);
    const std::set<std::string> empty_set;
    const auto &subscribed =
        snap ? snap->subscribed_topics : empty_set;

    if (subscribed.empty()) {
      return window(text("Topic Echo"),
                    vbox({text("Status: No topics currently subscribed.") |
                              color(Color::Yellow),
                          separator(), text("Instructions:"),
                          text("  - Select topics in left list using j/k or "
                               "Arrow keys."),
                          text("  - Press 'Space', 'e', or 'Enter' to toggle "
                               "subscribe/unsubscribe."),
                          text("  - Multiple topics can be subscribed "
                               "simultaneously!") |
                              bold})) |
             (topic_pane_focus_ == 1 ? borderLight : borderEmpty);
    }

    Elements topic_windows;
    const std::map<std::string, std::vector<std::string>> empty_messages;
    for (const auto &topic_name : subscribed) {
      Elements msgs;

      TopicDetail detail;
      if (snap) {
        auto dit = snap->topic_details.find(topic_name);
        if (dit != snap->topic_details.end()) {
          detail = dit->second;
        }
      }

      std::string py_module;
      if (python_plugin_engine_) {
        py_module = python_plugin_engine_->find_matching_plugin(topic_name,
                                                                detail.type);
      }

      if (!py_module.empty()) {
        // Rendered via Python Plugin
        const auto &msgs_map = snap ? snap->topic_messages : empty_messages;
        auto it = msgs_map.find(topic_name);
        if (it != msgs_map.end() && !it->second.empty()) {
          const std::string &raw = it->second.back();
          std::string json_body;
          if (raw.size() > 11 && raw[0] == '[' && raw[9] == ']') {
            json_body = raw.substr(11);
          } else {
            json_body = raw;
          }

          nlohmann::json ui_spec = python_plugin_engine_->render_message(
              py_module, topic_name, json_body);
          auto plugin_els = FTXUIConverter::parse_ui_spec(ui_spec);
          for (auto &el : plugin_els) {
            msgs.push_back(std::move(el));
          }
        } else {
          msgs.push_back(text("Waiting for messages...") | dim);
        }

        std::string win_title = " " + topic_name + " [" + py_module + ".py] ";
        topic_windows.push_back(window(text(win_title), vbox(msgs)) | flex);
      } else {
        // Default raw topic renderer
        msgs.push_back(
            text("Type: " + (detail.type.empty() ? "Unknown" : detail.type)) |
            dim);
        msgs.push_back(
            text("Publishers: " + std::to_string(detail.publisher_count) +
                 " | Subscribers: " + std::to_string(detail.subscriber_count)) |
            dim);
        msgs.push_back(separator());

        const auto &msgs_map = snap ? snap->topic_messages : empty_messages;
        auto it = msgs_map.find(topic_name);
        if (it != msgs_map.end() && !it->second.empty()) {
          for (const auto &m : it->second) {
            std::stringstream ss(m);
            std::string line;
            while (std::getline(ss, line)) {
              msgs.push_back(text(line));
            }
          }
        } else {
          msgs.push_back(text("Waiting for messages...") | dim);
        }

        topic_windows.push_back(
            window(text(" Echo: " + topic_name + " "), vbox(msgs)) | flex);
      }
    }

    return window(text("Live Topic Echoes (" +
                       std::to_string(subscribed.size()) + " active)"),
                  vbox(topic_windows)) |
           (topic_pane_focus_ == 1 ? borderLight : borderEmpty);
  });

  auto container =
      Container::Horizontal({left_pane, right_pane}, &topic_pane_focus_);

  return Renderer(container, [left_pane, right_pane]() {
    return hbox({left_pane->Render() | size(WIDTH, GREATER_THAN, 40),
                 right_pane->Render() | flex});
  });
}

Component LazyRTUIApp::make_services_tab() {
  MenuOption menu_opt;
  menu_opt.on_change = [this]() {
    // Prefill the request template for the newly selected service.
    auto snap = std::atomic_load(&ui_snapshot_);
    if (!snap || !ros_mgr_ || selected_service_ < 0 ||
        selected_service_ >= (int)snap->services_list.size()) {
      return;
    }
    const std::string& entry = snap->services_list[selected_service_];
    size_t pos = entry.find(" [");
    if (pos == std::string::npos) return;
    std::string name = entry.substr(0, pos);
    std::string type = entry.substr(pos + 2);
    if (!type.empty() && type.back() == ']') type.pop_back();
    std::string tmpl = ros_mgr_->get_service_request_json(name, type);
    try {
      service_request_json_ = nlohmann::json::parse(tmpl).dump();  // Minify.
    } catch (...) {
      service_request_json_ = tmpl;
    }
  };
  auto menu = Menu(ConstStringListRef(services_menu_.get()), &selected_service_,
                   menu_opt);

  auto left_pane = Renderer(menu, [this, menu]() {
    return window(text("Services"), menu->Render()) |
           (service_pane_focus_ == 0 ? borderLight : borderEmpty);
  });

  auto input_json = Input(&service_request_json_, "{}");
  auto call_btn = Button("Call Service", [this]() {
    auto snap = std::atomic_load(&ui_snapshot_);
    if (!snap || !ros_mgr_ || selected_service_ < 0 ||
        selected_service_ >= (int)snap->services_list.size()) {
      return;
    }
    const std::string& entry = snap->services_list[selected_service_];
    size_t pos = entry.find(" [");
    if (pos == std::string::npos) return;
    std::string name = entry.substr(0, pos);
    std::string type = entry.substr(pos + 2);
    if (!type.empty() && type.back() == ']') type.pop_back();

    {
      std::lock_guard<std::mutex> lock(data_mutex_);
      service_response_ = "Calling " + name + " ...";
    }
    std::string request_json = service_request_json_;
    ros_mgr_->call_service_async(
        name, type, request_json,
        [this](bool success, const std::string& response_json,
               double elapsed_ms) {
          std::lock_guard<std::mutex> lock(data_mutex_);
          service_response_ = (success ? "[ok] " : "[error] ") + response_json +
                              " (" + std::to_string(elapsed_ms) + " ms)";
        });
  });

  auto right_container = Container::Vertical({input_json, call_btn});

  auto right_pane = Renderer(right_container, [this, input_json, call_btn]() {
    auto snap = std::atomic_load(&ui_snapshot_);
    std::string selected = "None";
    if (snap && selected_service_ >= 0 &&
        selected_service_ < (int)snap->services_list.size()) {
      selected = snap->services_list[selected_service_];
    }

    // service_response_ is written from the service-call worker thread.
    std::string response;
    {
      std::lock_guard<std::mutex> lock(data_mutex_);
      response = service_response_;
    }

    return window(text("Service Caller: " + selected),
                  vbox({text("Request JSON:"), input_json->Render() | border,
                        call_btn->Render(), separator(), text("Response:"),
                        text(response) | borderLight})) |
           (service_pane_focus_ == 1 ? borderLight : borderEmpty);
  });

  auto container =
      Container::Horizontal({left_pane, right_pane}, &service_pane_focus_);

  return Renderer(container, [left_pane, right_pane]() {
    return hbox({left_pane->Render() | size(WIDTH, GREATER_THAN, 40),
                 right_pane->Render() | flex});
  });
}

Component LazyRTUIApp::make_actions_tab() {
  MenuOption menu_opt;
  menu_opt.on_change = [this]() {
    // Prefill the goal template for the newly selected action.
    auto snap = std::atomic_load(&ui_snapshot_);
    if (!snap || !ros_mgr_ || selected_action_ < 0 ||
        selected_action_ >= (int)snap->actions_list.size()) {
      return;
    }
    const std::string& entry = snap->actions_list[selected_action_];
    size_t pos = entry.find(" [");
    if (pos == std::string::npos) return;
    std::string name = entry.substr(0, pos);
    std::string type = entry.substr(pos + 2);
    if (!type.empty() && type.back() == ']') type.pop_back();
    std::string tmpl = ros_mgr_->get_action_goal_json(name, type);
    try {
      action_goal_json_ = nlohmann::json::parse(tmpl).dump();  // Minify.
    } catch (...) {
      action_goal_json_ = tmpl;
    }
  };
  auto menu = Menu(ConstStringListRef(actions_menu_.get()), &selected_action_,
                   menu_opt);

  auto left_pane = Renderer(menu, [this, menu]() {
    return window(text("Actions"), menu->Render()) |
           (action_pane_focus_ == 0 ? borderLight : borderEmpty);
  });

  auto input_json = Input(&action_goal_json_, "{}");
  auto goal_btn = Button("Send Goal", [this]() {
    auto snap = std::atomic_load(&ui_snapshot_);
    if (!snap || !ros_mgr_ || selected_action_ < 0 ||
        selected_action_ >= (int)snap->actions_list.size()) {
      return;
    }
    const std::string& entry = snap->actions_list[selected_action_];
    size_t pos = entry.find(" [");
    if (pos == std::string::npos) return;
    std::string name = entry.substr(0, pos);
    std::string type = entry.substr(pos + 2);
    if (!type.empty() && type.back() == ']') type.pop_back();

    // A goal is delivered through the action's send_goal service, reusing the
    // dynamic service client (no raw rcl_action needed).
    const std::string service_name = name + "/_action/send_goal";
    const std::string service_type = type + "_SendGoal_Service";

    nlohmann::json goal;
    try {
      goal = nlohmann::json::parse(action_goal_json_);
    } catch (...) {
      goal = nlohmann::json::object();
    }
    // goal_id is unique_identifier_msgs/msg/UUID (uint8 uuid[16]).
    std::vector<uint8_t> uuid_bytes(16);
    {
      std::random_device rd;
      for (auto& b : uuid_bytes) b = static_cast<uint8_t>(rd());
    }
    nlohmann::json request;
    request["goal_id"] = {{"uuid", uuid_bytes}};
    request["goal"] = goal;

    {
      std::lock_guard<std::mutex> lock(data_mutex_);
      action_response_ = "Sending goal to " + name + " ...";
    }
    ros_mgr_->call_service_async(
        service_name, service_type, request.dump(),
        [this](bool success, const std::string& response_json,
               double elapsed_ms) {
          std::lock_guard<std::mutex> lock(data_mutex_);
          action_response_ = (success ? "[ok] " : "[error] ") + response_json +
                             " (" + std::to_string(elapsed_ms) + " ms)";
        });
  });

  auto right_container = Container::Vertical({input_json, goal_btn});

  auto right_pane = Renderer(right_container, [this, input_json, goal_btn]() {
    auto snap = std::atomic_load(&ui_snapshot_);
    std::string selected = "None";
    if (snap && selected_action_ >= 0 &&
        selected_action_ < (int)snap->actions_list.size()) {
      selected = snap->actions_list[selected_action_];
    }

    // action_response_ is written from the service-call worker thread.
    std::string response;
    {
      std::lock_guard<std::mutex> lock(data_mutex_);
      response = action_response_;
    }

    return window(
               text("Action Client: " + selected),
               vbox({text("Goal JSON:"), input_json->Render() | border,
                     goal_btn->Render(), separator(), text("Response/Status:"),
                     text(response) | borderLight})) |
           (action_pane_focus_ == 1 ? borderLight : borderEmpty);
  });

  auto container =
      Container::Horizontal({left_pane, right_pane}, &action_pane_focus_);

  return Renderer(container, [left_pane, right_pane]() {
    return hbox({left_pane->Render() | size(WIDTH, GREATER_THAN, 40),
                 right_pane->Render() | flex});
  });
}

Component LazyRTUIApp::make_interfaces_tab() {
  MenuOption pkg_opt;
  pkg_opt.on_change = [this]() {
    // Publish the interface list of the newly selected package.
    auto snap = std::atomic_load(&ui_snapshot_);
    if (!snap || selected_interface_pkg_ < 0 ||
        selected_interface_pkg_ >= (int)snap->interfaces_tree.size()) {
      interfaces_item_menu_->publish(
          std::make_shared<const std::vector<std::string>>());
      return;
    }
    auto it = snap->interfaces_tree.begin();
    std::advance(it, selected_interface_pkg_);
    interfaces_item_menu_->publish(
        std::make_shared<const std::vector<std::string>>(it->second));
    selected_interface_item_ = 0;
  };
  auto pkg_menu = Menu(ConstStringListRef(interfaces_pkg_menu_.get()),
                       &selected_interface_pkg_, pkg_opt);

  MenuOption item_opt;
  item_opt.on_change = [this]() {
    // Fetch the definition of the selected interface (UI thread, bounded).
    auto snap = std::atomic_load(&ui_snapshot_);
    if (!snap || !ros_mgr_) return;
    std::string iface;
    if (selected_interface_pkg_ >= 0 &&
        selected_interface_pkg_ < (int)snap->interfaces_tree.size()) {
      auto it = snap->interfaces_tree.begin();
      std::advance(it, selected_interface_pkg_);
      if (selected_interface_item_ >= 0 &&
          selected_interface_item_ < (int)it->second.size()) {
        iface = it->second[selected_interface_item_];
      }
    }
    if (!iface.empty()) {
      interface_detail_ = ros_mgr_->get_interface_detail(iface);
    }
  };
  auto item_menu = Menu(ConstStringListRef(interfaces_item_menu_.get()),
                        &selected_interface_item_, item_opt);

  // Seed the initial package selection (on_change does not fire on startup).
  {
    auto snap = std::atomic_load(&ui_snapshot_);
    if (snap && !snap->interfaces_tree.empty()) {
      auto it = snap->interfaces_tree.begin();
      interfaces_item_menu_->publish(
          std::make_shared<const std::vector<std::string>>(it->second));
    }
  }

  auto left_pane = Renderer(pkg_menu, [this, pkg_menu]() {
    return window(text("Packages"), pkg_menu->Render()) |
           (interface_pane_focus_ == 0 ? borderLight : borderEmpty);
  });
  auto middle_pane = Renderer(item_menu, [this, item_menu]() {
    return window(text("Interfaces"), item_menu->Render()) |
           (interface_pane_focus_ == 1 ? borderLight : borderEmpty);
  });
  auto right_pane = Renderer([this]() {
    return window(text("Interface Definition"),
                  vbox({text(interface_detail_) | dim})) |
           (interface_pane_focus_ == 2 ? borderLight : borderEmpty);
  });

  auto container = Container::Horizontal(
      {left_pane, middle_pane, right_pane}, &interface_pane_focus_);

  return Renderer(container, [left_pane, middle_pane, right_pane]() {
    return hbox({left_pane->Render() | size(WIDTH, GREATER_THAN, 20),
                 middle_pane->Render() | size(WIDTH, GREATER_THAN, 30),
                 right_pane->Render() | flex});
  });
}

Component LazyRTUIApp::make_bags_tab() {
  return Renderer([]() {
    return window(text("Rosbag"), vbox({text("Status: Idle"), separator(),
                                        text("Commands Reference:"),
                                        text("  ros2 bag record -a"),
                                        text("  ros2 bag play <file>")})) |
           borderLight;
  });
}

Component LazyRTUIApp::make_tf_tab() {
  return Renderer([this]() {
    Elements items;
    if (ros_mgr_) {
      const auto roots = ros_mgr_->get_tf_tree().get_roots();
      std::function<void(const std::shared_ptr<TFTreeNode>&, int)> visit;
      visit = [&visit, &items](const std::shared_ptr<TFTreeNode>& node,
                               int depth) {
        std::string line;
        if (depth > 0) {
          line = std::string(depth * 2 - 2, ' ') + "└── ";
        }
        line += node->frame_id;
        std::stringstream ds;
        ds << std::fixed << std::setprecision(3)
           << "  (x: " << node->translation.x << ", y: " << node->translation.y
           << ", z: " << node->translation.z << " | rot: " << node->rotation.x
           << ", " << node->rotation.y << ", " << node->rotation.z << ", "
           << node->rotation.w << " | updated: " << node->last_update << "s)";
        line += ds.str();
        items.push_back(text(line));
        for (const auto& [child_id, child] : node->children) {
          (void)child_id;
          visit(child, depth + 1);
        }
      };
      for (const auto& [frame_id, node] : roots) {
        (void)frame_id;
        visit(node, 0);
      }
      if (items.empty()) {
        items.push_back(text("No TF frames received yet") | dim);
      }
    } else {
      items.push_back(text("ROS 2 not connected") | dim);
    }
    return window(text("TF Tree (live transforms)"), vbox(items)) | borderLight;
  });
}

Component LazyRTUIApp::make_about_tab() {
  auto left_pane = Renderer([]() {
    return window(text("About"),
                  vbox({text("LazyRTUI v" LAZYRTUI_VERSION) | bold,
                        text("ROS 2 Distro: Unknown"), separator(),
                        text("Keybindings:"), text("  1-8: Switch tabs"),
                        text("  w: Toggle focus"), text("  r: Refresh"),
                        text("  e: Echo topic"), text("  c: Call service"),
                        text("  g: Send action goal"), text("  ?: Help"),
                        text("  q: Quit")})) |
           borderLight;
  });

  auto right_pane = Renderer([this]() {
    return window(
               text("Settings"),
               vbox({text((config_.ui.auto_refresh_interval_ms > 0)
                              ? "[x] Auto-refresh"
                              : "[ ] Auto-refresh"),
                     text(config_.ui.mouse_support ? "[x] Mouse support"
                                                   : "[ ] Mouse support")})) |
           borderLight;
  });

  return Renderer(Container::Horizontal({left_pane, right_pane}),
                  [left_pane, right_pane]() {
                    return hbox(
                        {left_pane->Render() | size(WIDTH, GREATER_THAN, 40),
                         right_pane->Render() | flex});
                  });
}

void LazyRTUIApp::run() {
  auto screen = ScreenInteractive::Fullscreen();
  screen_ = &screen;

  auto tab_toggle = Toggle(&tab_names_, &selected_tab_);

  auto tab_container =
      Container::Tab({make_nodes_tab(), make_topics_tab(), make_services_tab(),
                      make_actions_tab(), make_interfaces_tab(),
                      make_bags_tab(), make_tf_tab(), make_about_tab()},
                     &selected_tab_);

  auto main_container = Container::Vertical({tab_toggle, tab_container});

  auto renderer = Renderer(main_container, [this, tab_toggle, tab_container]() {
    // Header
    auto header =
        hbox({text(" LazyRTUI v" LAZYRTUI_VERSION " ") | bold | inverted,
              text(" "), tab_toggle->Render() | flex, text(" "),
              text((ros_mgr_ && ros_mgr_->is_connected()) ? "●" : "●") |
                  color((ros_mgr_ && ros_mgr_->is_connected()) ? Color::Green
                                                               : Color::Red),
              text(" ")});

    // Footer
    auto footer = hbox({text(" 1-8:Tab  w:Focus  r:Refresh  e:Echo  c:Call  "
                             "g:Goal  ?:Help  q:Quit ") |
                        inverted | flex});

    auto main_view = vbox({header, separator(), tab_container->Render() | flex,
                           separator(), footer});

    if (show_help_) {
      auto help_modal =
          window(
              text(" Help ") | bold,
              vbox({text("Keybindings:"), separator(),
                    text(" 1-8 : Switch Tab"), text(" w   : Toggle pane focus"),
                    text(" j/k : Navigate lists (vim style)"),
                    text(" r   : Manual refresh"),
                    text(" e   : Echo topic (Topics tab)"),
                    text(" c   : Call service (Services tab)"),
                    text(" g   : Send goal (Actions tab)"),
                    text(" ?   : Toggle this help menu"),
                    text(" q   : Quit application"), text(""),
                    text("Press '?' or 'Esc' to dismiss") | dim})) |
          clear_under | center;
      return dbox({main_view, help_modal});
    }

    return main_view;
  });

  auto event_handler = CatchEvent(renderer, [this, &screen](Event e) {
    if (e == Event::Character('q')) {
      screen.Exit();
      return true;
    }
    if (e == Event::Character('?')) {
      show_help_ = !show_help_;
      return true;
    }
    if (e == Event::Escape && show_help_) {
      show_help_ = false;
      return true;
    }

    if (e == Event::Character('1')) {
      selected_tab_ = 0;
      return true;
    }
    if (e == Event::Character('2')) {
      selected_tab_ = 1;
      return true;
    }
    if (e == Event::Character('3')) {
      selected_tab_ = 2;
      return true;
    }
    if (e == Event::Character('4')) {
      selected_tab_ = 3;
      return true;
    }
    if (e == Event::Character('5')) {
      selected_tab_ = 4;
      return true;
    }
    if (e == Event::Character('6')) {
      selected_tab_ = 5;
      return true;
    }
    if (e == Event::Character('7')) {
      selected_tab_ = 6;
      return true;
    }
    if (e == Event::Character('8')) {
      selected_tab_ = 7;
      return true;
    }

    if (e == Event::Character('w')) {
      if (selected_tab_ == 0)
        node_pane_focus_ = 1 - node_pane_focus_;
      if (selected_tab_ == 1)
        topic_pane_focus_ = 1 - topic_pane_focus_;
      if (selected_tab_ == 2)
        service_pane_focus_ = 1 - service_pane_focus_;
      if (selected_tab_ == 3)
        action_pane_focus_ = 1 - action_pane_focus_;
      if (selected_tab_ == 4)
        interface_pane_focus_ = 1 - interface_pane_focus_;
      if (selected_tab_ == 6)
        tf_pane_focus_ = 1 - tf_pane_focus_;
      return true;
    }

    if (e == Event::Character('r')) {
      refresh_data();
      return true;
    }

    if ((e == Event::Character('e') || e == Event::Character(' ') ||
         e == Event::Return) &&
        selected_tab_ == 1) {
      toggle_topic_subscription(selected_topic_);
      return true;
    }

    if (e == Event::Character('c') && selected_tab_ == 2) {
      service_response_ = "{\n  \"success\": true\n}";
      return true;
    }

    if (e == Event::Character('g') && selected_tab_ == 3) {
      action_response_ = "Status: ACCEPTED\nResult: {}";
      return true;
    }

    if (e == Event::Character('j')) {
      screen.PostEvent(Event::ArrowDown);
      return true;
    }
    if (e == Event::Character('k')) {
      screen.PostEvent(Event::ArrowUp);
      return true;
    }

    return false;
  });

  start_refresh_timer();
  screen.Loop(event_handler);
  stop_refresh_timer();
  screen_ = nullptr;
}

} // namespace lazyrtui
