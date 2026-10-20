from homeassistant.components.number import NumberEntity, NumberMode
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity
from . import DOMAIN, NUMBER_SETTINGS

NAMES = {"brightness": "Luminosité", "contrast": "Contraste", "saturation": "Saturation", "sharpness": "Netteté", "hue": "Teinte", "wdr": "WDR", "exposure": "Compensation exposition", "anti_flicker": "Antiscintillement", "motion_sensitivity": "Sensibilité mouvement", "sound_sensitivity": "Sensibilité son", "white_balance_red": "Balance rouge", "white_balance_green": "Balance verte", "white_balance_blue": "Balance bleue"}

async def async_setup_entry(hass, entry, async_add_entities):
    bridge = hass.data[DOMAIN]
    async_add_entities([AnykaNumber(bridge, entry, key, limits) for key, limits in NUMBER_SETTINGS.items()])

class AnykaNumber(CoordinatorEntity, NumberEntity):
    _attr_has_entity_name = True
    _attr_native_step = 1
    _attr_mode = NumberMode.SLIDER

    def __init__(self, bridge, entry, key, limits):
        super().__init__(bridge.motion_coordinator)
        self.bridge, self.key = bridge, key
        self._attr_unique_id = entry.entry_id+"_"+key
        self._attr_name = NAMES[key]
        self._attr_native_min_value, self._attr_native_max_value = limits
        self._attr_device_info = DeviceInfo(identifiers={(DOMAIN, entry.entry_id)}, name="Camera Anyka")

    @property
    def native_value(self):
        return (self.coordinator.data or {}).get(self.key)

    @property
    def available(self):
        return super().available and self.native_value is not None

    async def async_set_native_value(self, value):
        await self.bridge.set_setting(self.key, int(value))
