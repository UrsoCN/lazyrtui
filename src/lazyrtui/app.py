import sys
from typing import ClassVar
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.screen import ModalScreen
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
    border: round $accent;
    padding: 1 2;
    margin: 1 2;
}

.setting-row {
    height: 3;
    padding: 0 1;
    align: left middle;
}

ModalScreen {
    align: center middle;
    background: rgba(0, 0, 0, 0.6);
}

#help-dialog {
    padding: 1 2;
    background: $surface;
    border: thick $accent;
    width: 70;
    height: auto;
    max-height: 85%;
}

#btn-close-help {
    margin-top: 1;
    width: 100%;
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
        # Periodically refresh ROS topology as DDS graph updates
        self.set_interval(2.0, self.refresh_all_data)

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
                        yield Markdown("Select a node on the left to inspect details.", id="text-node-detail")

            with TabPane("2: Topics", id="tab-topics"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-topics-list"):
                        yield Label("Active Topics", classes="pane-title")
                        yield DataTable(id="table-topics")
                    with Vertical(classes="pane-container", id="pane-topics-detail"):
                        yield Label("Topic Payload / Graph", classes="pane-title")
                        yield Markdown("Select a topic to echo or plot data.", id="text-topic-detail")

            with TabPane("3: Services", id="tab-services"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-services-list"):
                        yield Label("Available Services", classes="pane-title")
                        yield DataTable(id="table-services")
                    with Vertical(classes="pane-container", id="pane-services-detail"):
                        yield Label("Service Request Builder", classes="pane-title")
                        yield Markdown("Select a service to send request.", id="text-service-detail")

            with TabPane("4: Actions", id="tab-actions"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-actions-list"):
                        yield Label("Available Actions", classes="pane-title")
                        yield DataTable(id="table-actions")
                    with Vertical(classes="pane-container", id="pane-actions-detail"):
                        yield Label("Action Goal / Feedback", classes="pane-title")
                        yield Markdown("Select an action to send goal.", id="text-action-detail")

            with TabPane("5: Interfaces", id="tab-interfaces"):
                with Vertical(classes="pane-container"):
                    yield Label("Message / Service / Action Types", classes="pane-title")
                    yield Markdown("Interface Explorer", id="text-interface-tree")

            with TabPane("6: Bags", id="tab-bags"):
                with Vertical(classes="pane-container"):
                    yield Label("ROS Bag Record & Playback", classes="pane-title")
                    yield Markdown("Bag Manager", id="text-bag-manager")

            with TabPane("7: TF Tree", id="tab-tf"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-tf-tree"):
                        yield Label("TF Frame Hierarchy", classes="pane-title")
                        yield Tree("TF Frames", id="tree-tf")
                    with Vertical(classes="pane-container", id="pane-tf-detail"):
                        yield Label("Frame Details (Transform)", classes="pane-title")
                        yield Markdown("Select a frame in the tree to inspect transform data.", id="text-tf-detail")

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

### 💡 终端文本复制技巧 (Text Copying Guide)
- **终端原生鼠标选择**: 按住键盘 **`Shift`** 键不放并用鼠标在终端中划词，即可绕过 TUI 捕获，使用终端原生的选中和 `Ctrl+Shift+C` 复制功能！

---

### Quick Keybindings Reference
- **1 ~ 8**: Switch Tabs
- **w**: Toggle Focus between List & Detail Panes
- **r**: Refresh ROS Topology
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
        # Force refresh bypassing cache
        self._cached_nodes = None
        self._cached_topics = None
        self._cached_services = None
        self._cached_actions = None
        self._cached_tf_structure = None
        self.refresh_all_data()

    def on_data_table_row_highlighted(self, event: DataTable.RowHighlighted) -> None:
        """Event handler when user highlights a row in any data table."""
        table_id = event.data_table.id
        if not event.row_key:
            return

        if table_id == "table-nodes":
            try:
                row_vals = event.data_table.get_row(event.row_key)
                node_name = str(row_vals[0])
                namespace = str(row_vals[1])
                info = self.ros_manager.get_node_info(node_name, namespace)
                
                pubs_str = "\n".join([f"- `{p[0]}` ({', '.join(p[1])})" for p in info.get("publishers", [])]) or "_None_"
                subs_str = "\n".join([f"- `{s[0]}` ({', '.join(s[1])})" for s in info.get("subscribers", [])]) or "_None_"
                srvs_str = "\n".join([f"- `{v[0]}` ({', '.join(v[1])})" for v in info.get("services", [])]) or "_None_"
                
                md = f"""### Node: `{node_name}`
**Namespace**: `{namespace}`

#### 📢 Publishers
{pubs_str}

#### 📥 Subscribers
{subs_str}

#### ⚙️ Services
{srvs_str}
"""
                self.query_one("#text-node-detail", Markdown).update(md)
            except Exception:
                pass

        elif table_id == "table-topics":
            try:
                row_vals = event.data_table.get_row(event.row_key)
                topic_name = str(row_vals[0])
                info = self.ros_manager.get_topic_info(topic_name)
                types_str = ", ".join(info.get("types", []))
                md = f"""### Topic: `{topic_name}`
**Type(s)**: `{types_str}`

- **Publishers Count**: `{info.get('publisher_count', 0)}`
- **Subscribers Count**: `{info.get('subscriber_count', 0)}`

---
_Press 'e' to start Echo or 'p' to plot numerical data_
"""
                self.query_one("#text-topic-detail", Markdown).update(md)
            except Exception:
                pass

        elif table_id == "table-services":
            try:
                row_vals = event.data_table.get_row(event.row_key)
                service_name = str(row_vals[0])
                info = self.ros_manager.get_service_info(service_name)
                types_str = ", ".join(info.get("types", []))
                req_json = info.get("sample_request", "{}")
                md = f"""### Service: `{service_name}`
**Type(s)**: `{types_str}`

#### Sample Request Payload:
```json
{req_json}
```
---
_Press 'c' to build and invoke service_
"""
                self.query_one("#text-service-detail", Markdown).update(md)
            except Exception:
                pass

        elif table_id == "table-actions":
            try:
                row_vals = event.data_table.get_row(event.row_key)
                action_name = str(row_vals[0])
                action_type = str(row_vals[1])
                md = f"""### Action: `{action_name}`
**Type**: `{action_type}`

- **Status**: Ready
---
_Press 'c' to send action goal_
"""
                self.query_one("#text-action-detail", Markdown).update(md)
            except Exception:
                pass

    def on_tree_node_highlighted(self, event: Tree.NodeHighlighted) -> None:
        """Event handler when user highlights a TF frame in the TF Tree."""
        tf_node = event.node.data
        if tf_node:
            tx, ty, tz = tf_node.translation
            rx, ry, rz, rw = tf_node.rotation
            md = f"""### TF Frame: `{tf_node.frame_id}`
**Parent Frame**: `{tf_node.parent_id or "Root (No Parent)"}`

#### 📍 Translation (Position)
- **X**: `{tx:.4f}`
- **Y**: `{ty:.4f}`
- **Z**: `{tz:.4f}`

#### 🔄 Rotation (Quaternion)
- **X**: `{rx:.4f}`
- **Y**: `{ry:.4f}`
- **Z**: `{rz:.4f}`
- **W**: `{rw:.4f}`
"""
            self.query_one("#text-tf-detail", Markdown).update(md)

    def _update_table_smart(self, table_id: str, new_rows: list[tuple], columns: list[str], cache_attr: str) -> None:
        cached_data = getattr(self, cache_attr, None)
        if cached_data == new_rows:
            return  # Data has not changed! Do not touch table UI or reset cursor!

        setattr(self, cache_attr, new_rows)
        table = self.query_one(table_id, DataTable)

        # Save old cursor coordinate
        old_cursor = table.cursor_coordinate

        table.clear(columns=True)
        table.add_columns(*columns)
        for row in new_rows:
            table.add_row(*row)

        # Restore cursor position safely
        if table.row_count > 0:
            target_row = min(old_cursor.row, table.row_count - 1)
            target_col = min(old_cursor.column, max(0, len(columns) - 1))
            table.move_cursor(row=target_row, column=target_col)

    def refresh_all_data(self) -> None:
        nodes = [(name, ns) for name, ns in self.ros_manager.get_nodes()]
        self._update_table_smart("#table-nodes", nodes, ["Node Name", "Namespace"], "_cached_nodes")

        topics = [(name, ", ".join(types)) for name, types in self.ros_manager.get_topics()]
        self._update_table_smart("#table-topics", topics, ["Topic Name", "Type(s)"], "_cached_topics")

        services = [(name, ", ".join(types)) for name, types in self.ros_manager.get_services()]
        self._update_table_smart("#table-services", services, ["Service Name", "Type(s)"], "_cached_services")

        actions = [(name, ", ".join(types)) for name, types in self.ros_manager.get_actions()]
        self._update_table_smart("#table-actions", actions, ["Action Name", "Type(s)"], "_cached_actions")

        # Refresh TF Tree only if structure or frame IDs changed
        roots = self.ros_manager.get_tf_root_nodes()
        tf_structure_key = tuple(sorted(self.ros_manager.tf_frames.keys()))
        if getattr(self, "_cached_tf_structure", None) != tf_structure_key:
            self._cached_tf_structure = tf_structure_key
            tf_tree_widget = self.query_one("#tree-tf", Tree)
            tf_tree_widget.clear()
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
