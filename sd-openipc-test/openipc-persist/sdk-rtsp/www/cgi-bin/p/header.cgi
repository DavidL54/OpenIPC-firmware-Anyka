#!/usr/bin/haserl
<%in p/pages.cgi %>
<%
command -v esc >/dev/null 2>&1 || {
	printf 'HTTP/1.1 404 Not Found\nContent-type: text/plain; charset=UTF-8\n\n'
	printf 'Not a page.\n'
	exit 0
}

[ -z "$page_title" ] && page_title=$(page_label "$pagename")
%>Content-type: text/html; charset=UTF-8
Cache-Control: no-store
Pragma: no-cache

<!DOCTYPE html>
<%
: "${page_lang:=en}"
%>
<html lang="<% attr_escape "$page_lang" %>" data-bs-theme="<% attr_escape "${webui_theme:=dark}" %>">
<head>
	<meta charset="utf-8">
	<meta name="viewport" content="width=device-width, initial-scale=1">
	<meta name="darkreader-lock">
	<title><% html_title %></title>
	<script>if(document.documentElement.getAttribute('data-bs-theme')==='auto')document.documentElement.setAttribute('data-bs-theme',matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light');</script>
	<link rel="preconnect" href="https://fonts.googleapis.com">
	<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
	<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Montserrat:wght@400;500;600;700&family=PT+Mono&display=swap" media="print" onload="this.onload=null;this.media='all'">
	<link rel="stylesheet" href="/a/bootstrap.min.css">
	<link rel="stylesheet" href="/a/bootstrap.override.css">
	<script src="/a/main.js"></script>
<% if [ -n "$OPENIPC_ANYKA_WEBUI" ]; then %>
	<script>heartbeat=function(){};mjConfig=function(){return Promise.resolve(null);};</script>
<% else %>
	<script src="/a/cameras-switch.js" defer></script>
	<script src="/a/fw-changes.js" defer></script>
	<script src="/a/update-check.js" defer></script>
	<script src="/a/storage-verdict.js" defer></script>
	<script src="/a/storage-check.js" defer></script>
	<script src="/a/share.js" defer></script>
<% fi %>
</head>

<body id="page-<% attr_escape "$pagename" %>" class="<% attr_escape "$fw_variant" %>">
	<nav class="navbar navbar-expand-lg bg-body-tertiary">
		<div class="container">
			<a class="navbar-brand" href="dashboard.cgi"><img alt="Image: OpenIPC logo" height="32" src="/a/logo.svg"><span class="x-small ms-1"><% esc "$fw_variant" %></span></a>
			<button class="navbar-toggler" type="button" data-bs-toggle="collapse" data-bs-target="#navbarNav" aria-controls="navbarNav" aria-expanded="false" aria-label="Toggle navigation">
				<span class="navbar-toggler-icon"></span>
			</button>
			<div class="collapse navbar-collapse" id="navbarNav">
				<ul class="navbar-nav">
					<% if [ -n "$OPENIPC_ANYKA_WEBUI" ]; then %>
					<li class="nav-item"><a class="nav-link<% [ "$pagename" = dashboard ] && printf ' active' %>" href="dashboard.cgi">Information</a></li>
					<li class="nav-item"><a class="nav-link<% [ "$pagename" = live ] && printf ' active' %>" href="live.cgi">Preview</a></li>
					<li class="nav-item dropdown">
						<a aria-expanded="false" class="nav-link dropdown-toggle" data-bs-toggle="dropdown" href="#" id="dropdownCamera" role="button">Camera</a>
						<ul aria-labelledby="dropdownCamera" class="dropdown-menu">
							<li><a class="dropdown-item" href="camera.cgi"><% page_label camera %></a></li>
							<li><a class="dropdown-item" href="stream-urls.cgi"><% page_label stream-urls %></a></li>
						</ul>
					</li>
					<li class="nav-item dropdown">
						<a class="nav-link dropdown-toggle" data-bs-toggle="dropdown" href="#" role="button" aria-expanded="false">Tools</a>
						<ul class="dropdown-menu">
							<li><a class="dropdown-item" href="logs.cgi">Logs</a></li>
							<li><a class="dropdown-item" href="tools.cgi">Diagnostics / SD Card</a></li>
						</ul>
					</li>
					<li class="nav-item dropdown">
						<a class="nav-link dropdown-toggle" data-bs-toggle="dropdown" href="#" role="button" aria-expanded="false">Extensions</a>
						<ul class="dropdown-menu">
							<li><a class="dropdown-item" href="extensions.cgi">Extensions</a></li>
								<li><a class="dropdown-item" href="ntfy.cgi">Ntfy</a></li>
							</ul>
					</li>
					<li class="nav-item"><a class="nav-link" href="access.cgi">Access</a></li>
					<% if [ "${HTTP_X_ANYKA_LOGGED_IN:-false}" = true ]; then %>
					<li class="nav-item"><form action="/logout" method="post"><button class="nav-link" type="submit">Sign out</button></form></li>
					<% fi %>
					<% else %>
					<li class="nav-item"><a class="nav-link" href="dashboard.cgi"><% page_label dashboard %></a></li>
					<li class="nav-item"><a class="nav-link" href="live.cgi"><% page_label live %></a></li>
					<% if [ -e /dev/mmcblk0 ]; then %>
						<li class="nav-item"><a class="nav-link" href="recordings.cgi"><% page_label recordings %></a></li>
					<% fi %>
					<li class="nav-item dropdown">
						<a aria-expanded="false" class="nav-link dropdown-toggle" data-bs-toggle="dropdown" href="#" id="dropdownCamera" role="button">Camera</a>
						<ul aria-labelledby="dropdownCamera" class="dropdown-menu">
							<li><a class="dropdown-item" href="camera.cgi"><% page_label camera %></a></li>
							<% if [ -x /usr/sbin/usb-mode ] || [ -x /usr/bin/usb-mode ]; then %>
								<li><a class="dropdown-item" href="usb.cgi"><% page_label usb %></a></li>
							<% fi %>
							<li><a class="dropdown-item" href="stream-urls.cgi"><% page_label stream-urls %></a></li>
							<li><a class="dropdown-item" href="raw.cgi"><% page_label raw %></a></li>
							<li><a class="dropdown-item" href="config.cgi"><% page_label config %></a></li>
						</ul>
					</li>
					<li class="nav-item dropdown">
						<a aria-expanded="false" class="nav-link dropdown-toggle" data-bs-toggle="dropdown" href="#" id="dropdownSystem" role="button">System</a>
						<ul aria-labelledby="dropdownSystem" class="dropdown-menu">
							<li><h6 class="dropdown-header">Setup</h6></li>
							<li><a class="dropdown-item" href="network.cgi"><% page_label network %></a></li>
							<li><a class="dropdown-item" href="time.cgi"><% page_label time %></a></li>
							<li><a class="dropdown-item" href="access.cgi"><% page_label access %></a></li>
							<li><h6 class="dropdown-header">Maintenance</h6></li>
							<li><a class="dropdown-item" href="update.cgi"><% page_label update %></a></li>
							<li><a class="dropdown-item" href="backup.cgi"><% page_label backup %></a></li>
							<li>
								<form action="restart.cgi" method="post">
									<button type="submit" class="dropdown-item confirm text-danger"
											data-confirm="Restart the camera now?&#10;&#10;Settings are kept. Video and recording stop for about half a minute while it comes back."><% page_label restart %></button>
								</form>
							</li>
							<li><h6 class="dropdown-header">Diagnostics</h6></li>
							<li><a class="dropdown-item" href="logs.cgi"><% page_label logs %></a></li>
							<li><a class="dropdown-item" href="console.cgi"><% page_label console %></a></li>
							<li><a class="dropdown-item" href="files.cgi"><% page_label files %></a></li>
							<% if [ -e /dev/mmcblk0 ]; then %>
								<li><a class="dropdown-item" href="sdcard.cgi"><% page_label sdcard %></a></li>
							<% fi %>
						</ul>
					</li>
					<li class="nav-item dropdown">
						<a aria-expanded="false" class="nav-link dropdown-toggle" data-bs-toggle="dropdown" href="#" id="dropdownServices" role="button">Services</a>
						<ul aria-labelledby="dropdownServices" class="dropdown-menu">
							<li><h6 class="dropdown-header">Notifications</h6></li>
							<li id="nav-max" hidden><a class="dropdown-item" href="max.cgi"><% page_label max %></a></li>
							<li><a class="dropdown-item" href="openwall.cgi"><% page_label openwall %></a></li>
							<li><a class="dropdown-item" href="telegram.cgi"><% page_label telegram %></a></li>
							<li><a class="dropdown-item" href="ntfy.cgi"><% page_label ntfy %></a></li>
							<li><h6 class="dropdown-header">Networking</h6></li>
							<li><a class="dropdown-item" href="https://openipc.cloud">P2P network</a></li>
							<li><a class="dropdown-item" href="vtun.cgi"><% page_label vtun %></a></li>
							<li><a class="dropdown-item" href="wireguard.cgi"><% page_label wireguard %></a></li>
							<li><a class="dropdown-item" href="proxy.cgi"><% page_label proxy %></a></li>
						</ul>
					</li>
					<li class="nav-item dropdown d-none" id="cam-switch">
						<a aria-expanded="false" class="nav-link dropdown-toggle d-inline-flex align-items-center gap-1" data-bs-toggle="dropdown" href="#" id="cam-switch-toggle" role="button">
							<svg xmlns="http://www.w3.org/2000/svg" width="15" height="15" fill="currentColor" viewBox="0 0 16 16" aria-hidden="true">
								<path d="M0 5a2 2 0 0 1 2-2h7.5a2 2 0 0 1 1.983 1.738l3.11-1.382A1 1 0 0 1 16 4.269v7.462a1 1 0 0 1-1.406.913l-3.111-1.382A2 2 0 0 1 9.5 13H2a2 2 0 0 1-2-2z"/>
							</svg>
							<span class="fw-semibold" id="cam-switch-name">This camera</span>
							<span class="small fw-normal" id="cam-switch-count"></span>
						</a>
						<ul aria-labelledby="cam-switch-toggle" class="dropdown-menu dropdown-menu-end" id="cam-switch-menu"></ul>
					</li>
					<li class="nav-item nav-push-end"><a class="nav-link" data-camera="<% attr_escape "$network_hostname" %>" href="#" id="nav-share" title="Share this camera by link">Share</a></li>
					<li class="nav-item"><a class="nav-link" href="#" id="nav-logout" title="Sign out">Sign out</a></li>
					<% fi %>
				</ul>
			</div>
		</div>
	</nav>

<%
if { [ -f /etc/crash/pending ] || [ -f /etc/crash/failsafe ]; } && [ "$pagename" != "crashlog" ]; then %>
	<div class="container mt-3">
		<div class="alert alert-warning d-flex align-items-center justify-content-between flex-wrap gap-2 mb-0" role="alert">
			<span><strong>This camera recovered from a crash.</strong> Open the crash report.</span>
			<a class="btn btn-sm btn-warning" href="crashlog.cgi">View crash report</a>
		</div>
	</div>
<% fi %>

<% if [ -n "$full_bleed" ]; then %>
	<main class="mj-fullbleed">
		<div class="container">
<% else %>
	<main class="pb-4">
		<div class="container" style="min-height: 85vh">
			<div class="row mt-1 x-small">
<% if [ -z "$OPENIPC_ANYKA_WEBUI" ]; then %>
				<div class="col-lg-2">
					<div id="pb-memory" class="progress my-1" role="progressbar" aria-valuenow="0" aria-valuemin="0" aria-valuemax="100"><div class="progress-bar"></div></div>
					<div id="pb-overlay" class="progress my-1" role="progressbar" aria-valuenow="0" aria-valuemin="0" aria-valuemax="100"><div class="progress-bar"></div></div>
				</div>
<% fi %>
				<div class="col-md-6 mb-2">
					<% [ -z "$hide_signature" ] && signature %>
				</div>

				<div class="col-md-6 col-lg-4 mb-2 text-end">
					<div><time id="time-local"></time><span id="clock-drift" role="status" aria-live="polite"></span></div>
					<div class="text-secondary" id="soc-temp"></div>
				</div>
			</div>
<% fi %>


<% if [ -z "$network_gateway" ]; then %>
<% notice warn '<b>No default gateway</b> &mdash; nothing outside the local network is reachable from this camera.' '<a class="btn btn-sm btn-primary" href="network.cgi">Network settings</a>' %>
<% fi %>

<% if [ "${network_macaddr#00:00:23:34:45:}" != "$network_macaddr" ] && [ -f /etc/shadow- ] && [ -n $(grep root /etc/shadow- | cut -d: -f2) ]; then %>
<% if command -v get_mac >/dev/null 2>&1; then mac_advice="Set a different one here."; else mac_advice="Firmware that replaces it at boot is available, and upgrading is the lasting fix."; fi %>
<% notice danger "<b>This camera's MAC address is a placeholder</b> &mdash; <code>$(esc "$network_macaddr")</code> is one of the addresses a bootloader hands out when the camera's own was never set, and two cameras carrying it on one network will collide. $mac_advice" '<a class="btn btn-sm btn-primary" href="network.cgi#mac">Set the MAC address</a>' %>
<% fi %>

<% if [ -z "$OPENIPC_ANYKA_WEBUI" ] && [ ! -e "$(get_config)" ]; then %>
<% notice danger '<b>No camera configuration found</b> &mdash; there is no settings file for the camera to read.' '<a class="btn btn-sm btn-primary" href="config.cgi">Configuration file</a>' %>
<% fi %>

<% if [ -z "$OPENIPC_ANYKA_WEBUI" ] && [ "$pagename" != "update" ]; then %>
<div id="update-notice"
     data-mj-version="<% attr_escape "$mj_version" %>"
     data-soc-vendor="<% attr_escape "$soc_vendor" %>"></div>
<% fi %>

<div id="storage-notice"></div>

<% if [ -e /tmp/system-reboot ] || [ -n "$restart_pending" ]; then %>
<% notice warn '<b>Settings are waiting for a restart</b> &mdash; video and recording stop for about half a minute while the camera comes back.' '<form method="post" action="restart.cgi" class="d-inline"><button type="submit" class="btn btn-sm btn-danger confirm" data-confirm="Restart the camera now?&#10;&#10;Settings are kept. Video and recording stop for about half a minute while it comes back.">Restart camera</button></form>' %>
<% fi %>

<% if [ -z "$hide_title" ]; then %><h2><%= $page_title %></h2><% fi %>
<% log_read %>
<% if [ -n "$full_bleed" ]; then %>
	</div>
<% fi %>
