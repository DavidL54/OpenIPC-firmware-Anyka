#!/usr/bin/haserl
<%

page_label() {
	case "$1" in
	dashboard)      printf '%s' "Dashboard" ;;
	live)           printf '%s' "Live" ;;
	recordings)     printf '%s' "Recordings" ;;
	camera)         printf '%s' "Settings" ;;
	usb)            printf '%s' "USB" ;;
	stream-urls)    printf '%s' "Stream URLs" ;;
	raw)            printf '%s' "Raw" ;;
	config)         printf '%s' "Config file" ;;
	network)        printf '%s' "Network" ;;
	time)           printf '%s' "Time" ;;
	access)         printf '%s' "Access" ;;
	update)         printf '%s' "Update" ;;
	backup)         printf '%s' "Backup &amp; Restore" ;;
	restart)        printf '%s' "Restart camera" ;;
	logs)           printf '%s' "Logs" ;;
	console)        printf '%s' "Console" ;;
	files)          printf '%s' "File Manager" ;;
	sdcard)         printf '%s' "SD Card" ;;
	openwall)       printf '%s' "OpenWall" ;;
	telegram)       printf '%s' "Telegram" ;;
	ntfy)           printf '%s' "Ntfy" ;;
	max)            printf '%s' "MAX" ;;
	vtun)           printf '%s' "VTun" ;;
	wireguard)      printf '%s' "WireGuard" ;;
	proxy)          printf '%s' "Proxy" ;;
	editor)         printf '%s' "Text Editor" ;;
	factory-reset)  printf '%s' "Factory reset" ;;
	wfb)            printf '%s' "WFB Settings" ;;
	*)              printf '%s' "$1" ;;
	esac
}

page_menu() {
	case "$1" in
	camera|usb|stream-urls|raw|config)
		printf '%s' "Camera" ;;
	network|time|access|update|backup|logs|console|files|sdcard)
		printf '%s' "System" ;;
	openwall|telegram|ntfy|max|vtun|wireguard|proxy)
		printf '%s' "Services" ;;
	esac
}
%>
