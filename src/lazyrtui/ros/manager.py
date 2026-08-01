import sys
import threading
import logging
from typing import List, Tuple, Dict, Any, Optional

try:
    import rclpy
    from rclpy.node import Node
    RCLPY_AVAILABLE = True
except ImportError:
    RCLPY_AVAILABLE = False
    Node = object

class ROS2Manager:
    """
    Unified manager for ROS 2 operations.
    Maintains a single long-lived ROS Node to avoid generating ephemeral node logs.
    """

    def __init__(self, node_name: str = "lazy_rtui_node"):
        self.node_name = node_name
        self.is_connected = False
        self.node: Optional[Node] = None
        self._executor_thread: Optional[threading.Thread] = None
        self._stop_event = threading.Event()

    def start(self) -> bool:
        if not RCLPY_AVAILABLE:
            logging.warning("rclpy is not available in the current Python environment.")
            return False

        try:
            if not rclpy.ok():
                # Initialize rclpy suppressing file logs where possible
                rclpy.init(args=["--ros-args", "--log-level", "WARN"])

            self.node = rclpy.create_node(self.node_name)
            self.is_connected = True
            
            # Start background spinning thread for single node
            self._stop_event.clear()
            self._executor_thread = threading.Thread(target=self._spin_loop, daemon=True)
            self._executor_thread.start()
            return True
        except Exception as e:
            logging.error(f"Failed to initialize ROS 2 node: {e}")
            self.is_connected = False
            return False

    def _spin_loop(self):
        if not self.node:
            return
        while rclpy.ok() and not self._stop_event.is_set():
            try:
                rclpy.spin_once(self.node, timeout_sec=0.1)
            except Exception:
                break

    def stop(self):
        self._stop_event.set()
        if self._executor_thread and self._executor_thread.is_alive():
            self._executor_thread.join(timeout=1.0)

        if RCLPY_AVAILABLE and self.node:
            try:
                self.node.destroy_node()
            except Exception:
                pass
            self.node = None
            self.is_connected = False

    def get_nodes(self) -> List[Tuple[str, str]]:
        """Returns list of (node_name, namespace)."""
        if not self.is_connected or not self.node:
            return [("mock_turtle_node", "/"), ("mock_teleop_node", "/")]
        try:
            return self.node.get_node_names_and_namespaces()
        except Exception as e:
            logging.error(f"Failed to get nodes: {e}")
            return []

    def get_topics(self) -> List[Tuple[str, List[str]]]:
        """Returns list of (topic_name, topic_types)."""
        if not self.is_connected or not self.node:
            return [
                ("/turtle1/cmd_vel", ["geometry_msgs/msg/Twist"]),
                ("/turtle1/pose", ["turtlesim/msg/Pose"]),
                ("/rosout", ["rcl_interfaces/msg/Log"]),
            ]
        try:
            return self.node.get_topic_names_and_types()
        except Exception as e:
            logging.error(f"Failed to get topics: {e}")
            return []

    def get_services(self) -> List[Tuple[str, List[str]]]:
        """Returns list of (service_name, service_types)."""
        if not self.is_connected or not self.node:
            return [
                ("/spawn", ["turtlesim/srv/Spawn"]),
                ("/clear", ["std_srvs/srv/Empty"]),
                ("/reset", ["std_srvs/srv/Empty"]),
            ]
        try:
            return self.node.get_service_names_and_types()
        except Exception as e:
            logging.error(f"Failed to get services: {e}")
            return []

    def get_actions(self) -> List[Tuple[str, List[str]]]:
        """Returns list of (action_name, action_types)."""
        if not self.is_connected or not self.node:
            return [
                ("/turtle1/rotate_absolute", ["turtlesim/action/RotateAbsolute"])
            ]
        try:
            # Action discovery requires action client / node action queries
            return []
        except Exception as e:
            logging.error(f"Failed to get actions: {e}")
            return []
