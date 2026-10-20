import ipaddress

import voluptuous as vol

from homeassistant.config_entries import ConfigFlow


class AnykaControlsFlow(ConfigFlow, domain="anyka_controls"):
    VERSION = 1

    async def async_step_user(self, user_input=None):
        await self.async_set_unique_id("anyka_controls")
        self._abort_if_unique_id_configured()
        errors = {}
        if user_input is not None:
            try:
                ipaddress.ip_address(user_input["host"])
            except ValueError:
                errors["host"] = "invalid_host"
            else:
                return self.async_create_entry(title="Camera Anyka", data=user_input)
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema({vol.Required("host"): str, vol.Required("password"): str}),
            errors=errors,
        )