from homeassistant.components.select import SelectEntity
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity
from . import DOMAIN

async def async_setup_entry(hass, entry, async_add_entities):
    bridge = hass.data[DOMAIN]
    async_add_entities([AnykaSelect(bridge, entry, "power_hz", "Fréquence éclairage", {"50 Hz": 50, "60 Hz": 60}), AnykaSelect(bridge, entry, "privacy_style", "Masques confidentialité", {"Opaque": 0, "Mosaïque": 1})])

class AnykaSelect(CoordinatorEntity, SelectEntity):
    _attr_has_entity_name = True

    def __init__(self, bridge, entry, key, name, choices):
        super().__init__(bridge.motion_coordinator)
        self.bridge, self.key, self.choices = bridge, key, choices
        self._attr_unique_id = entry.entry_id+"_"+key
        self._attr_name = name
        self._attr_options = list(choices)
        self._attr_device_info = DeviceInfo(identifiers={(DOMAIN, entry.entry_id)}, name="Camera Anyka")

    @property
    def current_option(self):
        value = (self.coordinator.data or {}).get(self.key)
        return next((name for name, code in self.choices.items() if code == value), None)

    @property
    def available(self):
        return super().available and self.current_option is not None

    async def async_select_option(self, option):
        await self.bridge.set_setting(self.key, self.choices[option])
