import os
import sys
import time
import threading
import logging
import json
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
    from rosidl_runtime_py.utilities import get_message, get_service, get_action
    ROSIDL_AVAILABLE = True
except ImportError:
    ROSIDL_AVAILABLE = False
    def get_message(s): raise ImportError("rosidl not available")
    def get_service(s): raise ImportError("rosidl not available")
    def get_action(s): raise ImportError("rosidl not available")


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


# ---------------------------------------------------------------------------
# Interface field introspection helpers
# ---------------------------------------------------------------------------

def _default_value_for_type(type_str: str) -> Any:
    """Return a sensible default Python value for a rosidl field type string."""
    s = str(type_str).lower()
    if "bool" in s:
        return False
    if any(k in s for k in ("float", "double")):
        return 0.0
    if any(k in s for k in ("int", "uint", "byte")):
        return 0
    if "string" in s:
        return ""
    if "char" in s:
        return 0
    return None


def _ros_msg_to_dict(msg_class) -> dict:
    """Recursively introspect a rosidl message class and return a dict of default values."""
    result = {}
    try:
        slots = getattr(msg_class, "__slots__", [])
        annotations = getattr(msg_class, "__annotations__", {})
        for slot in slots:
            # Strip leading underscore used by some rclpy versions
            key = slot.lstrip("_")
            field_type = annotations.get(slot, annotations.get(key, None))
            if field_type is None:
                result[key] = None
                continue
            # Try to recursively expand nested message types
            try:
                if hasattr(field_type, "__slots__"):
                    result[key] = _ros_msg_to_dict(field_type)
                elif hasattr(field_type, "__origin__"):
                    # Generic alias (e.g., list[SomeMsg]) - use empty list
                    result[key] = []
                else:
                    result[key] = _default_value_for_type(field_type.__name__ if hasattr(field_type, "__name__") else str(field_type))
            except Exception:
                result[key] = None
    except Exception:
        pass
    return result


def _build_interface_default_json(type_str: str, kind: str = "msg") -> str:
    """
    Attempt to instantiate the interface class and introspect its fields.
    Returns a pretty-printed JSON string representing the default structure.
    kind: 'msg', 'srv_request', or 'action_goal'
    """
    if not ROSIDL_AVAILABLE:
        return "{}"
    try:
        if kind == "msg":
            cls = get_message(type_str)
            d = _ros_msg_to_dict(cls)
        elif kind == "srv_request":
            srv_cls = get_service(type_str)
            d = _ros_msg_to_dict(srv_cls.Request)
        elif kind == "action_goal":
            act_cls = get_action(type_str)
            d = _ros_msg_to_dict(act_cls.Goal)
        else:
            d = {}
        return json.dumps(d, indent=2, default=str)
    except Exception:
        return "{}"


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
                rclpy.init(args=["--ros-args", "--log-level", "WARN"])

            self.node = rclpy.create_node(self.node_name)
            self.is_connected = True

            # Subscribe to /tf and /tf_static
            self._setup_tf_subscribers()

            # Start background spinning thread
            self._stop_event.clear()
            self._executor_thread = threading.Thread(target=self._spin_loop, daemon=True)
            self._executor_thread.start()

            # Allow brief time for DDS graph discovery
            time.sleep(0.2)
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

    # ------------------------------------------------------------------
    # Topology discovery
    # ------------------------------------------------------------------

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
        """
        Returns list of (action_name, action_types).
        ROS 2 actions are implemented as three service groups; we discover them
        by filtering service names that end with '/_action/send_goal'.
        """
        if not self.is_connected or not self.node:
            return [
                ("/turtle1/rotate_absolute", ["turtlesim/action/RotateAbsolute"]),
            ]
        try:
            all_services = self.node.get_service_names_and_types()
            action_map: Dict[str, List[str]] = {}
            for svc_name, svc_types in all_services:
                if svc_name.endswith("/_action/send_goal"):
                    base = svc_name[: -len("/_action/send_goal")]
                    # Derive action type from service type (e.g. X_SendGoal_Service -> X)
                    for t in svc_types:
                        # Strip _SendGoal suffix from type if present
                        action_type = t.replace("_SendGoal_Request", "").replace("/_action/send_goal", "")
                        # Try to map e.g. turtlesim/action/RotateAbsolute_SendGoal -> turtlesim/action/RotateAbsolute
                        if action_type not in action_map:
                            action_map[base] = [action_type]
            return list(action_map.items()) if action_map else []
        except Exception as e:
            logging.error(f"Failed to get actions: {e}")
            return []

    # ------------------------------------------------------------------
    # Detailed info
    # ------------------------------------------------------------------

    def get_node_info(self, node_name: str, namespace: str = "/") -> Dict[str, Any]:
        """Returns details about a node (publishers, subscribers, services)."""
        if not self.is_connected or not self.node:
            if "turtle" in node_name:
                return {
                    "name": node_name,
                    "namespace": namespace,
                    "publishers": [("/turtle1/pose", ["turtlesim/msg/Pose"]), ("/rosout", ["rcl_interfaces/msg/Log"])],
                    "subscribers": [("/turtle1/cmd_vel", ["geometry_msgs/msg/Twist"])],
                    "services": [("/clear", ["std_srvs/srv/Empty"]), ("/spawn", ["turtlesim/srv/Spawn"]), ("/reset", ["std_srvs/srv/Empty"])],
                }
            return {
                "name": node_name,
                "namespace": namespace,
                "publishers": [("/rosout", ["rcl_interfaces/msg/Log"])],
                "subscribers": [],
                "services": [],
            }

        try:
            pubs = self.node.get_publisher_names_and_types_by_node(node_name, namespace)
            subs = self.node.get_subscriber_names_and_types_by_node(node_name, namespace)
            srvs = self.node.get_service_names_and_types_by_node(node_name, namespace)
            return {
                "name": node_name,
                "namespace": namespace,
                "publishers": pubs,
                "subscribers": subs,
                "services": srvs,
            }
        except Exception as e:
            logging.error(f"Failed to get info for node {node_name}: {e}")
            return {"name": node_name, "namespace": namespace, "publishers": [], "subscribers": [], "services": []}

    def get_topic_info(self, topic_name: str) -> Dict[str, Any]:
        """Returns details about a topic (publishers count, subscribers count, type)."""
        all_topics = dict(self.get_topics())
        topic_types = all_topics.get(topic_name, ["Unknown"])

        if not self.is_connected or not self.node:
            return {
                "topic": topic_name,
                "types": topic_types,
                "publisher_count": 1,
                "subscriber_count": 2,
            }

        try:
            pubs_info = self.node.get_publishers_info_by_topic(topic_name)
            subs_info = self.node.get_subscriptions_info_by_topic(topic_name)
            return {
                "topic": topic_name,
                "types": topic_types,
                "publisher_count": len(pubs_info),
                "subscriber_count": len(subs_info),
            }
        except Exception:
            return {
                "topic": topic_name,
                "types": topic_types,
                "publisher_count": 0,
                "subscriber_count": 0,
            }

    def get_service_info(self, service_name: str) -> Dict[str, Any]:
        """Returns details about a service including auto-generated default request JSON."""
        all_services = dict(self.get_services())
        service_types = all_services.get(service_name, ["Unknown"])
        srv_type = service_types[0] if service_types else ""
        return {
            "service": service_name,
            "types": service_types,
            "sample_request": _build_interface_default_json(srv_type, kind="srv_request"),
        }

    def get_action_info(self, action_name: str) -> Dict[str, Any]:
        """Returns details about an action including auto-generated default goal JSON."""
        all_actions = dict(self.get_actions())
        action_types = all_actions.get(action_name, ["Unknown"])
        act_type = action_types[0] if action_types else ""
        return {
            "action": action_name,
            "types": action_types,
            "sample_goal": _build_interface_default_json(act_type, kind="action_goal"),
        }

    def get_tf_root_nodes(self) -> List[TFTreeNode]:
        """Finds all root nodes (frames without a parent or whose parent is not tracked)."""
        roots = []
        for frame_id, node in self.tf_frames.items():
            if not node.parent_id or node.parent_id not in self.tf_frames:
                roots.append(node)
        return roots or list(self.tf_frames.values())[:1]

    # ------------------------------------------------------------------
    # Topic subscriptions
    # ------------------------------------------------------------------

    def subscribe_topic(self, topic_name: str, topic_type_str: str, callback: Callable[[Any], None]) -> bool:
        """
        Dynamically creates a subscription on the persistent node.
        In offline/demo mode, registers the callback without creating a real subscription.
        """
        if not self.is_connected or not self.node or not ROSIDL_AVAILABLE:
            self.topic_callbacks[topic_name] = callback
            return True  # Registered (no real ROS traffic in demo mode)
        try:
            # Remove existing subscription if any
            if topic_name in self.active_subscriptions:
                self.unsubscribe_topic(topic_name)

            msg_class = get_message(topic_type_str)

            def _wrapped_cb(msg):
                try:
                    callback(msg)
                except Exception as e:
                    logging.warning(f"Topic callback error for {topic_name}: {e}")

            sub = self.node.create_subscription(msg_class, topic_name, _wrapped_cb, 10)
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

    # ------------------------------------------------------------------
    # Service calls
    # ------------------------------------------------------------------

    def call_service_async(
        self,
        service_name: str,
        service_type_str: str,
        request_data: dict,
        callback: Callable[[bool, Any, str], None],
    ):
        """
        Calls a ROS 2 service asynchronously using the single persistent LazyRTUINode.
        Avoids creating ephemeral nodes or writing log files in ~/.ros/log.
        callback(success: bool, result: Any, duration_str: str)
        """
        if not self.is_connected or not self.node or not ROSIDL_AVAILABLE:
            time.sleep(0.05)
            callback(True, {"success": True, "message": f"[Demo] Mock call to '{service_name}'"}, "0.042s")
            return

        def _run():
            try:
                srv_class = get_service(service_type_str)
                client = self.node.create_client(srv_class, service_name)

                if not client.wait_for_service(timeout_sec=3.0):
                    self.node.destroy_client(client)
                    callback(False, None, "timeout: service not available")
                    return

                req = srv_class.Request()
                for k, v in request_data.items():
                    if hasattr(req, k):
                        try:
                            setattr(req, k, type(getattr(req, k))(v))
                        except Exception:
                            setattr(req, k, v)

                start_time = time.time()
                future = client.call_async(req)

                # Spin until done (blocking in this worker thread)
                while not future.done():
                    time.sleep(0.01)

                duration = f"{time.time() - start_time:.3f}s"
                try:
                    res = future.result()
                    # Convert result to serialisable dict
                    res_dict = {}
                    for slot in getattr(res, "__slots__", []):
                        key = slot.lstrip("_")
                        res_dict[key] = getattr(res, slot, getattr(res, key, None))
                    callback(True, res_dict, duration)
                except Exception as ex:
                    callback(False, str(ex), duration)
                finally:
                    try:
                        self.node.destroy_client(client)
                    except Exception:
                        pass
            except Exception as e:
                callback(False, str(e), "N/A")

        threading.Thread(target=_run, daemon=True).start()

    # ------------------------------------------------------------------
    # Interfaces explorer
    # ------------------------------------------------------------------

    def get_interfaces_tree(self) -> Dict[str, List[str]]:
        """
        Returns categorized dictionary of known ROS 2 msg/srv/action interfaces.
        When rclpy is available, this reflects actually installed packages.
        Otherwise returns a curated static list.
        """
        static = {
            "geometry_msgs": [
                "geometry_msgs/msg/Twist",
                "geometry_msgs/msg/TwistStamped",
                "geometry_msgs/msg/Pose",
                "geometry_msgs/msg/PoseStamped",
                "geometry_msgs/msg/Point",
                "geometry_msgs/msg/Quaternion",
                "geometry_msgs/msg/TransformStamped",
                "geometry_msgs/msg/Vector3",
            ],
            "sensor_msgs": [
                "sensor_msgs/msg/Image",
                "sensor_msgs/msg/CompressedImage",
                "sensor_msgs/msg/LaserScan",
                "sensor_msgs/msg/PointCloud2",
                "sensor_msgs/msg/Imu",
                "sensor_msgs/msg/NavSatFix",
                "sensor_msgs/msg/JointState",
                "sensor_msgs/msg/BatteryState",
            ],
            "nav_msgs": [
                "nav_msgs/msg/Odometry",
                "nav_msgs/msg/Path",
                "nav_msgs/msg/OccupancyGrid",
                "nav_msgs/srv/GetMap",
            ],
            "std_msgs": [
                "std_msgs/msg/String",
                "std_msgs/msg/Int32",
                "std_msgs/msg/Float64",
                "std_msgs/msg/Bool",
                "std_msgs/msg/Header",
            ],
            "std_srvs": [
                "std_srvs/srv/Empty",
                "std_srvs/srv/Trigger",
                "std_srvs/srv/SetBool",
            ],
            "rcl_interfaces": [
                "rcl_interfaces/msg/Log",
                "rcl_interfaces/msg/Parameter",
                "rcl_interfaces/msg/ParameterValue",
                "rcl_interfaces/srv/GetParameters",
                "rcl_interfaces/srv/SetParameters",
                "rcl_interfaces/srv/ListParameters",
            ],
            "tf2_msgs": [
                "tf2_msgs/msg/TFMessage",
                "tf2_msgs/srv/FrameGraph",
            ],
            "turtlesim": [
                "turtlesim/msg/Pose",
                "turtlesim/msg/Color",
                "turtlesim/srv/Spawn",
                "turtlesim/srv/Kill",
                "turtlesim/srv/SetPen",
                "turtlesim/srv/TeleportAbsolute",
                "turtlesim/srv/TeleportRelative",
                "turtlesim/action/RotateAbsolute",
            ],
            "action_msgs": [
                "action_msgs/msg/GoalInfo",
                "action_msgs/msg/GoalStatus",
                "action_msgs/msg/GoalStatusArray",
                "action_msgs/srv/CancelGoal",
            ],
        }

        if not ROSIDL_AVAILABLE:
            return static

        # Try to augment with actually installed packages via ros2 interface list
        try:
            import subprocess
            result = subprocess.run(
                ["ros2", "interface", "list"],
                capture_output=True, text=True, timeout=5
            )
            if result.returncode == 0:
                installed: Dict[str, List[str]] = {}
                for line in result.stdout.splitlines():
                    line = line.strip()
                    if not line or "/" not in line:
                        continue
                    parts = line.split("/")
                    pkg = parts[0]
                    if pkg not in installed:
                        installed[pkg] = []
                    installed[pkg].append(line)
                if installed:
                    return installed
        except Exception:
            pass

        return static
