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

#include <dlfcn.h>
#include <cmath>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#include <rosidl_typesupport_introspection_cpp/field_types.hpp>

struct TypeSupportHandleInfo {
    void* lib_handle = nullptr;
    const ::rosidl_typesupport_introspection_cpp::MessageMembers* members = nullptr;
};

static std::map<std::string, TypeSupportHandleInfo> g_typesupport_cache;
static std::mutex g_typesupport_mutex;

static const ::rosidl_typesupport_introspection_cpp::MessageMembers* get_message_members(const std::string& type_str) {
    std::lock_guard<std::mutex> lock(g_typesupport_mutex);
    auto it = g_typesupport_cache.find(type_str);
    if (it != g_typesupport_cache.end()) {
        return it->second.members;
    }

    std::string pkg, msg_name;
    size_t slash1 = type_str.find('/');
    if (slash1 != std::string::npos) {
        pkg = type_str.substr(0, slash1);
        size_t slash2 = type_str.find('/', slash1 + 1);
        if (slash2 != std::string::npos) {
            msg_name = type_str.substr(slash2 + 1);
        } else {
            msg_name = type_str.substr(slash1 + 1);
        }
    } else {
        return nullptr;
    }

    std::string lib_name = "lib" + pkg + "__rosidl_typesupport_introspection_cpp.so";
    void* handle = dlopen(lib_name.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    if (!handle) {
        lib_name = "lib" + pkg + "__rosidl_typesupport_c.so";
        handle = dlopen(lib_name.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    }
    if (!handle) {
        return nullptr;
    }

    std::string sym_name = "rosidl_typesupport_introspection_cpp__get_message_type_support_handle__" + pkg + "__msg__" + msg_name;
    using GetTSFn = const rosidl_message_type_support_t* (*)();
    GetTSFn get_ts_fn = reinterpret_cast<GetTSFn>(dlsym(handle, sym_name.c_str()));
    if (!get_ts_fn) {
        sym_name = "rosidl_typesupport_c__get_message_type_support_handle__" + pkg + "__msg__" + msg_name;
        get_ts_fn = reinterpret_cast<GetTSFn>(dlsym(handle, sym_name.c_str()));
    }

    if (!get_ts_fn) {
        dlclose(handle);
        return nullptr;
    }

    const rosidl_message_type_support_t* ts = get_ts_fn();
    if (!ts || !ts->data) {
        dlclose(handle);
        return nullptr;
    }

    const auto* members = static_cast<const ::rosidl_typesupport_introspection_cpp::MessageMembers*>(ts->data);
    g_typesupport_cache[type_str] = {handle, members};
    return members;
}

static bool parse_cdr_field(const ::rosidl_typesupport_introspection_cpp::MessageMember& member,
                            const uint8_t* buffer, size_t size, size_t& offset,
                            std::stringstream& ss, int indent_level);

static bool parse_cdr_members(const ::rosidl_typesupport_introspection_cpp::MessageMembers* members,
                             const uint8_t* buffer, size_t size, size_t& offset,
                             std::stringstream& ss, int indent_level) {
    if (!members) return false;
    std::string indent(indent_level * 2, ' ');
    ss << "{\n";
    for (uint32_t i = 0; i < members->member_count_; ++i) {
        const auto& member = members->members_[i];
        if (i > 0) ss << ",\n";
        ss << indent << "  \"" << member.name_ << "\": ";
        if (!parse_cdr_field(member, buffer, size, offset, ss, indent_level + 1)) {
            ss << "null";
        }
    }
    ss << "\n" << indent << "}";
    return true;
}

static bool parse_cdr_field(const ::rosidl_typesupport_introspection_cpp::MessageMember& member,
                            const uint8_t* buffer, size_t size, size_t& offset,
                            std::stringstream& ss, int indent_level) {
    using namespace rosidl_typesupport_introspection_cpp;

    auto align_offset = [&](size_t align) {
        while (offset % align != 0 && offset < size) offset++;
    };

    if (member.is_array_) {
        align_offset(4);
        uint32_t count = member.array_size_;
        if (!member.is_upper_bound_ && count == 0) {
            if (offset + 4 > size) return false;
            std::memcpy(&count, buffer + offset, 4);
            offset += 4;
        }

        ss << "[";
        if (count > 20) count = 20;
        for (uint32_t j = 0; j < count; ++j) {
            if (j > 0) ss << ", ";
            ::rosidl_typesupport_introspection_cpp::MessageMember elem_member = member;
            elem_member.is_array_ = false;
            elem_member.array_size_ = 0;
            if (!parse_cdr_field(elem_member, buffer, size, offset, ss, indent_level)) {
                ss << "null";
            }
        }
        ss << "]";
        return true;
    }

    switch (member.type_id_) {
        case ROS_TYPE_BOOLEAN: {
            if (offset >= size) return false;
            bool val = (buffer[offset] != 0);
            offset += 1;
            ss << (val ? "true" : "false");
            return true;
        }
        case ROS_TYPE_UINT8: {
            if (offset >= size) return false;
            uint8_t val = buffer[offset];
            offset += 1;
            ss << (int)val;
            return true;
        }
        case ROS_TYPE_INT8:
        case ROS_TYPE_CHAR: {
            if (offset >= size) return false;
            int8_t val = static_cast<int8_t>(buffer[offset]);
            offset += 1;
            ss << (int)val;
            return true;
        }
        case ROS_TYPE_UINT16: {
            align_offset(2);
            if (offset + 2 > size) return false;
            uint16_t val = 0;
            std::memcpy(&val, buffer + offset, 2);
            offset += 2;
            ss << val;
            return true;
        }
        case ROS_TYPE_INT16: {
            align_offset(2);
            if (offset + 2 > size) return false;
            int16_t val = 0;
            std::memcpy(&val, buffer + offset, 2);
            offset += 2;
            ss << val;
            return true;
        }
        case ROS_TYPE_UINT32: {
            align_offset(4);
            if (offset + 4 > size) return false;
            uint32_t val = 0;
            std::memcpy(&val, buffer + offset, 4);
            offset += 4;
            ss << val;
            return true;
        }
        case ROS_TYPE_INT32: {
            align_offset(4);
            if (offset + 4 > size) return false;
            int32_t val = 0;
            std::memcpy(&val, buffer + offset, 4);
            offset += 4;
            ss << val;
            return true;
        }
        case ROS_TYPE_UINT64: {
            align_offset(8);
            if (offset + 8 > size) return false;
            uint64_t val = 0;
            std::memcpy(&val, buffer + offset, 8);
            offset += 8;
            ss << val;
            return true;
        }
        case ROS_TYPE_INT64: {
            align_offset(8);
            if (offset + 8 > size) return false;
            int64_t val = 0;
            std::memcpy(&val, buffer + offset, 8);
            offset += 8;
            ss << val;
            return true;
        }
        case ROS_TYPE_FLOAT: {
            align_offset(4);
            if (offset + 4 > size) return false;
            float val = 0;
            std::memcpy(&val, buffer + offset, 4);
            offset += 4;
            ss << val;
            return true;
        }
        case ROS_TYPE_DOUBLE: {
            align_offset(8);
            if (offset + 8 > size) return false;
            double val = 0;
            std::memcpy(&val, buffer + offset, 8);
            offset += 8;
            ss << val;
            return true;
        }
        case ROS_TYPE_STRING: {
            align_offset(4);
            if (offset + 4 > size) return false;
            uint32_t len = 0;
            std::memcpy(&len, buffer + offset, 4);
            offset += 4;
            if (len > 0 && offset + len <= size) {
                std::string str_val(reinterpret_cast<const char*>(buffer + offset), (buffer[offset + len - 1] == '\0') ? len - 1 : len);
                offset += len;
                ss << "\"" << str_val << "\"";
                return true;
            } else if (len == 0) {
                ss << "\"\"";
                return true;
            }
            return false;
        }
        case ROS_TYPE_MESSAGE: {
            if (member.members_ && member.members_->data) {
                const auto* sub_members = static_cast<const ::rosidl_typesupport_introspection_cpp::MessageMembers*>(member.members_->data);
                return parse_cdr_members(sub_members, buffer, size, offset, ss, indent_level);
            }
            return false;
        }
        default:
            return false;
    }
}

static std::string format_serialized_message(const std::string& type_str, const rclcpp::SerializedMessage& serialized_msg) {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", std::localtime(&now_c));

    const uint8_t* buffer = static_cast<const uint8_t*>(serialized_msg.get_rcl_serialized_message().buffer);
    size_t size = serialized_msg.size();

    std::stringstream ss;
    ss << "[" << time_buf << "] ";

    if (size < 4) {
        ss << "{ \"data\": null }";
        return ss.str();
    }

    const auto* members = get_message_members(type_str);
    if (members) {
        size_t offset = 4; // Skip 4-byte CDR header
        std::stringstream json_ss;
        if (parse_cdr_members(members, buffer, size, offset, json_ss, 0)) {
            ss << json_ss.str();
            return ss.str();
        }
    }

    // Generic CDR inspection fallback if typesupport library unavailable
    size_t offset = 4;
    int field_idx = 1;
    ss << "{\n";
    bool first = true;
    while (offset < size && field_idx <= 16) {
        if (offset + 4 <= size) {
            uint32_t len = 0;
            std::memcpy(&len, buffer + offset, 4);
            if (len > 0 && len < 2048 && offset + 4 + len <= size) {
                bool valid_ascii = true;
                for (size_t i = 0; i < len - 1; ++i) {
                    char c = buffer[offset + 4 + i];
                    if (c < 32 || c > 126) {
                        if (c != '\n' && c != '\r' && c != '\t') { valid_ascii = false; break; }
                    }
                }
                if (valid_ascii && (buffer[offset + 4 + len - 1] == '\0' || len == 1)) {
                    std::string str_val(reinterpret_cast<const char*>(buffer + offset + 4), (buffer[offset + 4 + len - 1] == '\0') ? len - 1 : len);
                    if (!str_val.empty()) {
                        if (!first) ss << ",\n";
                        ss << "  \"str_" << field_idx++ << "\": \"" << str_val << "\"";
                        first = false;
                    }
                    offset += 4 + len;
                    while (offset % 4 != 0 && offset < size) offset++;
                    continue;
                }
            }
        }

        uint8_t byte_val = buffer[offset];
        if (byte_val == 0 || byte_val == 1) {
            if (!first) ss << ",\n";
            ss << "  \"flag_" << field_idx++ << "\": " << (byte_val ? "true" : "false");
            first = false;
            offset += 1;
            while (offset % 4 != 0 && offset < size) offset++;
            continue;
        }

        offset += 1;
    }
    if (first) {
        ss << "  \"payload_bytes\": " << size;
    }
    ss << "\n}";
    return ss.str();
}

bool ROS2Manager::subscribe_topic(const std::string& topic, const std::string& type_str, TopicCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_ || !impl_->node_) return false;

    if (impl_->subscriptions_.find(topic) != impl_->subscriptions_.end()) {
        return true;
    }

    try {
        rclcpp::QoS qos(10);
        auto pub_info = impl_->node_->get_publishers_info_by_topic(topic);
        if (!pub_info.empty()) {
            const auto& qos_profile = pub_info.front().qos_profile();
            qos.reliability(qos_profile.reliability());
            qos.durability(qos_profile.durability());
        } else {
            qos.best_effort();
        }

        auto cb = [topic, type_str, callback](std::shared_ptr<rclcpp::SerializedMessage> msg) {
            std::string formatted = format_serialized_message(type_str, *msg);
            callback(topic, formatted);
        };

        auto sub = impl_->node_->create_generic_subscription(topic, type_str, qos, cb);
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
