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
    Tree,
    Static,
    Label,
    Input,
    Markdown,
    Button,
    Switch
)

from lazyrtui import __version__
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

DataTable, Tree {
    height: 100%;
}

.about-card {
    border: rounded $accent;
    padding: 1 2;
    margin: 1 2;
}

.setting-row {
    height: 3;
    padding: 0 1;
    align: left middle;
}
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
        Binding("7", "select_tab('tab-tf')", "TF Tree", show=True),
        Binding("8", "select_tab('tab-about')", "About/Settings", show=True),
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
        self.title = f"LazyRTUI v{__version__} - ROS 2 Terminal Interface"
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

            with TabPane("7: TF Tree", id="tab-tf"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-tf-tree"):
                        yield Label("TF Frame Hierarchy", classes="pane-title")
                        yield Tree("TF Frames", id="tree-tf")
                    with Vertical(classes="pane-container", id="pane-tf-detail"):
                        yield Label("Frame Details (Transform)", classes="pane-title")
                        yield Static("Select a frame in the tree to inspect transform data.", id="text-tf-detail")

            with TabPane("8: About & Settings", id="tab-about"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-about-info"):
                        yield Label("About LazyRTUI", classes="pane-title")
                        about_md = f"""
# LazyRTUI v{__version__}

A keyboard-first, modular Terminal User Interface (TUI) for ROS 2.

- **ROS 2 Status**: {"Connected (`rclpy` detected)" if RCLPY_AVAILABLE else "Offline / Demo Mode (`rclpy` missing)"}
- **Python Version**: `{sys.version.split()[0]}`
- **Config Path**: `{self.config_loader.config_path}`
- **Log Prevention**: Enabled (Single Node `lazy_rtui_node`, zero file log spam)

---

### Quick Keybindings Reference
- **1 ~ 8**: Switch Tabs
- **w**: Toggle Focus between List & Detail Panes
- **r**: Refresh ROS Topology
- **/**: Quick Search Filter
- **q**: Quit
"""
                        yield Markdown(about_md)

                    with Vertical(classes="pane-container", id="pane-settings"):
                        yield Label("Settings & Configuration", classes="pane-title")
                        yield Label("\n[Preferences]", classes="setting-row")
                        with Horizontal(classes="setting-row"):
                            yield Label("Auto-refresh Topology (every 2s):  ")
                            yield Switch(value=True, id="switch-auto-refresh")
                        with Horizontal(classes="setting-row"):
                            yield Label("Enable Mouse Support:             ")
                            yield Switch(value=True, id="switch-mouse-support")
                        with Horizontal(classes="setting-row"):
                            yield Label("Single Node Log Suppression:      ")
                            yield Switch(value=True, id="switch-log-suppress")

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
        elif current_tab == "tab-tf":
            tf_tree = self.query_one("#tree-tf", Tree)
            if self.focused == tf_tree:
                self.set_focus(self.query_one("#text-tf-detail"))
            else:
                self.set_focus(tf_tree)

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

        # Populate TF Tree
        tf_tree_widget = self.query_one("#tree-tf", Tree)
        tf_tree_widget.clear()
        roots = self.ros_manager.get_tf_root_nodes()
        for root_node in roots:
            self._build_tf_tree_branch(tf_tree_widget.root, root_node)
        tf_tree_widget.root.expand()

    def _build_tf_tree_branch(self, parent_widget_node, tf_node):
        tx, ty, tz = tf_node.translation
        label = f"{tf_node.frame_id}  (Pos: [{tx:.2f}, {ty:.2f}, {tz:.2f}])"
        widget_node = parent_widget_node.add(label, data=tf_node)
        widget_node.expand()
        for child in tf_node.children.values():
            self._build_tf_tree_branch(widget_node, child)


def main():
    app = LazyRTUIApp()
    app.run()

if __name__ == "__main__":
    main()
