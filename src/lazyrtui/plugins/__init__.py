"""
Plugin architecture for LazyRTUI custom renderers and presets.
"""

class BaseTopicPlugin:
    """Base interface for custom topic data processing and visualization."""
    topic_type: str = ""

    def process_message(self, msg: Any) -> Any:
        return msg

class BaseServicePlugin:
    """Base interface for service parameter presets and auto-completion."""
    service_name: str = ""

    def get_preset_request(self) -> dict:
        return {}
