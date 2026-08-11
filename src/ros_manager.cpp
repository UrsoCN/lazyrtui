#include "lazyrtui/ros_manager.hpp"
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/generic_subscription.hpp>
#include <tf2_msgs/msg/tf_message.hpp>
#include <chrono>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <cstdio>
#include <array>
#include <memory>

using namespace std::chrono_literals;

namespace lazyrtui {

struct ROS2Manager::Impl {
    rclcpp::Node::SharedPtr node_;
    rclcpp::executors::SingleThreadedExecutor::SharedPtr executor_;
    std::map<std::string, std::shared_ptr<rclcpp::GenericSubscription>> subscriptions_;
    rclcpp::Subscription<tf2_msgs::msg::TFMessage>::SharedPtr tf_sub_;
    rclcpp::Subscription<tf2_msgs::msg::TFMessage>::SharedPtr tf_static_sub_;
};

ROS2Manager::ROS2Manager(const std::string& node_name)
    : node_name_(node_name), impl_(std::make_unique<Impl>()) {}

ROS2Manager::~ROS2Manager() {
    stop();
}

bool ROS2Manager::start(int argc, char** argv) {
    try {
        if (!rclcpp::ok()) {
            rclcpp::init(argc, argv);
        }
        impl_->node_ = std::make_shared<rclcpp::Node>(node_name_);
        impl_->executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
        impl_->executor_->add_node(impl_->node_);

        setup_tf_subscribers();

        connected_ = true;
        stop_requested_ = false;
        spin_thread_ = std::make_unique<std::thread>(&ROS2Manager::spin_loop, this);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error starting ROS 2 manager: " << e.what() << "\n";
        return false;
    }
}

void ROS2Manager::stop() {
    if (!connected_) return;
    stop_requested_ = true;
    if (spin_thread_ && spin_thread_->joinable()) {
        spin_thread_->join();
    }
    impl_->executor_->remove_node(impl_->node_);
    impl_->node_.reset();
    impl_->executor_.reset();
    connected_ = false;
    if (rclcpp::ok()) {
        rclcpp::shutdown();
    }
}

bool ROS2Manager::is_connected() const {
    return connected_;
}

void ROS2Manager::spin_loop() {
    while (!stop_requested_ && rclcpp::ok()) {
        try {
            impl_->executor_->spin_some(100ms);
        } catch (const std::exception& e) {
            std::cerr << "Exception in spin loop: " << e.what() << "\n";
        }
    }
}

void ROS2Manager::setup_tf_subscribers() {
    auto tf_callback = [this](const tf2_msgs::msg::TFMessage::SharedPtr msg) {
        for (const auto& transform : msg->transforms) {
            tf_tree_.update_transform(
                transform.header.frame_id,
                transform.child_frame_id,
                transform.transform.translation.x,
                transform.transform.translation.y,
                transform.transform.translation.z,
                transform.transform.rotation.x,
                transform.transform.rotation.y,
                transform.transform.rotation.z,
                transform.transform.rotation.w
            );
        }
    };

    rclcpp::QoS qos_tf(rclcpp::KeepLast(100));
    qos_tf.best_effort();
    impl_->tf_sub_ = impl_->node_->create_subscription<tf2_msgs::msg::TFMessage>(
        "/tf", qos_tf, tf_callback);

    rclcpp::QoS qos_tf_static(rclcpp::KeepLast(100));
    qos_tf_static.transient_local();
    impl_->tf_static_sub_ = impl_->node_->create_subscription<tf2_msgs::msg::TFMessage>(
        "/tf_static", qos_tf_static, tf_callback);
}

std::vector<NodeInfo> ROS2Manager::get_nodes() {
    std::vector<NodeInfo> result;
    if (!connected_ || !impl_->node_) return result;
    auto node_names_ns = impl_->node_->get_node_graph_interface()->get_node_names_and_namespaces();
    for (const auto& [name, ns] : node_names_ns) {
        result.push_back(NodeInfo{name, ns});
    }
    return result;
}

std::vector<TopicInfo> ROS2Manager::get_topics() {
    std::vector<TopicInfo> result;
    if (!connected_ || !impl_->node_) return result;
    auto topics = impl_->node_->get_topic_names_and_types();
    for (const auto& [name, types] : topics) {
        result.push_back(TopicInfo{name, types});
    }
    return result;
}

std::vector<ServiceInfo> ROS2Manager::get_services() {
    std::vector<ServiceInfo> result;
    if (!connected_ || !impl_->node_) return result;
    auto services = impl_->node_->get_service_names_and_types();
    for (const auto& [name, types] : services) {
        if (name.find("parameter_events") == std::string::npos) {
            result.push_back(ServiceInfo{name, types});
        }
    }
    return result;
}

std::vector<ActionInfo> ROS2Manager::get_actions() {
    std::vector<ActionInfo> result;
    if (!connected_ || !impl_->node_) return result;
    auto services = impl_->node_->get_service_names_and_types();
    std::map<std::string, std::vector<std::string>> action_map;
    
    std::string suffix = "/_action/send_goal";
    for (const auto& [name, types] : services) {
        if (name.length() > suffix.length() && 
            name.compare(name.length() - suffix.length(), suffix.length(), suffix) == 0) {
            std::string action_name = name.substr(0, name.length() - suffix.length());
            std::vector<std::string> action_types;
            for(const auto& t : types) {
                std::string t_act = t;
                auto pos = t_act.find("_SendGoal_Service");
                if(pos != std::string::npos) {
                    t_act = t_act.substr(0, pos) + "_Action";
                }
                action_types.push_back(t_act);
            }
            action_map[action_name] = action_types;
        }
    }
    
    for (const auto& [name, types] : action_map) {
        result.push_back(ActionInfo{name, types});
    }
    return result;
}

NodeDetail ROS2Manager::get_node_info(const std::string& name, const std::string& ns) {
    NodeDetail detail;
    detail.name = name;
    detail.ns = ns;
    if (!connected_ || !impl_->node_) return detail;

    try {
        auto topics_and_types = impl_->node_->get_topic_names_and_types();
        for (const auto& [topic_name, types] : topics_and_types) {
            std::string main_type = types.empty() ? "" : types.front();
            
            auto pubs = impl_->node_->get_publishers_info_by_topic(topic_name);
            for (const auto& endpoint : pubs) {
                if (endpoint.node_name() == name && (ns.empty() || endpoint.node_namespace() == ns)) {
                    detail.publishers.emplace_back(topic_name, main_type);
                    break;
                }
            }

            auto subs = impl_->node_->get_subscriptions_info_by_topic(topic_name);
            for (const auto& endpoint : subs) {
                if (endpoint.node_name() == name && (ns.empty() || endpoint.node_namespace() == ns)) {
                    detail.subscribers.emplace_back(topic_name, main_type);
                    break;
                }
            }
        }

        auto srvs = impl_->node_->get_service_names_and_types_by_node(name, ns);
        for (const auto& [srv, types] : srvs) {
            for (const auto& type : types) {
                detail.services.emplace_back(srv, type);
            }
        }
    } catch (...) {
    }

    return detail;
}

TopicDetail ROS2Manager::get_topic_info(const std::string& topic_name) {
    TopicDetail detail{topic_name, "", 0, 0};
    if (!connected_ || !impl_->node_) return detail;

    auto pubs = impl_->node_->get_publishers_info_by_topic(topic_name);
    auto subs = impl_->node_->get_subscriptions_info_by_topic(topic_name);
    
    detail.publisher_count = pubs.size();
    detail.subscriber_count = subs.size();
    
    if (!pubs.empty()) detail.type = pubs.front().topic_type();
    else if (!subs.empty()) detail.type = subs.front().topic_type();

    return detail;
}

std::string ROS2Manager::get_service_request_json(const std::string& service_name, const std::string& type_str) {
    // TODO: Dynamic message introspection
    return "{\n  \"message\": \"Dynamic request introspection not fully implemented yet\"\n}";
}

std::string ROS2Manager::get_action_goal_json(const std::string& action_name, const std::string& type_str) {
    // TODO: Dynamic message introspection
    return "{\n  \"message\": \"Dynamic goal introspection not fully implemented yet\"\n}";
}

bool ROS2Manager::subscribe_topic(const std::string& topic, const std::string& type_str, TopicCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || !impl_->node_) return false;

    if (impl_->subscriptions_.find(topic) != impl_->subscriptions_.end()) {
        return true;
    }

    try {
        auto cb = [this, topic, callback](std::shared_ptr<rclcpp::SerializedMessage> msg) {
            std::stringstream ss;
            ss << "{ \"_info\": \"Raw serialized message data\", \"size\": " << msg->size() << " }";
            // TODO: Implement full rosidl_typesupport_introspection_cpp
            callback(topic, ss.str());
        };

        auto sub = impl_->node_->create_generic_subscription(topic, type_str, rclcpp::QoS(10), cb);
        impl_->subscriptions_[topic] = sub;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to subscribe to " << topic << ": " << e.what() << "\n";
        return false;
    }
}

void ROS2Manager::unsubscribe_topic(const std::string& topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    impl_->subscriptions_.erase(topic);
}

bool ROS2Manager::is_topic_subscribed(const std::string& topic) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return impl_->subscriptions_.find(topic) != impl_->subscriptions_.end();
}

static std::string exec(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

void ROS2Manager::call_service_async(const std::string& service_name, const std::string& type_str,
                                     const std::string& request_json, ServiceCallback callback) {
    std::thread([service_name, type_str, request_json, callback]() {
        auto start_time = std::chrono::steady_clock::now();
        try {
            std::string escaped_json = request_json;
            size_t pos = 0;
            while ((pos = escaped_json.find("'", pos)) != std::string::npos) {
                escaped_json.replace(pos, 1, "'\\''");
                pos += 4;
            }
            
            std::string cmd = "ros2 service call " + service_name + " " + type_str + " '" + escaped_json + "'";
            std::string out = exec(cmd.c_str());
            
            auto end_time = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            
            callback(true, out, elapsed);
        } catch (const std::exception& e) {
            auto end_time = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            callback(false, e.what(), elapsed);
        }
    }).detach();
}

std::map<std::string, std::vector<std::string>> ROS2Manager::get_interfaces_tree() {
    std::map<std::string, std::vector<std::string>> result;
    try {
        std::string out = exec("ros2 interface list");
        std::istringstream iss(out);
        std::string line;
        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            if (line.find("Messages:") != std::string::npos ||
                line.find("Services:") != std::string::npos ||
                line.find("Actions:") != std::string::npos) {
                continue;
            }
            
            size_t start = line.find_first_not_of(" \t");
            if (start != std::string::npos) {
                std::string iface = line.substr(start);
                auto slash_pos = iface.find('/');
                if (slash_pos != std::string::npos) {
                    std::string pkg = iface.substr(0, slash_pos);
                    result[pkg].push_back(iface);
                }
            }
        }
    } catch (...) {
    }
    return result;
}

std::string ROS2Manager::get_interface_detail(const std::string& type_str) {
    try {
        std::string cmd = "ros2 interface show " + type_str;
        return exec(cmd.c_str());
    } catch (const std::exception& e) {
        return std::string("Error: ") + e.what();
    }
}

TFTree& ROS2Manager::get_tf_tree() {
    return tf_tree_;
}

std::string ROS2Manager::serialize_msg_to_json() const {
    return "{}";
}

} // namespace lazyrtui
