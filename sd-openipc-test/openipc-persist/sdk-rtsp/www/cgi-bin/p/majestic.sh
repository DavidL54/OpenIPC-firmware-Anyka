#!/bin/sh

mj_cfg() {
	_mj_r=$(curl -s -m 2 -w '|%{http_code}' \
		"http://127.0.0.1/api/v1/get?key=$1" 2>/dev/null) || return 2
	_mj_code=${_mj_r##*|}
	case "$_mj_code" in
		200) ;;
		404) return 1 ;;
		*) return 2 ;;
	esac
	printf '%s' "${_mj_r%|*}"
}

mj_ptz() {
	if [ -n "$1" ]; then
		_mj_r=$(curl -s -m 3 -X POST -w '|%{http_code}' \
			"http://127.0.0.1/ptz?move=$1" 2>/dev/null) || return 2
	else
		_mj_r=$(curl -s -m 3 -w '|%{http_code}' \
			"http://127.0.0.1/ptz" 2>/dev/null) || return 2
	fi
	_mj_code=${_mj_r##*|}
	case "$_mj_code" in
		200) ;;
		404) return 1 ;;
		503) return 3 ;;
		*) return 2 ;;
	esac
	printf '%s' "${_mj_r%|*}"
}

mj_clear() {
	mj_set "$1" null raw
}

mj_set() {
	if [ "$3" = raw ]; then
		_mj_body=$2
	else
		_mj_val=$(printf '%s' "$2" | sed 's/\\/\\\\/g; s/"/\\"/g')
		_mj_body="\"${_mj_val}\""
	fi
	_mj_path=$1
	while [ -n "$_mj_path" ]; do
		_mj_body="{\"${_mj_path##*.}\":${_mj_body}}"
		case "$_mj_path" in
		*.*) _mj_path=${_mj_path%.*} ;;
		*) _mj_path= ;;
		esac
	done

	_mj_r=$(curl -s -m 3 -w '|%{http_code}' -X POST \
		-H 'Content-Type: application/json' -d "$_mj_body" \
		"http://127.0.0.1/api/v1/config" 2>/dev/null) || return 2
	case "${_mj_r##*|}" in
	200 | 202) return 0 ;;
	4*) return 1 ;;
	*) return 2 ;;
	esac
}
