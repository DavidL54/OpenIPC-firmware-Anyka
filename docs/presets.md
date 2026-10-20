# Saved PTZ positions and startup behavior

Eight camera-owned slots store `{id, name, pan, tilt}`. Names are UTF-8 with a 48-byte maximum, not necessarily 48 characters. The configuration schema is version 1; `startup: 0` disables recall and leaves the normal homing center.

```json
{"version":1,"startup":0,"positions":[]}
```

Saving reads the actual current counted position after checking homed state. The gateway validates unique slots, coordinate ranges, object structure and the selected startup slot. Deleting the selected slot clears startup selection. Malformed persistent JSON is reported unavailable instead of moving the camera using guessed defaults.

The camera WebUI and Home Assistant use the same camera API and persistent file. HA does not own a separate preset database. Persistent writes share the private-write lock used for settings, synchronize and restore the configuration filesystem read-only.

After homing, a selected position is applied once per boot. `/tmp/anyka-ptz-startup-done` is created before moving, preventing a repeated startup sweep if the service restarts after a failed recall. SDK recovery and WebUI restart do not retrigger it.

The 20 October checks saved and recalled a 32-step offset, retained a UTF-8 name over reboot, reached selected coordinates 1985/482 after center 2017/482, and verified Stop during a longer movement. The test slot was deleted and startup reset to 0 afterward. These are specific counted-coordinate checks, not proof of absolute optical orientation across all power cycles.

---

[Documentation index](README.md) · [Project overview](../README.md)
