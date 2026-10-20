# Hardware identification

| Item | Development unit |
| --- | --- |
| Application model | GP5_A10S14A5 |
| DT board identifier | EVB_CBDM_AK3918av130_V1.0.0 |
| DT compatible | anyka,ak3918av130 |
| SoC | AK3918AV130_B |
| CPU / ABI | ARM926EJ-S, ARMv5TEJ, ARM mode, soft float |
| Physical RAM | 64 MiB |
| NOR | ZB25VQ64, 8 MiB, observed erase size 4096 bytes |
| Sensor | SC3336 / SDK SC2B3AK driver ID 0x2b3a |
| ISP configuration | /app/ak/sensor/isp_sc2b3ak_mipi_2lane_av130.conf |
| Wi-Fi | USB AltoBeam 007a:6162, stock atbm6132c_wifi_usb.ko |
| U-Boot | 2019.10.0-V4.0.27 |
| Linux | 4.4.302-cip94, original stock build |
| Console | ttySAK0, 115200, 8N1 |
| Motor interface | /dev/gv-motor, stock gv_step_motor ABI |
| GPIO interface | /dev/gv-gpio, stock gv_gpio ABI |

The toolchain folder historically contains `ak3918ev300` in its name. That does not make this an ARMv7 EV300 camera. Every production object must be built and inspected for ARMv5 compatibility.

Sensor marketing names, module filenames and sensor IDs are not interchangeable. Keep the exact observed ID/configuration until a replacement driver is independently verified.

PCB photographs, UART pad locations, voltage measurements, Wi-Fi antenna variants, power budget and other retail labels are not documented with sufficient evidence. Do not infer a pinout or power connection from the console name. Add redacted hardware photographs and measurements before recommending a physical recovery wiring diagram.

---

[Documentation index](README.md) · [Project overview](../README.md)
