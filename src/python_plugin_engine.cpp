#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "lazyrtui/python_plugin_engine.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// RAII guard acquiring the Python GIL for the calling thread. Every public
// entry point of PythonPluginEngine wraps its Python C API work in this guard
// so the engine is safe to call from any thread (UI, refresh timer, or ROS
// callbacks). PyGILState_Ensure/Release is reentrant, so nested Python calls
// on the same thread remain balanced.
class GilGuard {
 public:
  GilGuard() : state_(PyGILState_Ensure()) {}
  ~GilGuard() { PyGILState_Release(state_); }
  GilGuard(const GilGuard &) = delete;
  GilGuard &operator=(const GilGuard &) = delete;

 private:
  PyGILState_STATE state_;
};

}  // namespace

namespace lazyrtui {

PythonPluginEngine::PythonPluginEngine() {
  if (!Py_IsInitialized()) {
    Py_Initialize();
    initialized_ = true;
  }
}

PythonPluginEngine::~PythonPluginEngine() {
  GilGuard guard;  // Py_XDECREF requires the GIL.
  for (auto &[topic, state] : topic_states_) {
    Py_XDECREF(state);
  }
  topic_states_.clear();

  for (auto &[mod_name, mod_obj] : plugin_modules_) {
    Py_XDECREF(mod_obj);
  }
  plugin_modules_.clear();

  if (initialized_ && Py_IsInitialized()) {
    // Py_Finalize(); // Optionally finalize if owned
  }
}

std::string PythonPluginEngine::fetch_python_error() {
  if (!PyErr_Occurred())
    return "";

  PyObject *ptype, *pvalue, *ptraceback;
  PyErr_Fetch(&ptype, &pvalue, &ptraceback);
  PyErr_NormalizeException(&ptype, &pvalue, &ptraceback);

  std::string err_msg = "Python Exception: ";
  if (pvalue) {
    PyObject *pstr = PyObject_Str(pvalue);
    if (pstr) {
      const char *utf8 = PyUnicode_AsUTF8(pstr);
      if (utf8)
        err_msg += utf8;
      Py_DECREF(pstr);
    }
  }

  Py_XDECREF(ptype);
  Py_XDECREF(pvalue);
  Py_XDECREF(ptraceback);
  return err_msg;
}

void PythonPluginEngine::load_plugins_from_dir(const std::string &dir_path) {
  GilGuard guard;
  if (!fs::exists(dir_path) || !fs::is_directory(dir_path))
    return;

  // Add dir_path to sys.path
  PyObject *sys_path = PySys_GetObject("path");
  if (sys_path) {
    PyObject *py_dir = PyUnicode_FromString(dir_path.c_str());
    PyList_Append(sys_path, py_dir);
    Py_DECREF(py_dir);
  }

  for (const auto &entry : fs::directory_iterator(dir_path)) {
    if (entry.is_regular_file() && entry.path().extension() == ".py") {
      load_plugin_file(entry.path().string());
    }
  }
}

bool PythonPluginEngine::load_plugin_file(const std::string &file_path) {
  GilGuard guard;
  fs::path p(file_path);
  std::string module_name = p.stem().string();
  std::string parent_dir = p.parent_path().string();

  // Ensure parent_dir is in sys.path
  PyObject *sys_path = PySys_GetObject("path");
  if (sys_path) {
    PyObject *py_dir = PyUnicode_FromString(parent_dir.c_str());
    PyList_Append(sys_path, py_dir);
    Py_DECREF(py_dir);
  }

  // Import or reload python module
  PyObject *mod_name_obj = PyUnicode_FromString(module_name.c_str());
  PyObject *module = PyImport_Import(mod_name_obj);
  Py_DECREF(mod_name_obj);

  if (!module) {
    std::cerr << "[lazyrtui] Error loading python plugin '" << file_path
              << "': " << fetch_python_error() << std::endl;
    return false;
  }

  // Check if match and render functions exist
  if (!PyObject_HasAttrString(module, "match") ||
      !PyObject_HasAttrString(module, "render")) {
    std::cerr << "[lazyrtui] Plugin '" << file_path
              << "' missing required match() or render() functions."
              << std::endl;
    Py_DECREF(module);
    return false;
  }

  // Store module
  if (plugin_modules_.count(module_name)) {
    Py_DECREF(plugin_modules_[module_name]);
  }
  plugin_modules_[module_name] = module;

  PythonPluginInfo info;
  info.name = module_name;
  info.module_name = module_name;
  info.file_path = file_path;

  // Replace or add info
  bool updated = false;
  for (auto &item : loaded_plugins_) {
    if (item.module_name == module_name) {
      item = info;
      updated = true;
      break;
    }
  }
  if (!updated)
    loaded_plugins_.push_back(info);

  return true;
}

std::string
PythonPluginEngine::find_matching_plugin(const std::string &topic_name,
                                         const std::string &msg_type) {
  GilGuard guard;
  for (const auto &[mod_name, module] : plugin_modules_) {
    PyObject *match_func = PyObject_GetAttrString(module, "match");
    if (match_func && PyCallable_Check(match_func)) {
      PyObject *args =
          Py_BuildValue("(ss)", topic_name.c_str(), msg_type.c_str());
      PyObject *result = PyObject_CallObject(match_func, args);
      Py_DECREF(args);
      Py_DECREF(match_func);

      if (result) {
        bool is_match = PyObject_IsTrue(result);
        Py_DECREF(result);
        if (is_match)
          return mod_name;
      } else {
        fetch_python_error();
      }
    } else {
      Py_XDECREF(match_func);
    }
  }
  return "";
}

nlohmann::json
PythonPluginEngine::render_message(const std::string &module_name,
                                   const std::string &topic_name,
                                   const std::string &json_body) {
  GilGuard guard;
  auto it = plugin_modules_.find(module_name);
  if (it == plugin_modules_.end()) {
    return {{"type", "text"},
            {"content", "[Plugin " + module_name + " not loaded]"},
            {"style", "red"}};
  }
  PyObject *module = it->second;

  PyObject *render_func = PyObject_GetAttrString(module, "render");
  if (!render_func || !PyCallable_Check(render_func)) {
    Py_XDECREF(render_func);
    return {{"type", "text"},
            {"content", "[Plugin render() function missing]"},
            {"style", "red"}};
  }

  // Get or create persistent state dict for this topic
  PyObject *state_dict = nullptr;
  auto s_it = topic_states_.find(topic_name);
  if (s_it != topic_states_.end()) {
    state_dict = s_it->second;
  } else {
    state_dict = PyDict_New();
    topic_states_[topic_name] = state_dict;
  }

  // Parse json_body via Python json module
  PyObject *json_module = PyImport_ImportModule("json");
  if (!json_module) {
    Py_DECREF(render_func);
    return {{"type", "text"},
            {"content", "[Python json module error]"},
            {"style", "red"}};
  }

  PyObject *loads_func = PyObject_GetAttrString(json_module, "loads");
  PyObject *py_json_str = PyUnicode_FromString(json_body.c_str());
  PyObject *msg_dict =
      PyObject_CallFunctionObjArgs(loads_func, py_json_str, NULL);
  Py_DECREF(py_json_str);
  Py_DECREF(loads_func);

  if (!msg_dict) {
    std::string err = fetch_python_error();
    Py_DECREF(json_module);
    Py_DECREF(render_func);
    // If JSON parsing fails, fallback to empty dict
    PyErr_Clear();
    msg_dict = PyDict_New();
  }

  // Call render(msg_dict, state_dict)
  PyObject *render_result =
      PyObject_CallFunctionObjArgs(render_func, msg_dict, state_dict, NULL);
  Py_DECREF(msg_dict);
  Py_DECREF(render_func);

  if (!render_result) {
    std::string err = fetch_python_error();
    Py_DECREF(json_module);
    return {{"type", "text"},
            {"content", "[Plugin Render Exception: " + err + "]"},
            {"style", "red"}};
  }

  // Convert render_result to JSON string using json.dumps
  PyObject *dumps_func = PyObject_GetAttrString(json_module, "dumps");
  PyObject *res_json_obj =
      PyObject_CallFunctionObjArgs(dumps_func, render_result, NULL);
  Py_DECREF(render_result);
  Py_DECREF(dumps_func);
  Py_DECREF(json_module);

  if (!res_json_obj) {
    fetch_python_error();
    return {{"type", "text"},
            {"content", "[Plugin return value not JSON serializable]"},
            {"style", "red"}};
  }

  const char *res_c_str = PyUnicode_AsUTF8(res_json_obj);
  std::string res_str = res_c_str ? res_c_str : "[]";
  Py_DECREF(res_json_obj);

  try {
    return nlohmann::json::parse(res_str);
  } catch (...) {
    return {{"type", "text"},
            {"content", "[Failed to parse plugin UI spec JSON]"},
            {"style", "red"}};
  }
}

} // namespace lazyrtui
