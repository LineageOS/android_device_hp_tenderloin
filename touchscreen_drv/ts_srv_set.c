/*
 * This is a binary for sending data via socket to the TouchPad's ts_srv
 * touchscreen driver to change settings.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * The code was written from scratch by Dees_Troy dees_troy at
 * yahoo
 *
 * Copyright (c) 2012 CyanogenMod Touchpad Project.
 *
 *
 */

/* Standalone binary for setting mode of operation for the touchscreen
 * Run the binary and supply 1 argument to indicate the mode of operation
 * F = Finger
 * S = Stylus
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

#define TS_SOCKET_LOCATION "/dev/socket/tsdriver"

void send_ts_socket(char *send_data) {
	// Connects to the touchscreen socket
	struct sockaddr_un unaddr;
	int ts_fd, len;

	ts_fd = socket(AF_UNIX, SOCK_STREAM, 0);

	if (ts_fd >= 0) {
		unaddr.sun_family = AF_UNIX;
		strcpy(unaddr.sun_path, TS_SOCKET_LOCATION);
		len = strlen(unaddr.sun_path) + sizeof(unaddr.sun_family);
		if (connect(ts_fd, (struct sockaddr *)&unaddr, len) >= 0) {
			int send_ret;
			send_ret = send(ts_fd, send_data, 1, 0);
			if (send_ret <= 0)
				printf("Unable to send data to socket\n");
			else
				if ((strcmp(send_data, "F") == 0))
					printf("Touchscreen set for finger mode\n");
				else
					printf("Touchscreen set for stylus mode\n");
		} else
			printf("Unable to connect socket\n");
		close(ts_fd);
	} else
		printf("Unable to create socket\n");
}

int main(int argc, char** argv)
{
	if (argc != 2 || strlen(argv[1]) != 1 ||
		(strcmp(argv[1], "F") != 0 && strcmp(argv[1], "S") != 0)) {
		printf("Please supply exactly 1 argument: F for finger or S for stylus\n");
		printf("This is used to set the mode of operation for the touchscreen driver on the TouchPad\n");
		return 0;
	} else
		send_ts_socket(argv[1]);

	return 0;
}