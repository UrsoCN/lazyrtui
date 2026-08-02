"""
LazyRTUI - Main Textual Application
====================================
Layout: left list pane  |  right detail/action pane
Tabs: Nodes | Topics | Services | Actions | Interfaces | Bags | TF Tree | About
"""
from __future__ import annotations

import sys
import json
import time
import threading
from typing import ClassVar, Any

from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical, Container, ScrollableContainer
from textual.widget import Widget
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
    Switch,
    TextArea,
)
from textual.reactive import reactive

from lazyrtui import __version__
from lazyrtui.ros import ROS2Manager, RCLPY_AVAILABLE
from lazyrtui.config import ConfigLoader


# ---------------------------------------------------------------------------
# CSS
# ---------------------------------------------------------------------------
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

/* ---- Generic pane containers ---- */
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

Tree {
    height: 100%;
}

/* ---- Service / Action panel ---- */
.section-label {
    padding: 0 1;
    color: $text-muted;
    text-style: bold;
}

#input-service-req, #input-action-goal {
    height: 8;
    margin: 0 1 0 1;
}

#btn-call-service, #btn-send-goal {
    margin: 0 1 1 1;
    width: 1fr;
}

#text-service-response, #text-action-response {
    margin: 0 1;
    padding: 0 1;
    border: solid $primary-darken-3;
    height: 8;
    overflow-y: auto;
}

/* ---- Topic echo card ---- */
TopicEchoCard {
    height: auto;
    border: solid $primary;
    margin: 0 1 1 1;
    padding: 0 1;
}

.echo-card-header {
    layout: horizontal;
    height: 1;
    background: $primary-darken-2;
}

.echo-card-name {
    width: 1fr;
    color: $text;
    text-style: bold;
}

.echo-card-type {
    color: $text-muted;
    width: 1fr;
}

.echo-card-body {
    height: auto;
    max-height: 8;
    overflow-y: auto;
    padding: 0;
}

#pane-topics-monitors {
    width: 1fr;
    border: solid $primary;
    overflow-y: auto;
}

#topics-monitor-list {
    height: auto;
}

/* ---- Settings ---- */
.setting-row {
    height: 3;
    padding: 0 1;
    align: left middle;
}
"""


# ---------------------------------------------------------------------------
# TopicEchoCard — a composable widget representing one monitored topic
# ---------------------------------------------------------------------------
class TopicEchoCard(Widget):
    """
    A self-contained card widget that shows the latest message received
    on a single subscribed topic.
    Inherits from Widget (not Static) so it can properly contain child widgets.
    """

    DEFAULT_CSS = """
    TopicEchoCard {
        height: auto;
        min-height: 6;
        border: solid $primary;
        margin: 0 1 1 1;
        padding: 0 0 1 0;
    }
    """

    # Reactive content — changing this automatically re-renders the Markdown body
    _content: reactive[str] = reactive("_Waiting for first message..._")

    def __init__(self, topic_name: str, topic_type: str):
        super().__init__()
        self.topic_name = topic_name
        self.topic_type = topic_type

    def compose(self) -> ComposeResult:
        with Horizontal(classes="echo-card-header"):
            yield Label(f"📡 {self.topic_name}", classes="echo-card-name")
            yield Label(f" [{self.topic_type}]", classes="echo-card-type")
            yield Button("✕", variant="error", id=f"btn-unsub-{self._safe_id()}", classes="echo-card-btn")
        yield Static(self._content, id=f"card-body-{self._safe_id()}")

    def _safe_id(self) -> str:
        import re
        safe = re.sub(r"[^a-zA-Z0-9_-]", "_", self.topic_name)
        safe = re.sub(r"_+", "_", safe).strip("_")
        if safe and safe[0].isdigit():
            safe = "t_" + safe
        return safe or "topic"

    def watch__content(self, new_content: str) -> None:
        """Automatically called by Textual when _content reactive changes."""
        try:
            self.query_one(f"#card-body-{self._safe_id()}", Static).update(new_content)
        except Exception:
            pass

    def update_message(self, msg_str: str) -> None:
        """
        Thread-safe update: mutating a reactive attribute is safe to call
        from any thread in Textual — it schedules a DOM update internally.
        """
        self._content = msg_str


# ---------------------------------------------------------------------------
# Main App
# ---------------------------------------------------------------------------
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
        Binding("w", "toggle_pane_focus", "Switch Pane", show=True),
        Binding("r", "refresh_ros", "Refresh", show=True),
        Binding("e", "toggle_topic_echo", "Echo Topic", show=True),
        Binding("c", "call_service", "Call Service", show=True),
        Binding("g", "send_action_goal", "Send Goal", show=True),
        Binding("q", "quit", "Quit", show=True),
    ]

    def __init__(self):
        super().__init__()
        self.config_loader = ConfigLoader()
        self.config = self.config_loader.config
        self.ros_manager = ROS2Manager()
        # topic_name -> TopicEchoCard
        self._echo_cards: dict[str, TopicEchoCard] = {}

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def on_mount(self) -> None:
        self.title = f"LazyRTUI v{__version__} - ROS 2 Terminal Interface"
        self.sub_title = "ROS 2 Management Tool"
        self.ros_manager.start()
        self.refresh_all_data()
        self.set_interval(2.0, self.refresh_all_data)

    def on_unmount(self) -> None:
        self.ros_manager.stop()

    # ------------------------------------------------------------------
    # Layout
    # ------------------------------------------------------------------

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)

        status_text = (
            "  ✅  ROS 2: Connected"
            if RCLPY_AVAILABLE
            else "  ⚠️  ROS 2: Offline / Demo Mode (rclpy not detected)"
        )
        yield Static(status_text, id="header-status")

        with TabbedContent(initial="tab-nodes", id="main-tabs"):

            # ---- Tab 1: Nodes ----
            with TabPane("1: Nodes", id="tab-nodes"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-nodes-list"):
                        yield Label("Active Nodes", classes="pane-title")
                        yield DataTable(id="table-nodes")
                    with Vertical(classes="pane-container", id="pane-nodes-detail"):
                        yield Label("Node Details", classes="pane-title")
                        yield Markdown(
                            "Select a node on the left to inspect its pub/sub/service graph.",
                            id="text-node-detail",
                        )

            # ---- Tab 2: Topics ----
            with TabPane("2: Topics", id="tab-topics"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-topics-list"):
                        yield Label("Active Topics  ( press 'e' to add/remove monitor )", classes="pane-title")
                        yield DataTable(id="table-topics")
                    # Right pane: multi-topic live monitor cards
                    with Vertical(id="pane-topics-monitors"):
                        yield Label("Live Topic Monitors", classes="pane-title")
                        yield Static(
                            "  Press [bold]e[/bold] on a topic in the list to start monitoring.\n"
                            "  Multiple topics can be monitored simultaneously.",
                            id="topics-monitor-hint",
                        )
                        yield Container(id="topics-monitor-list")

            # ---- Tab 3: Services ----
            with TabPane("3: Services", id="tab-services"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-services-list"):
                        yield Label("Available Services", classes="pane-title")
                        yield DataTable(id="table-services")
                    with Vertical(classes="pane-container", id="pane-services-detail"):
                        yield Label("Service Call Panel", classes="pane-title")
                        yield Markdown(
                            "Select a service on the left to load its request template.",
                            id="text-service-detail",
                        )
                        yield Label("Request Payload (JSON):", classes="section-label")
                        yield TextArea("{}", id="input-service-req", language="json")
                        yield Button("▶  Call Service  (c)", variant="success", id="btn-call-service")
                        yield Label("Response:", classes="section-label")
                        yield Markdown("_Ready to call..._", id="text-service-response")

            # ---- Tab 4: Actions ----
            with TabPane("4: Actions", id="tab-actions"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-actions-list"):
                        yield Label("Available Actions", classes="pane-title")
                        yield DataTable(id="table-actions")
                    with Vertical(classes="pane-container", id="pane-actions-detail"):
                        yield Label("Action Goal Panel", classes="pane-title")
                        yield Markdown(
                            "Select an action on the left to load its goal template.",
                            id="text-action-detail",
                        )
                        yield Label("Goal Payload (JSON):", classes="section-label")
                        yield TextArea("{}", id="input-action-goal", language="json")
                        yield Button("▶  Send Goal  (g)", variant="success", id="btn-send-goal")
                        yield Label("Response / Feedback:", classes="section-label")
                        yield Markdown("_Ready to send goal..._", id="text-action-response")

            # ---- Tab 5: Interfaces ----
            with TabPane("5: Interfaces", id="tab-interfaces"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-interfaces-tree"):
                        yield Label("Interface Types Explorer  ( ▶ expand / ◀ collapse )", classes="pane-title")
                        yield Tree("ROS 2 Interfaces", id="tree-interfaces")
                    with Vertical(classes="pane-container", id="pane-interfaces-detail"):
                        yield Label("Interface Details", classes="pane-title")
                        yield Markdown(
                            "Select an interface type to see its field definitions.",
                            id="text-interface-detail",
                        )

            # ---- Tab 6: Bags ----
            with TabPane("6: Bags", id="tab-bags"):
                with Vertical(classes="pane-container"):
                    yield Label("ROS Bag Record & Playback Manager", classes="pane-title")
                    yield Markdown(
                        """
### 📼 ROS Bag Recording

| Field | Value |
|-------|-------|
| Record Status | `Idle` |
| Output Directory | `~/.ros/bags/` |
| Storage Format | `mcap` / `db3` |

---

> Run `ros2 bag record -a` in a separate terminal to start recording all topics.
> Run `ros2 bag play <bag_path>` to replay a recorded bag.

_Plugin-based GUI controls are planned for Phase 5._
""",
                        id="text-bag-manager",
                    )

            # ---- Tab 7: TF Tree ----
            with TabPane("7: TF Tree", id="tab-tf"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-tf-tree"):
                        yield Label("TF Frame Hierarchy", classes="pane-title")
                        yield Tree("TF Frames", id="tree-tf")
                    with Vertical(classes="pane-container", id="pane-tf-detail"):
                        yield Label("Frame Details (Transform)", classes="pane-title")
                        yield Markdown(
                            "Select a frame in the tree to inspect its transform data.",
                            id="text-tf-detail",
                        )

            # ---- Tab 8: About & Settings ----
            with TabPane("8: About & Settings", id="tab-about"):
                with Horizontal():
                    with Vertical(classes="pane-container", id="pane-about-info"):
                        yield Label("About LazyRTUI", classes="pane-title")
                        about_md = f"""
# LazyRTUI v{__version__}

A keyboard-first, modular Terminal User Interface (TUI) for ROS 2.

| Field | Value |
|-------|-------|
| ROS 2 Status | {"✅ Connected (`rclpy` detected)" if RCLPY_AVAILABLE else "⚠️ Offline / Demo Mode"} |
| Python Version | `{sys.version.split()[0]}` |
| Config Path | `{self.config_loader.config_path}` |
| Log Prevention | Enabled — Single `lazy_rtui_node`, zero `/log` spam |

---

### 💡 Keybindings

| Key | Action |
|-----|--------|
| `1` – `8` | Switch tabs |
| `w` | Toggle pane focus (list ↔ detail) |
| `r` | Force refresh ROS topology |
| `e` | Add / remove Topic echo monitor card |
| `c` | Call selected Service |
| `g` | Send Action goal |
| `q` | Quit |

---

### 📋 Text Copying (Shift + Mouse)
Hold **`Shift`** while dragging the mouse to use your terminal's native selection,
bypassing TUI capture. Then `Ctrl+Shift+C` to copy.
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

    # ------------------------------------------------------------------
    # Tab switching
    # ------------------------------------------------------------------

    def action_select_tab(self, tab_id: str) -> None:
        self.query_one("#main-tabs", TabbedContent).active = tab_id

    # ------------------------------------------------------------------
    # Pane focus toggle
    # ------------------------------------------------------------------

    def action_toggle_pane_focus(self) -> None:
        """Cycles focus between list and detail panes."""
        tabs = self.query_one("#main-tabs", TabbedContent)
        pairs = {
            "tab-nodes": ("#table-nodes", "#text-node-detail"),
            "tab-topics": ("#table-topics", "#topics-monitor-list"),
            "tab-services": ("#table-services", "#input-service-req"),
            "tab-actions": ("#table-actions", "#input-action-goal"),
            "tab-interfaces": ("#tree-interfaces", "#text-interface-detail"),
            "tab-tf": ("#tree-tf", "#text-tf-detail"),
        }
        pair = pairs.get(tabs.active)
        if not pair:
            return
        left_id, right_id = pair
        try:
            left = self.query_one(left_id)
            right = self.query_one(right_id)
            if self.focused is left:
                self.set_focus(right)
            else:
                self.set_focus(left)
        except Exception:
            pass

    # ------------------------------------------------------------------
    # Force refresh
    # ------------------------------------------------------------------

    def action_refresh_ros(self) -> None:
        for attr in ("_cached_nodes", "_cached_topics", "_cached_services",
                     "_cached_actions", "_cached_tf_structure"):
            setattr(self, attr, None)
        self.refresh_all_data()

    # ------------------------------------------------------------------
    # Topic Echo
    # ------------------------------------------------------------------

    def action_toggle_topic_echo(self) -> None:
        """Add or remove a topic echo card for the currently highlighted topic."""
        tabs = self.query_one("#main-tabs", TabbedContent)
        if tabs.active != "tab-topics":
            return
        table = self.query_one("#table-topics", DataTable)
        if table.row_count == 0:
            return
        try:
            row_vals = table.get_row_at(table.cursor_coordinate.row)
        except Exception:
            return
        # col 0 = indicator, col 1 = actual topic name, col 2 = type(s)
        topic_name = str(row_vals[1])
        topic_type = str(row_vals[2]).split(",")[0].strip()

        if topic_name in self._echo_cards:
            # Already monitoring — remove card
            self._remove_echo_card(topic_name)
        else:
            # Add new card
            self._add_echo_card(topic_name, topic_type)

    def _add_echo_card(self, topic_name: str, topic_type: str) -> None:
        """Create a new TopicEchoCard and subscribe to the topic."""
        # Hide hint label once we have at least one card
        try:
            self.query_one("#topics-monitor-hint").display = False
        except Exception:
            pass

        card = TopicEchoCard(topic_name, topic_type)
        self._echo_cards[topic_name] = card
        monitor_list = self.query_one("#topics-monitor-list", Container)
        monitor_list.mount(card)

        def _on_msg(msg: Any):
            """ROS callback — runs in the rclpy spin thread."""
            try:
                slots = getattr(msg, "__slots__", [])
                d = {}
                for slot in slots:
                    key = slot.lstrip("_")
                    val = getattr(msg, slot, getattr(msg, key, None))
                    if hasattr(val, "__slots__"):
                        inner = {}
                        for s2 in getattr(val, "__slots__", []):
                            k2 = s2.lstrip("_")
                            inner[k2] = getattr(val, s2, getattr(val, k2, None))
                        d[key] = inner
                    else:
                        d[key] = val
                msg_str = json.dumps(d, default=str, indent=2)
            except Exception as e:
                msg_str = f"(parse error: {e})\n{str(msg)[:200]}"

            # IMPORTANT: card._content is a Textual reactive — must be set on the
            # main event-loop thread.  call_from_thread() schedules the lambda on
            # the Textual event loop so the reactive watcher fires safely.
            self.call_from_thread(card.update_message, msg_str)

        ok, err = self.ros_manager.subscribe_topic(topic_name, topic_type, _on_msg)
        if not ok:
            card._content = (
                f"⚠️ Cannot subscribe to `{topic_name}`\n\n"
                f"> {err}\n\n"
                "Make sure the interface package is sourced before launching LazyRTUI."
            )

    def _remove_echo_card(self, topic_name: str) -> None:
        """Destroy the echo card and unsubscribe from the topic."""
        self.ros_manager.unsubscribe_topic(topic_name)
        card = self._echo_cards.pop(topic_name, None)
        if card:
            card.remove()
        # Show hint again when all cards removed
        if not self._echo_cards:
            try:
                self.query_one("#topics-monitor-hint").display = True
            except Exception:
                pass

    def on_button_pressed(self, event: Button.Pressed) -> None:
        btn_id = event.button.id or ""
        if btn_id == "btn-call-service":
            self._do_call_service()
        elif btn_id == "btn-send-goal":
            self._do_send_action_goal()
        elif btn_id.startswith("btn-unsub-"):
            # Derive topic name from the card that owns this button
            try:
                card = event.button.ancestors_with_type(TopicEchoCard)
                c = next(card)
                self._remove_echo_card(c.topic_name)
            except Exception:
                pass

    # ------------------------------------------------------------------
    # Service Call
    # ------------------------------------------------------------------

    def action_call_service(self) -> None:
        tabs = self.query_one("#main-tabs", TabbedContent)
        if tabs.active == "tab-services":
            self._do_call_service()

    def _do_call_service(self) -> None:
        srv_table = self.query_one("#table-services", DataTable)
        if srv_table.row_count == 0:
            return
        try:
            row_vals = srv_table.get_row_at(srv_table.cursor_coordinate.row)
        except Exception:
            return
        srv_name = str(row_vals[0])
        srv_type = str(row_vals[1]).split(",")[0].strip()

        req_text = self.query_one("#input-service-req", TextArea).text
        try:
            data = json.loads(req_text)
        except Exception as e:
            self.query_one("#text-service-response", Markdown).update(
                f"❌ **JSON parse error**: `{e}`\n\nPlease fix the payload above and try again."
            )
            return

        resp_widget = self.query_one("#text-service-response", Markdown)
        resp_widget.update("⏳ **Calling service...**")

        def _on_result(success: bool, result: Any, duration: str):
            if success:
                try:
                    result_str = json.dumps(result, indent=2, default=str)
                except Exception:
                    result_str = str(result)
                msg = f"✅ **Success** — `{srv_name}` (took `{duration}`)\n\n```json\n{result_str}\n```"
            else:
                msg = f"❌ **Failed** — `{srv_name}` (took `{duration}`)\n\n```\n{result}\n```"
            self.call_from_thread(resp_widget.update, msg)

        self.ros_manager.call_service_async(srv_name, srv_type, data, _on_result)

    # ------------------------------------------------------------------
    # Action Goal
    # ------------------------------------------------------------------

    def action_send_action_goal(self) -> None:
        tabs = self.query_one("#main-tabs", TabbedContent)
        if tabs.active == "tab-actions":
            self._do_send_action_goal()

    def _do_send_action_goal(self) -> None:
        act_table = self.query_one("#table-actions", DataTable)
        if act_table.row_count == 0:
            return
        try:
            row_vals = act_table.get_row_at(act_table.cursor_coordinate.row)
        except Exception:
            return
        act_name = str(row_vals[0])
        act_type = str(row_vals[1]).split(",")[0].strip()

        goal_text = self.query_one("#input-action-goal", TextArea).text
        try:
            data = json.loads(goal_text)
        except Exception as e:
            self.query_one("#text-action-response", Markdown).update(
                f"❌ **JSON parse error**: `{e}`\n\nPlease fix the goal payload and try again."
            )
            return

        resp_widget = self.query_one("#text-action-response", Markdown)
        resp_widget.update("⏳ **Sending action goal...**")

        # Actions are dispatched via the send_goal service internally;
        # We reuse call_service_async on the /_action/send_goal endpoint.
        send_goal_srv = f"{act_name}/_action/send_goal"
        goal_payload = {"goal": data}

        def _on_result(success: bool, result: Any, duration: str):
            if success:
                try:
                    result_str = json.dumps(result, indent=2, default=str)
                except Exception:
                    result_str = str(result)
                msg = f"✅ **Goal Accepted** — `{act_name}` (took `{duration}`)\n\n```json\n{result_str}\n```"
            else:
                msg = f"❌ **Goal Rejected / Failed** — `{act_name}` (took `{duration}`)\n\n```\n{result}\n```"
            self.call_from_thread(resp_widget.update, msg)

        self.ros_manager.call_service_async(send_goal_srv, act_type, goal_payload, _on_result)

    # ------------------------------------------------------------------
    # Row highlight handlers
    # ------------------------------------------------------------------

    def on_data_table_row_highlighted(self, event: DataTable.RowHighlighted) -> None:
        table_id = event.data_table.id
        if not event.row_key:
            return

        if table_id == "table-nodes":
            self._update_node_detail(event.data_table, event.row_key)
        elif table_id == "table-topics":
            self._update_topic_detail(event.data_table, event.row_key)
        elif table_id == "table-services":
            self._update_service_detail(event.data_table, event.row_key)
        elif table_id == "table-actions":
            self._update_action_detail(event.data_table, event.row_key)

    def _update_node_detail(self, table: DataTable, row_key) -> None:
        try:
            row_vals = table.get_row(row_key)
            node_name = str(row_vals[0])
            namespace = str(row_vals[1])
            info = self.ros_manager.get_node_info(node_name, namespace)

            def fmt_list(items):
                return "\n".join(
                    [f"- `{x[0]}` — *{', '.join(x[1])}*" for x in items]
                ) or "_None_"

            md = f"""### Node: `{node_name}`
**Namespace**: `{namespace}`

#### 📢 Publishers
{fmt_list(info.get("publishers", []))}

#### 📥 Subscribers
{fmt_list(info.get("subscribers", []))}

#### ⚙️ Services
{fmt_list(info.get("services", []))}
"""
            self.query_one("#text-node-detail", Markdown).update(md)
        except Exception:
            pass

    def _update_topic_detail(self, table: DataTable, row_key) -> None:
        try:
            row_vals = table.get_row(row_key)
            # col 0 = indicator, col 1 = actual topic name, col 2 = type(s)
            topic_name = str(row_vals[1])
            info = self.ros_manager.get_topic_info(topic_name)
            types_str = ", ".join(info.get("types", []))
            is_monitored = topic_name in self._echo_cards
            echo_hint = "📡 **Currently monitored** — press `e` to remove card" if is_monitored else "Press `e` to add a live monitor card for this topic"
            md = f"""### Topic: `{topic_name}`
**Type(s)**: `{types_str}`

| | Count |
|---|---|
| Publishers | `{info.get('publisher_count', 0)}` |
| Subscribers | `{info.get('subscriber_count', 0)}` |

---
{echo_hint}
"""
            # Topics tab doesn't have its own detail markdown — update hint area
            # Hint is part of the monitor pane, so we update the title label
            try:
                label = self.query_one("#pane-topics-monitors > .pane-title", Label)
                label.update(f"Live Topic Monitors — selected: {topic_name}")
            except Exception:
                pass
        except Exception:
            pass

    def _update_service_detail(self, table: DataTable, row_key) -> None:
        try:
            row_vals = table.get_row(row_key)
            service_name = str(row_vals[0])
            info = self.ros_manager.get_service_info(service_name)
            types_str = ", ".join(info.get("types", []))
            sample = info.get("sample_request", "{}")

            md = f"""### Service: `{service_name}`
**Type**: `{types_str}`

Fill in the JSON payload below and press **`c`** or the button to call.
"""
            self.query_one("#text-service-detail", Markdown).update(md)
            # Pre-fill the TextArea with the interface-derived default payload
            self.query_one("#input-service-req", TextArea).load_text(sample)
        except Exception:
            pass

    def _update_action_detail(self, table: DataTable, row_key) -> None:
        try:
            row_vals = table.get_row(row_key)
            action_name = str(row_vals[0])
            info = self.ros_manager.get_action_info(action_name)
            types_str = ", ".join(info.get("types", []))
            sample_goal = info.get("sample_goal", "{}")

            md = f"""### Action: `{action_name}`
**Type**: `{types_str}`

Fill in the goal JSON payload below and press **`g`** or the button to send.
"""
            self.query_one("#text-action-detail", Markdown).update(md)
            self.query_one("#input-action-goal", TextArea).load_text(sample_goal)
        except Exception:
            pass

    # ------------------------------------------------------------------
    # TF Tree node highlight
    # ------------------------------------------------------------------

    def on_tree_node_highlighted(self, event: Tree.NodeHighlighted) -> None:
        # Only handle TF tree and interfaces tree
        tree_id = event.control.id  # event.control is the Tree widget that fired this event
        if tree_id == "tree-tf":
            self._update_tf_detail(event.node)
        elif tree_id == "tree-interfaces":
            self._update_interface_detail(event.node)

    def _update_tf_detail(self, node) -> None:
        tf_node = node.data
        if not tf_node:
            return
        tx, ty, tz = tf_node.translation
        rx, ry, rz, rw = tf_node.rotation
        md = f"""### TF Frame: `{tf_node.frame_id}`
**Parent**: `{tf_node.parent_id or "Root (no parent)"}`

#### 📍 Translation
| Axis | Value |
|------|-------|
| X | `{tx:.6f}` |
| Y | `{ty:.6f}` |
| Z | `{tz:.6f}` |

#### 🔄 Rotation (Quaternion)
| Component | Value |
|-----------|-------|
| X | `{rx:.6f}` |
| Y | `{ry:.6f}` |
| Z | `{rz:.6f}` |
| W | `{rw:.6f}` |

**Children**: {len(tf_node.children)}
"""
        self.query_one("#text-tf-detail", Markdown).update(md)

    def _update_interface_detail(self, node) -> None:
        label = str(node.label)
        # Only leaf nodes are interface type strings (contain '/')
        if "/" not in label:
            return
        try:
            from lazyrtui.ros.manager import _build_interface_default_json
            kind = "srv_request" if "/srv/" in label else "action_goal" if "/action/" in label else "msg"
            fields_json = _build_interface_default_json(label, kind=kind)
            md = f"""### Interface: `{label}`

**Kind**: `{"Service (Request)" if kind == "srv_request" else "Action (Goal)" if kind == "action_goal" else "Message"}`

#### Default Field Structure
```json
{fields_json}
```
"""
        except Exception:
            md = f"### Interface: `{label}`\n\n_Field introspection unavailable_"
        self.query_one("#text-interface-detail", Markdown).update(md)

    # ------------------------------------------------------------------
    # Smart table update (cursor-preserving)
    # ------------------------------------------------------------------

    def _update_table_smart(
        self,
        table_id: str,
        new_rows: list[tuple],
        columns: list[str],
        cache_attr: str,
    ) -> None:
        cached_data = getattr(self, cache_attr, None)
        if cached_data == new_rows:
            return  # No change — do not disturb the UI

        setattr(self, cache_attr, new_rows)
        table = self.query_one(table_id, DataTable)
        old_cursor = table.cursor_coordinate

        table.clear(columns=True)
        table.add_columns(*columns)
        for row in new_rows:
            table.add_row(*row)

        if table.row_count > 0:
            target_row = min(old_cursor.row, table.row_count - 1)
            target_col = min(old_cursor.column, max(0, len(columns) - 1))
            table.move_cursor(row=target_row, column=target_col)

    # ------------------------------------------------------------------
    # Data refresh
    # ------------------------------------------------------------------

    def refresh_all_data(self) -> None:
        # Nodes
        nodes = [(name, ns) for name, ns in self.ros_manager.get_nodes()]
        self._update_table_smart(
            "#table-nodes", nodes, ["Node Name", "Namespace"], "_cached_nodes"
        )

        # Topics — indicator column | name column | type column
        topics_raw = self.ros_manager.get_topics()
        topics = []
        for name, types in topics_raw:
            indicator = "📡" if name in self._echo_cards else " "
            topics.append((indicator, name, ", ".join(types)))
        self._update_table_smart(
            "#table-topics", topics, [" ", "Topic Name", "Type(s)"], "_cached_topics"
        )

        # Services
        services = [(name, ", ".join(types)) for name, types in self.ros_manager.get_services()]
        self._update_table_smart(
            "#table-services", services, ["Service Name", "Type(s)"], "_cached_services"
        )

        # Actions
        actions = [(name, ", ".join(types)) for name, types in self.ros_manager.get_actions()]
        self._update_table_smart(
            "#table-actions", actions, ["Action Name", "Type(s)"], "_cached_actions"
        )

        # TF Tree (only rebuild when frame set changes)
        tf_structure_key = tuple(sorted(self.ros_manager.tf_frames.keys()))
        if getattr(self, "_cached_tf_structure", None) != tf_structure_key:
            self._cached_tf_structure = tf_structure_key
            tf_tree_widget = self.query_one("#tree-tf", Tree)
            tf_tree_widget.clear()
            for root_node in self.ros_manager.get_tf_root_nodes():
                self._build_tf_tree_branch(tf_tree_widget.root, root_node)
            tf_tree_widget.root.expand()

        # Interfaces Tree (populated once)
        if not getattr(self, "_interfaces_populated", False):
            self._interfaces_populated = True
            if_tree = self.query_one("#tree-interfaces", Tree)
            if_tree.clear()
            for pkg, types in self.ros_manager.get_interfaces_tree().items():
                pkg_node = if_tree.root.add(pkg)
                for t in types:
                    pkg_node.add_leaf(t)
            if_tree.root.expand()

    def _build_tf_tree_branch(self, parent_widget_node, tf_node) -> None:
        tx, ty, tz = tf_node.translation
        label = f"{tf_node.frame_id}  [{tx:.2f}, {ty:.2f}, {tz:.2f}]"
        widget_node = parent_widget_node.add(label, data=tf_node)
        widget_node.expand()
        for child in tf_node.children.values():
            self._build_tf_tree_branch(widget_node, child)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    app = LazyRTUIApp()
    app.run()


if __name__ == "__main__":
    main()
