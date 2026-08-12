#include "lazyrtui/app.hpp"
#include "lazyrtui/config_loader.hpp"
#include "lazyrtui/ros_manager.hpp"

#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>

namespace {
std::atomic<bool> g_shutdown_requested{false};
std::atomic<int> g_shutdown_signal{0};
}  // namespace

// Async-signal-safe handler: only re-arms the default disposition and records
// the shutdown request. All teardown (ROS2Manager::stop, thread joins, heap
// deallocation) happens later on the main thread's normal cleanup path.
void handle_signal(int sig) {
  std::signal(sig, SIG_DFL);  // Second signal force-terminates the process.
  g_shutdown_requested.store(true, std::memory_order_relaxed);
  g_shutdown_signal.store(sig, std::memory_order_relaxed);
}

int main(int argc, char **argv) {
  // Load configuration
  lazyrtui::ConfigLoader config_loader;
  auto config = config_loader.load();

  // Initialize ROS 2 manager
  auto ros_mgr = std::make_shared<lazyrtui::ROS2Manager>();
  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  if (!ros_mgr->start(argc, argv)) {
    std::cerr << "Warning: Failed to initialize ROS 2. Running in demo mode."
              << std::endl;
  }

  // Run TUI application (skipped if a shutdown signal arrived before startup
  // completed; during the loop, FTXUI's own handlers exit gracefully).
  if (!g_shutdown_requested.load()) {
    lazyrtui::LazyRTUIApp app(ros_mgr, config);
    app.run();
  }

  // Cleanup — always runs on the main thread, never inside a signal handler.
  if (ros_mgr) {
    ros_mgr->stop();
    ros_mgr.reset();
  }

  return 0;
}
