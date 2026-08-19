#include "lazyrtui/ros_manager.hpp"
#include "cdr_utils.hpp"
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
#include <map>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <dlfcn.h>
#include <cmath>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <ament_index_cpp/get_packages_with_prefixes.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>
#include <rcl/error_handling.h>
#include <rcl/graph.h>
#include <rcl/service.h>
#include <rosidl_typesupport_introspection_cpp/service_introspection.hpp>
#include <rosidl_runtime_cpp/message_initialization.hpp>
#include <nlohmann/json.hpp>
#include <rmw/serialized_message.h>
#include <rmw/types.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace std::chrono_literals;

namespace lazyrtui {

// Forward declaration: defined near the typesupport-cache section below.
static void cleanup_typesupport_caches();

// Forward declarations: introspection helpers defined later in this file.
static const ::rosidl_typesupport_introspection_cpp::MessageMembers* get_message_members(
    const std::string& type_str);
static const ::rosidl_typesupport_introspection_cpp::ServiceMembers* get_service_members(
    const std::string& type_str, const rosidl_service_type_support_t** out_ts);
static void read_struct_members(
    const ::rosidl_typesupport_introspection_cpp::MessageMembers* members, const void* base,
    std::stringstream& ss, int indent_level);

struct ROS2Manager::Impl {
    rclcpp::Node::SharedPtr node_;
    rclcpp::executors::SingleThreadedExecutor::SharedPtr executor_;
    std::map<std::string, std::shared_ptr<rclcpp::GenericSubscription>> subscriptions_;
    rclcpp::Subscription<tf2_msgs::msg::TFMessage>::SharedPtr tf_sub_;
    rclcpp::Subscription<tf2_msgs::msg::TFMessage>::SharedPtr tf_static_sub_;

    // Managed worker for async service calls (Issue #5): no more per-call
    // detached threads. The worker is created unconditionally so calls work
    // even before a ROS connection exists; it is joined exactly once in
    // stop() (and as a safety net in ~Impl).
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<std::function<void()>> tasks_;
    std::atomic<bool> worker_shutdown_{false};
    std::thread worker_;

    Impl() : worker_([this] { worker_loop(); }) {}

    ~Impl() {
        // Safety net: guarantee the worker is joined exactly once even if
        // stop() never ran (e.g. constructor/start failure paths).
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            worker_shutdown_ = true;
        }
        queue_cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait(lock, [this] {
                    return worker_shutdown_.load() || !tasks_.empty();
                });
                if (worker_shutdown_.load()) {
                    return;  // Drop remaining tasks at shutdown.
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            try {
                task();
            } catch (...) {
            }
        }
    }
};

ROS2Manager::ROS2Manager(const std::string& node_name)
    : node_name_(node_name), impl_(std::make_unique<Impl>()) {}

ROS2Manager::~ROS2Manager() {
    stop();
    cleanup_typesupport_caches();
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
    // Signal the background worker first: in-flight service calls poll these
    // flags and exit promptly. Idempotent — worker_ is joined exactly once.
    stop_requested_ = true;
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex_);
        impl_->worker_shutdown_ = true;
    }
    impl_->queue_cv_.notify_all();
    if (impl_->worker_.joinable()) {
        impl_->worker_.join();
    }
    if (!connected_) return;
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
            const double timestamp =
                static_cast<double>(transform.header.stamp.sec) +
                static_cast<double>(transform.header.stamp.nanosec) * 1e-9;
            tf_tree_.update_transform(
                transform.header.frame_id,
                transform.child_frame_id,
                transform.transform.translation.x,
                transform.transform.translation.y,
                transform.transform.translation.z,
                transform.transform.rotation.x,
                transform.transform.rotation.y,
                transform.transform.rotation.z,
                transform.transform.rotation.w,
                timestamp
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
                    t_act = t_act.substr(0, pos);
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
    (void)service_name;
    const rosidl_service_type_support_t* ts = nullptr;
    const auto* members = get_service_members(type_str, &ts);
    if (!members || !members->request_members_) {
        return "{\n  \"error\": \"Cannot load request typesupport for '" + type_str + "'\"\n}";
    }
    // Build the default request: initialize an ALL-defaulted struct and read
    // it back as JSON (reuses the introspection read path; no CDR involved).
    const auto* req_members = members->request_members_;
    std::vector<uint8_t> storage(req_members->size_of_);
    void* base = storage.data();
    req_members->init_function(base, rosidl_runtime_cpp::MessageInitialization::ALL);
    std::stringstream ss;
    read_struct_members(req_members, base, ss, 0);
    req_members->fini_function(base);
    return ss.str();
}

std::string ROS2Manager::get_action_goal_json(const std::string& action_name, const std::string& type_str) {
    (void)action_name;
    // In ROS 2 the action goal is introspected as its `_Goal` message.
    const std::string goal_type = type_str + "_Goal";
    const auto* members = get_message_members(goal_type);
    if (!members) {
        return "{\n  \"error\": \"Cannot load goal typesupport for '" + goal_type + "'\"\n}";
    }
    std::vector<uint8_t> storage(members->size_of_);
    void* base = storage.data();
    members->init_function(base, rosidl_runtime_cpp::MessageInitialization::ALL);
    std::stringstream ss;
    read_struct_members(members, base, ss, 0);
    members->fini_function(base);
    return ss.str();
}

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

    std::string pkg, kind = "msg", msg_name;
    size_t slash1 = type_str.find('/');
    if (slash1 != std::string::npos) {
        pkg = type_str.substr(0, slash1);
        size_t slash2 = type_str.find('/', slash1 + 1);
        if (slash2 != std::string::npos) {
            // Middle segment is "msg", "srv" or "action" (action-generated
            // messages like pkg/action/Name_Goal export with `action`).
            kind = type_str.substr(slash1 + 1, slash2 - slash1 - 1);
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

    std::string sym_name = "rosidl_typesupport_introspection_cpp__get_message_type_support_handle__" + pkg + "__" + kind + "__" + msg_name;
    using GetTSFn = const rosidl_message_type_support_t* (*)();
    GetTSFn get_ts_fn = reinterpret_cast<GetTSFn>(dlsym(handle, sym_name.c_str()));
    if (!get_ts_fn) {
        sym_name = "rosidl_typesupport_c__get_message_type_support_handle__" + pkg + "__" + kind + "__" + msg_name;
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

struct ServiceTypeSupportHandleInfo {
    void* intro_lib_handle = nullptr;
    void* client_lib_handle = nullptr;
    const rosidl_service_type_support_t* client_ts = nullptr;
    const ::rosidl_typesupport_introspection_cpp::ServiceMembers* members = nullptr;
};

static std::map<std::string, ServiceTypeSupportHandleInfo> g_service_typesupport_cache;

// Loads the runtime introspection typesupport for a service ("pkg/srv/Name").
// Returns the ServiceMembers (request/response field metadata) and, via
// out_ts, the rosidl_service_type_support_t* (from rosidl_typesupport_c/cpp)
// needed by rcl_client_init / RMW.
static const ::rosidl_typesupport_introspection_cpp::ServiceMembers* get_service_members(
    const std::string& type_str, const rosidl_service_type_support_t** out_ts) {
    // Shares the same guard as the message cache: worker threads call this.
    std::lock_guard<std::mutex> lock(g_typesupport_mutex);
    auto it = g_service_typesupport_cache.find(type_str);
    if (it != g_service_typesupport_cache.end()) {
        if (out_ts) *out_ts = it->second.client_ts;
        return it->second.members;
    }

    std::string pkg, kind = "srv", srv_name;
    size_t slash1 = type_str.find('/');
    if (slash1 == std::string::npos) return nullptr;
    pkg = type_str.substr(0, slash1);
    size_t slash2 = type_str.find('/', slash1 + 1);
    if (slash2 != std::string::npos) {
        // Middle segment is "srv" or "action" (action-generated services like
        // pkg/action/Name_SendGoal_Service export with the `action` segment).
        kind = type_str.substr(slash1 + 1, slash2 - slash1 - 1);
        srv_name = type_str.substr(slash2 + 1);
    } else {
        srv_name = type_str.substr(slash1 + 1);
    }
    if (pkg.empty() || srv_name.empty()) return nullptr;
    // Action services export without the "_Service" suffix (e.g.
    // ...__action__Name_SendGoal), while the graph type keeps it.
    if (kind == "action" && srv_name.size() > 8 &&
        srv_name.compare(srv_name.size() - 8, 8, "_Service") == 0) {
        srv_name.erase(srv_name.size() - 8);
    }

    // 1. Load introspection typesupport for member metadata
    std::string intro_lib_name =
        "lib" + pkg + "__rosidl_typesupport_introspection_cpp.so";
    void* intro_handle = dlopen(intro_lib_name.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    if (!intro_handle) return nullptr;

    std::string intro_sym_name =
        "rosidl_typesupport_introspection_cpp__get_service_type_support_handle__" +
        pkg + "__" + kind + "__" + srv_name;
    using GetTSFn = const rosidl_service_type_support_t* (*)();
    GetTSFn get_intro_ts_fn =
        reinterpret_cast<GetTSFn>(dlsym(intro_handle, intro_sym_name.c_str()));
    if (!get_intro_ts_fn) {
        dlclose(intro_handle);
        return nullptr;
    }

    const rosidl_service_type_support_t* intro_ts = get_intro_ts_fn();
    if (!intro_ts || !intro_ts->data) {
        dlclose(intro_handle);
        return nullptr;
    }

    const auto* members = static_cast<
        const ::rosidl_typesupport_introspection_cpp::ServiceMembers*>(intro_ts->data);

    // 2. Load C/CPP typesupport dispatcher handle for rcl_client_init / RMW
    void* client_handle = nullptr;
    const rosidl_service_type_support_t* client_ts = nullptr;

    std::string c_lib_name = "lib" + pkg + "__rosidl_typesupport_c.so";
    client_handle = dlopen(c_lib_name.c_str(), RTLD_LAZY | RTLD_GLOBAL);
    if (client_handle) {
        std::string c_sym_name =
            "rosidl_typesupport_c__get_service_type_support_handle__" +
            pkg + "__" + kind + "__" + srv_name;
        GetTSFn get_c_ts_fn =
            reinterpret_cast<GetTSFn>(dlsym(client_handle, c_sym_name.c_str()));
        if (get_c_ts_fn) {
            client_ts = get_c_ts_fn();
        }
    }

    if (!client_ts) {
        if (client_handle) {
            dlclose(client_handle);
            client_handle = nullptr;
        }
        std::string cpp_lib_name = "lib" + pkg + "__rosidl_typesupport_cpp.so";
        client_handle = dlopen(cpp_lib_name.c_str(), RTLD_LAZY | RTLD_GLOBAL);
        if (client_handle) {
            std::string cpp_sym_name =
                "rosidl_typesupport_cpp__get_service_type_support_handle__" +
                pkg + "__" + kind + "__" + srv_name;
            GetTSFn get_cpp_ts_fn =
                reinterpret_cast<GetTSFn>(dlsym(client_handle, cpp_sym_name.c_str()));
            if (get_cpp_ts_fn) {
                client_ts = get_cpp_ts_fn();
            }
        }
    }

    g_service_typesupport_cache[type_str] = {intro_handle, client_handle,
                                             client_ts, members};
    if (out_ts) *out_ts = client_ts;
    return members;
}

// Closes all dynamically loaded typesupport libraries. Called from
// ~ROS2Manager; idempotent and safe on repeated shutdown.
static void cleanup_typesupport_caches() {
    std::lock_guard<std::mutex> lock(g_typesupport_mutex);
    for (auto& [type, info] : g_typesupport_cache) {
        (void)type;
        if (info.lib_handle) dlclose(info.lib_handle);
    }
    g_typesupport_cache.clear();
    for (auto& [type, info] : g_service_typesupport_cache) {
        (void)type;
        if (info.intro_lib_handle) dlclose(info.intro_lib_handle);
        if (info.client_lib_handle) dlclose(info.client_lib_handle);
    }
    g_service_typesupport_cache.clear();
}

// --- Introspection-driven message struct construction ----------------------
// Fills the (already initialized) C++ message struct at `base` from `obj`,
// writing each field at its introspection offset_. Returns false on a missing
// or type-invalid field so requests never silently go out half-filled.

static bool fill_struct_field(const ::rosidl_typesupport_introspection_cpp::MessageMember& member,
                              void* field, const nlohmann::json& value) {
    using namespace ::rosidl_typesupport_introspection_cpp;

    if (member.is_array_) {
        if (!value.is_array()) return false;
        const size_t count = value.size();
        const bool is_fixed = (member.is_upper_bound_ || member.array_size_ > 0);
        if (!is_fixed) {
            member.resize_function(field, count);  // Default-constructs elements.
        } else if (member.is_upper_bound_ && count > member.array_size_) {
            return false;  // Exceeds the declared upper bound.
        } else if (!member.is_upper_bound_ && count != member.array_size_) {
            return false;  // Fixed array size mismatch.
        }
        MessageMember elem_member = member;
        elem_member.is_array_ = false;
        elem_member.array_size_ = 0;
        for (size_t j = 0; j < count; ++j) {
            void* elem = member.get_function ? member.get_function(field, j) : nullptr;
            if (elem) {
                if (!fill_struct_field(elem_member, elem, value[j])) return false;
            } else if (member.type_id_ == ROS_TYPE_BOOLEAN && member.assign_function) {
                // std::vector<bool> stores packed bits and exposes no element
                // accessor: assign through the generated assign_function.
                if (!value[j].is_boolean()) return false;
                bool b = value[j].get<bool>();
                member.assign_function(field, j, &b);
            } else {
                return false;  // No element accessor available.
            }
        }
        return true;
    }

    switch (member.type_id_) {
        case ROS_TYPE_BOOLEAN: {
            if (!value.is_boolean()) return false;
            *static_cast<bool*>(field) = value.get<bool>();
            return true;
        }
        case ROS_TYPE_UINT8: {
            if (!value.is_number_unsigned() && !value.is_number_integer()) return false;
            *static_cast<uint8_t*>(field) = value.get<uint8_t>();
            return true;
        }
        case ROS_TYPE_INT8:
        case ROS_TYPE_CHAR: {
            if (!value.is_number_integer() && !value.is_number_unsigned()) return false;
            *static_cast<int8_t*>(field) = value.get<int8_t>();
            return true;
        }
        case ROS_TYPE_UINT16: {
            if (!value.is_number_unsigned() && !value.is_number_integer()) return false;
            *static_cast<uint16_t*>(field) = value.get<uint16_t>();
            return true;
        }
        case ROS_TYPE_INT16: {
            if (!value.is_number_integer() && !value.is_number_unsigned()) return false;
            *static_cast<int16_t*>(field) = value.get<int16_t>();
            return true;
        }
        case ROS_TYPE_UINT32: {
            if (!value.is_number_unsigned() && !value.is_number_integer()) return false;
            *static_cast<uint32_t*>(field) = value.get<uint32_t>();
            return true;
        }
        case ROS_TYPE_INT32: {
            if (!value.is_number_integer() && !value.is_number_unsigned()) return false;
            *static_cast<int32_t*>(field) = value.get<int32_t>();
            return true;
        }
        case ROS_TYPE_UINT64: {
            if (!value.is_number_unsigned() && !value.is_number_integer()) return false;
            *static_cast<uint64_t*>(field) = value.get<uint64_t>();
            return true;
        }
        case ROS_TYPE_INT64: {
            if (!value.is_number_integer() && !value.is_number_unsigned()) return false;
            *static_cast<int64_t*>(field) = value.get<int64_t>();
            return true;
        }
        case ROS_TYPE_FLOAT: {
            if (!value.is_number()) return false;
            *static_cast<float*>(field) = value.get<float>();
            return true;
        }
        case ROS_TYPE_DOUBLE: {
            if (!value.is_number()) return false;
            *static_cast<double*>(field) = value.get<double>();
            return true;
        }
        case ROS_TYPE_STRING: {
            if (!value.is_string()) return false;
            std::string str = value.get<std::string>();
            if (member.string_upper_bound_ > 0 &&
                str.size() > member.string_upper_bound_) {
                return false;  // Exceeds the declared upper bound.
            }
            static_cast<std::string*>(field)->assign(str);
            return true;
        }
        case ROS_TYPE_MESSAGE: {
            if (!value.is_object()) return false;
            if (!member.members_ || !member.members_->data) return false;
            const auto* sub_members = static_cast<const MessageMembers*>(member.members_->data);
            for (uint32_t i = 0; i < sub_members->member_count_; ++i) {
                const auto& sub_member = sub_members->members_[i];
                auto it = value.find(sub_member.name_);
                if (it == value.end()) return false;  // Missing field.
                if (!fill_struct_field(sub_member,
                                       static_cast<char*>(field) + sub_member.offset_,
                                       *it)) {
                    return false;
                }
            }
            return true;
        }
        default:
            return false;  // Unsupported type (e.g. WSTRING): fail loudly.
    }
}

static bool fill_struct_members(const ::rosidl_typesupport_introspection_cpp::MessageMembers* members,
                                void* base, const nlohmann::json& obj) {
    if (!members || !obj.is_object()) return false;
    for (uint32_t i = 0; i < members->member_count_; ++i) {
        const auto& member = members->members_[i];
        if (std::string(member.name_) == "structure_needs_at_least_one_member") {
            continue;
        }
        auto it = obj.find(member.name_);
        if (it == obj.end()) return false;  // Missing field: reject loudly.
        if (!fill_struct_field(member, static_cast<char*>(base) + member.offset_, *it)) {
            return false;
        }
    }
    return true;
}

// --- Introspection-driven message struct reading (host struct -> JSON) -----

static void read_struct_field(const ::rosidl_typesupport_introspection_cpp::MessageMember& member,
                              const void* field, std::stringstream& ss, int indent_level);

static void read_struct_members(const ::rosidl_typesupport_introspection_cpp::MessageMembers* members,
                                const void* base, std::stringstream& ss, int indent_level) {
    if (!members) return;
    if (members->member_count_ == 0 ||
        (members->member_count_ == 1 &&
         std::string(members->members_[0].name_) == "structure_needs_at_least_one_member")) {
        ss << "{}";
        return;
    }
    std::string indent(indent_level * 2, ' ');
    ss << "{\n";
    bool first = true;
    for (uint32_t i = 0; i < members->member_count_; ++i) {
        const auto& member = members->members_[i];
        if (std::string(member.name_) == "structure_needs_at_least_one_member") {
            continue;
        }
        if (!first) ss << ",\n";
        first = false;
        ss << indent << "  \"" << member.name_ << "\": ";
        read_struct_field(member, static_cast<const char*>(base) + member.offset_, ss, indent_level + 1);
    }
    ss << "\n" << indent << "}";
}

static void read_struct_field(const ::rosidl_typesupport_introspection_cpp::MessageMember& member,
                              const void* field, std::stringstream& ss, int indent_level) {
    using namespace ::rosidl_typesupport_introspection_cpp;

    if (member.is_array_) {
        const size_t count = member.size_function(field);
        ss << "[";
        MessageMember elem_member = member;
        elem_member.is_array_ = false;
        elem_member.array_size_ = 0;
        for (size_t j = 0; j < count; ++j) {
            if (j > 0) ss << ", ";
            const void* elem =
                member.get_const_function ? member.get_const_function(field, j) : nullptr;
            if (elem) {
                read_struct_field(elem_member, elem, ss, indent_level);
            } else if (member.type_id_ == ROS_TYPE_BOOLEAN && member.fetch_function) {
                // std::vector<bool>: fetch the packed bit into a local bool.
                bool b = false;
                member.fetch_function(field, j, &b);
                ss << (b ? "true" : "false");
            } else {
                ss << "null";
            }
        }
        ss << "]";
        return;
    }

    switch (member.type_id_) {
        case ROS_TYPE_BOOLEAN:
            ss << (*static_cast<const bool*>(field) ? "true" : "false");
            break;
        case ROS_TYPE_UINT8:
            ss << static_cast<unsigned>(*static_cast<const uint8_t*>(field));
            break;
        case ROS_TYPE_INT8:
        case ROS_TYPE_CHAR:
            ss << static_cast<int>(*static_cast<const int8_t*>(field));
            break;
        case ROS_TYPE_UINT16:
            ss << *static_cast<const uint16_t*>(field);
            break;
        case ROS_TYPE_INT16:
            ss << *static_cast<const int16_t*>(field);
            break;
        case ROS_TYPE_UINT32:
            ss << *static_cast<const uint32_t*>(field);
            break;
        case ROS_TYPE_INT32:
            ss << *static_cast<const int32_t*>(field);
            break;
        case ROS_TYPE_UINT64:
            ss << *static_cast<const uint64_t*>(field);
            break;
        case ROS_TYPE_INT64:
            ss << *static_cast<const int64_t*>(field);
            break;
        case ROS_TYPE_FLOAT:
            ss << *static_cast<const float*>(field);
            break;
        case ROS_TYPE_DOUBLE:
            ss << *static_cast<const double*>(field);
            break;
        case ROS_TYPE_STRING:
            ss << "\"" << *static_cast<const std::string*>(field) << "\"";
            break;
        case ROS_TYPE_MESSAGE: {
            if (member.members_ && member.members_->data) {
                const auto* sub_members = static_cast<const MessageMembers*>(member.members_->data);
                read_struct_members(sub_members, field, ss, indent_level);
            } else {
                ss << "null";
            }
            break;
        }
        default:
            ss << "null";
            break;
    }
}

static bool parse_cdr_field(const ::rosidl_typesupport_introspection_cpp::MessageMember& member,
                            const uint8_t* buffer, size_t size, size_t& offset,
                            std::stringstream& ss, int indent_level, bool swap_bytes);

static bool parse_cdr_members(const ::rosidl_typesupport_introspection_cpp::MessageMembers* members,
                             const uint8_t* buffer, size_t size, size_t& offset,
                             std::stringstream& ss, int indent_level, bool swap_bytes) {
    if (!members) return false;
    std::string indent(indent_level * 2, ' ');
    ss << "{\n";
    for (uint32_t i = 0; i < members->member_count_; ++i) {
        const auto& member = members->members_[i];
        if (i > 0) ss << ",\n";
        ss << indent << "  \"" << member.name_ << "\": ";
        if (!parse_cdr_field(member, buffer, size, offset, ss, indent_level + 1,
                             swap_bytes)) {
            ss << "null";
        }
    }
    ss << "\n" << indent << "}";
    return true;
}

static bool parse_cdr_field(const ::rosidl_typesupport_introspection_cpp::MessageMember& member,
                            const uint8_t* buffer, size_t size, size_t& offset,
                            std::stringstream& ss, int indent_level, bool swap_bytes) {
    using namespace rosidl_typesupport_introspection_cpp;

    auto align_offset = [&](size_t align) {
        while (offset % align != 0 && offset < size) offset++;
    };

    if (member.is_array_) {
        align_offset(4);
        uint32_t count = member.array_size_;
        // Read the actual element count from the wire for every non-fixed
        // array (unbounded AND bounded sequences); only fixed arrays have no
        // count on the wire.
        if (member.is_upper_bound_ || count == 0) {
            if (offset + 4 > size) return false;
            std::memcpy(&count, buffer + offset, 4);
            if (swap_bytes) count = cdr_byte_swap(count);
            offset += 4;
        }

        ss << "[";
        if (count > 20) count = 20;
        for (uint32_t j = 0; j < count; ++j) {
            if (j > 0) ss << ", ";
            ::rosidl_typesupport_introspection_cpp::MessageMember elem_member = member;
            elem_member.is_array_ = false;
            elem_member.array_size_ = 0;
            if (!parse_cdr_field(elem_member, buffer, size, offset, ss, indent_level,
                                 swap_bytes)) {
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
            if (swap_bytes) val = cdr_byte_swap(val);
            offset += 2;
            ss << val;
            return true;
        }
        case ROS_TYPE_INT16: {
            align_offset(2);
            if (offset + 2 > size) return false;
            int16_t val = 0;
            std::memcpy(&val, buffer + offset, 2);
            if (swap_bytes) val = cdr_byte_swap(val);
            offset += 2;
            ss << val;
            return true;
        }
        case ROS_TYPE_UINT32: {
            align_offset(4);
            if (offset + 4 > size) return false;
            uint32_t val = 0;
            std::memcpy(&val, buffer + offset, 4);
            if (swap_bytes) val = cdr_byte_swap(val);
            offset += 4;
            ss << val;
            return true;
        }
        case ROS_TYPE_INT32: {
            align_offset(4);
            if (offset + 4 > size) return false;
            int32_t val = 0;
            std::memcpy(&val, buffer + offset, 4);
            if (swap_bytes) val = cdr_byte_swap(val);
            offset += 4;
            ss << val;
            return true;
        }
        case ROS_TYPE_UINT64: {
            align_offset(8);
            if (offset + 8 > size) return false;
            uint64_t val = 0;
            std::memcpy(&val, buffer + offset, 8);
            if (swap_bytes) val = cdr_byte_swap(val);
            offset += 8;
            ss << val;
            return true;
        }
        case ROS_TYPE_INT64: {
            align_offset(8);
            if (offset + 8 > size) return false;
            int64_t val = 0;
            std::memcpy(&val, buffer + offset, 8);
            if (swap_bytes) val = cdr_byte_swap(val);
            offset += 8;
            ss << val;
            return true;
        }
        case ROS_TYPE_FLOAT: {
            align_offset(4);
            if (offset + 4 > size) return false;
            float val = 0;
            std::memcpy(&val, buffer + offset, 4);
            if (swap_bytes) val = cdr_byte_swap(val);
            offset += 4;
            ss << val;
            return true;
        }
        case ROS_TYPE_DOUBLE: {
            align_offset(8);
            if (offset + 8 > size) return false;
            double val = 0;
            std::memcpy(&val, buffer + offset, 8);
            if (swap_bytes) val = cdr_byte_swap(val);
            offset += 8;
            ss << val;
            return true;
        }
        case ROS_TYPE_STRING: {
            align_offset(4);
            if (offset + 4 > size) return false;
            uint32_t len = 0;
            std::memcpy(&len, buffer + offset, 4);
            if (swap_bytes) len = cdr_byte_swap(len);
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
                return parse_cdr_members(sub_members, buffer, size, offset, ss, indent_level, swap_bytes);
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

    // CDR encapsulation header byte 0: 0x01 = little-endian, 0x00 = big-endian
    // (bytes 1-3 are options/reserved). Swap when the stream order differs
    // from the host.
    const bool swap_bytes = (buffer[0] == 0x01) != g_host_is_little_endian;

    const auto* members = get_message_members(type_str);
    if (members) {
        size_t offset = 4; // Skip 4-byte CDR header
        std::stringstream json_ss;
        if (parse_cdr_members(members, buffer, size, offset, json_ss, 0, swap_bytes)) {
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
            if (swap_bytes) len = cdr_byte_swap(len);
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

void ROS2Manager::call_service_async(const std::string& service_name, const std::string& type_str,
                                     const std::string& request_json, ServiceCallback callback) {
    // Route the call onto the managed worker; no per-call detached threads.
    std::lock_guard<std::mutex> lock(impl_->queue_mutex_);
    if (impl_->worker_shutdown_.load() || stop_requested_.load()) {
        // Fail fast instead of silently dropping the task: the UI would
        // otherwise wait forever for a response callback after shutdown.
        callback(false, "ROS 2 is shutting down", 0.0);
        return;
    }
    impl_->tasks_.emplace([this, service_name, type_str, request_json, callback]() {
        execute_service_call(service_name, type_str, request_json, callback);
    });
    impl_->queue_cv_.notify_one();
}

void ROS2Manager::execute_service_call(const std::string& service_name, const std::string& type_str,
                                       const std::string& request_json, ServiceCallback callback) {
    auto start_time = std::chrono::steady_clock::now();
    auto elapsed_ms = [&start_time]() {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - start_time)
            .count();
    };

    // Node lifetime guard: an in-flight call may overlap stop(). stop()
    // sets the shutdown flags before joining the worker, so a running
    // call observes them and exits its poll loops promptly.
    if (!rclcpp::ok() || stop_requested_.load()) {
        callback(false, "ROS 2 is shutting down", elapsed_ms());
        return;
    }

    // Keep the node alive for the whole call: stop() resets impl_->node_
    // after the worker is joined, but we still copy the shared_ptr so the
    // rcl_node_t memory stays valid for the duration.
    auto node = impl_->node_;
    if (!node) {
        callback(false, "ROS 2 is shutting down", elapsed_ms());
        return;
    }

    const rosidl_service_type_support_t* ts = nullptr;
    const auto* members = get_service_members(type_str, &ts);
    if (!members || !ts) {
        callback(false, "Failed to load service typesupport for '" + type_str + "'",
                 elapsed_ms());
        return;
    }

    nlohmann::json req;
    try {
        req = nlohmann::json::parse(request_json);
    } catch (const std::exception& e) {
        callback(false, std::string("Invalid request JSON: ") + e.what(), elapsed_ms());
        return;
    }
    if (!req.is_object()) {
        callback(false, "Request JSON must be an object", elapsed_ms());
        return;
    }

    rcl_node_t* node_handle =
        node->get_node_base_interface()->get_rcl_node_handle();

    // RAII: rcl_client_fini needs the node handle and runs on every exit.
    struct ClientRaii {
        rcl_client_t client = rcl_get_zero_initialized_client();
        rcl_node_t* node = nullptr;
        bool initialized = false;
        ~ClientRaii() {
            if (initialized) {
                // A destructor cannot propagate the error; store the result to
                // silence -Wunused-result (a plain (void) cast does not).
                const rcl_ret_t rc = rcl_client_fini(&client, node);
                (void)rc;
            }
        }
    } client_raii;
    client_raii.node = node_handle;

    rcl_client_options_t options = rcl_client_get_default_options();
    rcl_ret_t ret = rcl_client_init(&client_raii.client, node_handle, ts,
                                    service_name.c_str(), &options);
    if (ret != RCL_RET_OK) {
        callback(false, std::string("rcl_client_init failed: ") + rcl_get_error_string().str,
                 elapsed_ms());
        rcl_reset_error();
        return;
    }
    client_raii.initialized = true;

    // Wait for a server to become available (bounded; mirrors `ros2 service call`).
    bool available = false;
    auto avail_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < avail_deadline && rclcpp::ok() &&
           !stop_requested_.load()) {
        if (rcl_service_server_is_available(node_handle, &client_raii.client,
                                            &available) != RCL_RET_OK)
            break;
        if (available) break;
        std::this_thread::sleep_for(50ms);
    }
    if (!available) {
        callback(false, "Service '" + service_name + "' not available", elapsed_ms());
        return;
    }

    // Build the request message struct from JSON via introspection.
    const auto* req_members = members->request_members_;
    std::vector<uint8_t> req_storage(req_members->size_of_);
    void* req_struct = req_storage.data();
    req_members->init_function(req_struct,
                               rosidl_runtime_cpp::MessageInitialization::ALL);

    bool filled = false;
    try {
        filled = fill_struct_members(req_members, req_struct, req);
    } catch (const std::exception&) {
        filled = false;
    }
    if (!filled) {
        req_members->fini_function(req_struct);
        callback(false, "Failed to build request (missing or invalid fields)", elapsed_ms());
        return;
    }

    int64_t sequence_number = 0;
    ret = rcl_send_request(&client_raii.client, req_struct, &sequence_number);
    // Serialization is synchronous; the request struct can be destroyed now.
    req_members->fini_function(req_struct);
    if (ret != RCL_RET_OK) {
        callback(false, std::string("rcl_send_request failed: ") + rcl_get_error_string().str,
                 elapsed_ms());
        rcl_reset_error();
        return;
    }

    // Take the response struct (poll, bounded 10 s).
    const auto* resp_members = members->response_members_;
    std::vector<uint8_t> resp_storage(resp_members->size_of_);
    void* resp_struct = resp_storage.data();
    resp_members->init_function(resp_struct,
                                rosidl_runtime_cpp::MessageInitialization::ALL);

    const auto resp_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool got_response = false;
    std::string take_error;
    while (std::chrono::steady_clock::now() < resp_deadline && rclcpp::ok() &&
           !stop_requested_.load()) {
        rmw_request_id_t resp_id;
        rcl_ret_t tr = rcl_take_response(&client_raii.client, &resp_id, resp_struct);
        if (tr == RCL_RET_OK) {
            got_response = true;
            break;
        }
        if (tr != RCL_RET_CLIENT_TAKE_FAILED) {
            take_error = std::string("rcl_take_response failed: ") + rcl_get_error_string().str;
            rcl_reset_error();
            break;
        }
        std::this_thread::sleep_for(25ms);
    }

    if (!got_response) {
        resp_members->fini_function(resp_struct);
        callback(false,
                 take_error.empty() ? "Service call timed out after 10s" : take_error,
                 elapsed_ms());
        return;
    }

    // Read the response struct directly into JSON text.
    std::stringstream ss;
    read_struct_members(resp_members, resp_struct, ss, 0);
    resp_members->fini_function(resp_struct);
    callback(true, ss.str(), elapsed_ms());
}

std::map<std::string, std::vector<std::string>> ROS2Manager::get_interfaces_tree() {
    std::map<std::string, std::vector<std::string>> result;
    try {
        const auto packages = ament_index_cpp::get_packages_with_prefixes();
        for (const auto& [pkg, prefix] : packages) {
            (void)prefix;
            std::string share_dir;
            try {
                share_dir = ament_index_cpp::get_package_share_directory(pkg);
            } catch (const std::exception&) {
                continue;  // Package share dir unavailable.
            }
            std::vector<std::string> entries;
            for (const char* sub : {"msg", "srv", "action"}) {
                std::filesystem::path dir = std::filesystem::path(share_dir) / sub;
                std::error_code ec;
                if (!std::filesystem::is_directory(dir, ec)) continue;
                std::string expected_ext = std::string(".") + sub;
                for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                    if (!entry.is_regular_file(ec)) continue;
                    if (entry.path().extension().string() == expected_ext) {
                        entries.push_back(pkg + "/" + sub + "/" +
                                          entry.path().stem().string());
                    }
                }
            }
            std::sort(entries.begin(), entries.end());
            result[pkg] = std::move(entries);
        }
    } catch (...) {
    }
    return result;
}

std::string ROS2Manager::get_interface_detail(const std::string& type_str) {
    try {
        // Accept "pkg/msg/Type", "pkg/srv/Type", "pkg/action/Type", or "pkg/Type".
        std::string pkg, kind, name;
        size_t s1 = type_str.find('/');
        if (s1 == std::string::npos) return "Error: invalid type '" + type_str + "'";
        pkg = type_str.substr(0, s1);
        size_t s2 = type_str.find('/', s1 + 1);
        if (s2 != std::string::npos) {
            kind = type_str.substr(s1 + 1, s2 - s1 - 1);
            name = type_str.substr(s2 + 1);
        } else {
            name = type_str.substr(s1 + 1);
        }
        if (pkg.empty() || name.empty()) return "Error: invalid type '" + type_str + "'";

        // Reject path traversal: name/kind become file-path components.
        auto is_unsafe = [](const std::string& s) {
            return s.find('/') != std::string::npos ||
                   s.find('\\') != std::string::npos || s == "..";
        };
        if (is_unsafe(name) || is_unsafe(kind)) {
            return "Error: invalid type '" + type_str + "'";
        }

        std::vector<std::string> kinds;
        std::vector<std::string> exts;
        if (!kind.empty()) {
            kinds = {kind};
            exts = {"." + kind};
        } else {
            kinds = {"msg", "srv", "action"};
            exts = {".msg", ".srv", ".action"};
        }

        const std::string share_dir = ament_index_cpp::get_package_share_directory(pkg);
        for (size_t i = 0; i < kinds.size(); ++i) {
            std::filesystem::path file =
                std::filesystem::path(share_dir) / kinds[i] / (name + exts[i]);
            std::error_code ec;
            if (!std::filesystem::is_regular_file(file, ec)) continue;
            std::ifstream in(file);
            if (!in) continue;
            std::stringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }
        return "Error: interface not found: '" + type_str + "'";
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
