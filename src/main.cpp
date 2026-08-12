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
// The handler below runs in async-signal-safe context; these primitives must
// be lock-free on every supported platform.
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<int>::is_always_lock_free);
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
  // completed; during the loop, FTXUI's own handlers exit gracefully). This
  // check is best-effort: a signal landing between this check and FTXUI's
  // handler installation is caught and handled by FTXUI's loop instead.
  if (!g_shutdown_requested.load()) {
    lazyrtui::LazyRTUIApp app(ros_mgr, config);
    app.run();
    // Join the service-call worker here, while the app (whose members the
    // worker callbacks touch) is still alive; the cleanup block below is an
    // idempotent second stop().
    ros_mgr->stop();
  }

  // Cleanup — always runs on the main thread, never inside a signal handler.
  if (ros_mgr) {
    ros_mgr->stop();
    ros_mgr.reset();
  }

  // Preserve pre-loop signal exit codes (e.g. SIGINT -> 2) for scripts/systemd;
  // 0 when the app exits normally or via FTXUI's in-loop handler.
  return g_shutdown_signal.load();
}
