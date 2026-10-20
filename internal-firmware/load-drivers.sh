#!/bin/sh
set -eu
load() {
    name=${1%.ko}
    name=$(printf '%s' "$name" | /bin/busybox tr - _)
    [ -d "/sys/module/$name" ] || /bin/busybox insmod "/app/ak/modules/$1"
}
for module in ak_i2c ak_pcm ak_ion ak_isp ak_venc_adapter ak_venc_bridge \
    usb-common usbcore ak_hcd ak_saradc ak_pwm_char loop ak_npu; do
    load "$module.ko"
done
# Original NPU is retained for the SDK helper's RAM-only restore path.
# SD driver remains available for a future explicit recovery procedure.
[ -d /sys/module/gv_gpio ] || /bin/busybox insmod /app/drivers/gv-gpio.ko
/bin/busybox sleep 1
[ -d /sys/module/atbm6132c_wifi_usb ] || \
    /bin/busybox insmod /app/drivers/atbm6132c_wifi_usb.ko
