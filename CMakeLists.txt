cmake_minimum_required(VERSION 3.14)
project(lazyrtui VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(third_party/FTXUI_v7.0.3)
add_subdirectory(third_party/yaml-cpp_0.9.0)
add_subdirectory(third_party/nlohmann_json_v3.12.0)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_msgs REQUIRED)
find_package(rosidl_typesupport_introspection_cpp REQUIRED)
find_package(rosidl_typesupport_cpp REQUIRED)
find_package(rcl_interfaces REQUIRED)

add_executable(lazyrtui
  src/main.cpp
  src/app.cpp
  src/ros_manager.cpp
  src/config_loader.cpp
  src/tf_tree.cpp
)

target_include_directories(lazyrtui PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(lazyrtui
  ftxui::screen
  ftxui::dom
  ftxui::component
  yaml-cpp
  nlohmann_json::nlohmann_json
)

ament_target_dependencies(lazyrtui
  rclcpp
  std_msgs
  geometry_msgs
  sensor_msgs
  tf2_ros
  tf2_msgs
  rosidl_typesupport_introspection_cpp
  rosidl_typesupport_cpp
  rcl_interfaces
)

install(TARGETS lazyrtui
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY include/
  DESTINATION include)

ament_package()
