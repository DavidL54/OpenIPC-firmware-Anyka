from homeassistant.components.switch import SwitchEntity
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.update_coordinator import CoordinatorEntity
from . import DOMAIN, BOOL_SETTINGS

NAMES = {"flip_vertical": "Inversion verticale", "flip_horizontal": "Inversion horizontale", "osd_clock": "Date et heure", "motion_enabled": "Détection mouvement", "sound_enabled": "Détection sonore", "person_enabled": "Détection personne", "tracking_enabled": "Suivi automatique", "white_balance_auto": "Balance des blancs automatique", "privacy_mode": "Mode privé"}

async def async_setup_entry(hass, entry, async_add_entities):
    bridge = hass.data[DOMAIN]
    async_add_entities([AnykaSwitch(bridge, entry, key) for key in sorted(BOOL_SETTINGS)])

class AnykaSwitch(CoordinatorEntity, SwitchEntity):
    _attr_has_entity_name = True

    def __init__(self, bridge, entry, key):
        super().__init__(bridge.motion_coordinator)
        self.bridge, self.key = bridge, key
        self._attr_unique_id = entry.entry_id+"_"+key
        self._attr_name = NAMES[key]
        self._attr_device_info = DeviceInfo(identifiers={(DOMAIN, entry.entry_id)}, name="Camera Anyka")

    @property
    def available(self):
        return super().available and type((self.coordinator.data or {}).get(self.key)) in (bool, int)

    @property
    def is_on(self):
        return bool((self.coordinator.data or {}).get(self.key))

    async def async_turn_on(self, **kwargs):
        await self.bridge.set_setting(self.key, True)

    async def async_turn_off(self, **kwargs):
        await self.bridge.set_setting(self.key, False)
