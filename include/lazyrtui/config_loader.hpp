#pragma once
#include <map>
#include <string>
#include <vector>

namespace lazyrtui {

struct TopicConfig {
  std::vector<std::string> plot_fields;
  std::string plot_type = "line";
};

struct ServicePreset {
  std::map<std::string, std::string> params; // key -> value as string
};

struct UIConfig {
  int auto_refresh_interval_ms = 2000;
  bool mouse_support = true;
};

struct Keybindings {
  std::string switch_focus = "w";
  std::string refresh = "r";
  std::string search = "/";
  std::string echo_topic = "e";
  std::string plot_topic = "p";
  std::string call_service = "c";
  std::string send_goal = "g";
  std::string help = "?";
  std::string quit = "q";
};

struct Config {
  Keybindings keybindings;
  std::map<std::string, TopicConfig> topics;
  std::map<std::string, ServicePreset> services;
  UIConfig ui;
};

class ConfigLoader {
public:
  explicit ConfigLoader(const std::string &config_path = "");
  Config load();

private:
  std::string resolve_config_path() const;
  std::string config_path_;
};

} // namespace lazyrtui
