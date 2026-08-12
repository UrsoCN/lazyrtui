#include "lazyrtui/ftxui_converter.hpp"
#include <gtest/gtest.h>

namespace lazyrtui {

TEST(FTXUIConverterTest, ParsesTextNode) {
  nlohmann::json spec = {{"type", "text"}, {"content", "hello"}};
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  EXPECT_FALSE(elements.empty());
}

TEST(FTXUIConverterTest, ParsesMultipleTextNodes) {
  nlohmann::json spec = nlohmann::json::array(
      {{{"type", "text"}, {"content", "a"}}, {{"type", "text"}, {"content", "b"}}});
  auto elements = FTXUIConverter::parse_ui_spec(spec);
  EXPECT_EQ(elements.size(), 2u);
}

}  // namespace lazyrtui
