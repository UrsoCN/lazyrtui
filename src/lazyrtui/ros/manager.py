import sys
import time
import threading
import logging
from typing import List, Tuple, Dict, Any, Optional, Callable

try:
    import rclpy
    from rclpy.node import Node
    from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
    RCLPY_AVAILABLE = True
except ImportError:
    RCLPY_AVAILABLE = False
    Node = object

try:
    from rosidl_runtime_py.utilities import get_message, get_service
    ROSIDL_AVAILABLE = True
except ImportError:
    ROSIDL_AVAILABLE = False


class TFTreeNode:
    """Represents a frame node in the TF Tree."""
    def __init__(self, frame_id: str, parent_id: str = ""):
        self.frame_id = frame_id
        self.parent_id = parent_id
        self.children: Dict[str, "TFTreeNode"] = {}
        self.translation = (0.0, 0.0, 0.0)
        self.rotation = (0.0, 0.0, 0.0, 1.0)
        self.last_update = time.time()

    def to_dict(self) -> Dict[str, Any]:
        return {
            "frame_id": self.frame_id,
            "parent_id": self.parent_id,
            "translation": self.translation,
            "rotation": self.rotation,
            "last_update": self.last_update,
            "children": {k: v.to_dict() for k, v in self.children.items()}
        }


class ROS2Manager:
    """
    Unified manager for ROS 2 operations.
    Maintains a single long-lived ROS Node to avoid generating ephemeral node logs (~/.ros/log spam).
    """

    def __init__(self, node_name: str = "lazy_rtui_node"):
        self.node_name = node_name
        self.is_connected = False
        self.node: Optional[Node] = None
        self._executor_thread: Optional[threading.Thread] = None
        self._stop_event = threading.Event()
        
        # Subscriptions & Clients cache
        self.active_subscriptions: Dict[str, Any] = {}
        self.topic_callbacks: Dict[str, Callable[[Any], None]] = {}
        
        # TF Tree storage: frame_id -> TFTreeNode
        self.tf_frames: Dict[str, TFTreeNode] = {}
        self._init_mock_tf_tree()

    def _init_mock_tf_tree(self):
        """Mock TF tree hierarchy for offline / demo mode."""
        world = TFTreeNode("world", "")
        map_node = TFTreeNode("map", "world")
        map_node.translation = (0.0, 0.0, 0.0)
        
        odom = TFTreeNode("odom", "map")
        odom.translation = (1.5, 0.8, 0.0)
        
        base_link = TFTreeNode("base_link", "odom")
        base_link.translation = (2.3, 1.2, 0.0)
        
        laser = TFTreeNode("laser_frame", "base_link")
        laser.translation = (0.2, 0.0, 0.15)
        
        camera = TFTreeNode("camera_link", "base_link")
        camera.translation = (0.1, 0.0, 0.5)

        base_link.children["laser_frame"] = laser
        base_link.children["camera_link"] = camera
        odom.children["base_link"] = base_link
        map_node.children["odom"] = odom
        world.children["map"] = map_node

        self.tf_frames = {
            "world": world,
            "map": map_node,
            "odom": odom,
            "base_link": base_link,
            "laser_frame": laser,
            "camera_link": camera,
        }

    def start(self) -> bool:
        if not RCLPY_AVAILABLE:
            logging.warning("rclpy is not available in the current Python environment.")
            return False

        try:
            if not rclpy.ok():
                # Initialize rclpy suppressing file log spam
                rclpy.init(args=["--ros-args", "--log-level", "WARN"])

            self.node = rclpy.create_node(self.node_name)
            self.is_connected = True
            
            # Subscribe to /tf and /tf_static if available
            self._setup_tf_subscribers()

            # Start background spinning thread for single node
            self._stop_event.clear()
            self._executor_thread = threading.Thread(target=self._spin_loop, daemon=True)
            self._executor_thread.start()
            return True
        except Exception as e:
            logging.error(f"Failed to initialize ROS 2 node: {e}")
            self.is_connected = False
            return False

    def _setup_tf_subscribers(self):
        if not self.node or not ROSIDL_AVAILABLE:
            return
        try:
            tf_msg_type = get_message("tf2_msgs/msg/TFMessage")
            qos = QoSProfile(depth=100, reliability=ReliabilityPolicy.BEST_EFFORT)
            self.node.create_subscription(tf_msg_type, "/tf", self._on_tf_message, qos)
            
            qos_static = QoSProfile(depth=100, durability=DurabilityPolicy.TRANSIENT_LOCAL)
            self.node.create_subscription(tf_msg_type, "/tf_static", self._on_tf_message, qos_static)
        except Exception as e:
            logging.warning(f"Could not setup TF subscribers: {e}")

    def _on_tf_message(self, msg: Any):
        if not hasattr(msg, "transforms"):
            return
        for transform in msg.transforms:
            parent = transform.header.frame_id.lstrip("/")
            child = transform.child_frame_id.lstrip("/")
            t = transform.transform.translation
            r = transform.transform.rotation

            if child not in self.tf_frames:
                self.tf_frames[child] = TFTreeNode(child, parent)
            
            node = self.tf_frames[child]
            node.parent_id = parent
            node.translation = (t.x, t.y, t.z)
            node.rotation = (r.x, r.y, r.z, r.w)
            node.last_update = time.time()

            # Link parent -> child
            if parent in self.tf_frames:
                self.tf_frames[parent].children[child] = node

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
            return [
                ("turtlesim", "/"),
                ("teleop_turtle", "/"),
                ("robot_state_publisher", "/"),
            ]
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
                ("/tf", ["tf2_msgs/msg/TFMessage"]),
                ("/tf_static", ["tf2_msgs/msg/TFMessage"]),
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
                ("/turtle1/set_pen", ["turtlesim/srv/SetPen"]),
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
            # Action discovery via node API if available
            return []
        except Exception as e:
            logging.error(f"Failed to get actions: {e}")
            return []

    def get_tf_root_nodes(self) -> List[TFTreeNode]:
        """Finds all root nodes (frames without a parent or whose parent is not tracked)."""
        roots = []
        for frame_id, node in self.tf_frames.items():
            if not node.parent_id or node.parent_id not in self.tf_frames:
                roots.append(node)
        return roots or list(self.tf_frames.values())[:1]

    def subscribe_topic(self, topic_name: str, topic_type_str: str, callback: Callable[[Any], None]) -> bool:
        """Dynamically creates a subscription on the persistent node."""
        if not self.is_connected or not self.node or not ROSIDL_AVAILABLE:
            self.topic_callbacks[topic_name] = callback
            return True
        try:
            msg_class = get_message(topic_type_str)
            sub = self.node.create_subscription(
                msg_class,
                topic_name,
                lambda msg: callback(msg),
                10
            )
            self.active_subscriptions[topic_name] = sub
            self.topic_callbacks[topic_name] = callback
            return True
        except Exception as e:
            logging.error(f"Failed to subscribe to {topic_name}: {e}")
            return False

    def unsubscribe_topic(self, topic_name: str):
        """Destroys subscription dynamically without leaking resources."""
        if topic_name in self.active_subscriptions and self.node:
            try:
                self.node.destroy_subscription(self.active_subscriptions[topic_name])
            except Exception:
                pass
            del self.active_subscriptions[topic_name]
        if topic_name in self.topic_callbacks:
            del self.topic_callbacks[topic_name]
