/*
 * Copyright 2004-2014 Freescale Semiconductor, Inc. All rights reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "../../include/test_utils.h"

#define LOOPBACK	0x8000
#define MESSAGE		"Test\0"
#define MESSAGE_SIZE	sizeof(MESSAGE)

int main(int argc, char **argv)
{
	int uart_file1;
	unsigned int line_val;
	char buf[5];
	struct termios mxc, old;
	int retval = 0;
	int retries = 5;

	print_name(argv);

	if (argc == 2) {
		/* Open the specified UART device */
		if ((uart_file1 = open(*++argv, O_RDWR)) == -1) {
			printf("Error opening %s\n", *argv);
			exit(1);
		} else {
			printf("%s opened\n", *argv);
		}
	} else {
		printf("Usage: mxc_uart_test <UART device name>\n");
		exit(1);
	}

	tcgetattr(uart_file1, &old);
	mxc = old;
	mxc.c_lflag &= ~(ICANON | ECHO | ISIG);
	tcsetattr(uart_file1, TCSANOW, &mxc);
	printf("Attributes set\n");

	line_val = LOOPBACK;
	ioctl(uart_file1, TIOCMSET, &line_val);
	printf("Test: IOCTL Set\n");

	tcflush(uart_file1, TCIOFLUSH);

	write(uart_file1, MESSAGE, MESSAGE_SIZE);
	printf("Data Written= %s\n", MESSAGE);

	sleep(1);
	memset(buf, 0, MESSAGE_SIZE);
	while (retries-- && retval < 5)
		retval += read(uart_file1, buf + retval, MESSAGE_SIZE - retval);
	printf("Data Read back= %s\n", buf);
	sleep(2);
	ioctl(uart_file1, TIOCMBIC, &line_val);

	retval = tcsetattr(uart_file1, TCSAFLUSH, &old);

	close(uart_file1);

	if (memcmp(buf, MESSAGE, MESSAGE_SIZE)) {
		printf("Data read back %s is different than data sent %s\n",
			buf, MESSAGE);
		print_result(argv);
		return 1;
	}

	print_result(argv);
	return 0;
}
