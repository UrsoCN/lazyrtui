#include "lazyrtui/config_loader.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace lazyrtui {

TEST(ConfigLoaderTest, ParsesCustomKeybindingsAndUi) {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "lazyrtui_test_cfg";
  std::filesystem::create_directories(dir);
  const std::filesystem::path file = dir / "config.yaml";
  {
    std::ofstream out(file);
    out << "keybindings:\n"
           "  quit: 'x'\n"
           "  refresh: 'z'\n"
           "ui:\n"
           "  auto_refresh_interval_ms: 500\n";
  }
  ConfigLoader loader(file.string());
  Config cfg = loader.load();
  EXPECT_EQ(cfg.keybindings.quit, "x");
  EXPECT_EQ(cfg.keybindings.refresh, "z");
  EXPECT_EQ(cfg.ui.auto_refresh_interval_ms, 500);
  std::filesystem::remove_all(dir);
}

TEST(ConfigLoaderTest, EmptyConfigKeepsDefaults) {
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "lazyrtui_test_empty";
  std::filesystem::create_directories(dir);
  const std::filesystem::path file = dir / "config.yaml";
  { std::ofstream out(file); }  // Empty file -> defaults.
  ConfigLoader loader(file.string());
  Config cfg = loader.load();
  EXPECT_EQ(cfg.keybindings.quit, "q");
  EXPECT_EQ(cfg.keybindings.switch_focus, "w");
  EXPECT_EQ(cfg.ui.auto_refresh_interval_ms, 2000);
  std::filesystem::remove_all(dir);
}

}  // namespace lazyrtui
