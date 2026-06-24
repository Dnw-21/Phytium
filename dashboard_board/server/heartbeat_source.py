import os
import time
import logging
from abc import ABC, abstractmethod

logger = logging.getLogger(__name__)


class HeartbeatSource(ABC):
    @abstractmethod
    def is_alive(self, node_id: str) -> bool:
        ...

    @abstractmethod
    def last_seen_ms(self, node_id: str) -> int:
        ...


class MockHeartbeatSource(HeartbeatSource):
    def __init__(self, config_dict: dict):
        self._sim = config_dict["heartbeat_simulated"]

    def is_alive(self, node_id: str) -> bool:
        return bool(self._sim.get(node_id, False))

    def last_seen_ms(self, node_id: str) -> int:
        if self.is_alive(node_id):
            return int(time.time() * 1000)
        return 0


class LoRaHeartbeatSource(HeartbeatSource):
    def __init__(self, shm_base_path: str = "/dev/shm/lora_nodes"):
        self._base = shm_base_path

    def _heartbeat_path(self, node_id: str) -> str:
        return os.path.join(self._base, node_id, "heartbeat")

    def is_alive(self, node_id: str) -> bool:
        path = self._heartbeat_path(node_id)
        if not os.path.exists(path):
            logger.info("LoRa heartbeat not available for %s", node_id)
            return False
        return True

    def last_seen_ms(self, node_id: str) -> int:
        path = self._heartbeat_path(node_id)
        if not os.path.exists(path):
            logger.info("LoRa heartbeat not available for %s", node_id)
            return 0
        return int(os.path.getmtime(path) * 1000)


def make_heartbeat_source(config: dict) -> HeartbeatSource:
    source_type = config["heartbeat_source"]
    if source_type == "mock":
        return MockHeartbeatSource(config)
    if source_type == "lora":
        return LoRaHeartbeatSource()
    raise ValueError(f"Unknown heartbeat_source: {source_type}")