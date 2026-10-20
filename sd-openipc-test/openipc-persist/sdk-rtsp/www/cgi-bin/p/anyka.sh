#!/bin/sh
pagename=${SCRIPT_NAME##*/}
pagename=${pagename%.cgi}
case "$pagename" in
	landscape) pagename=live ;;
esac
case "$pagename" in
	dashboard) page_title=Information ;;
	live) page_title=Preview ;;
	camera) page_title=Camera ;;
	*) page_title='Stream URLs' ;;
esac
fw_variant='Anyka SD'
fw_version='AnyCloud 1.03'
fw_build='Stock kernel / SD runtime'
soc=AK3918AV130_B
soc_family=Anyka
sensor=SC3336
flash_size=8
flash_type='NOR (read-only)'
network_hostname=GP5_A10S14A5
network_interface=wlan0
# These headers are overwritten by the authenticated Go gateway. Avoid
# starting eight helper processes every time a page is rendered.
network_address=${HTTP_X_ANYKA_ADDRESS:-127.0.0.1}
network_gateway=${HTTP_X_ANYKA_GATEWAY:-}
network_macaddr=
read -r network_macaddr < /sys/class/net/wlan0/address || true
webui_theme=dark
bb() {
	/media/lib/ld-musl-arm.so.1 --library-path /media/lib:/media/usr/lib /media/bin/busybox "$@"
}
log_file=/tmp/webui/logfile.txt
rtsp_state=${HTTP_X_ANYKA_RTSP_STATE:-unavailable}
signature() {
	printf '%s' 'Anyka AK3918AV130 / AnyCloud RTSP'
}
