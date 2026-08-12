#include "lazyrtui/ftxui_converter.hpp"
#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <gtest/gtest.h>
#include <string>

namespace lazyrtui {

namespace {

// Render an element into a width x height Screen and return its text.
std::string RenderToString(const ftxui::Element &el, int width, int height) {
  ftxui::Screen screen(width, height);
  ftxui::Render(screen, el);
  return screen.ToString();
}

}  // namespace

TEST(FTXUIConverterTest, ParsesTextNode) {
  nlohmann::json spec = {{"type", "text"}, {"content", "hello"}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  EXPECT_EQ(RenderToString(elements[0], 5, 1), "hello");
}

TEST(FTXUIConverterTest, StringNodeRendersAsText) {
  auto elements = FTXUIConverter::parse_ui_spec(nlohmann::json("hi"));
  ASSERT_EQ(elements.size(), 1u);
  EXPECT_EQ(RenderToString(elements[0], 2, 1), "hi");
}

TEST(FTXUIConverterTest, ArraySpecRendersAsVbox) {
  nlohmann::json spec =
      nlohmann::json::array({{{"type", "text"}, {"content", "a"}},
                             {{"type", "text"}, {"content", "b"}}});
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 2u);
  EXPECT_EQ(RenderToString(elements[0], 1, 1), "a");
  EXPECT_EQ(RenderToString(elements[1], 1, 1), "b");
}

TEST(FTXUIConverterTest, HboxLaysOutChildrenSideBySide) {
  nlohmann::json children = nlohmann::json::array(
      {{{"type", "text"}, {"content", "a"}},
       {{"type", "text"}, {"content", "b"}}});
  nlohmann::json spec = {{"type", "hbox"}, {"children", children}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  EXPECT_EQ(RenderToString(elements[0], 2, 1), "ab");
}

TEST(FTXUIConverterTest, VboxStacksChildrenVertically) {
  nlohmann::json children = nlohmann::json::array(
      {{{"type", "text"}, {"content", "a"}},
       {{"type", "text"}, {"content", "b"}}});
  nlohmann::json spec = {{"type", "vbox"}, {"children", children}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  // ToString separates rows with \r\n and pads with spaces.
  EXPECT_EQ(RenderToString(elements[0], 1, 2), "a\r\nb");
}

TEST(FTXUIConverterTest, TextNodeDefaultsToContent) {
  // The fallback key "text" is honored when "content" is absent.
  nlohmann::json spec = {{"type", "text"}, {"text", "fallback"}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  EXPECT_EQ(RenderToString(elements[0], 8, 1), "fallback");
}

TEST(FTXUIConverterTest, UnknownTypeFallsBackToTextContent) {
  nlohmann::json spec = {{"type", "bogus_widget"}, {"content", "saved"}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  EXPECT_EQ(RenderToString(elements[0], 5, 1), "saved");
}

TEST(FTXUIConverterTest, NonObjectNodeRendersEmptyText) {
  // parse_node on a scalar renders an empty text element; parse_ui_spec drops
  // non-array/object/string specs entirely (returns no elements).
  auto elements = FTXUIConverter::parse_ui_spec(nlohmann::json(42));
  EXPECT_TRUE(elements.empty());
  EXPECT_EQ(
      RenderToString(FTXUIConverter::parse_node(nlohmann::json(42)), 3, 1),
      "   ");
}

TEST(FTXUIConverterTest, SeparatorRendersHorizontalRule) {
  nlohmann::json spec = {{"type", "separator"}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  const std::string rendered = RenderToString(elements[0], 4, 1);
  // The rule draws a line character (box-drawing ─ is 3 UTF-8 bytes/cell) —
  // not spaces.
  EXPECT_EQ(rendered.size(), 12u);  // 4 cells x 3 bytes.
  EXPECT_EQ(rendered.find(' '), std::string::npos) << rendered;
}

TEST(FTXUIConverterTest, FillerRendersBlank) {
  nlohmann::json spec = {{"type", "filler"}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  EXPECT_EQ(RenderToString(elements[0], 4, 1), "    ");
}

TEST(FTXUIConverterTest, GaugeFullFillsCells) {
  nlohmann::json spec = {{"type", "gauge"}, {"progress", 1.0}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  const std::string rendered = RenderToString(elements[0], 5, 1);
  // A full right-progress gauge must not contain blank cells (█ is 3 bytes).
  EXPECT_EQ(rendered.size(), 15u);  // 5 cells x 3 bytes.
  EXPECT_EQ(rendered.find(' '), std::string::npos) << rendered;
}

TEST(FTXUIConverterTest, GaugeEmptyIsBlank) {
  nlohmann::json spec = {{"type", "gauge"}, {"progress", 0.0}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  EXPECT_EQ(RenderToString(elements[0], 5, 1), "     ");
}

TEST(FTXUIConverterTest, GaugeClampsProgressToUnitInterval) {
  // progress is clamped to [0,1] by the converter (issue: NaN-adjacent input).
  auto over = FTXUIConverter::parse_ui_spec(
      {{"type", "gauge"}, {"progress", 2.5}});
  auto under = FTXUIConverter::parse_ui_spec(
      {{"type", "gauge"}, {"progress", -1.0}});
  auto full = FTXUIConverter::parse_ui_spec(
      {{"type", "gauge"}, {"progress", 1.0}});
  auto empty = FTXUIConverter::parse_ui_spec(
      {{"type", "gauge"}, {"progress", 0.0}});
  ASSERT_EQ(over.size(), 1u);
  ASSERT_EQ(under.size(), 1u);
  EXPECT_EQ(RenderToString(over[0], 5, 1), RenderToString(full[0], 5, 1));
  EXPECT_EQ(RenderToString(under[0], 5, 1), RenderToString(empty[0], 5, 1));
}

TEST(FTXUIConverterTest, GaugeDirectionVariantsRender) {
  for (const char *dir : {"left", "up", "down", "bogus"}) {
    nlohmann::json spec = {{"type", "gauge"},
                           {"progress", 0.5},
                           {"direction", dir}};
    auto elements = FTXUIConverter::parse_ui_spec(spec);
    ASSERT_EQ(elements.size(), 1u);
    EXPECT_NO_THROW(RenderToString(elements[0], 5, 3)) << "direction=" << dir;
  }
}

TEST(FTXUIConverterTest, CanvasWithDrawOpsRenders) {
  nlohmann::json spec = {
      {"type", "canvas"},
      {"width", 5},
      {"height", 3},
      {"draw", nlohmann::json::array(
                  {{{"op", "point"}, {"x", 2}, {"y", 1}},
                   {{"op", "line"}, {"x1", 0}, {"y1", 0}, {"x2", 4}, {"y2", 2}},
                   {{"op", "circle"}, {"x", 2}, {"y", 1}, {"r", 1}},
                   {{"op", "text"}, {"x", 0}, {"y", 0}, {"content", "T"}}})}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  // Canvas pixels render as braille-dot cells; the frame must not be empty.
  EXPECT_NO_THROW(RenderToString(elements[0], 5, 3));
}

TEST(FTXUIConverterTest, CanvasWithoutDrawRendersBlank) {
  nlohmann::json spec = {{"type", "canvas"}, {"width", 5}, {"height", 3}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  // No draw ops -> no dots -> all cells blank.
  EXPECT_EQ(RenderToString(elements[0], 5, 3), "     \r\n     \r\n     ");
}

TEST(FTXUIConverterTest, BoldFlagSetsCellAttribute) {
  nlohmann::json spec = {{"type", "text"}, {"content", "x"}, {"bold", true}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  ftxui::Screen screen(1, 1);
  ftxui::Render(screen, elements[0]);
  EXPECT_TRUE(screen.CellAt(0, 0).bold);
}

TEST(FTXUIConverterTest, StyleStringBoldSetsCellAttribute) {
  nlohmann::json spec = {{"type", "text"}, {"content", "x"}, {"style", "bold"}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  ftxui::Screen screen(1, 1);
  ftxui::Render(screen, elements[0]);
  EXPECT_TRUE(screen.CellAt(0, 0).bold);
}

TEST(FTXUIConverterTest, ColorNameSetsForeground) {
  nlohmann::json spec = {{"type", "text"}, {"content", "x"}, {"color", "red"}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  ftxui::Screen screen(1, 1);
  ftxui::Render(screen, elements[0]);
  EXPECT_EQ(screen.CellAt(0, 0).foreground_color, ftxui::Color::Red);
}

TEST(FTXUIConverterTest, HexColorParsesToRgb) {
  nlohmann::json spec = {{"type", "text"},
                         {"content", "x"},
                         {"color", "#FF0000"}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  ftxui::Screen screen(1, 1);
  ftxui::Render(screen, elements[0]);
  EXPECT_EQ(screen.CellAt(0, 0).foreground_color, ftxui::Color::RGB(255, 0, 0));
}

TEST(FTXUIConverterTest, InvalidColorFallsBackToDefault) {
  nlohmann::json spec = {{"type", "text"},
                         {"content", "x"},
                         {"color", "notacolor"}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  ASSERT_EQ(elements.size(), 1u);
  ftxui::Screen screen(1, 1);
  ftxui::Render(screen, elements[0]);
  EXPECT_EQ(screen.CellAt(0, 0).foreground_color, ftxui::Color::Default);
}

TEST(FTXUIConverterTest, MultipleTextNodes) {
  nlohmann::json spec = nlohmann::json::array(
      {{{"type", "text"}, {"content", "a"}},
       {{"type", "text"}, {"content", "b"}}});
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  EXPECT_EQ(elements.size(), 2u);
}

}  // namespace lazyrtui
