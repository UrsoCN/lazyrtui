import os
import yaml
from pathlib import Path
from typing import Dict, Any

DEFAULT_CONFIG = {
    "keybindings": {
        "switch_focus": "w",
        "refresh": "r",
        "search": "/",
        "echo_topic": "e",
        "plot_topic": "p",
        "call_service": "c",
        "help": "?",
        "quit": "q",
    },
    "topics": {},
    "services": {},
}

class ConfigLoader:
    """Manages loading and parsing user configurations for LazyRTUI."""

    def __init__(self, config_path: str | None = None):
        self.config_path = config_path or self._get_default_config_path()
        self.config: Dict[str, Any] = DEFAULT_CONFIG.copy()
        self.load()

    def _get_default_config_path(self) -> Path:
        try:
            config_dir = Path.home() / ".config" / "lazyrtui"
            config_dir.mkdir(parents=True, exist_ok=True)
            return config_dir / "config.yaml"
        except (OSError, PermissionError):
            return Path.cwd() / "config.yaml"

    def load(self) -> Dict[str, Any]:
        path = Path(self.config_path)
        if path.exists():
            try:
                with open(path, "r", encoding="utf-8") as f:
                    user_cfg = yaml.safe_load(f) or {}
                    self._deep_update(self.config, user_cfg)
            except Exception as e:
                print(f"Warning: Failed to load config from {path}: {e}")
        return self.config

    def _deep_update(self, base: Dict[str, Any], update: Dict[str, Any]):
        for k, v in update.items():
            if isinstance(v, dict) and k in base and isinstance(base[k], dict):
                self._deep_update(base[k], v)
            else:
                base[k] = v
