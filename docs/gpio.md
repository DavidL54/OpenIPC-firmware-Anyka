# GPIO and IR control

The stock GPIO interface is `/dev/gv-gpio`. The observed request is four native 32-bit integers `{pin, direction, value, force}`. Output direction is 1. Opening an unrelated guessed SDK GPIO device or assigning an unverified pin is not an equivalent operation.

| ioctl | Hex value |
| --- | --- |
| GPIO_INIT | 0x4004476e |
| GPIO_SET | 0x40044770 |
| GPIO_GET | 0x40044771 |

| Function | GPIO | Observed behavior |
| --- | --- | --- |
| IR illumination | 56 | Active high |
| Speaker amplifier | 67 | Active low; always mute on exit |
| Blue indicator | 68 | Active low |
| IR-cut drive A | 17 | Night pulse, about 100 ms |
| IR-cut drive B | 18 | Day pulse, about 100 ms |

Both IR-cut outputs rest at 0. Do not hold a coil energized continuously. A red LED assignment, GPIO69, board test pads and electrical pin voltages are not validated by this publication.

This mapping belongs in a board-specific OpenIPC/builder overlay, not as unconditional defaults for every Anyka board. Keep GPIO/ISP changes serialized with the actual controls and preserve day/night ownership. The helpers use the stock kernel ABI; any proposed kernel replacement must revalidate it.

---

[Documentation index](README.md) · [Project overview](../README.md)
