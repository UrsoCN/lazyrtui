#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <map>

#include "lazyrtui/config_loader.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

namespace lazyrtui {

// Forward declarations
class ROS2Manager;

class LazyRTUIApp {
public:
    LazyRTUIApp(std::shared_ptr<ROS2Manager> ros_mgr, const Config& config);
    ~LazyRTUIApp();
    void run();

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

    // UI state
    int selected_tab_ = 0;
    std::vector<std::string> tab_names_;
    bool show_help_ = false;

    // Data refresh
    void refresh_data();
    void start_refresh_timer();
    void stop_refresh_timer();

    std::shared_ptr<ROS2Manager> ros_mgr_;
    Config config_;

    // Screen pointer for PostEvent
    ftxui::ScreenInteractive* screen_ = nullptr;

    // Refresh timer
    std::atomic<bool> refresh_running_{false};
    std::unique_ptr<std::thread> refresh_thread_;

    // Data cached for UI
    mutable std::mutex data_mutex_;
    std::vector<std::string> nodes_list_;
    std::vector<std::string> topics_list_;
    std::vector<std::string> services_list_;
    std::vector<std::string> actions_list_;
    
    // Node tab state
    int selected_node_ = 0;
    int node_pane_focus_ = 0; // 0=left, 1=right
    
    // Topic tab state
    int selected_topic_ = 0;
    int topic_pane_focus_ = 0;
    std::vector<std::string> topic_messages_;
    bool is_echoing_ = false;

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
    int selected_interface_ = 0;
    int interface_pane_focus_ = 0;

    // TF tab state
    int selected_tf_ = 0;
    int tf_pane_focus_ = 0;
};

} // namespace lazyrtui
