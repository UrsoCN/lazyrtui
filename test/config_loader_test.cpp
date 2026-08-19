#include "lazyrtui/config_loader.hpp"
#include <gtest/gtest.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace lazyrtui {

namespace {

// Hermetic temp-dir fixture: every test gets a pid-unique scratch dir, a HOME
// pointed at an empty dir (so the real ~/.config/lazyrtui can never leak in),
// and a CWD inside the scratch dir (so ./config/default_config.yaml only
// resolves when a test deliberately creates it).
class ConfigLoaderTest : public ::testing::Test {
protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("lazyrtui_cfg_test_" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);
    home_dir_ = dir_ / "home";
    std::filesystem::create_directories(home_dir_);

    if (const char *old = std::getenv("HOME")) {
      old_home_ = old;
    }
    ASSERT_EQ(::setenv("HOME", home_dir_.c_str(), 1), 0);

    old_cwd_ = std::filesystem::current_path();
    std::filesystem::current_path(dir_);
  }
  void TearDown() override {
    std::filesystem::current_path(old_cwd_);
    if (old_home_.empty()) {
      ::unsetenv("HOME");
    } else {
      ::setenv("HOME", old_home_.c_str(), 1);
    }
    std::filesystem::remove_all(dir_);
  }

  void WriteFile(const std::filesystem::path &path,
                 const std::string &content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
  }

  std::string WriteConfig(const std::string &content) {
    const std::filesystem::path file = dir_ / "config.yaml";
    WriteFile(file, content);
    return file.string();
  }

  std::filesystem::path dir_;
  std::filesystem::path home_dir_;
  std::string old_home_;
  std::filesystem::path old_cwd_;
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
  EXPECT_EQ(cfg.keybindings.switch_focus, "");  // Untouched key keeps default.
  EXPECT_EQ(cfg.ui.auto_refresh_interval_ms, 2000);
  EXPECT_TRUE(cfg.topics.empty());
}

TEST_F(ConfigLoaderTest, EmptyConfigKeepsDefaults) {
  const std::string path = WriteConfig("");
  ConfigLoader loader(path);
  Config cfg = loader.load();
  EXPECT_EQ(cfg.keybindings.quit, "q");
  EXPECT_EQ(cfg.keybindings.switch_focus, "");
  EXPECT_EQ(cfg.keybindings.refresh, "");
  EXPECT_EQ(cfg.ui.auto_refresh_interval_ms, 2000);
  EXPECT_TRUE(cfg.ui.mouse_support);
  EXPECT_TRUE(cfg.topics.empty());
  EXPECT_TRUE(cfg.services.empty());
}

TEST_F(ConfigLoaderTest, MissingFileFallsBackToDefaults) {
  // HOME is an empty dir and CWD is the scratch dir with no config/ subdir,
  // so every fallback link misses deterministically: this pins the pure
  // defaults path regardless of the host's real ~/.config/lazyrtui.
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
  Config cfg = loader.load();  // YAML::LoadFile exception caught -> defaults.
  EXPECT_EQ(cfg.keybindings.quit, "q");
  EXPECT_EQ(cfg.ui.auto_refresh_interval_ms, 2000);
  EXPECT_TRUE(cfg.topics.empty());
}

TEST_F(ConfigLoaderTest, ConversionErrorLeavesPartialConfig) {
  // A field with an unconvertible value throws YAML::BadConversion mid-parse;
  // fields parsed before it are kept, later ones keep their defaults.
  const std::string path = WriteConfig(
      "keybindings:\n"
      "  quit: 'x'\n"
      "ui:\n"
      "  auto_refresh_interval_ms: not_a_number\n");
  ConfigLoader loader(path);
  Config cfg = loader.load();
  EXPECT_EQ(cfg.keybindings.quit, "x");  // Parsed before the bad node.
  EXPECT_EQ(cfg.ui.auto_refresh_interval_ms, 2000);  // Default after the throw.
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
  EXPECT_EQ(cfg.keybindings.switch_focus, "");  // Unknown keys untouched.
  EXPECT_TRUE(cfg.services.empty());
}

TEST_F(ConfigLoaderTest, HomeFallbackIsUsedWhenNoExplicitPath) {
  // Fixture HOME points at home_dir_; writing the standard location there
  // must be picked up by the empty-path resolution.
  WriteFile(home_dir_ / ".config" / "lazyrtui" / "config.yaml",
            "keybindings:\n  quit: 'Q'\n");
  ConfigLoader loader("");  // No explicit path -> $HOME fallback.
  Config cfg = loader.load();
  EXPECT_EQ(cfg.keybindings.quit, "Q");
}

TEST_F(ConfigLoaderTest, ExplicitPathWinsOverHome) {
  WriteFile(home_dir_ / ".config" / "lazyrtui" / "config.yaml",
            "keybindings:\n  quit: 'H'\n");
  const std::string path = WriteConfig("keybindings:\n  quit: 'E'\n");
  ConfigLoader loader(path);
  Config cfg = loader.load();
  EXPECT_EQ(cfg.keybindings.quit, "E");  // Explicit path has precedence.
}

TEST_F(ConfigLoaderTest, DefaultConfigFallbackIsUsedLast) {
  // No explicit path, empty HOME, but a ./config/default_config.yaml relative
  // to the fixture CWD -> that fallback link is exercised deterministically.
  WriteFile(dir_ / "config" / "default_config.yaml",
            "keybindings:\n  quit: 'D'\n");
  ConfigLoader loader("");
  Config cfg = loader.load();
  EXPECT_EQ(cfg.keybindings.quit, "D");
}

}  // namespace lazyrtui
