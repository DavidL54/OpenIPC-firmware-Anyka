#define _GNU_SOURCE
#include <dlfcn.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "ak_video.h"
#include "ak_rtsp_server.h"

int printf(const char *format, ...)
{
	int (*write_formatted)(const char *, va_list) = dlsym(RTLD_NEXT, "vprintf");
	if (!write_formatted)
		_exit(1);
	if (strncmp(format, "pMgr->iListenFd=", sizeof("pMgr->iListenFd=") - 1) == 0)
		return 0;
	va_list arguments;
	va_start(arguments, format);
	int result = write_formatted(format, arguments);
	va_end(arguments);
	return result;
}

int ak_app_video_bind_chn(const APP_CHN_S *source, const APP_CHN_S *destination, const APP_BIND_PARAM *input)
{
	int (*bind)(const APP_CHN_S *, const APP_CHN_S *, const APP_BIND_PARAM *) = dlsym(RTLD_NEXT, "ak_app_video_bind_chn");
	if (!bind)
		_exit(1);
	if (!input || input->frame_depth <= 2)
		return bind(source, destination, input);
	APP_BIND_PARAM settings = *input;
	settings.frame_depth = 2;
	return bind(source, destination, &settings);
}

int rtsp_server_init_param(rtsp_init_param *input)
{
	int (*initialize)(rtsp_init_param *) = dlsym(RTLD_NEXT, "rtsp_server_init_param");
	if (!initialize)
		_exit(1);
	rtsp_init_param settings = {0};
	if (input)
		settings = *input;
	settings.iServerPort = 8555;
	memset(settings.cUserName, 0, sizeof(settings.cUserName));
	memset(settings.cPassword, 0, sizeof(settings.cPassword));
	return initialize(&settings);
}

int bind(int descriptor, const struct sockaddr *address, socklen_t length)
{
	int (*original)(int, const struct sockaddr *, socklen_t) = dlsym(RTLD_NEXT, "bind");
	if (!original)
		_exit(1);
	if (address && address->sa_family == AF_INET && length >= sizeof(struct sockaddr_in)) {
		struct sockaddr_in local = *(const struct sockaddr_in *)address;
		if (ntohs(local.sin_port) == 554 || ntohs(local.sin_port) == 8555) {
			local.sin_port = htons(8555);
			local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			return original(descriptor, (const struct sockaddr *)&local, sizeof(local));
		}
	}
	if (address && address->sa_family == AF_INET6 && length >= sizeof(struct sockaddr_in6)) {
		struct sockaddr_in6 local = *(const struct sockaddr_in6 *)address;
		if (ntohs(local.sin6_port) == 554 || ntohs(local.sin6_port) == 8555) {
			local.sin6_port = htons(8555);
			memset(&local.sin6_addr, 0, sizeof(local.sin6_addr));
			local.sin6_addr.s6_addr[15] = 1;
			return original(descriptor, (const struct sockaddr *)&local, sizeof(local));
		}
	}
	return original(descriptor, address, length);
}