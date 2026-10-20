from homeassistant.components.binary_sensor import BinarySensorDeviceClass, BinarySensorEntity
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity
from homeassistant.util.dt import utc_from_timestamp

from . import DOMAIN


async def async_setup_entry(hass, entry, async_add_entities):
    bridge = hass.data[DOMAIN]
    async_add_entities([AnykaMotionSensor(bridge.motion_coordinator, entry),
        AnykaDetectionSensor(bridge.motion_coordinator, entry, "sound", "Son", BinarySensorDeviceClass.SOUND),
        AnykaDetectionSensor(bridge.motion_coordinator, entry, "person", "Personne", BinarySensorDeviceClass.OCCUPANCY)])


class AnykaMotionSensor(CoordinatorEntity, BinarySensorEntity):
    _attr_name = "Mouvement"
    _attr_has_entity_name = True
    _attr_device_class = BinarySensorDeviceClass.MOTION
    _attr_suggested_object_id = "camera_anyka_mouvement"

    def __init__(self, coordinator, entry):
        super().__init__(coordinator)
        self._attr_unique_id = entry.entry_id + "_motion"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.entry_id)}, name="Camera Anyka",
            manufacturer="Anyka", model="GP5_A10S14A5",
        )

    @property
    def available(self):
        data = self.coordinator.data or {}
        return super().available and (
            data.get("motion_enabled") is False or data.get("motion_ready") is True
        )

    @property
    def is_on(self):
        return bool((self.coordinator.data or {}).get("motion_detected"))

    @property
    def extra_state_attributes(self):
        data = self.coordinator.data or {}
        last = data.get("motion_last_event", 0)
        return {
            "enabled": data.get("motion_enabled"),
            "sensitivity": data.get("motion_sensitivity"),
            "suspended": data.get("motion_suspended"),
            "hold_seconds": 5,
            "events": data.get("motion_events"),
            "last_detection": utc_from_timestamp(last).isoformat() if last else None,
            "zone": data.get("motion_zone"),
            "boxes": data.get("motion_boxes"),
        }


class AnykaDetectionSensor(CoordinatorEntity, BinarySensorEntity):
    _attr_has_entity_name = True

    def __init__(self, coordinator, entry, kind, name, device_class):
        super().__init__(coordinator)
        self.kind = kind
        self._attr_name = name
        self._attr_device_class = device_class
        self._attr_unique_id = entry.entry_id + "_" + kind
        self._attr_suggested_object_id = "camera_anyka_" + ("son" if kind == "sound" else "personne")
        self._attr_device_info = DeviceInfo(identifiers={(DOMAIN, entry.entry_id)}, name="Camera Anyka")

    @property
    def available(self):
        data = self.coordinator.data or {}
        return super().available and (data.get(self.kind+"_enabled") is False or data.get(self.kind+"_ready") is True)

    @property
    def is_on(self):
        return bool((self.coordinator.data or {}).get(self.kind+"_detected"))

    @property
    def extra_state_attributes(self):
        return {key[len(self.kind)+1:]: value for key, value in (self.coordinator.data or {}).items() if key.startswith(self.kind+"_") and not key.endswith("_detected")}
