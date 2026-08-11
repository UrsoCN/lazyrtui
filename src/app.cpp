#include "lazyrtui/app.hpp"
#include "lazyrtui/ros_manager.hpp"
#include "lazyrtui/config_loader.hpp"
#include "lazyrtui/tf_tree.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/screen.hpp>

#include <algorithm>
#include <chrono>
#include <sstream>

#ifndef LAZYRTUI_VERSION
#define LAZYRTUI_VERSION "0.1.0"
#endif

using namespace ftxui;

namespace lazyrtui {

LazyRTUIApp::LazyRTUIApp(std::shared_ptr<ROS2Manager> ros_mgr, const Config& config)
    : ros_mgr_(std::move(ros_mgr)), config_(config) {
    tab_names_ = {
        "1:Nodes", "2:Topics", "3:Services", "4:Actions",
        "5:Interfaces", "6:Bags", "7:TF", "8:About"
    };

    // Dummy data to populate menus
    nodes_list_ = {"/turtlesim", "/teleop_turtle"};
    topics_list_ = {"/turtle1/cmd_vel [geometry_msgs/msg/Twist]", "/turtle1/pose [turtlesim/msg/Pose]"};
    services_list_ = {"/clear [std_srvs/srv/Empty]", "/spawn [turtlesim/srv/Spawn]"};
    actions_list_ = {"/turtle1/rotate_absolute [turtlesim/action/RotateAbsolute]"};
}

LazyRTUIApp::~LazyRTUIApp() {
    stop_refresh_timer();
    if (ros_mgr_ && !current_subscribed_topic_.empty()) {
        ros_mgr_->unsubscribe_topic(current_subscribed_topic_);
    }
}

void LazyRTUIApp::start_refresh_timer() {
    if (config_.ui.auto_refresh_interval_ms <= 0) return;
    refresh_running_ = true;
    refresh_thread_ = std::make_unique<std::thread>([this]() {
        while (refresh_running_) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            refresh_data();
            if (screen_) {
                screen_->PostEvent(Event::Custom);
            }
        }
    });
}

void LazyRTUIApp::stop_refresh_timer() {
    refresh_running_ = false;
    if (refresh_thread_ && refresh_thread_->joinable()) {
        refresh_thread_->join();
    }
}

void LazyRTUIApp::refresh_data() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (!ros_mgr_ || !ros_mgr_->is_connected()) return;

    try {
        auto nodes = ros_mgr_->get_nodes();
        std::vector<std::string> new_nodes;
        for (const auto& n : nodes) {
            new_nodes.push_back((n.ns == "/" ? "" : n.ns) + "/" + n.name);
        }
        if (new_nodes != nodes_list_) nodes_list_ = std::move(new_nodes);

        auto topics = ros_mgr_->get_topics();
        std::vector<std::string> new_topics;
        for (const auto& t : topics) {
            std::string type_str = t.types.empty() ? "" : t.types.front();
            new_topics.push_back(t.name + " [" + type_str + "]");
        }
        if (new_topics != topics_list_) topics_list_ = std::move(new_topics);

        auto services = ros_mgr_->get_services();
        std::vector<std::string> new_services;
        for (const auto& s : services) {
            std::string type_str = s.types.empty() ? "" : s.types.front();
            new_services.push_back(s.name + " [" + type_str + "]");
        }
        if (new_services != services_list_) services_list_ = std::move(new_services);

        auto actions = ros_mgr_->get_actions();
        std::vector<std::string> new_actions;
        for (const auto& a : actions) {
            std::string type_str = a.types.empty() ? "" : a.types.front();
            new_actions.push_back(a.name + " [" + type_str + "]");
        }
        if (new_actions != actions_list_) actions_list_ = std::move(new_actions);
    } catch (...) {
        // Silently handle exceptions during refresh
    }
}

Component LazyRTUIApp::make_nodes_tab() {
    auto menu = Menu(&nodes_list_, &selected_node_);
    
    auto left_pane = Renderer(menu, [this, menu]() {
        return window(text("Nodes"), menu->Render())
               | (node_pane_focus_ == 0 ? borderLight : borderEmpty);
    });

    auto right_pane = Renderer([this]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        std::string selected = (selected_node_ >= 0 && selected_node_ < (int)nodes_list_.size()) 
                                ? nodes_list_[selected_node_] : "None";
        
        Elements items;
        if (selected != "None" && ros_mgr_) {
            std::string name = selected;
            std::string ns = "/";
            size_t last_slash = selected.find_last_of('/');
            if (last_slash != std::string::npos && last_slash > 0) {
                ns = selected.substr(0, last_slash);
                name = selected.substr(last_slash + 1);
            } else if (last_slash == 0) {
                name = selected.substr(1);
            }
            auto detail = ros_mgr_->get_node_info(name, ns);
            
            items.push_back(text("Publishers:") | bold);
            if (detail.publishers.empty()) {
                items.push_back(text("  (None)") | dim);
            } else {
                for (const auto& [t, type] : detail.publishers) {
                    items.push_back(text("  - " + t + " [" + type + "]"));
                }
            }
            items.push_back(separator());

            items.push_back(text("Subscribers:") | bold);
            if (detail.subscribers.empty()) {
                items.push_back(text("  (None)") | dim);
            } else {
                for (const auto& [t, type] : detail.subscribers) {
                    items.push_back(text("  - " + t + " [" + type + "]"));
                }
            }
            items.push_back(separator());

            items.push_back(text("Services:") | bold);
            if (detail.services.empty()) {
                items.push_back(text("  (None)") | dim);
            } else {
                for (const auto& [s, type] : detail.services) {
                    items.push_back(text("  - " + s + " [" + type + "]"));
                }
            }
        } else {
            items.push_back(text("No node selected"));
        }

        return window(text("Node Details: " + selected), vbox(items)) 
               | (node_pane_focus_ == 1 ? borderLight : borderEmpty);
    });

    auto container = Container::Horizontal({left_pane, right_pane}, &node_pane_focus_);
    
    return Renderer(container, [left_pane, right_pane]() {
        return hbox({
            left_pane->Render() | size(WIDTH, GREATER_THAN, 30),
            right_pane->Render() | flex
        });
    });
}

Component LazyRTUIApp::make_topics_tab() {
    auto menu = Menu(&topics_list_, &selected_topic_);
    
    auto left_pane = Renderer(menu, [this, menu]() {
        return window(text("Topics"), menu->Render())
               | (topic_pane_focus_ == 0 ? borderLight : borderEmpty);
    });

    auto right_pane = Renderer([this]() {
        std::lock_guard<std::mutex> lock(data_mutex_);
        std::string selected = (selected_topic_ >= 0 && selected_topic_ < (int)topics_list_.size()) 
                                ? topics_list_[selected_topic_] : "None";
        
        Elements msgs;
        if (selected != "None") {
            size_t pos = selected.find(" [");
            if (pos != std::string::npos) {
                std::string topic_name = selected.substr(0, pos);
                std::string type_str = selected.substr(pos + 2);
                if (!type_str.empty() && type_str.back() == ']') type_str.pop_back();

                if (ros_mgr_) {
                    auto detail = ros_mgr_->get_topic_info(topic_name);
                    msgs.push_back(text("Type: " + (detail.type.empty() ? type_str : detail.type)) | dim);
                    msgs.push_back(text("Publishers: " + std::to_string(detail.publisher_count) + 
                                        " | Subscribers: " + std::to_string(detail.subscriber_count)) | dim);
                    msgs.push_back(separator());
                }

                if (is_echoing_ && topic_name != current_subscribed_topic_) {
                    if (ros_mgr_ && !current_subscribed_topic_.empty()) {
                        ros_mgr_->unsubscribe_topic(current_subscribed_topic_);
                    }
                    current_subscribed_topic_ = topic_name;
                    topic_messages_.clear();
                    if (ros_mgr_) {
                        ros_mgr_->subscribe_topic(topic_name, type_str, [this](const std::string& t, const std::string& msg) {
                            std::lock_guard<std::mutex> lock(data_mutex_);
                            if (topic_messages_.size() > 50) {
                                topic_messages_.erase(topic_messages_.begin());
                            }
                            topic_messages_.push_back(msg);
                            if (screen_) {
                                screen_->PostEvent(Event::Custom);
                            }
                        });
                    }
                }
            }
        }

        if (is_echoing_) {
            msgs.push_back(text("Status: Subscribed (" + current_subscribed_topic_ + ")") | color(Color::Green));
            msgs.push_back(separator());
            for (const auto& m : topic_messages_) {
                msgs.push_back(text(m));
            }
            if (topic_messages_.empty()) {
                msgs.push_back(text("Waiting for messages...") | dim);
            }
        } else {
            msgs.push_back(text("Status: Not Subscribed") | color(Color::GrayDark));
            msgs.push_back(text("Press 'e' to start/stop echoing messages."));
        }

        return window(text("Topic Echo: " + selected), vbox(msgs)) 
               | (topic_pane_focus_ == 1 ? borderLight : borderEmpty);
    });

    auto container = Container::Horizontal({left_pane, right_pane}, &topic_pane_focus_);
    
    return Renderer(container, [left_pane, right_pane]() {
        return hbox({
            left_pane->Render() | size(WIDTH, GREATER_THAN, 40),
            right_pane->Render() | flex
        });
    });
}

Component LazyRTUIApp::make_services_tab() {
    auto menu = Menu(&services_list_, &selected_service_);
    
    auto left_pane = Renderer(menu, [this, menu]() {
        return window(text("Services"), menu->Render())
               | (service_pane_focus_ == 0 ? borderLight : borderEmpty);
    });

    auto input_json = Input(&service_request_json_, "{}");
    auto call_btn = Button("Call Service", [this]() {
        service_response_ = "{\n  \"success\": true\n}";
    });
    
    auto right_container = Container::Vertical({input_json, call_btn});

    auto right_pane = Renderer(right_container, [this, input_json, call_btn]() {
        std::string selected = (selected_service_ >= 0 && selected_service_ < (int)services_list_.size()) 
                                ? services_list_[selected_service_] : "None";
        
        return window(text("Service Caller: " + selected), 
            vbox({
                text("Request JSON:"),
                input_json->Render() | border,
                call_btn->Render(),
                separator(),
                text("Response:"),
                text(service_response_) | borderLight
            })
        ) | (service_pane_focus_ == 1 ? borderLight : borderEmpty);
    });

    auto container = Container::Horizontal({left_pane, right_pane}, &service_pane_focus_);
    
    return Renderer(container, [left_pane, right_pane]() {
        return hbox({
            left_pane->Render() | size(WIDTH, GREATER_THAN, 40),
            right_pane->Render() | flex
        });
    });
}

Component LazyRTUIApp::make_actions_tab() {
    auto menu = Menu(&actions_list_, &selected_action_);
    
    auto left_pane = Renderer(menu, [this, menu]() {
        return window(text("Actions"), menu->Render())
               | (action_pane_focus_ == 0 ? borderLight : borderEmpty);
    });

    auto input_json = Input(&action_goal_json_, "{}");
    auto goal_btn = Button("Send Goal", [this]() {
        action_response_ = "Status: ACCEPTED\nResult: {}";
    });
    
    auto right_container = Container::Vertical({input_json, goal_btn});

    auto right_pane = Renderer(right_container, [this, input_json, goal_btn]() {
        std::string selected = (selected_action_ >= 0 && selected_action_ < (int)actions_list_.size()) 
                                ? actions_list_[selected_action_] : "None";
        
        return window(text("Action Client: " + selected), 
            vbox({
                text("Goal JSON:"),
                input_json->Render() | border,
                goal_btn->Render(),
                separator(),
                text("Response/Status:"),
                text(action_response_) | borderLight
            })
        ) | (action_pane_focus_ == 1 ? borderLight : borderEmpty);
    });

    auto container = Container::Horizontal({left_pane, right_pane}, &action_pane_focus_);
    
    return Renderer(container, [left_pane, right_pane]() {
        return hbox({
            left_pane->Render() | size(WIDTH, GREATER_THAN, 40),
            right_pane->Render() | flex
        });
    });
}

Component LazyRTUIApp::make_interfaces_tab() {
    auto left_pane = Renderer([this]() {
        return window(text("Packages"), 
            vbox({
                text("std_msgs"),
                text("  msg/String"),
                text("  msg/Int32")
            })
        ) | (interface_pane_focus_ == 0 ? borderLight : borderEmpty);
    });

    auto right_pane = Renderer([this]() {
        return window(text("Interface Definition"), 
            vbox({
                text("std_msgs/msg/String"),
                separator(),
                text("string data")
            })
        ) | (interface_pane_focus_ == 1 ? borderLight : borderEmpty);
    });

    auto container = Container::Horizontal({left_pane, right_pane}, &interface_pane_focus_);
    
    return Renderer(container, [left_pane, right_pane]() {
        return hbox({
            left_pane->Render() | size(WIDTH, GREATER_THAN, 30),
            right_pane->Render() | flex
        });
    });
}

Component LazyRTUIApp::make_bags_tab() {
    return Renderer([]() {
        return window(text("Rosbag"), 
            vbox({
                text("Status: Idle"),
                separator(),
                text("Commands Reference:"),
                text("  ros2 bag record -a"),
                text("  ros2 bag play <file>")
            })
        ) | borderLight;
    });
}

Component LazyRTUIApp::make_tf_tab() {
    auto left_pane = Renderer([this]() {
        return window(text("TF Tree"), 
            vbox({
                text("world"),
                text("  └── base_link"),
                text("      └── laser_link")
            })
        ) | (tf_pane_focus_ == 0 ? borderLight : borderEmpty);
    });

    auto right_pane = Renderer([this]() {
        return window(text("Transform Details"), 
            vbox({
                text("Translation:"),
                text("  x: 0.0"),
                text("  y: 0.0"),
                text("  z: 0.0"),
                separator(),
                text("Rotation (Quaternion):"),
                text("  x: 0.0"),
                text("  y: 0.0"),
                text("  z: 0.0"),
                text("  w: 1.0")
            })
        ) | (tf_pane_focus_ == 1 ? borderLight : borderEmpty);
    });

    auto container = Container::Horizontal({left_pane, right_pane}, &tf_pane_focus_);
    
    return Renderer(container, [left_pane, right_pane]() {
        return hbox({
            left_pane->Render() | size(WIDTH, GREATER_THAN, 30),
            right_pane->Render() | flex
        });
    });
}

Component LazyRTUIApp::make_about_tab() {
    auto left_pane = Renderer([]() {
        return window(text("About"), 
            vbox({
                text("LazyRTUI v" LAZYRTUI_VERSION) | bold,
                text("ROS 2 Distro: Unknown"),
                separator(),
                text("Keybindings:"),
                text("  1-8: Switch tabs"),
                text("  w: Toggle focus"),
                text("  r: Refresh"),
                text("  e: Echo topic"),
                text("  c: Call service"),
                text("  g: Send action goal"),
                text("  ?: Help"),
                text("  q: Quit")
            })
        ) | borderLight;
    });

    auto right_pane = Renderer([this]() {
        return window(text("Settings"), 
            vbox({
                text((config_.ui.auto_refresh_interval_ms > 0) ? "[x] Auto-refresh" : "[ ] Auto-refresh"),
                text(config_.ui.mouse_support ? "[x] Mouse support" : "[ ] Mouse support")
            })
        ) | borderLight;
    });

    return Renderer(Container::Horizontal({left_pane, right_pane}), [left_pane, right_pane]() {
        return hbox({
            left_pane->Render() | size(WIDTH, GREATER_THAN, 40),
            right_pane->Render() | flex
        });
    });
}

void LazyRTUIApp::run() {
    auto screen = ScreenInteractive::Fullscreen();
    screen_ = &screen;

    auto tab_toggle = Toggle(&tab_names_, &selected_tab_);
    
    auto tab_container = Container::Tab({
        make_nodes_tab(),
        make_topics_tab(),
        make_services_tab(),
        make_actions_tab(),
        make_interfaces_tab(),
        make_bags_tab(),
        make_tf_tab(),
        make_about_tab()
    }, &selected_tab_);

    auto main_container = Container::Vertical({
        tab_toggle,
        tab_container
    });

    auto renderer = Renderer(main_container, [this, tab_toggle, tab_container]() {
        // Header
        auto header = hbox({
            text(" LazyRTUI v" LAZYRTUI_VERSION " ") | bold | inverted,
            text(" "),
            tab_toggle->Render() | flex,
            text(" "),
            text((ros_mgr_ && ros_mgr_->is_connected()) ? "●" : "●") | color((ros_mgr_ && ros_mgr_->is_connected()) ? Color::Green : Color::Red),
            text(" ")
        });

        // Footer
        auto footer = hbox({
            text(" 1-8:Tab  w:Focus  r:Refresh  e:Echo  c:Call  g:Goal  ?:Help  q:Quit ") | inverted | flex
        });

        auto main_view = vbox({
            header,
            separator(),
            tab_container->Render() | flex,
            separator(),
            footer
        });
        
        if (show_help_) {
            auto help_modal = window(text(" Help ") | bold, vbox({
                text("Keybindings:"),
                separator(),
                text(" 1-8 : Switch Tab"),
                text(" w   : Toggle pane focus"),
                text(" j/k : Navigate lists (vim style)"),
                text(" r   : Manual refresh"),
                text(" e   : Echo topic (Topics tab)"),
                text(" c   : Call service (Services tab)"),
                text(" g   : Send goal (Actions tab)"),
                text(" ?   : Toggle this help menu"),
                text(" q   : Quit application"),
                text(""),
                text("Press '?' or 'Esc' to dismiss") | dim
            })) | clear_under | center;
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
        
        if (e == Event::Character('1')) { selected_tab_ = 0; return true; }
        if (e == Event::Character('2')) { selected_tab_ = 1; return true; }
        if (e == Event::Character('3')) { selected_tab_ = 2; return true; }
        if (e == Event::Character('4')) { selected_tab_ = 3; return true; }
        if (e == Event::Character('5')) { selected_tab_ = 4; return true; }
        if (e == Event::Character('6')) { selected_tab_ = 5; return true; }
        if (e == Event::Character('7')) { selected_tab_ = 6; return true; }
        if (e == Event::Character('8')) { selected_tab_ = 7; return true; }
        
        if (e == Event::Character('w')) {
            if (selected_tab_ == 0) node_pane_focus_ = 1 - node_pane_focus_;
            if (selected_tab_ == 1) topic_pane_focus_ = 1 - topic_pane_focus_;
            if (selected_tab_ == 2) service_pane_focus_ = 1 - service_pane_focus_;
            if (selected_tab_ == 3) action_pane_focus_ = 1 - action_pane_focus_;
            if (selected_tab_ == 4) interface_pane_focus_ = 1 - interface_pane_focus_;
            if (selected_tab_ == 6) tf_pane_focus_ = 1 - tf_pane_focus_;
            return true;
        }
        
        if (e == Event::Character('r')) {
            refresh_data();
            return true;
        }
        
        if (e == Event::Character('e') && selected_tab_ == 1) {
            std::lock_guard<std::mutex> lock(data_mutex_);
            is_echoing_ = !is_echoing_;
            if (is_echoing_) {
                topic_messages_.clear();
                if (selected_topic_ >= 0 && selected_topic_ < (int)topics_list_.size()) {
                    std::string full_str = topics_list_[selected_topic_];
                    size_t pos = full_str.find(" [");
                    if (pos != std::string::npos) {
                        std::string topic_name = full_str.substr(0, pos);
                        std::string type_str = full_str.substr(pos + 2);
                        if (!type_str.empty() && type_str.back() == ']') type_str.pop_back();

                        current_subscribed_topic_ = topic_name;
                        if (ros_mgr_) {
                            ros_mgr_->subscribe_topic(topic_name, type_str, [this](const std::string& t, const std::string& msg) {
                                std::lock_guard<std::mutex> lock(data_mutex_);
                                if (topic_messages_.size() > 50) {
                                    topic_messages_.erase(topic_messages_.begin());
                                }
                                topic_messages_.push_back(msg);
                                if (screen_) {
                                    screen_->PostEvent(Event::Custom);
                                }
                            });
                        }
                    }
                }
            } else {
                if (ros_mgr_ && !current_subscribed_topic_.empty()) {
                    ros_mgr_->unsubscribe_topic(current_subscribed_topic_);
                    current_subscribed_topic_.clear();
                }
                topic_messages_.clear();
            }
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
