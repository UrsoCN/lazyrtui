cmake_minimum_required(VERSION 3.14)

# ── Version: MAJOR.MINOR.YYMMDD from git tag and latest commit date ──
execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_TAG_RAW
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(GIT_TAG_RAW)
    string(REGEX REPLACE "^[vV]" "" GIT_TAG_CLEAN "${GIT_TAG_RAW}")
    string(REPLACE "." ";" GIT_TAG_LIST "${GIT_TAG_CLEAN}")
    list(LENGTH GIT_TAG_LIST LIST_LEN)
    if(LIST_LEN GREATER_EQUAL 2)
        list(GET GIT_TAG_LIST 0 MAJOR_VER)
        list(GET GIT_TAG_LIST 1 MINOR_VER)
        set(MAJOR_MINOR "${MAJOR_VER}.${MINOR_VER}")
    elseif(LIST_LEN EQUAL 1)
        list(GET GIT_TAG_LIST 0 MAJOR_VER)
        set(MAJOR_MINOR "${MAJOR_VER}.0")
    else()
        set(MAJOR_MINOR "0.1")
    endif()
else()
    set(MAJOR_MINOR "0.1")
endif()

execute_process(
    COMMAND git log -1 --format=%cd --date=format:%y%m%d
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_DATE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_COMMIT_DATE OR GIT_COMMIT_DATE STREQUAL "")
    set(GIT_COMMIT_DATE "000000")
endif()

set(PROJECT_VER "${MAJOR_MINOR}.${GIT_COMMIT_DATE}")
message(STATUS "Project version: ${PROJECT_VER}")

project(lazyrtui VERSION ${PROJECT_VER})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(third_party/FTXUI_v7.0.3)
add_subdirectory(third_party/yaml-cpp_0.9.0)
add_subdirectory(third_party/nlohmann_json_v3.12.0)

find_package(Python3 COMPONENTS Development REQUIRED)

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
find_package(rcl REQUIRED)
find_package(ament_index_cpp REQUIRED)

add_executable(lazyrtui
  src/main.cpp
  src/app.cpp
  src/ros_manager.cpp
  src/config_loader.cpp
  src/tf_tree.cpp
  src/python_plugin_engine.cpp
  src/ftxui_converter.cpp
)

target_compile_definitions(lazyrtui PRIVATE LAZYRTUI_VERSION="${PROJECT_VER}")

target_include_directories(lazyrtui PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
  ${Python3_INCLUDE_DIRS}
)

target_link_libraries(lazyrtui
  ftxui::screen
  ftxui::dom
  ftxui::component
  yaml-cpp
  nlohmann_json::nlohmann_json
  ${Python3_LIBRARIES}
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
  rcl
  ament_index_cpp
)

install(TARGETS lazyrtui
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY include/
  DESTINATION include)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_tf_tree test/tf_tree_test.cpp src/tf_tree.cpp)
  target_include_directories(test_tf_tree PRIVATE include)

  ament_add_gtest(test_config_loader test/config_loader_test.cpp src/config_loader.cpp)
  target_include_directories(test_config_loader PRIVATE include)
  target_link_libraries(test_config_loader yaml-cpp)

  ament_add_gtest(test_ftxui_converter test/ftxui_converter_test.cpp src/ftxui_converter.cpp)
  target_include_directories(test_ftxui_converter PRIVATE include)
  target_link_libraries(test_ftxui_converter ftxui::dom nlohmann_json::nlohmann_json)

  ament_add_gtest(test_cdr_utils test/cdr_utils_test.cpp)
  target_include_directories(test_cdr_utils PRIVATE src)
endif()

ament_package()
