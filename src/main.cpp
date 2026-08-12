#include "lazyrtui/app.hpp"
#include "lazyrtui/config_loader.hpp"
#include "lazyrtui/ros_manager.hpp"

#include <csignal>
#include <iostream>
#include <memory>

static std::shared_ptr<lazyrtui::ROS2Manager> g_ros_mgr;

void signal_handler(int sig) {
  if (g_ros_mgr) {
    g_ros_mgr->stop();
  }
  std::exit(sig);
}

int main(int argc, char **argv) {
  // Load configuration
  lazyrtui::ConfigLoader config_loader;
  auto config = config_loader.load();

  // Initialize ROS 2 manager
  g_ros_mgr = std::make_shared<lazyrtui::ROS2Manager>();
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  if (!g_ros_mgr->start(argc, argv)) {
    std::cerr << "Warning: Failed to initialize ROS 2. Running in demo mode."
              << std::endl;
  }

  // Run TUI application
  lazyrtui::LazyRTUIApp app(g_ros_mgr, config);
  app.run();

  // Cleanup
  if (g_ros_mgr) {
    g_ros_mgr->stop();
    g_ros_mgr.reset();
  }

  return 0;
}
