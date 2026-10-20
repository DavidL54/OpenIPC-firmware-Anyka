#!/bin/sh
set -eu
if [ "${REQUEST_METHOD:-GET}" != GET ]; then
	printf 'Status: 405 Method Not Allowed\r\nAllow: GET\r\n\r\n'
	exit 0
fi
read -r cpu_name cpu_user cpu_nice cpu_system cpu_idle cpu_iowait cpu_irq cpu_softirq cpu_steal cpu_rest < /proc/stat
cpu_total=$((cpu_user + cpu_nice + cpu_system + cpu_idle + cpu_iowait + cpu_irq + cpu_softirq + cpu_steal))
cpu_idle=$((cpu_idle + cpu_iowait))
mem_total=0
mem_available=0
mem_anon=0
mem_shmem=0
mem_kernel=0
while read -r mem_key mem_value mem_unit; do
	case "$mem_key" in
		MemTotal:) mem_total=$((mem_value * 1024)) ;;
		MemAvailable:) mem_available=$((mem_value * 1024)) ;;
		AnonPages:) mem_anon=$((mem_value * 1024)) ;;
		Shmem:) mem_shmem=$((mem_value * 1024)) ;;
		SUnreclaim:|KernelStack:|PageTables:) mem_kernel=$((mem_kernel + mem_value * 1024)) ;;
	esac
done < /proc/meminfo
read -r uptime uptime_idle < /proc/uptime
read -r load_avg load_rest < /proc/loadavg
read -r net_rx < /sys/class/net/wlan0/statistics/rx_bytes
read -r net_tx < /sys/class/net/wlan0/statistics/tx_bytes
wifi_rssi=null
while read -r wifi_name wifi_flags wifi_link wifi_signal wifi_rest; do
	[ "$wifi_name" = wlan0: ] || continue
	wifi_signal=${wifi_signal%.}
	case "$wifi_signal" in
		''|*[!0-9-]*) ;;
		*) if [ "$wifi_signal" -gt -120 ] && [ "$wifi_signal" -le 0 ]; then wifi_rssi=$wifi_signal; fi ;;
	esac
done < /proc/net/wireless
temperature=null
for thermal_file in /sys/class/thermal/thermal_zone*/temp; do
	[ -r "$thermal_file" ] && [ -r "${thermal_file%/*}/type" ] || continue
	read -r thermal_type < "${thermal_file%/*}/type" || continue
	case "$thermal_type" in *cpu*|*CPU*|*soc*|*SoC*|*SOC*|*ak39*|*AK39*) ;; *) continue ;; esac
	read -r thermal_value < "$thermal_file" || continue
	case "$thermal_value" in -*) thermal_digits=${thermal_value#-} ;; *) thermal_digits=$thermal_value ;; esac
	case "$thermal_digits" in ''|*[!0-9]*) continue ;; esac
	[ "${#thermal_digits}" -le 6 ] || continue
	if [ "$thermal_value" -ge -40000 ] && [ "$thermal_value" -le 125000 ]; then
		temperature=$thermal_value
		break
	fi
done
printf 'Content-Type: application/json\r\nCache-Control: no-store\r\n\r\n'
printf '{"cpuTotal":%s,"cpuIdle":%s,"memTotal":%s,"memAvailable":%s,"memAnon":%s,"memShmem":%s,"memKernel":%s,"uptime":%s,"load":%s,"rx":%s,"tx":%s,"wifiRssi":%s,"temperatureMilliC":%s}\n' "$cpu_total" "$cpu_idle" "$mem_total" "$mem_available" "$mem_anon" "$mem_shmem" "$mem_kernel" "$uptime" "$load_avg" "$net_rx" "$net_tx" "$wifi_rssi" "$temperature"