#pragma once

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Forward declaration of PyObject to avoid pulling Python.h into all headers
typedef struct _object PyObject;

namespace lazyrtui {

struct PythonPluginInfo {
  std::string name;
  std::string module_name;
  std::string file_path;
};

class PythonPluginEngine {
public:
  PythonPluginEngine();
  ~PythonPluginEngine();

  /// Load all *.py plugin scripts from a directory.
  void load_plugins_from_dir(const std::string &dir_path);

  /// Load a single *.py plugin script file.
  bool load_plugin_file(const std::string &file_path);

  /// Find if any loaded Python plugin matches the given topic_name & msg_type.
  /// Returns the module name, or empty string if no match.
  std::string find_matching_plugin(const std::string &topic_name,
                                   const std::string &msg_type);

  /// Render a topic message using a matched Python plugin module.
  /// Converts input json_body string -> Python dict msg, calls render(msg,
  /// state), updates state dict, and returns UI specification as
  /// nlohmann::json.
  nlohmann::json render_message(const std::string &module_name,
                                const std::string &topic_name,
                                const std::string &json_body);

  /// Get list of loaded Python plugins
  const std::vector<PythonPluginInfo> &loaded_plugins() const {
    return loaded_plugins_;
  }

private:
  std::string fetch_python_error();

  bool initialized_ = false;
  std::vector<PythonPluginInfo> loaded_plugins_;

  // Map of module_name -> PyObject* (borrowed/owned module reference)
  std::map<std::string, PyObject *> plugin_modules_;

  // Map of topic_name -> PyObject* (persistent Python dict state per topic)
  std::map<std::string, PyObject *> topic_states_;
};

} // namespace lazyrtui
