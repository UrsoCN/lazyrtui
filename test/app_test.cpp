#include <gtest/gtest.h>
#include "lazyrtui/app.hpp"
#include "lazyrtui/config_loader.hpp"
#include <ftxui/component/event.hpp>
#include <ftxui/screen/screen.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <diagnostic_msgs/msg/key_value.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/bool.hpp>

namespace lazyrtui {

TEST(AppTest, GlobalHotkeysWorkOutsideInputMode) {
  Config cfg;
  LazyRTUIApp app(nullptr, cfg);
  bool exited = false;
  auto comp = app.build_main_component([&exited]() { exited = true; });

  // Initial tab is Nodes (0), focus on left pane.
  EXPECT_EQ(app.selected_tab(), 0);
  EXPECT_FALSE(app.is_text_input_focused());

  // '3' switches to Services tab (2).
  comp->OnEvent(ftxui::Event::Character('3'));
  EXPECT_EQ(app.selected_tab(), 2);
  EXPECT_EQ(app.service_pane_focus(), 0);
  EXPECT_FALSE(app.is_text_input_focused());

  // '4' switches to Actions tab (3).
  comp->OnEvent(ftxui::Event::Character('4'));
  EXPECT_EQ(app.selected_tab(), 3);
  EXPECT_EQ(app.action_pane_focus(), 0);
  EXPECT_FALSE(app.is_text_input_focused());

  // '1' switches back to Nodes tab (0).
  comp->OnEvent(ftxui::Event::Character('1'));
  EXPECT_EQ(app.selected_tab(), 0);

  // Esc moves from left list to Top Bar
  comp->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(app.main_vertical_focus(), 0);
  EXPECT_FALSE(app.show_exit_dialog());

  // Esc in Top Bar opens Exit confirmation dialog
  comp->OnEvent(ftxui::Event::Escape);
  EXPECT_TRUE(app.show_exit_dialog());
  EXPECT_FALSE(exited);

  // 'y' confirms exit
  comp->OnEvent(ftxui::Event::Character('y'));
  EXPECT_TRUE(exited);
}

TEST(AppTest, SuppressesHotkeysAndTypesInServiceInput) {
  Config cfg;
  LazyRTUIApp app(nullptr, cfg);
  auto comp = app.build_main_component();

  // Switch to Services tab (2)
  comp->OnEvent(ftxui::Event::Character('3'));
  EXPECT_EQ(app.selected_tab(), 2);
  EXPECT_EQ(app.service_pane_focus(), 0);

  // Switch pane focus to right pane (1) using Tab
  comp->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(app.service_pane_focus(), 1);
  EXPECT_TRUE(app.is_text_input_focused());

  // Type keys that would otherwise be hotkeys: 'r' (refresh), 'c' (call), '1' (tab 0), 'q' (quit)
  std::string test_input = "{\"data\": true}";
  for (char ch : test_input) {
    comp->OnEvent(ftxui::Event::Character(ch));
  }

  // Verify tab did NOT change (e.g. from '1' or '3')
  EXPECT_EQ(app.selected_tab(), 2);
  EXPECT_EQ(app.service_pane_focus(), 1);
  EXPECT_TRUE(app.is_text_input_focused());

  // Verify that the characters (including 'r', 't', 'u', 'e', etc.) are in service_request_json
  EXPECT_NE(app.service_request_json().find("true"), std::string::npos);

  // Pressing Escape exits input mode and returns focus to left menu
  comp->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(app.service_pane_focus(), 0);
  EXPECT_FALSE(app.is_text_input_focused());

  // Now '1' switches back to tab 0
  comp->OnEvent(ftxui::Event::Character('1'));
  EXPECT_EQ(app.selected_tab(), 0);
}

TEST(AppTest, SuppressesHotkeysAndTypesInActionInput) {
  Config cfg;
  LazyRTUIApp app(nullptr, cfg);
  auto comp = app.build_main_component();

  // Switch to Actions tab (3)
  comp->OnEvent(ftxui::Event::Character('4'));
  EXPECT_EQ(app.selected_tab(), 3);
  EXPECT_EQ(app.action_pane_focus(), 0);

  // Switch pane focus to right pane (1) using Tab
  comp->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(app.action_pane_focus(), 1);
  EXPECT_TRUE(app.is_text_input_focused());

  // Type characters into action goal JSON including 'g' (send goal), 'r' (refresh), 'q' (quit)
  std::string test_input = "{\"goal\": 42}";
  for (char ch : test_input) {
    comp->OnEvent(ftxui::Event::Character(ch));
  }

  // Tab did NOT switch, app did NOT quit
  EXPECT_EQ(app.selected_tab(), 3);
  EXPECT_EQ(app.action_pane_focus(), 1);
  EXPECT_TRUE(app.is_text_input_focused());
  EXPECT_NE(app.action_goal_json().find("42"), std::string::npos);

  // Escape exits input mode and returns focus to left menu
  comp->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(app.action_pane_focus(), 0);
  EXPECT_FALSE(app.is_text_input_focused());
}

TEST(AppTest, HelpModalCapturesAndDismissesEvents) {
  Config cfg;
  cfg.keybindings.help = "?";
  LazyRTUIApp app(nullptr, cfg);
  auto comp = app.build_main_component();

  EXPECT_EQ(app.selected_tab(), 0);

  // '?' opens help modal
  comp->OnEvent(ftxui::Event::Character('?'));
  EXPECT_TRUE(app.show_help());

  // When help modal is open, '2' should NOT switch tab
  comp->OnEvent(ftxui::Event::Character('2'));
  EXPECT_EQ(app.selected_tab(), 0);

  // 'Escape' closes help modal
  comp->OnEvent(ftxui::Event::Escape);
  EXPECT_FALSE(app.show_help());

  // Now '2' switches tab
  comp->OnEvent(ftxui::Event::Character('2'));
  EXPECT_EQ(app.selected_tab(), 1);
}

TEST(AppTest, VerticalNavigationBetweenTopBarAndContent) {
  Config cfg;
  LazyRTUIApp app(nullptr, cfg);
  auto comp = app.build_main_component();

  // Initially in tab content (1)
  EXPECT_EQ(app.main_vertical_focus(), 1);
  EXPECT_EQ(app.selected_tab(), 0);

  // ArrowUp from top of list moves focus to Top Bar (0)
  comp->OnEvent(ftxui::Event::ArrowUp);
  EXPECT_EQ(app.main_vertical_focus(), 0);

  // Left/Right in Top Bar switches selected tab
  comp->OnEvent(ftxui::Event::ArrowRight);
  EXPECT_EQ(app.selected_tab(), 1);

  // ArrowDown drops focus back into tab content (1)
  comp->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(app.main_vertical_focus(), 1);

  // Direct hotkey '4' switches to tab 3 and focuses content
  comp->OnEvent(ftxui::Event::Character('4'));
  EXPECT_EQ(app.selected_tab(), 3);
  EXPECT_EQ(app.main_vertical_focus(), 1);
}

TEST(AppTest, EscKeyHierarchicalBackNavigation) {
  Config cfg;
  LazyRTUIApp app(nullptr, cfg);
  bool exited = false;
  auto comp = app.build_main_component([&exited]() { exited = true; });

  // 1. Switch to Services tab (2)
  comp->OnEvent(ftxui::Event::Character('3'));
  EXPECT_EQ(app.selected_tab(), 2);
  EXPECT_EQ(app.main_vertical_focus(), 1);
  EXPECT_EQ(app.service_pane_focus(), 0);

  // 2. Tab into input
  comp->OnEvent(ftxui::Event::Tab);
  EXPECT_EQ(app.service_pane_focus(), 1);
  EXPECT_TRUE(app.is_text_input_focused());

  // 3. First Esc: Exits input mode to left menu
  comp->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(app.service_pane_focus(), 0);
  EXPECT_EQ(app.main_vertical_focus(), 1);
  EXPECT_FALSE(app.is_text_input_focused());

  // 4. Second Esc: Exits left menu to Top Bar
  comp->OnEvent(ftxui::Event::Escape);
  EXPECT_EQ(app.main_vertical_focus(), 0);
  EXPECT_FALSE(app.show_exit_dialog());

  // 5. Third Esc: Opens Exit confirmation dialog
  comp->OnEvent(ftxui::Event::Escape);
  EXPECT_TRUE(app.show_exit_dialog());

  // 6. Pressing 'n' or any key cancels dialog
  comp->OnEvent(ftxui::Event::Character('n'));
  EXPECT_FALSE(app.show_exit_dialog());
  EXPECT_FALSE(exited);
  EXPECT_EQ(app.main_vertical_focus(), 0);

  // 7. In Top Bar, ArrowDown returns to tab content
  comp->OnEvent(ftxui::Event::ArrowDown);
  EXPECT_EQ(app.main_vertical_focus(), 1);
  EXPECT_EQ(app.service_pane_focus(), 0);

  // 8. Test confirming exit with 'Y'
  comp->OnEvent(ftxui::Event::Escape);  // Back to Top Bar
  comp->OnEvent(ftxui::Event::Escape);  // Open Exit Dialog
  EXPECT_TRUE(app.show_exit_dialog());
  comp->OnEvent(ftxui::Event::Character('Y'));
  EXPECT_TRUE(exited);
}

TEST(AppTest, TabNavigationBetweenInputAndButton) {
  Config cfg;
  LazyRTUIApp app(nullptr, cfg);
  auto comp = app.build_main_component();

  // Switch to Services tab (2) and right pane (1)
  comp->OnEvent(ftxui::Event::Character('3'));
  comp->OnEvent(ftxui::Event::Tab);
  EXPECT_TRUE(app.is_text_input_focused());

  // Tab moves focus from input to "Call Service" button
  comp->OnEvent(ftxui::Event::Tab);
  EXPECT_FALSE(app.is_text_input_focused());

  // Tab again wraps back to input
  comp->OnEvent(ftxui::Event::Tab);
  EXPECT_TRUE(app.is_text_input_focused());
}

TEST(AppTest, VimKeysInInputVsNormalMode) {
  Config cfg;
  LazyRTUIApp app(nullptr, cfg);
  auto comp = app.build_main_component();

  // Switch to Services tab (2) and right pane (1)
  comp->OnEvent(ftxui::Event::Character('3'));
  comp->OnEvent(ftxui::Event::Tab);
  EXPECT_TRUE(app.is_text_input_focused());

  // Typing 'j' and 'k' in input mode types letters rather than posting arrow keys
  comp->OnEvent(ftxui::Event::Character('j'));
  comp->OnEvent(ftxui::Event::Character('k'));

  EXPECT_NE(app.service_request_json().find("jk"), std::string::npos);
}

TEST(AppTest, ServiceRequestTemplateGeneration) {
  auto ros_mgr = std::make_shared<ROS2Manager>();
  std::string empty_tmpl =
      ros_mgr->get_service_request_json("/clear", "std_srvs/srv/Empty");
  EXPECT_EQ(empty_tmpl, "{}");

  std::string set_bool_tmpl =
      ros_mgr->get_service_request_json("/set_bool", "std_srvs/srv/SetBool");
  EXPECT_NE(set_bool_tmpl.find("\"data\": false"), std::string::npos);
}

TEST(AppTest, ActionGoalTemplateGeneration) {
  auto ros_mgr = std::make_shared<ROS2Manager>();
  std::string goal_tmpl1 =
      ros_mgr->get_action_goal_json("/rotate_absolute", "turtlesim/action/RotateAbsolute");
  EXPECT_NE(goal_tmpl1.find("\"theta\": 0"), std::string::npos);
  EXPECT_EQ(goal_tmpl1.find("error"), std::string::npos);

  std::string goal_tmpl2 =
      ros_mgr->get_action_goal_json("/rotate_absolute", "turtlesim/action/RotateAbsolute_SendGoal");
  EXPECT_NE(goal_tmpl2.find("\"theta\": 0"), std::string::npos);
  EXPECT_EQ(goal_tmpl2.find("error"), std::string::npos);

  std::string goal_tmpl3 =
      ros_mgr->get_action_goal_json("/rotate_absolute", "turtlesim/action/RotateAbsolute_SendGoal_Goal");
  EXPECT_NE(goal_tmpl3.find("\"theta\": 0"), std::string::npos);
  EXPECT_EQ(goal_tmpl3.find("error"), std::string::npos);

  std::string goal_tmpl4 =
      ros_mgr->get_action_goal_json("/rotate_absolute", "turtlesim/RotateAbsolute");
  EXPECT_NE(goal_tmpl4.find("\"theta\": 0"), std::string::npos);
  EXPECT_EQ(goal_tmpl4.find("error"), std::string::npos);
}

TEST(AppTest, ServiceCallTypesupportAndExecution) {
  auto ros_mgr = std::make_shared<ROS2Manager>();
  int argc = 1;
  char arg0[] = "test_app";
  char *argv[] = {arg0, nullptr};
  bool started = ros_mgr->start(argc, argv);
  ASSERT_TRUE(started);

  std::mutex cv_m;
  std::condition_variable cv;
  bool called = false;
  bool call_success = false;
  std::string call_response;

  ros_mgr->call_service_async(
      "/non_existent_service", "std_srvs/srv/Empty", "{}",
      [&](bool success, const std::string &response, double elapsed) {
        (void)elapsed;
        std::lock_guard<std::mutex> lock(cv_m);
        called = true;
        call_success = success;
        call_response = response;
        cv.notify_one();
      });

  {
    std::unique_lock<std::mutex> lock(cv_m);
    cv.wait_for(lock, std::chrono::seconds(5), [&]() { return called; });
  }

  EXPECT_TRUE(called);
  EXPECT_FALSE(call_success);
  // It must NOT fail with "Type support not from this implementation"
  EXPECT_EQ(call_response.find("Type support not from this implementation"),
            std::string::npos);
  EXPECT_NE(call_response.find("not available"), std::string::npos);

  ros_mgr->stop();
}

TEST(AppTest, ServiceCallRoundtripWithResponseString) {
  auto ros_mgr = std::make_shared<ROS2Manager>();
  int argc = 1;
  char arg0[] = "test_app";
  char *argv[] = {arg0, nullptr};
  bool started = ros_mgr->start(argc, argv);
  ASSERT_TRUE(started);

  auto server_node = std::make_shared<rclcpp::Node>("test_set_bool_server");
  auto server = server_node->create_service<std_srvs::srv::SetBool>(
      "/test_set_bool",
      [](const std::shared_ptr<std_srvs::srv::SetBool::Request> request,
         std::shared_ptr<std_srvs::srv::SetBool::Response> response) {
        response->success = request->data;
        response->message = request->data ? "enabled successfully"
                                          : "disabled successfully";
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(server_node);
  std::atomic<bool> server_running{true};
  std::thread server_thread([&]() {
    while (server_running.load() && rclcpp::ok()) {
      executor.spin_some(std::chrono::milliseconds(20));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });

  std::mutex cv_m;
  std::condition_variable cv;
  bool called = false;
  bool call_success = false;
  std::string call_response;

  ros_mgr->call_service_async(
      "/test_set_bool", "std_srvs/srv/SetBool", "{\"data\": true}",
      [&](bool success, const std::string &response, double elapsed) {
        (void)elapsed;
        std::lock_guard<std::mutex> lock(cv_m);
        called = true;
        call_success = success;
        call_response = response;
        cv.notify_one();
      });

  {
    std::unique_lock<std::mutex> lock(cv_m);
    cv.wait_for(lock, std::chrono::seconds(5), [&]() { return called; });
  }

  server_running = false;
  server_thread.join();
  ros_mgr->stop();

  EXPECT_TRUE(called);
  EXPECT_TRUE(call_success);
  EXPECT_NE(call_response.find("enabled successfully"), std::string::npos);
  EXPECT_NE(call_response.find("\"success\": true"), std::string::npos);
}

TEST(AppTest, AltEnterInsertsNewlineInServiceInput) {
  Config cfg;
  LazyRTUIApp app(nullptr, cfg);
  auto comp = app.build_main_component();

  // Switch to Services tab (2) and right pane (1)
  comp->OnEvent(ftxui::Event::Character('3'));
  comp->OnEvent(ftxui::Event::Tab);
  EXPECT_TRUE(app.is_text_input_focused());

  // Type "{" then Alt+Enter (\x1b\r), then "\"data\": true", then Alt+Enter (\x1b\n), then "}"
  comp->OnEvent(ftxui::Event::Character('{'));
  comp->OnEvent(ftxui::Event::Special("\x1b\r"));
  for (char ch : std::string("\"data\": true")) {
    comp->OnEvent(ftxui::Event::Character(ch));
  }
  comp->OnEvent(ftxui::Event::Special("\x1b\n"));
  comp->OnEvent(ftxui::Event::Character('}'));

  const std::string &req = app.service_request_json();
  EXPECT_NE(req.find("{\n"), std::string::npos);
  EXPECT_NE(req.find("true\n"), std::string::npos);
  EXPECT_TRUE(app.is_text_input_focused());
}

TEST(AppTest, AltEnterInsertsNewlineInActionInput) {
  Config cfg;
  LazyRTUIApp app(nullptr, cfg);
  auto comp = app.build_main_component();

  // Switch to Actions tab (3) and right pane (1)
  comp->OnEvent(ftxui::Event::Character('4'));
  comp->OnEvent(ftxui::Event::Tab);
  EXPECT_TRUE(app.is_text_input_focused());

  // Type "{" then Alt+Enter (\x1b\r), then "\"goal\": 1", then "}"
  comp->OnEvent(ftxui::Event::Character('{'));
  comp->OnEvent(ftxui::Event::Special("\x1b\r"));
  for (char ch : std::string("\"goal\": 1")) {
    comp->OnEvent(ftxui::Event::Character(ch));
  }
  comp->OnEvent(ftxui::Event::Character('}'));

  const std::string &goal = app.action_goal_json();
  EXPECT_NE(goal.find("{\n"), std::string::npos);
  EXPECT_TRUE(app.is_text_input_focused());
}

TEST(AppTest, TopicEchoDeserializesStringAndBoolFields) {
  auto ros_mgr = std::make_shared<ROS2Manager>("test_topic_echo_node");
  ASSERT_TRUE(ros_mgr->start());

  auto test_pub_node =
      std::make_shared<rclcpp::Node>("test_echo_pub_node");
  auto pub = test_pub_node->create_publisher<diagnostic_msgs::msg::KeyValue>(
      "/test/echo_topic", 10);

  std::promise<std::string> msg_promise;
  auto msg_future = msg_promise.get_future();
  std::atomic<bool> received{false};

  bool sub_ok = ros_mgr->subscribe_topic(
      "/test/echo_topic", "diagnostic_msgs/msg/KeyValue",
      [&](const std::string &topic, const std::string &serialized_msg) {
        if (!received.exchange(true)) {
          msg_promise.set_value(serialized_msg);
        }
      });
  ASSERT_TRUE(sub_ok);

  diagnostic_msgs::msg::KeyValue kv;
  kv.key = "speech_text";
  kv.value = "recognized speech audio";

  auto start = std::chrono::steady_clock::now();
  while (msg_future.wait_for(std::chrono::milliseconds(50)) !=
         std::future_status::ready) {
    pub->publish(kv);
    rclcpp::spin_some(test_pub_node);
    if (std::chrono::steady_clock::now() - start > std::chrono::seconds(3)) {
      break;
    }
  }

  ASSERT_EQ(msg_future.wait_for(std::chrono::seconds(1)),
            std::future_status::ready);
  std::string formatted = msg_future.get();
  EXPECT_NE(formatted.find("\"key\": \"speech_text\""), std::string::npos);
  EXPECT_NE(formatted.find("\"value\": \"recognized speech audio\""),
            std::string::npos);
  EXPECT_EQ(formatted.find("\"key\": null"), std::string::npos);
  EXPECT_EQ(formatted.find("\"value\": null"), std::string::npos);

  ros_mgr->unsubscribe_topic("/test/echo_topic");
  ros_mgr->stop();
}

} // namespace lazyrtui
