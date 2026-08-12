#include "lazyrtui/config_loader.hpp"
#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace lazyrtui {

namespace {

// Unique temp-dir fixture so tests don't litter the tree and stay hermetic.
class ConfigLoaderTest : public ::testing::Test {
protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() / "lazyrtui_cfg_test";
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);
  }
  void TearDown() override { std::filesystem::remove_all(dir_); }

  std::string WriteConfig(const std::string &content) {
    const std::filesystem::path file = dir_ / "config.yaml";
    std::ofstream out(file);
    out << content;
    return file.string();
  }

  std::filesystem::path dir_;
};

}  // namespace

TEST_F(ConfigLoaderTest, ParsesAllKeybindings) {
  const std::string path = WriteConfig(
      "keybindings:\n"
      "  switch_focus: '1'\n"
      "  refresh: '2'\n"
      "  search: '3'\n"
      "  echo_topic: '4'\n"
      "  plot_topic: '5'\n"
      "  call_service: '6'\n"
      "  send_goal: '7'\n"
      "  help: '8'\n"
      "  quit: '9'\n");
  ConfigLoader loader(path);
  Config cfg = loader.load();
  EXPECT_EQ(cfg.keybindings.switch_focus, "1");
  EXPECT_EQ(cfg.keybindings.refresh, "2");
  EXPECT_EQ(cfg.keybindings.search, "3");
  EXPECT_EQ(cfg.keybindings.echo_topic, "4");
  EXPECT_EQ(cfg.keybindings.plot_topic, "5");
  EXPECT_EQ(cfg.keybindings.call_service, "6");
  EXPECT_EQ(cfg.keybindings.send_goal, "7");
  EXPECT_EQ(cfg.keybindings.help, "8");
  EXPECT_EQ(cfg.keybindings.quit, "9");
}

TEST_F(ConfigLoaderTest, ParsesTopicsWithPlotConfig) {
  const std::string path = WriteConfig(
      "topics:\n"
      "  /chatter:\n"
      "    plot_fields: [a, b]\n"
      "    plot_type: bar\n"
      "  /empty:\n");
  ConfigLoader loader(path);
  Config cfg = loader.load();
  ASSERT_EQ(cfg.topics.size(), 2u);
  const auto &chatter = cfg.topics["/chatter"];
  ASSERT_EQ(chatter.plot_fields.size(), 2u);
  EXPECT_EQ(chatter.plot_fields[0], "a");
  EXPECT_EQ(chatter.plot_fields[1], "b");
  EXPECT_EQ(chatter.plot_type, "bar");
  // Topic entry without config falls back to its defaults.
  EXPECT_EQ(cfg.topics["/empty"].plot_type, "line");
  EXPECT_TRUE(cfg.topics["/empty"].plot_fields.empty());
}

TEST_F(ConfigLoaderTest, ParsesServicePresets) {
  const std::string path = WriteConfig(
      "services:\n"
      "  /add_two_ints:\n"
      "    params:\n"
      "      a: '1'\n"
      "      b: '2'\n");
  ConfigLoader loader(path);
  Config cfg = loader.load();
  ASSERT_EQ(cfg.services.size(), 1u);
  const auto &preset = cfg.services["/add_two_ints"];
  ASSERT_EQ(preset.params.size(), 2u);
  EXPECT_EQ(preset.params.at("a"), "1");
  EXPECT_EQ(preset.params.at("b"), "2");
}

TEST_F(ConfigLoaderTest, ParsesUiOptions) {
  const std::string path = WriteConfig(
      "ui:\n"
      "  auto_refresh_interval_ms: 500\n"
      "  mouse_support: false\n");
  ConfigLoader loader(path);
  Config cfg = loader.load();
  EXPECT_EQ(cfg.ui.auto_refresh_interval_ms, 500);
  EXPECT_FALSE(cfg.ui.mouse_support);
}

TEST_F(ConfigLoaderTest, PartialConfigKeepsOtherDefaults) {
  const std::string path = WriteConfig("keybindings:\n  quit: 'x'\n");
  ConfigLoader loader(path);
  Config cfg = loader.load();
  EXPECT_EQ(cfg.keybindings.quit, "x");
  EXPECT_EQ(cfg.keybindings.switch_focus, "w");  // Untouched key keeps default.
  EXPECT_EQ(cfg.ui.auto_refresh_interval_ms, 2000);
  EXPECT_TRUE(cfg.topics.empty());
}

TEST_F(ConfigLoaderTest, EmptyConfigKeepsDefaults) {
  const std::string path = WriteConfig("");
  ConfigLoader loader(path);
  Config cfg = loader.load();
  EXPECT_EQ(cfg.keybindings.quit, "q");
  EXPECT_EQ(cfg.keybindings.switch_focus, "w");
  EXPECT_EQ(cfg.keybindings.refresh, "r");
  EXPECT_EQ(cfg.ui.auto_refresh_interval_ms, 2000);
  EXPECT_TRUE(cfg.ui.mouse_support);
  EXPECT_TRUE(cfg.topics.empty());
  EXPECT_TRUE(cfg.services.empty());
}

TEST_F(ConfigLoaderTest, MissingFileFallsBackToDefaults) {
  const std::string path = (dir_ / "does_not_exist.yaml").string();
  ConfigLoader loader(path);
  Config cfg = loader.load();  // Must not throw; prints a note to stderr.
  EXPECT_EQ(cfg.keybindings.quit, "q");
  EXPECT_EQ(cfg.ui.auto_refresh_interval_ms, 2000);
  EXPECT_TRUE(cfg.topics.empty());
}

TEST_F(ConfigLoaderTest, MalformedYamlFallsBackToDefaults) {
  const std::string path = WriteConfig("keybindings: [unclosed\n  bad: yaml::");
  ConfigLoader loader(path);
  Config cfg = loader.load();  // YAML::Exception caught -> defaults.
  EXPECT_EQ(cfg.keybindings.quit, "q");
  EXPECT_EQ(cfg.ui.auto_refresh_interval_ms, 2000);
  EXPECT_TRUE(cfg.topics.empty());
}

TEST_F(ConfigLoaderTest, UnknownKeysAreIgnored) {
  const std::string path = WriteConfig(
      "keybindings:\n"
      "  quit: 'x'\n"
      "  nonsense_key: 'y'\n"
      "unknown_section:\n"
      "  a: 1\n");
  ConfigLoader loader(path);
  Config cfg = loader.load();
  EXPECT_EQ(cfg.keybindings.quit, "x");
  EXPECT_EQ(cfg.keybindings.switch_focus, "w");  // Unknown keys untouched.
  EXPECT_TRUE(cfg.services.empty());
}

TEST_F(ConfigLoaderTest, HomeFallbackIsUsedWhenNoExplicitPath) {
  // Point HOME at a temp dir containing ~/.config/lazyrtui/config.yaml; the
  // empty constructor path must resolve through $HOME.
  const std::filesystem::path home = dir_ / "home";
  std::filesystem::create_directories(home / ".config" / "lazyrtui");
  {
    std::ofstream out(home / ".config" / "lazyrtui" / "config.yaml");
    out << "keybindings:\n  quit: 'Q'\n";
  }
  const char *old_home = std::getenv("HOME");
  ASSERT_EQ(::setenv("HOME", home.c_str(), 1), 0);
  ConfigLoader loader("");  // No explicit path -> $HOME fallback.
  Config cfg = loader.load();
  EXPECT_EQ(cfg.keybindings.quit, "Q");
  if (old_home) {
    ::setenv("HOME", old_home, 1);
  } else {
    ::unsetenv("HOME");
  }
}

}  // namespace lazyrtui
