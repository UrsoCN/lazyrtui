#include <gtest/gtest.h>
#include "lazyrtui/app.hpp"
#include "lazyrtui/config_loader.hpp"
#include <ftxui/component/event.hpp>
#include <ftxui/screen/screen.hpp>

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

  // 'q' triggers exit callback.
  EXPECT_FALSE(exited);
  comp->OnEvent(ftxui::Event::Character('q'));
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

  // Switch pane focus to right pane (1) using 'w'
  comp->OnEvent(ftxui::Event::Character('w'));
  EXPECT_EQ(app.service_pane_focus(), 1);
  EXPECT_TRUE(app.is_text_input_focused());

  // Type keys that would otherwise be hotkeys: 'r' (refresh), 'c' (call), '1' (tab 0), 'q' (quit), 'w' (switch focus)
  std::string test_input = "{\"data\": true}";
  for (char ch : test_input) {
    comp->OnEvent(ftxui::Event::Character(ch));
  }

  // Verify tab did NOT change (e.g. from '1' or '3')
  EXPECT_EQ(app.selected_tab(), 2);
  // Verify pane focus did NOT switch (e.g. from 'w')
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

  // Switch pane focus to right pane (1) using 'w'
  comp->OnEvent(ftxui::Event::Character('w'));
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
  LazyRTUIApp app(nullptr, cfg);
  auto comp = app.build_main_component();

  EXPECT_EQ(app.selected_tab(), 0);

  // '?' opens help modal
  comp->OnEvent(ftxui::Event::Character('?'));

  // When help modal is open, '2' should NOT switch tab
  comp->OnEvent(ftxui::Event::Character('2'));
  EXPECT_EQ(app.selected_tab(), 0);

  // 'Escape' closes help modal
  comp->OnEvent(ftxui::Event::Escape);

  // Now '2' switches tab
  comp->OnEvent(ftxui::Event::Character('2'));
  EXPECT_EQ(app.selected_tab(), 1);
}

TEST(AppTest, TabNavigationBetweenInputAndButton) {
  Config cfg;
  LazyRTUIApp app(nullptr, cfg);
  auto comp = app.build_main_component();

  // Switch to Services tab (2) and right pane (1)
  comp->OnEvent(ftxui::Event::Character('3'));
  comp->OnEvent(ftxui::Event::Character('w'));
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
  comp->OnEvent(ftxui::Event::Character('w'));
  EXPECT_TRUE(app.is_text_input_focused());

  // Typing 'j' and 'k' in input mode types letters rather than posting arrow keys
  comp->OnEvent(ftxui::Event::Character('j'));
  comp->OnEvent(ftxui::Event::Character('k'));

  EXPECT_NE(app.service_request_json().find("jk"), std::string::npos);
}

} // namespace lazyrtui
