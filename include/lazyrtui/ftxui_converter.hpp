#pragma once

#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace lazyrtui {

class FTXUIConverter {
public:
  /// Convert a JSON specification (from Python plugin) into FTXUI Elements.
  static ftxui::Elements parse_ui_spec(const nlohmann::json &spec);

  /// Convert a single JSON node into an FTXUI Element.
  static ftxui::Element parse_node(const nlohmann::json &node);

private:
  static ftxui::Decorator parse_style(const nlohmann::json &node);
  static ftxui::Color parse_color(const std::string &color_name);
  static ftxui::Element parse_canvas(const nlohmann::json &node);
};

} // namespace lazyrtui
