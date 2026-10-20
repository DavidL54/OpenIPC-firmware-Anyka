import asyncio
import json
import logging
from datetime import timedelta
import math
import tempfile
import time

import aiohttp
from aiohttp import web
import voluptuous as vol

from homeassistant.components.http import HomeAssistantView
from homeassistant.components.http.auth import async_sign_path
from homeassistant.components.http.const import KEY_HASS, KEY_HASS_USER
from homeassistant.components import websocket_api
from homeassistant.const import Platform
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed


DOMAIN = "anyka_controls"
PTZ = {"left", "right", "up", "down", "stop"}
GPIO = {"ir-on", "ir-off", "blue-on", "blue-off", "mode-auto", "mode-day", "mode-night", "mode-manual"}
LOGGER = logging.getLogger(__name__)
PLATFORMS = [Platform.BINARY_SENSOR, Platform.NUMBER, Platform.SWITCH, Platform.SELECT]
BOOL_SETTINGS = {"flip_vertical", "flip_horizontal", "osd_clock", "motion_enabled", "sound_enabled", "person_enabled", "tracking_enabled", "white_balance_auto", "privacy_mode"}
NUMBER_SETTINGS = {
    **{name: (-50, 50) for name in ("brightness", "contrast", "saturation", "sharpness", "hue", "wdr", "exposure")},
    "motion_sensitivity": (1, 100), "sound_sensitivity": (1, 100), "anti_flicker": (0, 100),
    **{name: (64, 1024) for name in ("white_balance_red", "white_balance_green", "white_balance_blue")},
}
RECT_SETTINGS = {"motion_zone", "privacy_mask1", "privacy_mask2", "privacy_mask3", "privacy_mask4"}
MP3_UPLOAD_LIMIT = 64 * 1024 * 1024
MP3_PLAY_LIMIT = 290
PCM_FRAME_BYTES = 640


def setting_params(action, value):
    if action in BOOL_SETTINGS and type(value) is bool:
        return {action: int(value)}
    if action in NUMBER_SETTINGS and type(value) is int:
        low, high = NUMBER_SETTINGS[action]
        if low <= value <= high:
            return {action: value}
    if action == "power_hz" and type(value) is int and value in (50, 60):
        return {action: value}
    if action == "privacy_style" and type(value) is int and value in (0, 1):
        return {action: value}
    if action in RECT_SETTINGS and type(value) is list and len(value) == 4 and all(type(v) is int and 0 <= v <= 100 for v in value):
        x, y, width, height = value
        if (width > 0 and height > 0 and x+width <= 100 and y+height <= 100) or (action != "motion_zone" and value == [0, 0, 0, 0]):
            return {action: ",".join(map(str, value))}
    raise ValueError("Invalid camera setting")


class CameraBridge:
    def __init__(self, host, password):
        self.base = "http://" + host
        self.password = password
        self.session = aiohttp.ClientSession(
            cookie_jar=aiohttp.CookieJar(unsafe=True),
            timeout=aiohttp.ClientTimeout(total=15),
        )
        self.lock = asyncio.Lock()
        self.image_lock = asyncio.Lock()
        self.logged_at = 0
        self.audio_active = False
        self.motion_coordinator = None

    async def motion_status(self):
        try:
            status, data = await self.request("/cgi-bin/isp.cgi", {})
            if status != 200 or type(data.get("motion_detected")) is not bool:
                raise UpdateFailed("Camera motion state unavailable")
            return data
        except (aiohttp.ClientError, TimeoutError, ValueError, RuntimeError) as error:
            raise UpdateFailed("Camera motion state unavailable") from error

    async def login(self):
        async with self.lock:
            if time.monotonic() - self.logged_at < 3600:
                return
            async with self.session.post(
                self.base + "/cgi-bin/access.cgi",
                data={"action": "login", "ui_username": "admin", "password_default": self.password},
                headers={"Origin": self.base}, allow_redirects=False,
            ) as response:
                if response.status != 303 or "anyka_session" not in response.cookies:
                    raise RuntimeError("Camera authentication unavailable")
            self.logged_at = time.monotonic()

    async def request(self, path, params, method="GET", json_payload=None, *, form=None, timeout=None):
        await self.login()
        for attempt in range(2):
            async with self.session.request(
                method, self.base + path, params=params, json=json_payload, data=form,
                timeout=aiohttp.ClientTimeout(total=timeout or 15),
                headers={"Origin": self.base}, allow_redirects=False,
            ) as response:
                if response.status in (401, 303) and attempt == 0:
                    self.logged_at = 0
                else:
                    return response.status, await response.json()
            await self.login()
        raise RuntimeError("Camera session unavailable")

    async def set_setting(self, action, value):
        if action == "privacy_mode":
            if type(value) is not bool:
                raise ValueError("Invalid privacy mode")
            status, data = await self.request("/api/privacy", {}, "POST", {"enabled": value})
        else:
            status, data = await self.request_image(setting_params(action, value))
        if status != 200 or data.get("ok") is False:
            raise RuntimeError("Camera rejected setting")
        await self.motion_coordinator.async_request_refresh()
        return data

    async def request_image(self, params):
        # The camera accepts only one image operation at a time. Both the
        # Lovelace card and number entities use this lock, so quick changes
        # cannot race at its non-blocking CGI lock.
        async with self.image_lock:
            for attempt in range(4):
                status, data = await self.request("/cgi-bin/isp.cgi", params, "POST")
                if status != 409 or data.get("error") != "Image operation busy" or attempt == 3:
                    return status, data
                await asyncio.sleep(1)


class CameraView(HomeAssistantView):
    requires_auth = True

    def bridge(self, request):
        if not request[KEY_HASS_USER].is_admin:
            raise web.HTTPForbidden()
        bridge = request.app[KEY_HASS].data.get(DOMAIN)
        if bridge is None:
            raise web.HTTPServiceUnavailable()
        return bridge


class StatusView(CameraView):
    url = "/api/anyka_controls/status"
    name = "api:anyka_controls:status"

    async def get(self, request):
        bridge = self.bridge(request)
        result = {}
        for key, path, params in (
            ("gpio", "/cgi-bin/gpio.cgi", {"action": "status"}),
            ("daynight", "/cgi-bin/gpio.cgi", {"action": "daynight-status"}),
            ("image", "/cgi-bin/isp.cgi", {}),
            ("presets", "/api/ptz/presets", {}),
        ):
            try:
                status, data = await bridge.request(path, params)
                result[key] = data if status == 200 else None
            except (aiohttp.ClientError, TimeoutError, ValueError, RuntimeError):
                result[key] = None
        return self.json(result)


class CommandView(CameraView):
    url = "/api/anyka_controls/command"
    name = "api:anyka_controls:command"

    async def post(self, request):
        bridge = self.bridge(request)
        if request.content_length is None or request.content_length > 1024:
            raise web.HTTPBadRequest()
        payload = await request.json()
        if not isinstance(payload, dict):
            raise web.HTTPBadRequest()
        action = payload.get("action")
        if type(action) is not str:
            raise web.HTTPBadRequest()
        if action == "privacy_mode":
            if type(payload.get("value")) is not bool:
                raise web.HTTPBadRequest()
            try:
                data = await bridge.set_setting(action, payload["value"])
                return self.json(data)
            except (aiohttp.ClientError, TimeoutError, ValueError, RuntimeError):
                return self.json({"ok": False, "error": "Camera unavailable"}, status_code=503)
        if action in PTZ:
            path, params = "/cgi-bin/ptz.cgi", {"action": action}
        elif action in GPIO:
            path, params = "/cgi-bin/gpio.cgi", {"action": action}
        else:
            try:
                path, params = "/cgi-bin/isp.cgi", setting_params(action, payload.get("value"))
            except (ValueError, TypeError):
                raise web.HTTPBadRequest() from None
        try:
            if path == "/cgi-bin/isp.cgi":
                status, data = await bridge.request_image(params)
            else:
                status, data = await bridge.request(path, params, "POST")
            return self.json(data, status_code=status)
        except (aiohttp.ClientError, TimeoutError, ValueError, RuntimeError):
            return self.json({"ok": False, "error": "Camera unavailable"}, status_code=503)


class PresetsView(CameraView):
    url = "/api/anyka_controls/presets"
    name = "api:anyka_controls:presets"

    async def get(self, request):
        bridge = self.bridge(request)
        try:
            status, data = await bridge.request("/api/ptz/presets", {})
            return self.json(data, status_code=status)
        except (aiohttp.ClientError, TimeoutError, ValueError, RuntimeError):
            return self.json({"ok": False, "error": "Positions indisponibles"}, status_code=503)

    async def post(self, request):
        bridge = self.bridge(request)
        if request.content_length is None or request.content_length > 1024:
            raise web.HTTPBadRequest()
        payload = await request.json()
        if not isinstance(payload, dict):
            raise web.HTTPBadRequest()
        action, slot = payload.get("action"), payload.get("id")
        if type(action) is not str or action not in {"save", "recall", "delete", "startup"}:
            raise web.HTTPBadRequest()
        if type(slot) is not int or not ((0 if action == "startup" else 1) <= slot <= 8):
            raise web.HTTPBadRequest()
        form = {"action": action, "id": str(slot)}
        if action == "save":
            name = payload.get("name")
            if type(name) is not str or not name.strip() or any(ord(c) < 32 or ord(c) == 127 or 0xD800 <= ord(c) <= 0xDFFF for c in name) or len(name.encode("utf-8")) > 48:
                raise web.HTTPBadRequest()
            form["name"] = name.strip()
        try:
            # A recall may take longer than a normal CGI request. Do not hold
            # the login/image lock: the existing Stop route must stay available.
            status, data = await bridge.request("/api/ptz/presets", {}, "POST", form=form,
                                                timeout=70 if action == "recall" else 15)
            return self.json(data, status_code=status)
        except (aiohttp.ClientError, TimeoutError, ValueError, RuntimeError):
            # Never retry a movement on timeout: its physical outcome is unknown.
            return self.json({"ok": False, "error": "Position indisponible ; actualise l’état avant de réessayer"}, status_code=503)


class AudioView(CameraView):
    url = "/api/anyka_controls/audio"
    name = "api:anyka_controls:audio"

    async def get(self, request):
        bridge = self.bridge(request)
        value = request.query.get("volume", "")
        try:
            volume = int(value)
        except ValueError:
            raise web.HTTPBadRequest() from None
        if str(volume) != value or not -30 <= volume <= 0:
            raise web.HTTPBadRequest()
        rate_text = request.query.get("rate", "8000")
        nr_text = request.query.get("nr", "0")
        if rate_text != "8000" or nr_text not in ("0", "1"):
            raise web.HTTPBadRequest()
        if request.query.get("format", "pcm") != "pcm":
            raise web.HTTPBadRequest()
        options = {"volume": volume, "rate": rate_text, "nr": nr_text, "format": "pcm"}
        frame_size = int(rate_text)*2//25
        if bridge.audio_active:
            raise web.HTTPConflict()
        bridge.audio_active = True
        try:
            await bridge.login()
            async with bridge.session.ws_connect(
                bridge.base + "/api/audio/live", params=options,
                headers={"Origin": bridge.base}, max_msg_size=2048,
            ) as upstream:
                socket = web.WebSocketResponse(max_msg_size=2048)
                await socket.prepare(request)

                async def forward_responses():
                    async for message in upstream:
                        if message.type == aiohttp.WSMsgType.TEXT:
                            await socket.send_str(message.data)
                    await socket.close()

                task = asyncio.create_task(forward_responses())
                try:
                    async with asyncio.timeout(300):
                        while not socket.closed:
                            message = await socket.receive(timeout=3)
                            if message.type == aiohttp.WSMsgType.TEXT and message.data == "end":
                                await upstream.send_str("end")
                                await task
                                break
                            if message.type != aiohttp.WSMsgType.BINARY:
                                break
                            if len(message.data) != frame_size:
                                break
                            await upstream.send_bytes(message.data)
                except (TimeoutError, aiohttp.ClientError):
                    pass
                finally:
                    task.cancel()
                    await asyncio.gather(task, return_exceptions=True)
                    await socket.close()
                return socket
        except (aiohttp.ClientError, TimeoutError, RuntimeError):
            raise web.HTTPServiceUnavailable() from None
        finally:
            bridge.audio_active = False


async def probe_mp3(path):
    process = await asyncio.create_subprocess_exec(
        "ffprobe", "-v", "error", "-show_entries", "format=duration:stream=codec_name,codec_type",
        "-of", "json", path, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL,
    )
    try:
        output, _ = await asyncio.wait_for(process.communicate(), 10)
    except TimeoutError:
        process.kill()
        await process.wait()
        raise ValueError("Analyse MP3 trop longue") from None
    if process.returncode or len(output) > 16384:
        raise ValueError("Fichier MP3 illisible")
    try:
        info = json.loads(output)
        duration = float(info["format"]["duration"])
        if not any(s.get("codec_type") == "audio" and s.get("codec_name") == "mp3" for s in info["streams"]):
            raise ValueError
        if not math.isfinite(duration) or not 0 < duration <= MP3_PLAY_LIMIT:
            raise ValueError
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError("MP3 invalide ou durée supérieure à 4 min 50 s") from error
    return duration


class AudioFileView(CameraView):
    url = "/api/anyka_controls/mp3"
    name = "api:anyka_controls:mp3"

    async def get(self, request):
        bridge = self.bridge(request)
        try:
            volume = int(request.query["volume"])
            nr = request.query["nr"]
            if not -30 <= volume <= 0 or nr not in ("0", "1"):
                raise ValueError
        except (KeyError, ValueError):
            raise web.HTTPBadRequest() from None
        if bridge.audio_active:
            raise web.HTTPConflict()
        bridge.audio_active = True
        socket = web.WebSocketResponse(max_msg_size=PCM_FRAME_BYTES * 64)
        await socket.prepare(request)
        process = None
        watcher = None
        try:
            with tempfile.NamedTemporaryFile(mode="w+b", prefix="anyka-mp3-") as file:
                await socket.send_json({"type": "upload_ready"})
                size = 0
                async with asyncio.timeout(120):
                    while True:
                        message = await socket.receive(timeout=15)
                        if message.type == aiohttp.WSMsgType.TEXT and message.data == "upload_end":
                            break
                        if message.type != aiohttp.WSMsgType.BINARY or not 0 < len(message.data) <= 32768:
                            return socket
                        size += len(message.data)
                        if size > MP3_UPLOAD_LIMIT:
                            raise ValueError("Le MP3 dépasse 64 Mio")
                        file.write(message.data)
                        await socket.send_json({"type": "upload_credit", "received": size})
                if not size:
                    raise ValueError("Fichier MP3 vide")
                file.flush()
                duration = await probe_mp3(file.name)
                await bridge.login()
                async with bridge.session.ws_connect(
                    bridge.base + "/api/audio/live",
                    params={"volume": volume, "rate": 8000, "nr": nr, "format": "pcm"},
                    headers={"Origin": bridge.base}, max_msg_size=2048,
                ) as upstream:
                    response = await upstream.receive_json(timeout=12)
                    if response.get("type") != "ready":
                        raise RuntimeError("Haut-parleur indisponible")
                    await socket.send_json({"type": "ready", "duration": round(duration, 1)})
                    watcher = asyncio.create_task(socket.receive())
                    process = await asyncio.create_subprocess_exec(
                        "ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin",
                        "-i", file.name, "-vn", "-ac", "1", "-ar", "8000",
                        "-f", "s16le", "pipe:1", stdout=asyncio.subprocess.PIPE,
                        stderr=asyncio.subprocess.DEVNULL,
                    )
                    started = time.monotonic()
                    frames = 0
                    async with asyncio.timeout(295):
                        while frames < MP3_PLAY_LIMIT * 25:
                            if watcher.done():
                                return socket
                            try:
                                pcm = await process.stdout.readexactly(PCM_FRAME_BYTES)
                            except asyncio.IncompleteReadError as error:
                                pcm = error.partial
                                if not pcm:
                                    break
                                pcm = pcm.ljust(PCM_FRAME_BYTES, b"\0")
                            await asyncio.sleep(max(0, started + frames * .04 - time.monotonic()))
                            if watcher.done():
                                return socket
                            await upstream.send_bytes(pcm)
                            frames += 1
                            if frames % 25 == 0:
                                await socket.send_json({"type": "progress", "seconds": frames // 25})
                        if frames == MP3_PLAY_LIMIT * 25 and await process.stdout.read(1):
                            raise ValueError("Le MP3 dépasse 4 min 50 s après décodage")
                        await asyncio.wait_for(process.wait(), 5)
                        if process.returncode or frames == 0 or frames / 25 < duration - 3:
                            raise ValueError("Décodage MP3 incomplet")
                        await upstream.send_str("end")
                        response = await upstream.receive_json(timeout=8)
                        if response.get("type") != "ended":
                            raise RuntimeError("Fin de lecture non confirmée")
                        await socket.send_json({"type": "ended", "seconds": frames / 25})
        except (ValueError, RuntimeError, TimeoutError, aiohttp.ClientError, OSError) as error:
            if not socket.closed:
                await socket.send_json({"type": "error", "value": str(error) or "Lecture MP3 indisponible"})
        finally:
            if watcher is not None:
                watcher.cancel()
                await asyncio.gather(watcher, return_exceptions=True)
            if process is not None:
                if process.returncode is None:
                    process.kill()
                await process.wait()
            bridge.audio_active = False
            await socket.close()
        return socket


@websocket_api.websocket_command({
    vol.Required("type"): "anyka_controls/audio_url",
    vol.Required("volume"): vol.All(int, vol.Range(min=-30, max=0)),
    vol.Optional("rate", default=8000): vol.In([8000]),
    vol.Optional("nr", default=False): bool,
})
@websocket_api.async_response
async def audio_url(hass, connection, message):
    if not connection.user.is_admin:
        connection.send_error(message["id"], "unauthorized", "Administrator required")
        return
    path = "/api/anyka_controls/audio?volume=" + str(message["volume"]) + "&rate=" + str(message["rate"]) + "&nr=" + str(int(message["nr"])) + "&format=pcm"
    signed = async_sign_path(hass, path, timedelta(seconds=30), refresh_token_id=connection.refresh_token_id)
    connection.send_result(message["id"], {"url": signed})


@websocket_api.websocket_command({
    vol.Required("type"): "anyka_controls/mp3_url",
    vol.Required("volume"): vol.All(int, vol.Range(min=-30, max=0)),
    vol.Optional("nr", default=False): bool,
})
@websocket_api.async_response
async def mp3_url(hass, connection, message):
    if not connection.user.is_admin:
        connection.send_error(message["id"], "unauthorized", "Administrator required")
        return
    path = "/api/anyka_controls/mp3?volume=" + str(message["volume"]) + "&nr=" + str(int(message["nr"]))
    signed = async_sign_path(hass, path, timedelta(seconds=30), refresh_token_id=connection.refresh_token_id)
    connection.send_result(message["id"], {"url": signed})


async def async_setup_entry(hass, entry):
    bridge = CameraBridge(entry.data["host"], entry.data["password"])
    hass.data[DOMAIN] = bridge
    bridge.motion_coordinator = DataUpdateCoordinator(
        hass, LOGGER, config_entry=entry, name="Camera Anyka motion",
        update_method=bridge.motion_status, update_interval=timedelta(seconds=1),
    )
    await bridge.motion_coordinator.async_refresh()
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    if not hass.data.get(DOMAIN + "_registered"):
        for view in (StatusView(), CommandView(), PresetsView(), AudioView(), AudioFileView()):
            hass.http.register_view(view)
        websocket_api.async_register_command(hass, audio_url)
        websocket_api.async_register_command(hass, mp3_url)
        hass.data[DOMAIN + "_registered"] = True
    return True


async def async_unload_entry(hass, entry):
    if not await hass.config_entries.async_unload_platforms(entry, PLATFORMS):
        return False
    bridge = hass.data.pop(DOMAIN, None)
    if bridge:
        await bridge.session.close()
    return True
