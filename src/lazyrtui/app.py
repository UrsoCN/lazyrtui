import sys
from typing import ClassVar
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Container, Horizontal, Vertical
from textual.widgets import (
    Header,
    Footer,
    TabbedContent,
    TabPane,
    DataTable,
    Static,
    Label,
    Input,
    Markdown
)

from lazyrtui.ros import ROS2Manager, RCLPY_AVAILABLE
from lazyrtui.config import ConfigLoader

TCSS_STYLES = """
Screen {
    layout: vertical;
    background: $surface;
}

#header-status {
    dock: top;
    height: 1;
    background: $accent;
    color: $text;
    content-align: center middle;
}

.pane-container {
    height: 100%;
    border: solid $primary;
}

.pane-container:focus-within {
    border: double $accent;
}

.pane-title {
    background: $primary-darken-2;
    color: $text;
    text-style: bold;
    padding: 0 1;
}

DataTable {
    height: 100%;
}
"""

HELP_MARKDOWN = """
# LazyRTUI 快捷键帮助

- **1 ~ 6**: 快速切换页签 (Node, Topic, Service, Action, Interface, Bag)
- **Tab / Shift+Tab**: 在焦点元素间移动
- **w**: 在面板区之间切换焦点 (Switch Focus Pane)
- **j / k** 或 **Up / Down**: 列表中光标上下移动
- **/**: 打开过滤搜索框
- **r**: 刷新 ROS 拓扑状态
- **e**: 开启/停止 Topic Echo
- **p**: 开启/停止 Topic 实时绘图
- **c**: 调用 Service 或发送 Action Goal
- **q**: 退出程序
"""

class LazyRTUIApp(App):
    """Main Textual Application for ROS 2 TUI."""

    CSS = TCSS_STYLES

    BINDINGS: ClassVar[list[Binding]] = [
        Binding("1", "select_tab('tab-nodes')", "Nodes", show=True),
        Binding("2", "select_tab('tab-topics')", "Topics", show=True),
        Binding("3", "select_tab('tab-services')", "Services", show=True),
        Binding("4", "select_tab('tab-actions')", "Actions", show=True),
        Binding("5", "select_tab('tab-interfaces')", "Interfaces", show=True),
        Binding("6", "select_tab('tab-bags')", "Bags", show=True),
        Binding("w", "toggle_pane_focus", "Switch Focus", show=True),
        Binding("r", "refresh_ros", "Refresh", show=True),
        Binding("q", "quit", "Quit", show=True),
    ]

    def __init__(self):
        super().__init__()
        self.config_loader = ConfigLoader()
        self.config = self.config_loader.config
        self.ros_manager = ROS2Manager()

    def on_mount(self) -> None:
        self.title = "LazyRTUI - ROS 2 Terminal Interface"
        self.sub_title = "ROS 2 Management Tool"
        self.ros_manager.start()
        self.refresh_all_data()

    def on_unmount(self) -> None:
        self.ros_manager.stop()

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        
        status_text = "ROS 2 Status: Connected" if RCLPY_AVAILABLE else "ROS 2 Status: Offline / Demo Mode (rclpy not detected)"
        yield Static(status_text, id="header-status")

        with TabbedContent(initial="tab-nodes", id="main-tabs"):
            with TabPane("1: Nodes", id="tab-nodes"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-nodes-list"):
                        yield Label("Active Nodes", classes="pane-title")
                        yield DataTable(id="table-nodes")
                    with Vertical(classes="pane-container", id="pane-nodes-detail"):
                        yield Label("Node Details", classes="pane-title")
                        yield Static("Select a node on the left to inspect details.", id="text-node-detail")

            with TabPane("2: Topics", id="tab-topics"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-topics-list"):
                        yield Label("Active Topics", classes="pane-title")
                        yield DataTable(id="table-topics")
                    with Vertical(classes="pane-container", id="pane-topics-detail"):
                        yield Label("Topic Payload / Graph", classes="pane-title")
                        yield Static("Select a topic to echo or plot data.", id="text-topic-detail")

            with TabPane("3: Services", id="tab-services"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-services-list"):
                        yield Label("Available Services", classes="pane-title")
                        yield DataTable(id="table-services")
                    with Vertical(classes="pane-container", id="pane-services-detail"):
                        yield Label("Service Request Builder", classes="pane-title")
                        yield Static("Select a service to send request.", id="text-service-detail")

            with TabPane("4: Actions", id="tab-actions"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-actions-list"):
                        yield Label("Available Actions", classes="pane-title")
                        yield DataTable(id="table-actions")
                    with Vertical(classes="pane-container", id="pane-actions-detail"):
                        yield Label("Action Goal / Feedback", classes="pane-title")
                        yield Static("Select an action to send goal.", id="text-action-detail")

            with TabPane("5: Interfaces", id="tab-interfaces"):
                with Vertical(classes="pane-container"):
                    yield Label("Message / Service / Action Types", classes="pane-title")
                    yield Static("Interface Explorer", id="text-interface-tree")

            with TabPane("6: Bags", id="tab-bags"):
                with Vertical(classes="pane-container"):
                    yield Label("ROS Bag Record & Playback", classes="pane-title")
                    yield Static("Bag Manager", id="text-bag-manager")

        yield Footer()

    def action_select_tab(self, tab_id: str) -> None:
        tabs = self.query_one("#main-tabs", TabbedContent)
        tabs.active = tab_id

    def action_toggle_pane_focus(self) -> None:
        """Cycles focus between list and detail panes in the active tab."""
        tabs = self.query_one("#main-tabs", TabbedContent)
        current_tab = tabs.active
        if current_tab == "tab-nodes":
            node_table = self.query_one("#table-nodes", DataTable)
            if self.focused == node_table:
                self.set_focus(self.query_one("#text-node-detail"))
            else:
                self.set_focus(node_table)
        elif current_tab == "tab-topics":
            topic_table = self.query_one("#table-topics", DataTable)
            if self.focused == topic_table:
                self.set_focus(self.query_one("#text-topic-detail"))
            else:
                self.set_focus(topic_table)

    def action_refresh_ros(self) -> None:
        self.refresh_all_data()

    def refresh_all_data(self) -> None:
        # Populate Nodes table
        nodes_table = self.query_one("#table-nodes", DataTable)
        nodes_table.clear(columns=True)
        nodes_table.add_columns("Node Name", "Namespace")
        for name, ns in self.ros_manager.get_nodes():
            nodes_table.add_row(name, ns)

        # Populate Topics table
        topics_table = self.query_one("#table-topics", DataTable)
        topics_table.clear(columns=True)
        topics_table.add_columns("Topic Name", "Type(s)")
        for name, types in self.ros_manager.get_topics():
            topics_table.add_row(name, ", ".join(types))

        # Populate Services table
        services_table = self.query_one("#table-services", DataTable)
        services_table.clear(columns=True)
        services_table.add_columns("Service Name", "Type(s)")
        for name, types in self.ros_manager.get_services():
            services_table.add_row(name, ", ".join(types))

        # Populate Actions table
        actions_table = self.query_one("#table-actions", DataTable)
        actions_table.clear(columns=True)
        actions_table.add_columns("Action Name", "Type(s)")
        for name, types in self.ros_manager.get_actions():
            actions_table.add_row(name, ", ".join(types))


def main():
    app = LazyRTUIApp()
    app.run()

if __name__ == "__main__":
    main()
