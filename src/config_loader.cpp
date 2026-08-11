#include "lazyrtui/config_loader.hpp"
#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <iostream>
#include <cstdlib>

namespace lazyrtui {

ConfigLoader::ConfigLoader(const std::string& config_path)
    : config_path_(config_path) {}

std::string ConfigLoader::resolve_config_path() const {
    if (!config_path_.empty() && std::filesystem::exists(config_path_)) {
        return config_path_;
    }

    const char* home = std::getenv("HOME");
    if (home) {
        std::string home_config = std::string(home) + "/.config/lazyrtui/config.yaml";
        if (std::filesystem::exists(home_config)) {
            return home_config;
        }
    }

    std::string default_config = "./config/default_config.yaml";
    if (std::filesystem::exists(default_config)) {
        return default_config;
    }

    return "";
}

Config ConfigLoader::load() {
    Config config;
    std::string path = resolve_config_path();

    if (path.empty()) {
        std::cerr << "No config file found, using defaults.\n";
        return config;
    }

    try {
        YAML::Node node = YAML::LoadFile(path);

        if (node["keybindings"]) {
            auto kb = node["keybindings"];
            if (kb["switch_focus"]) config.keybindings.switch_focus = kb["switch_focus"].as<std::string>();
            if (kb["refresh"]) config.keybindings.refresh = kb["refresh"].as<std::string>();
            if (kb["search"]) config.keybindings.search = kb["search"].as<std::string>();
            if (kb["echo_topic"]) config.keybindings.echo_topic = kb["echo_topic"].as<std::string>();
            if (kb["plot_topic"]) config.keybindings.plot_topic = kb["plot_topic"].as<std::string>();
            if (kb["call_service"]) config.keybindings.call_service = kb["call_service"].as<std::string>();
            if (kb["send_goal"]) config.keybindings.send_goal = kb["send_goal"].as<std::string>();
            if (kb["help"]) config.keybindings.help = kb["help"].as<std::string>();
            if (kb["quit"]) config.keybindings.quit = kb["quit"].as<std::string>();
        }

        if (node["ui"]) {
            auto ui = node["ui"];
            if (ui["auto_refresh_interval_ms"]) config.ui.auto_refresh_interval_ms = ui["auto_refresh_interval_ms"].as<int>();
            if (ui["mouse_support"]) config.ui.mouse_support = ui["mouse_support"].as<bool>();
        }

        if (node["topics"]) {
            for (auto it = node["topics"].begin(); it != node["topics"].end(); ++it) {
                std::string topic_name = it->first.as<std::string>();
                TopicConfig t_conf;
                if (it->second["plot_fields"]) {
                    for (const auto& field : it->second["plot_fields"]) {
                        t_conf.plot_fields.push_back(field.as<std::string>());
                    }
                }
                if (it->second["plot_type"]) {
                    t_conf.plot_type = it->second["plot_type"].as<std::string>();
                }
                config.topics[topic_name] = t_conf;
            }
        }

        if (node["services"]) {
            for (auto it = node["services"].begin(); it != node["services"].end(); ++it) {
                std::string service_name = it->first.as<std::string>();
                ServicePreset s_preset;
                if (it->second["params"]) {
                    for (auto p_it = it->second["params"].begin(); p_it != it->second["params"].end(); ++p_it) {
                        s_preset.params[p_it->first.as<std::string>()] = p_it->second.as<std::string>();
                    }
                }
                config.services[service_name] = s_preset;
            }
        }

    } catch (const YAML::Exception& e) {
        std::cerr << "Error parsing config file " << path << ": " << e.what() << "\n";
    }

    return config;
}

} // namespace lazyrtui
