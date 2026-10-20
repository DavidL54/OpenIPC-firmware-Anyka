#!/usr/bin/haserl
<%
IFS_ORIG=$IFS

. ./p/majestic.sh

card_head() {
	echo "<div class=\"mj-live-head\">" \
		"<h3 class=\"mj-cap\">${1}</h3>" \
		"<span class=\"mj-live-rule\"></span>"
	[ -n "$2" ] && echo "<span class=\"mj-live-note\">${2}</span>"
	echo "</div>"
}

group_head() {
	echo "<div class=\"mj-live-grp-head\">" \
		"<span class=\"mj-cap\">${1}</span>" \
		"<span class=\"mj-live-rule\"></span></div>"
}

button_submit() {
	local t="$1"
	[ -z "$t" ] && t="Save Changes"
	local c="$2"
	[ -z "$c" ] && c="primary"
	local x="$3"
	[ -z "$x" ] && x=" ${x}"
	local n="$4"
	echo "<div class=\"mj-foot\">"
	[ -n "$n" ] && echo "<span class=\"mj-foot-note\">${n}</span>"
	echo "<input type=\"submit\" class=\"btn btn-${c}\"${x} value=\"${t}\"></div>"
}

uses_default_password() {
	local user hash rest method salt found=

	while IFS=: read -r user hash rest; do
		[ "$user" = "root" ] && { found=1; break; }
	done 2>/dev/null < /etc/shadow
	[ -n "$found" ] || return 1

	case "$hash" in
		"") return 0;;		# root really has no password: worse than default
		'$1$'*) method=md5;;
		'$5$'*) method=sha256;;
		'$6$'*) method=sha512;;
		*) return 1;;		# locked (! or *), or a scheme we cannot rebuild
	esac

	salt=${hash#\$}; salt=${salt#*\$}; salt=${salt%%\$*}
	case "$salt" in
		""|rounds=*) return 1;;	# mkpasswd has no way to set a round count
	esac

	[ "$(mkpasswd -m "$method" -S "$salt" 12345 2>/dev/null)" = "$hash" ]
}

check_password() {
	local p="/cgi-bin/access.cgi"
	[ -z "$SCRIPT_NAME" ] || [ "$SCRIPT_NAME" = "${p}" ] && return
	if uses_default_password; then
		redirect_to "${p}" "danger" "You must set your own secure password!"
	fi
}

ex() {
	echo "<div class=\"${2:-ex}\"><h6># ${1}</h6><pre class=\"small\">"
	eval "$1" 2>&1 | sed "s/&/\&amp;/g;s/</\&lt;/g;s/>/\&gt;/g;s/\"/\&quot;/g"
	echo "</pre></div>"
}

field_hidden() {
	local n="$1"
	local v="$2"
	echo "<input type=\"hidden\" name=\"${n}\" id=\"${n}\" value=\"$(attr_escape "$v")\" class=\"form-hidden\">"
}

field_password() {
	local n="$1"
	local l="$2"
	local h="$3"
	local v=$(t_value "$n")
	echo "<p class=\"password mj-row\" id=\"${n}_wrap\">" \
		"<label for=\"${n}\" class=\"form-label\">${l}</label>" \
		"<span class=\"mj-ctl\"><span class=\"mj-ctl-in\"><span class=\"input-group\">" \
		"<input type=\"password\" id=\"${n}\" name=\"${n}\" class=\"form-control\" value=\"$(attr_escape "$v")\">" \
		"<label class=\"input-group-text\">" \
		"<input type=\"checkbox\" class=\"form-check-input me-1\" data-for=\"${n}\"> show" \
		"</label></span></span></span>"
	[ -n "$h" ] && echo "<span class=\"hint text-secondary\">${h}</span>"
	echo "</p>"
}

attr_escape() {
	printf '%s' "$1" | sed \
		-e 's/&/\&amp;/g' \
		-e 's/</\&lt;/g' \
		-e 's/>/\&gt;/g' \
		-e 's/"/\&quot;/g' \
		-e 's/&amp;#\([0-9][0-9]*\);/\&#\1;/g'
}

esc() {
	local s="$1"
	s=${s//&/&amp;}
	s=${s//</&lt;}
	s=${s//>/&gt;}
	s=${s//\"/&quot;}
	printf '%s' "$s"
}

shq() {
	printf "'"
	printf '%s' "$1" | sed "s/'/'\\\\''/g"
	printf "'"
}

conf_write() {
	printf '%s=' "$1"
	shq "$2"
	printf '\n'
}

field_switch() {
	local n="$1"
	local l="$2"
	local v="$3"
	local h="$4"
	local c="$5"
	local extra=""
	[ "$v" = "eval" ] && v=$(t_value "$n")
	case "$v" in
		true) v="checked" ;;
		*)    v="" ;;
	esac
	[ -n "$c" ] && extra=" data-confirm=\"$(attr_escape "$c")\""
	echo "<p class=\"boolean mj-row$([ -n "$c" ] && echo ' destructive')\">" \
		"<label for=\"${n}\" class=\"form-label\">${l}</label>" \
		"<span class=\"mj-ctl\"><span class=\"mj-ctl-in\">" \
		"<span class=\"form-check form-switch\">" \
		"<input type=\"hidden\" id=\"${n}-false\" name=\"${n}\" value=\"false\">" \
		"<input type=\"checkbox\" id=\"${n}\" name=\"${n}\" value=\"true\" class=\"form-check-input\" ${v}${extra}>" \
		"</span><span class=\"mj-state\" aria-hidden=\"true\"></span></span></span>"
	[ -n "$h" ] && echo "<span class=\"hint text-secondary\">${h}</span>"
	echo "</p>"
}

field_string() {
	local n="$1"
	local l="$2"
	local v="$3"
	local e="$4"
	local h="$5"
	[ "$v" = "eval" ] && v=$(t_value "$n")
	if [ -n "$e" ]; then
		echo "<p class=\"select mj-row\" id=\"${n}_wrap\">" \
			"<label for=\"${n}\" class=\"form-label\">${l}</label>" \
			"<span class=\"mj-ctl\"><span class=\"mj-ctl-in\">" \
			"<select class=\"form-select\" id=\"${n}\" name=\"${n}\">"
		for e in $e; do
			echo -n "<option value=\"${e}\""
			[ "$v" = "$e" ] && echo -n " selected"
			echo ">${e}</option>"
		done
		echo "</select></span></span>"
	else
		echo "<p class=\"string mj-row\" id=\"${n}_wrap\">" \
			"<label for=\"${n}\" class=\"form-label\">${l}</label>" \
			"<span class=\"mj-ctl\"><span class=\"mj-ctl-in\">" \
			"<input type=\"text\" id=\"${n}\" name=\"${n}\" class=\"form-control\" value=\"$(attr_escape "$v")\">" \
			"</span></span>"
	fi
	[ -n "$h" ] && echo "<span class=\"hint text-secondary\">${h}</span>"
	echo "</p>"
}

field_text() {
	local n="$1"
	local l="$2"
	local h="$3"
	local v=$(t_value "$n")
	echo "<p class=\"string mj-row\" id=\"${n}_wrap\">" \
		"<label for=\"${n}\" class=\"form-label\">${l}</label>" \
		"<span class=\"mj-ctl\"><span class=\"mj-ctl-in\">" \
		"<input type=\"text\" id=\"${n}\" name=\"${n}\" class=\"form-control\" value=\"$(attr_escape "$v")\">" \
		"</span></span>"
	[ -n "$h" ] && echo "<span class=\"hint text-secondary\">${h}</span>"
	echo "</p>"
}

field_textedit() {
	local n="$1"
	local l="$2"
	local v=""
	[ -f "$3" ] && [ -r "$3" ] && v=$(cat "$3" 2>/dev/null)
	echo "<p class=\"textarea\" id=\"${n}_wrap\">" \
		"<label for=\"${n}\" class=\"form-label\">${l}</label>" \
		"<textarea id=\"${n}\" name=\"${n}\" class=\"form-control\">${v}</textarea>"
	echo "</p>"
}

get_config() {
	echo ${1}/etc/majestic.yaml
}

majestic_reload() {
	pidof majestic >/dev/null 2>&1 || return 1
	(sleep 2; killall -1 majestic) </dev/null >/dev/null 2>&1 &
	return 0
}

crond_tz_want=""
crond_tz_ok() {
	local pid have
	pid=$(pidof crond 2>/dev/null)
	pid=${pid%% *}
	[ -n "$pid" ] || return 1
	have=$(tr '\0' '\n' <"/proc/$pid/environ" 2>/dev/null | sed -n 's/^TZ=//p')
	[ "$have" = "$crond_tz_want" ]
}
crond_tz_sync() {
	local i
	crond_tz_want=$(cat /etc/TZ 2>/dev/null)
	crond_tz_ok && return 0
	[ -x /etc/init.d/S60crond ] || return 1
	TZ="$crond_tz_want" /etc/init.d/S60crond restart </dev/null >/dev/null 2>&1
	for i in 1 2 3; do
		crond_tz_ok && return 0
		sleep 1
	done
	return 1
}

log_create() {
	echo "${1}:${2}" > "$log_file"
}

notice_icon() {
	local d
	case "$1" in
		danger) d='<circle cx="12" cy="12" r="8.7"/><path d="M12 7.6v5"/><path d="M12 16.3h.01"/>' ;;
		warn) d='<path d="M12 4.6 21.2 19.4H2.8z"/><path d="M12 10.2v4"/><path d="M12 17.1h.01"/>' ;;
		ok) d='<circle cx="12" cy="12" r="8.7"/><path d="m8.3 12.3 2.6 2.6 4.9-5.3"/>' ;;
		*) d='<circle cx="12" cy="12" r="8.7"/><path d="M12 11.2v5.2"/><path d="M12 7.7h.01"/>' ;;
	esac
	printf '<svg class="mj-notice-ico" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">%s</svg>' "$d"
}

notice() {
	printf '<div class="mj-notice mj-notice-%s" role="alert">' "$1"
	notice_icon "$1"
	printf '<div class="mj-notice-txt">%s</div>' "$2"
	[ -n "$3" ] && printf '<span class="mj-notice-acts">%s</span>' "$3"
	[ -n "$4" ] && printf '<button type="button" class="btn-close" data-bs-dismiss="alert" aria-label="Close"></button>'
	printf '</div>\n'
}

log_read() {
	[ ! -f "$log_file" ] && return
	[ -z "$(cat $log_file)" ] && return
	local c
	local m
	local l
	OIFS="$IFS"
	IFS=$'\n'
	for l in $(cat "$log_file"); do
		case "$(echo $l | cut -d':' -f1)" in
			success) c=ok ;;
			warning) c=warn ;;
			danger) c=danger ;;
			*) c=info ;;
		esac
		m="$(echo $l | cut -d':' -f2-)"
		notice "$c" "$m" "" 1
	done
	IFS=$OIFS
	rm -f "$log_file"
}

set_error_flag() {
	echo "danger:${1}" >> "$log_file"
	error=1
}

html_title() {
	local m=$(page_menu "$pagename")
	[ -n "$m" ] && printf '%s - ' "$m"
	printf '%s - OpenIPC' "$page_title"
}

include() {
	[ -f "$1" ] && . "$1"
}

hook_say() {
	if [ "$notify_hooks_lang" = ru ]; then
		case "$1" in
		clip_unreadable)
			printf '%s' "Один из файлов настроек уведомлений не читается, поэтому то, что камера запускает по окончании записи, осталось без изменений."
			return ;;
		clip_cannot_ask)
			printf '%s' "Не удалось спросить камеру, что она запускает по окончании записи, поэтому настройка осталась без изменений."
			return ;;
		clip_refused)
			printf '%s' "Камера не приняла настройку отправки записей."
			return ;;
		clip_not_kept)
			printf '%s' "Камера не сохранила настройку отправки записей."
			return ;;
		clip_taken)
			printf '%s' "По окончании записи камера уже запускает свою команду, поэтому её не трогали. Очистите её, чтобы отправлять записи отсюда."
			return ;;
		clip_stop_refused)
			printf '%s' "Камера не смогла прекратить запуск отправки записей."
			return ;;
		motion_unreadable)
			printf '%s' "Один из файлов настроек уведомлений не читается, поэтому то, что камера запускает при начале движения, осталось без изменений."
			return ;;
		motion_no_worker)
			printf '%s' "В этой прошивке нет части, которая отправляет движение с камеры без карты памяти, поэтому отправлять можно только записи."
			return ;;
		motion_taken)
			printf '%s' "При начале движения камера уже запускает свой скрипт, поэтому его не трогали. Удалите ${motion_hook_path}, чтобы отправлять движение отсюда на камере без карты памяти."
			return ;;
		motion_refused)
			printf '%s' "Камера не приняла скрипт, который отправляет движение."
			return ;;
		esac
	fi

	case "$1" in
	clip_unreadable)
		printf '%s' "One of the notification settings files could not be read, so what the camera runs when a recording finishes was left alone." ;;
	clip_cannot_ask)
		printf '%s' "The camera could not be asked what it runs when a recording finishes, so that was left alone." ;;
	clip_refused)
		printf '%s' "The camera would not take the setting that sends recordings." ;;
	clip_not_kept)
		printf '%s' "The camera did not keep the setting that sends recordings." ;;
	clip_taken)
		printf '%s' "The camera already runs a command of its own when a recording finishes, so it was left alone. Clear it to send recordings from here." ;;
	clip_stop_refused)
		printf '%s' "The camera would not stop running the recording sender." ;;
	motion_unreadable)
		printf '%s' "One of the notification settings files could not be read, so what the camera runs when movement starts was left alone." ;;
	motion_no_worker)
		printf '%s' "This firmware does not include the part that sends movement from a camera with no memory card, so only recordings can be sent." ;;
	motion_taken)
		printf '%s' "The camera already runs a script of its own when movement starts, so it was left alone. Remove ${motion_hook_path} to have movement sent from here on a camera with no memory card." ;;
	motion_refused)
		printf '%s' "The camera would not take the script that sends movement." ;;
	esac
}

clip_hook_wanted() {
	_ch_unsure=0

	for _ch_name in telegram ntfy max; do
		[ -e "/etc/webui/${_ch_name}.conf" ] || continue
		(
			. "/etc/webui/${_ch_name}.conf"
			eval "[ \"\$${_ch_name}_enabled\" = true ] &&
				[ \"\$${_ch_name}_clips\" = true ]"
		)
		case $? in
		0) return 0 ;;
		1) ;;
		*) _ch_unsure=1 ;;
		esac
	done

	[ "$_ch_unsure" = 1 ] && return 2

	return 1
}

clip_hook_sync() {
	clip_hook_msg=""
	_ch_hook=/usr/sbin/record.sh
	_ch_now=$(mj_cfg records.onClose)

	case $? in
	0) ;;
	1) _ch_now="" ;;
	*)
		clip_hook_msg=$(hook_say clip_cannot_ask)
		return 1
		;;
	esac

	clip_hook_wanted
	_ch_want=$?

	if [ "$_ch_want" = 2 ]; then
		clip_hook_msg=$(hook_say clip_unreadable)
		return 1
	fi

	if [ "$_ch_want" = 0 ]; then
		case "$_ch_now" in
		"$_ch_hook") ;;
		"")
			if ! mj_set records.onClose "$_ch_hook"; then
				clip_hook_msg=$(hook_say clip_refused)
				return 1
			fi
			if [ "$(mj_cfg records.onClose)" != "$_ch_hook" ]; then
				clip_hook_msg=$(hook_say clip_not_kept)
				return 1
			fi
			;;
		*)
			clip_hook_msg=$(hook_say clip_taken)
			return 1
			;;
		esac
	elif [ "$_ch_now" = "$_ch_hook" ]; then
		if ! mj_clear records.onClose; then
			clip_hook_msg=$(hook_say clip_stop_refused)
			return 1
		fi
	fi

	return 0
}

motion_hook_path=/usr/sbin/motion.sh
motion_hook_worker=/usr/sbin/motion-notify.sh
motion_hook_mark="# written by the OpenIPC WebUI notification pages"

motion_hook_sync() {
	motion_hook_msg=""

	clip_hook_wanted
	_mh_want=$?

	if [ "$_mh_want" = 2 ]; then
		motion_hook_msg=$(hook_say motion_unreadable)
		return 1
	fi

	if [ "$_mh_want" = 0 ]; then
		if [ ! -x "$motion_hook_worker" ]; then
			motion_hook_msg=$(hook_say motion_no_worker)
			return 1
		fi

		if [ -e "$motion_hook_path" ]; then
			if grep -qF "$motion_hook_mark" "$motion_hook_path" 2>/dev/null; then
				return 0
			fi
			motion_hook_msg=$(hook_say motion_taken)
			return 1
		fi

		cat > "$motion_hook_path" <<-HOOK
			#!/bin/sh
			${motion_hook_mark}
			#
			# Delete this file to stop movement being sent, or replace it with
			# a script of your own -- nothing here will overwrite one.
			exec ${motion_hook_worker} "\$@"
		HOOK
		if [ ! -s "$motion_hook_path" ] || ! chmod 0755 "$motion_hook_path" ||
			[ ! -x "$motion_hook_path" ]; then
			rm -f "$motion_hook_path"
			motion_hook_msg=$(hook_say motion_refused)
			return 1
		fi
		return 0
	fi

	if [ -e "$motion_hook_path" ] &&
		grep -qF "$motion_hook_mark" "$motion_hook_path" 2>/dev/null; then
		rm -f "$motion_hook_path"
	fi

	return 0
}

notify_hooks_sync() {
	notify_hooks_msg=""

	clip_hook_sync || notify_hooks_msg="$clip_hook_msg"
	motion_hook_sync ||
		notify_hooks_msg="${notify_hooks_msg}${notify_hooks_msg:+ }${motion_hook_msg}"

	[ -z "$notify_hooks_msg" ]
}

pre() {
	local c="$2"
	[ -n "$c" ] && c=" class=\"${c}\""
	local x="$3"
	[ -n "$x" ] && x=" ${x}"
	echo "<pre${c}${x}>$(esc "$(echo -e "$1")")</pre>"
}

redirect_back() {
	redirect_to "${HTTP_REFERER:-/}" "$1" "$2"
}

moved_to() {
	local q=
	[ -n "$QUERY_STRING" ] && q="?$QUERY_STRING"
	echo "HTTP/1.1 307 Temporary Redirect"
	echo "Content-type: text/html; charset=UTF-8"
	echo "Cache-Control: no-store"
	echo "Location: /cgi-bin/${1}${q}"
	echo
	exit 0
}

redirect_to() {
	[ -n "$3" ] && log_create "$2" "$3"
	echo "HTTP/1.1 303 See Other"
	echo "Content-type: text/html; charset=UTF-8"
	echo "Cache-Control: no-store"
	echo "Pragma: no-cache"
	echo "Location: $1"
	echo
	exit 0
}

generate_signature() {
	local sig="$network_hostname"
	local lower=$(echo "$network_hostname" | tr 'A-Z' 'a-z')
	local soc_l=$(echo "$soc" | tr 'A-Z' 'a-z')
	local sensor_l=$(echo "$sensor" | tr 'A-Z' 'a-z')
	case "$lower" in *"$soc_l"*) ;; *) sig="$sig, $soc" ;; esac
	[ "$sensor_l" != "unknown" ] && case "$lower" in *"$sensor_l"*) ;; *) sig="$sig, $sensor" ;; esac
	sig="$sig, ${fw_version}-${fw_variant}"
	sig="${sig#, }"
	esc "$sig" > $signature_file
}

signature() {
	[ ! -f "$signature_file" ] && generate_signature
	cat $signature_file
}

t_value() {
	eval "printf '%s' \"\$${1}\""
}

update_caminfo() {
	flash_type=$(ipcinfo --flash-type)
	mtd_size=$(grep -E "nor|nand" $(ls /sys/class/mtd/mtd*/type) | sed -E "s|type.+|size|g")
	flash_size=$(awk '{sum+=$1} END{print sum/1024/1024}' $mtd_size)

	sensor=$(fw_printenv -n sensor)
	[ -z "$sensor" ] && sensor="unknown"

	soc_vendor=$(ipcinfo --vendor)
	soc_family=$(ipcinfo --family)

	soc=$(ipcinfo --chip-name)
	if [ -z "$soc" ] || [ "$soc_vendor" = "sigmastar" ]; then
		soc=$(fw_printenv -n soc)
	fi

	soc_temp=$(ipcinfo --temp 2> /dev/null)
	if [ -n "$soc_temp" ]; then
		soc_has_temp="true"
	else
		soc_has_temp="false"
	fi

	fw_version=$(grep "OPENIPC_VERSION" /etc/os-release | cut -d= -f2 | tr -d '"')
	fw_variant=$(grep "BUILD_OPTION" /etc/os-release | cut -d= -f2 | tr -d '"')
	fw_build=$(grep "GITHUB_VERSION" /etc/os-release | cut -d= -f2 | tr -d '"')
	mj_version=$($mj_bin_file -v)
	uboot_version=$(fw_printenv -n ver)
	mj_pid=$(pidof majestic 2>/dev/null)
	mj_pid=${mj_pid%% *}
	mj_started=""
	[ -n "$mj_pid" ] && read_starttime "$mj_pid" && mj_started=$mj_started_now

	ui_password=$(grep root /etc/shadow | cut -d: -f2)
	if [ -s /etc/webui/webui.version ]; then
		webui_version=$(cat /etc/webui/webui.version)
	elif [ -s /var/www/.version ]; then
		webui_version=$(cat /var/www/.version)
	else
		webui_version=""
	fi
	webui_version=$(printf '%s' "$webui_version" | tr -cd 'A-Za-z0-9 .,:+/@()_-')
	ptz_support=""; ptz_backend=""; ptz_reason=""
	ptz_control=$(fw_printenv -n ptz_control 2>/dev/null)
	case "$ptz_control" in
		gpio)
			if command -v gpio-motors >/dev/null 2>&1 &&
				{ [ -n "$(fw_printenv -n ptz_gpio 2>/dev/null)" ] || [ -n "$(fw_printenv -n gpio_motors 2>/dev/null)" ]; }; then
				ptz_support="1"; ptz_backend="gpio"
			fi
			;;
		pelco-d|pelco-xm|ms41908|gpiostep)
			mj_ptz > /dev/null 2>&1
			case $? in
				0|2) ptz_support="1"; ptz_backend="pelco" ;;
				3)
					ptz_support="1"; ptz_backend="pelco"
					ptz_reason="This camera has no PTZ driver installed: add the majestic-af package to its firmware."
					;;
			esac
			;;
		motor)
			if [ -x /usr/bin/motor ] &&
				{ [ -n "$(fw_printenv -n ptz_profile 2>/dev/null)" ] || [ -n "$(fw_printenv -n ptz 2>/dev/null)" ]; }; then
				ptz_support="1"; ptz_backend="motor"
			fi
			;;
	esac

	ptz_caps=""
	local cap
	for cap in $(fw_printenv -n ptz_caps 2>/dev/null); do
		case "$cap" in
			pan|tilt|zoom|focus) ptz_caps="$ptz_caps $cap" ;;
		esac
	done
	ptz_caps="${ptz_caps# }"
	[ -z "$ptz_caps" ] && [ "$ptz_control" = gpiostep ] && ptz_caps="pan tilt"


	network_interface=$(ip route | awk '/default/ {print $5}' | head -n1)
	network_address=$(ip route | grep ${network_interface:-eth0} | awk '/src/ {print $7}')
	network_gateway=$(ip route | awk '/default/ {print $3}')
	network_hostname=$(hostname -s)
	network_macaddr=$(cat /sys/class/net/${network_interface:-eth0}/address)

	overlay_root="/overlay"

	tz_data=$(cat /etc/TZ)
	tz_name=$(cat /etc/timezone)
	if [ -z "$tz_data" ] || [ -z "$tz_name" ]; then
		tz_data="GMT0"; echo "$tz_data" > /etc/TZ
		tz_name="Etc/GMT"; echo "$tz_name" > /etc/timezone
	fi

	local variables="flash_size flash_type fw_build fw_variant fw_version mj_pid mj_started mj_version network_address
		network_gateway network_hostname network_interface network_macaddr overlay_root ptz_support
		ptz_backend ptz_caps ptz_reason sensor soc soc_family soc_has_temp soc_vendor tz_data tz_name uboot_version ui_password webui_version"
	rm -f ${sysinfo_file}

	local v val
	for v in $variables; do
		eval "val=\$${v}"
		conf_write "$v" "$val" >> ${sysinfo_file}
	done

	generate_signature
}

if [ -n "$OPENIPC_ANYKA_WEBUI" ]; then
	. ./p/anyka.sh
else
mj_bin_file=/usr/bin/majestic
log_file=/tmp/webui/logfile.txt
signature_file=/tmp/webui/signature.txt
sysinfo_file=/tmp/webui/sysinfo.txt

[ ! -d /etc/webui ] && mkdir -p /etc/webui
[ ! -d /tmp/webui ] && mkdir -p /tmp/webui


read_starttime() {
	mj_started_now=""
	local line=""
	{ read line < "/proc/$1/stat"; } 2>/dev/null
	[ -n "$line" ] || return 1
	set -f
	set -- $line
	set +f
	mj_started_now=${22}
	[ -n "$mj_started_now" ]
}

caminfo_stale() {
	if [ -n "$mj_pid" ] && [ -n "$mj_started" ]; then
		local comm=""
		{ read comm < "/proc/$mj_pid/comm"; } 2>/dev/null
		[ "$comm" = "majestic" ] || return 0
		read_starttime "$mj_pid" || return 0
		[ "$mj_started_now" = "$mj_started" ] && return 1
		return 0
	fi
	pidof majestic >/dev/null 2>&1
}

include $sysinfo_file
if [ ! -f $sysinfo_file ] || caminfo_stale; then
	update_caminfo
fi
include $sysinfo_file

pagename=$(basename "$SCRIPT_NAME")
pagename="${pagename%%.*}"

include /etc/webui/webui.conf

check_password
fi
%>
