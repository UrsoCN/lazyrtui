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
  const nlohmann::json first =
      engine.render_message("plugin_state", "/a", "{}");
  const nlohmann::json second =
      engine.render_message("plugin_state", "/a", "{}");
  const nlohmann::json other =
      engine.render_message("plugin_state", "/b", "{}");
  ASSERT_TRUE(first.is_object());
  ASSERT_TRUE(second.is_object());
  ASSERT_TRUE(other.is_object());
  EXPECT_EQ(first["content"], "1");   // State dict persists per topic.
  EXPECT_EQ(second["content"], "2");
  EXPECT_EQ(other["content"], "1");   // Different topic -> fresh state.
}

TEST_F(PythonPluginEngineTest, ReloadSameModuleNameReplacesPlugin) {
  // Loading a second file with the same module stem must replace the stored
  // plugin (single entry) instead of duplicating it.
  const std::string first_path = WritePlugin(
      "plugin_reload",
      "def match(topic, msg):\n"
      "    return topic == '/one'\n"
      "def render(msg, state):\n"
      "    return {'type': 'text', 'content': 'first'}\n");
  const std::string second_path = WritePlugin(
      "plugin_reload",
      "def match(topic, msg):\n"
      "    return topic == '/two'\n"
      "def render(msg, state):\n"
      "    return {'type': 'text', 'content': 'second'}\n");
  PythonPluginEngine engine;
  ASSERT_TRUE(engine.load_plugin_file(first_path));
  ASSERT_TRUE(engine.load_plugin_file(second_path));
  const auto &plugins = engine.loaded_plugins();
  ASSERT_EQ(plugins.size(), 1u);             // Replaced, not duplicated.
  EXPECT_EQ(plugins[0].file_path, second_path);
  EXPECT_EQ(engine.find_matching_plugin("/two", ""), "plugin_reload");
  EXPECT_EQ(engine.find_matching_plugin("/one", ""), "");
}

TEST_F(PythonPluginEngineTest, FindMatchingPluginSkipsRaisingMatch) {
  // A plugin whose match() raises must be skipped (error swallowed) and the
  // search continues; here the second plugin is the actual match.
  const std::string bad_path = WritePlugin(
      "plugin_raisematch",
      "def match(topic, msg):\n"
      "    raise RuntimeError('match exploded')\n"
      "def render(msg, state):\n"
      "    return {}\n");
  const std::string good_path = WritePlugin(
      "plugin_goodmatch",
      "def match(topic, msg):\n"
      "    return topic == '/demo'\n"
      "def render(msg, state):\n"
      "    return {}\n");
  PythonPluginEngine engine;
  ASSERT_TRUE(engine.load_plugin_file(bad_path));
  ASSERT_TRUE(engine.load_plugin_file(good_path));
  EXPECT_EQ(engine.find_matching_plugin("/demo", ""), "plugin_goodmatch");
}

TEST_F(PythonPluginEngineTest, RenderMessageNonDictReturnIsSerialized) {
  // render() returning a bare string is JSON-serializable and must come back
  // as a JSON string node, not an error.
  const std::string path = WritePlugin(
      "plugin_strreturn",
      "def match(topic, msg):\n"
      "    return False\n"
      "def render(msg, state):\n"
      "    return 'plain string'\n");
  PythonPluginEngine engine;
  ASSERT_TRUE(engine.load_plugin_file(path));
  const nlohmann::json spec =
      engine.render_message("plugin_strreturn", "/demo", "{}");
  ASSERT_TRUE(spec.is_string());
  EXPECT_EQ(spec.get<std::string>(), "plain string");
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
  // NOTE: this fallback path must run before the other render tests. Under the
  // pre-fix double-DECREF bug it frees json while sys.modules["json"] still
  // references it, and the NEXT render_message's PyImport_ImportModule("json")
  // crashes — so the regression catch depends on declaration order.
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

TEST_F(PythonPluginEngineTest, LoadPluginsFromNestedSubdirectories) {
  namespace fs = std::filesystem;
  fs::create_directories(dir_ / "speech");
  fs::create_directories(dir_ / "teleop" / "nested");
  fs::create_directories(dir_ / "__pycache__");

  const std::string p1 = (dir_ / "speech" / "plugin_speech.py").string();
  {
    std::ofstream out(p1);
    out << "def match(t, m):\n    return t == '/speech'\ndef render(msg, s):\n    return {'type': 'text', 'content': 'speech'}\n";
  }

  const std::string p2 = (dir_ / "teleop" / "nested" / "plugin_teleop.py").string();
  {
    std::ofstream out(p2);
    out << "def match(t, m):\n    return t == '/cmd_vel'\ndef render(msg, s):\n    return {'type': 'text', 'content': 'teleop'}\n";
  }

  // File inside __pycache__ should be ignored
  const std::string p_cache = (dir_ / "__pycache__" / "cached.py").string();
  {
    std::ofstream out(p_cache);
    out << "def match(t, m):\n    return True\ndef render(msg, s):\n    return {}\n";
  }

  PythonPluginEngine engine;
  engine.load_plugins_from_dir(dir_.string());

  const auto &plugins = engine.loaded_plugins();
  ASSERT_EQ(plugins.size(), 2u);

  EXPECT_EQ(engine.find_matching_plugin("/speech", "std_msgs/msg/String"),
            "plugin_speech");
  EXPECT_EQ(engine.find_matching_plugin("/cmd_vel", "geometry_msgs/msg/Twist"),
            "plugin_teleop");
}

}  // namespace lazyrtui
