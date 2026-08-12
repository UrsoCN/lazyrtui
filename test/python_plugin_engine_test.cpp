#include "lazyrtui/python_plugin_engine.hpp"
#include <gtest/gtest.h>
#include <sys/types.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace lazyrtui {

namespace {

// Hermetic temp-dir fixture for plugin scripts. Each test gets its own
// pid+counter-suffixed directory and that directory is never reused or
// recreated: Python caches directory listings in sys.path_importer_cache
// keyed by path string, so deleting/recreating a shared path between tests
// would serve stale entries to later tests ("No module named" flakiness).
// Module names MUST also be unique per test: Python caches imported modules
// in sys.modules for the whole process.
class PythonPluginEngineTest : public ::testing::Test {
protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("lazyrtui_plugin_test_" + std::to_string(::getpid()) + "_" +
            std::to_string(counter_++));
    std::filesystem::create_directories(dir_);
  }
  void TearDown() override { std::filesystem::remove_all(dir_); }

  // Write a plugin script and return its file path.
  std::string WritePlugin(const std::string &name, const std::string &content) {
    const std::filesystem::path file = dir_ / (name + ".py");
    std::ofstream out(file);
    out << content;
    return file.string();
  }

  std::filesystem::path dir_;
  static int counter_;
};

int PythonPluginEngineTest::counter_ = 0;

}  // namespace

TEST_F(PythonPluginEngineTest, LoadPluginFileStoresPlugin) {
  const std::string path = WritePlugin(
      "plugin_ok",
      "def match(topic, msg):\n"
      "    return False\n"
      "def render(msg, state):\n"
      "    return {'type': 'text', 'content': 'hi'}\n");
  PythonPluginEngine engine;
  ASSERT_TRUE(engine.load_plugin_file(path));
  const auto &plugins = engine.loaded_plugins();
  ASSERT_EQ(plugins.size(), 1u);
  EXPECT_EQ(plugins[0].module_name, "plugin_ok");
  EXPECT_EQ(plugins[0].file_path, path);
}

TEST_F(PythonPluginEngineTest, LoadPluginFileMissingFunctionsFails) {
  const std::string path = WritePlugin("plugin_missing", "x = 1\n");
  PythonPluginEngine engine;
  EXPECT_FALSE(engine.load_plugin_file(path));
  EXPECT_TRUE(engine.loaded_plugins().empty());
}

TEST_F(PythonPluginEngineTest, LoadPluginFileNonexistentFails) {
  PythonPluginEngine engine;
  EXPECT_FALSE(engine.load_plugin_file((dir_ / "nope.py").string()));
  EXPECT_TRUE(engine.loaded_plugins().empty());
}

TEST_F(PythonPluginEngineTest, LoadPluginsFromDirSkipsNonPy) {
  WritePlugin(
      "plugin_a",
      "def match(topic, msg):\n"
      "    return False\n"
      "def render(msg, state):\n"
      "    return {}\n");
  WritePlugin(
      "plugin_b",
      "def match(topic, msg):\n"
      "    return False\n"
      "def render(msg, state):\n"
      "    return {}\n");
  {
    std::ofstream out(dir_ / "notes.txt");
    out << "not a plugin";
  }
  PythonPluginEngine engine;
  engine.load_plugins_from_dir(dir_.string());
  ASSERT_EQ(engine.loaded_plugins().size(), 2u);
}

TEST_F(PythonPluginEngineTest, LoadPluginsFromMissingDirIsNoOp) {
  PythonPluginEngine engine;
  engine.load_plugins_from_dir((dir_ / "absent").string());
  EXPECT_TRUE(engine.loaded_plugins().empty());
}

TEST_F(PythonPluginEngineTest, FindMatchingPluginHit) {
  const std::string path = WritePlugin(
      "plugin_match",
      "def match(topic, msg):\n"
      "    return topic == '/demo' and msg == 'demo_msgs/msg/Demo'\n"
      "def render(msg, state):\n"
      "    return {'type': 'text', 'content': 'x'}\n");
  PythonPluginEngine engine;
  ASSERT_TRUE(engine.load_plugin_file(path));
  EXPECT_EQ(engine.find_matching_plugin("/demo", "demo_msgs/msg/Demo"),
            "plugin_match");
}

TEST_F(PythonPluginEngineTest, FindMatchingPluginMiss) {
  const std::string path = WritePlugin(
      "plugin_nomatch",
      "def match(topic, msg):\n"
      "    return False\n"
      "def render(msg, state):\n"
      "    return {}\n");
  PythonPluginEngine engine;
  ASSERT_TRUE(engine.load_plugin_file(path));
  EXPECT_EQ(engine.find_matching_plugin("/other", "std_msgs/msg/String"), "");
}

TEST_F(PythonPluginEngineTest, RenderMessageRoundTrip) {
  const std::string path = WritePlugin(
      "plugin_render",
      "def match(topic, msg):\n"
      "    return False\n"
      "def render(msg, state):\n"
      "    return {'type': 'text', 'content': str(msg.get('v', 0))}\n");
  PythonPluginEngine engine;
  ASSERT_TRUE(engine.load_plugin_file(path));
  const nlohmann::json spec =
      engine.render_message("plugin_render", "/demo", "{\"v\": 42}");
  ASSERT_TRUE(spec.is_object());
  EXPECT_EQ(spec["type"], "text");
  EXPECT_EQ(spec["content"], "42");
}

TEST_F(PythonPluginEngineTest, RenderMessageStatePersistsPerTopic) {
  const std::string path = WritePlugin(
      "plugin_state",
      "def match(topic, msg):\n"
      "    return False\n"
      "def render(msg, state):\n"
      "    state['n'] = state.get('n', 0) + 1\n"
      "    return {'type': 'text', 'content': str(state['n'])}\n");
  PythonPluginEngine engine;
  ASSERT_TRUE(engine.load_plugin_file(path));
  EXPECT_EQ(engine.render_message("plugin_state", "/a", "{}")["content"], "1");
  EXPECT_EQ(engine.render_message("plugin_state", "/a", "{}")["content"], "2");
  EXPECT_EQ(engine.render_message("plugin_state", "/b", "{}")["content"], "1");
}

TEST_F(PythonPluginEngineTest, RenderMessageUnknownModuleReportsError) {
  PythonPluginEngine engine;
  const nlohmann::json spec =
      engine.render_message("plugin_absent", "/demo", "{}");
  ASSERT_TRUE(spec.is_object());
  EXPECT_EQ(spec["type"], "text");
  EXPECT_NE(spec["content"].get<std::string>().find("not loaded"),
            std::string::npos);
}

TEST_F(PythonPluginEngineTest, RenderMessageInvalidJsonFallsBackToEmpty) {
  const std::string path = WritePlugin(
      "plugin_badjson",
      "def match(topic, msg):\n"
      "    return False\n"
      "def render(msg, state):\n"
      "    return {'type': 'text', 'content': str(msg)}\n");
  PythonPluginEngine engine;
  ASSERT_TRUE(engine.load_plugin_file(path));
  const nlohmann::json spec =
      engine.render_message("plugin_badjson", "/demo", "{not valid json");
  ASSERT_TRUE(spec.is_object());
  EXPECT_EQ(spec["content"], "{}");  // Fallback empty dict -> str({}) == "{}".
}

TEST_F(PythonPluginEngineTest, RenderMessageExceptionIsCaught) {
  const std::string path = WritePlugin(
      "plugin_exc",
      "def match(topic, msg):\n"
      "    return False\n"
      "def render(msg, state):\n"
      "    raise RuntimeError('boom')\n");
  PythonPluginEngine engine;
  ASSERT_TRUE(engine.load_plugin_file(path));
  const nlohmann::json spec =
      engine.render_message("plugin_exc", "/demo", "{}");
  ASSERT_TRUE(spec.is_object());
  EXPECT_EQ(spec["type"], "text");
  EXPECT_NE(spec["content"].get<std::string>().find("boom"),
            std::string::npos);
}

TEST_F(PythonPluginEngineTest, RenderMessageNonSerializableReturnIsCaught) {
  const std::string path = WritePlugin(
      "plugin_nonserial",
      "def match(topic, msg):\n"
      "    return False\n"
      "def render(msg, state):\n"
      "    return {'bad': {1, 2}}\n");  // set is not JSON-serializable
  PythonPluginEngine engine;
  ASSERT_TRUE(engine.load_plugin_file(path));
  const nlohmann::json spec =
      engine.render_message("plugin_nonserial", "/demo", "{}");
  ASSERT_TRUE(spec.is_object());
  EXPECT_EQ(spec["type"], "text");
  EXPECT_NE(spec["content"].get<std::string>().find("not JSON serializable"),
            std::string::npos);
}

}  // namespace lazyrtui
