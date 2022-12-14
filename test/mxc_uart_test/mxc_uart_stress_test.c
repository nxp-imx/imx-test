/*
 * Copyright (C) 2014 Freescale Semiconductor, Inc. All rights reserved.
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
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <asm-generic/ioctls.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include "../../include/test_utils.h"

#define TIOCM_LOOP      0x8000
#define CHUNK_SIZE_DEFAULT 1024

static int log_out = 0;
static int loopflag = 0;

static int uart_speed(int s)
{
	switch (s) {
	case 9600:
		return B9600;
	case 19200:
		return B19200;
	case 38400:
		return B38400;
	case 57600:
		return B57600;
	case 115200:
		return B115200;
	case 230400:
		return B230400;
	case 460800:
		return B460800;
	case 500000:
		return B500000;
	case 576000:
		return B576000;
	case 921600:
		return B921600;
	case 1000000:
		return B1000000;
	case 1152000:
		return B1152000;
	case 1500000:
		return B1500000;
	case 2000000:
		return B2000000;
	case 2500000:
		return B2500000;
	case 3000000:
		return B3000000;
	case 3500000:
		return B3500000;
	case 4000000:
		return B4000000;
	default:
		return B0;
	}
}

int set_speed(int fd, struct termios *ti, int speed)
{
	cfsetospeed(ti, uart_speed(speed));
	cfsetispeed(ti, uart_speed(speed));
	if (tcsetattr(fd, TCSANOW, ti) < 0) {
		perror("Can't set speed");
		return -1;
	}

	/* wait baud rate stable */
	if (speed < 230400)
		usleep(500000);

	printf("Speed set to %d\n", speed);
	return 0;
}

int set_flow_control(int fd, struct termios *ti, int flow_control)
{
	if (flow_control)
		ti->c_cflag |= CRTSCTS;
	else
		ti->c_cflag &= ~CRTSCTS;

	if (tcsetattr(fd, TCSANOW, ti) < 0) {
		perror("Can't set flow control");
		return -1;
	}
	printf("%sUsing flow control\n", (flow_control? "" : "NOT "));
	return 0;
}

int init_uart(char *dev, struct termios *ti)
{
	int fd;
	int pins;

	fd = open(dev, O_RDWR |  O_NOCTTY);
	if (fd < 0) {
		perror("Can't open serial port");
		return -1;
	}

	tcflush(fd, TCIOFLUSH);

	ioctl(fd, TIOCMGET, &pins);
	if (loopflag)
		pins |= TIOCM_LOOP;
	else
		pins &= ~TIOCM_LOOP;
	ioctl(fd, TIOCMSET, &pins);

	if (tcgetattr(fd, ti) < 0) {
		perror("Can't get port settings");
		close(fd);
		return -1;
	}

	cfmakeraw(ti);
	printf("Serial port %s opened\n", dev);
	return fd;
}
#define STR(X) #X
#define XSTR(X) STR(X)


int CHUNK_SIZE = CHUNK_SIZE_DEFAULT;
int CHUNKS = 100;

void* r_op(void *arg)
{
	int fd = *(int*)arg;
	int nfds;
	char tmp[CHUNK_SIZE_DEFAULT];
	int i = 0, k = 0, ret;
	size_t total = 0;
	int count;
	fd_set rfds;
	struct timeval tv;
	int res = 0;

	nfds = fd;

	/* init buffer */
	for (i = 0; i < CHUNK_SIZE; i++)
		tmp[i] = (i + 1) % 0x100;
	i = 0;

	while (total < (CHUNK_SIZE * CHUNKS) && k < 10)
	{
		i++;

		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		tv.tv_sec = 15;
		tv.tv_usec = 0;

		ret = select(nfds + 1, &rfds, NULL, NULL, &tv);

		/* clear it first */
		memset(tmp, 0, sizeof(tmp));

		if (ret == -1) {
			perror("select");
			count = 0;
		} else if (ret) {
			count = read(fd, tmp, CHUNK_SIZE);
		} else {
			printf("timeout\n");
			k++;
			count = 0;
		}

#ifdef ANDROID
		if (count > SSIZE_MAX)
#else
		if (count < 0)
#endif
		{
			printf("\nError while read: %d attempt %d\n",
				(int)count, i);
			k++;
		} else {
			total += count;

			int v;
			static int sum = 0;
			int bad = 0;

			for (v = 0; v < CHUNK_SIZE && v < count; v++, sum++) {
				if (tmp[v] == (((sum % CHUNK_SIZE) + 1) % 0x100)) {
					continue;
				} else {
					bad = 1;
					k++;
					printf("%d (should be %x): but %x\n",
						v, ((sum % CHUNK_SIZE) + 1) % 0x100, tmp[v]);
				}
			}

			if (count > 0 && log_out)
				printf(":)[ %d ] READ is %s, count : %d\n",
					i, bad ? "bad!!!" : "good.", count);
			else if (count == 0)
				printf("We read 0 byte\n");

			if (count == 0)
				k++;
		}
	}
	printf("\nread done : total : %zd\n", total);
	if (k > 0) {
		printf("too many errors or empty read\n");
		res = -1;
	}

	if (i == CHUNKS)
		printf("\t read : %d calls issued, %d bytes per call\n",
			CHUNKS, CHUNK_SIZE);
	else
		printf("\t%d read calls isued\n"
			"\t total : %zd\n",
			i, total);
	*(int*)arg = res;
	return arg;
}

void* w_op(void *arg)
{
	int fd = *(int *)arg;
	char tmp[CHUNK_SIZE_DEFAULT];
	int i = 0, k = 0;
	size_t total = 0;
	int count;
	int res = 0;

	/* init buffer */
	for (i = 0; i < CHUNK_SIZE; i++)
		tmp[i] = (i + 1) % 0x100;
	i = 0;

	while (total < (CHUNK_SIZE * CHUNKS) && k < 10)
	{
		i++;
		count = write(fd, tmp, CHUNK_SIZE);

#ifdef ANDROID
		if (count > SSIZE_MAX)
#else
		if (count < 0 || count != CHUNK_SIZE)
#endif
		{
			printf("\nError while write: %d attempt %d\n",
				(int)count, i);
			k++;
		} else {
			total += count;
			if (count == 0)
				k++;
		}
	}
	printf("\nwrite done : total : %zd\n", total);
	if (k > 0) {
		printf("too many errors or empty write\n");
		res = -1;
	}

	if (i == CHUNKS)
		printf("\t write : %d calls issued, %d bytes per call\n",
			CHUNKS, CHUNK_SIZE);
	else
		printf("\t%d write calls isued\n"
			"\t total : %zd\n",
			i, total);
	*(int*)arg = res;
	return arg;
}

int real_op_loop(int fd)
{
	pthread_t rid, wid;
	int ret, arg_r = fd, arg_w = fd;
	int *res;

	ret = pthread_create(&rid, NULL, &r_op, &arg_r);
	if (ret != 0) {
		printf("Create read thread failed\n");
		return -1;
	}

	ret = pthread_create(&wid, NULL, &w_op, &arg_w);
	if (ret != 0) {
		printf("Create write thread failed\n");
		return -1;
	}

	ret = pthread_join(rid, (void**)&res);
	if (ret || *res) {
		printf("Read thread had failures\n");
		return -1;
	}

	ret = pthread_join(wid, (void**)&res);
	if (ret || *res) {
		printf("Write thread had failures\n");
		return -1;
	}

	return 0;
}

int real_op_duplex(int fd)
{
	return real_op_loop(fd);
}

int real_op(int fd, char op)
{
	char tmp[CHUNK_SIZE_DEFAULT];
	int i = 0, k = 0, r;
	size_t count, total = 0;
	char *opstr = (op == 'R' ? "reading" : "writing");
	int flags;
	/*
	int nfds;
	fd_set rfds;
	struct timeval tv;


	nfds = fd;
	tv.tv_sec = 15;
	tv.tv_usec = 0;
	*/

	printf("Hit enter to start %s\n", opstr);
	flags = read(STDIN_FILENO, tmp, 3);
	if (flags < 0)
		return flags;

	/* init buffer */
	if (op == 'W') {
		for (i = 0; i < CHUNK_SIZE; i++)
			tmp[i] = (i + 1) % 0x100;
		i = 0;
	}

	while (total < (CHUNK_SIZE * CHUNKS) && k < 10)
	{
		i++;
		if (op == 'R') {
			/*
			FD_ZERO(&rfds);
			FD_SET(fd, &rfds);
			r = select(nfds + 1, &rfds, NULL, NULL, &tv);
			*/
			r = 1;
			/* clear it first */
			memset(tmp, 0, sizeof(tmp));
			if (r > 0)
				count = read(fd, tmp, CHUNK_SIZE);
			else {
				printf("timeout\n");
				count = 0;
			}
		} else {
			count = write(fd, tmp, CHUNK_SIZE);
			if (log_out)
				printf("tx:%zd\n", count);
		}

#ifdef ANDROID
		if (count > SSIZE_MAX)
#else
		if (count < 0)
#endif
		{
			printf("\nError while %s: %d attempt %d\n",
				opstr, (int)count, i);
			k++;
		} else {
			total += count;

			if (op == 'R') {
				int v;
				static int sum = 0;
				int bad = 0;

				for (v = 0; v < CHUNK_SIZE && v < count; v++, sum++) {
					if (tmp[v] == (((sum % CHUNK_SIZE) + 1) % 0x100)) {
						continue;
					} else {
						bad = 1;
						k++;
						printf("%d (should be %x): but %x\n",
							v, ((sum % CHUNK_SIZE) + 1) % 0x100, tmp[v]);
					}
				}

				if (count > 0 && (log_out || bad))
					printf(":)[ %d ] READ is %s, count : %zd\n",
						i, bad ? "bad!!!" : "good.", count);
				else if (count == 0)
					printf("We read 0 byte\n");
			}
			if (count == 0)
				k++;
		}
	}
	printf("\ndone : total : %zd\n", total);

	if (k > 0) {
		printf("too many errors or empty %ss\n", opstr);
		return -1;
	}

	if (i == CHUNKS)
		printf("\t %s : %d calls issued, %d bytes per call\n",
			opstr, CHUNKS, CHUNK_SIZE);
	else
		printf("\t%d %s calls isued\n"
			"\t total : %zd\n",
			i, opstr, total);
	return 0;
}

int main(int argc, char* argv[])
{
	struct termios ti;
	int fd, ret;
	int flow_control = 0;
	char op;

	print_name(argv);
	if (argc < 8) {
		printf("Usage:\n\t%s <PORT> <BAUDRATE> <F> <R/W/L/D> <X> <Y> <O>\n"
			"<PORT>: like /dev/ttymxc4\n"
			"<BAUDRATE>: the baud rate number value (0~4000000)\n"
			"<F>: enable flow control; other chars: disable flow control\n"
			"<R/W/L/D>: R: read test; W: write test; L:loopback test; D: full duplex test\n"
			"<X> : the group number: >= 1\n"
			"<Y> : the group size: 1 - 1000\n"
			"<O> : print log; other chars: disable log\n\n"
			"For example:\n"
			"in mx23, send one byte:\n"
			"         ./mxc_uart_stress_test.out /dev/ttySP1 115200 F W 1 1 O\n\n"
			"in mx28, receive one byte:\n"
			"         ./mxc_uart_stress_test.out /dev/ttySP0 115200 F R 1 1 O\n\n"
			"in mx28, loop one byte (Internal loopback hw flow control disable):\n"
			"         ./mxc_uart_stress_test.out /dev/ttySP0 115200 D L 1 1 O\n\n"
			"in mx28, loop one byte (External loopback hw flow control enable):\n"
			"         ./mxc_uart_stress_test.out /dev/ttySP0 115200 F L 1 1 O\n\n"
			"in mx53-evk(with the extension card), receive one byte:\n"
			"         ./mxc_uart_stress_test.out /dev/ttymxc1 115200 F R 1 1 O\n\n"
			"in mx6, internal loopback test (disable flowcontrol, printout log)"
			"	  ./mxc_uart_stress_test.out /dev/ttymxc4 115200 D L 1000 1000 O\n\n"
			"in mx6, two board single duplex Read test (flowcontrol disable)"
			"	  ./mxc_uart_stress_test.out /dev/ttymxc4 115200 D R 1000 1000 O\n\n"
			"in mx6, two board single duplex Write test (flowcontrol disable)"
			"	  ./mxc_uart_stress_test.out /dev/ttymxc4 115200 D W 1000 1000 O\n\n"
			"in mx6, two board single duplex Read test (flowcontrol enable)"
			"	  ./mxc_uart_stress_test.out /dev/ttymxc4 115200 F R 1000 1000 O\n\n"
			"in mx6, two board single duplex Read test (flowcontrol enable)"
			"	  ./mxc_uart_stress_test.out /dev/ttymxc4 115200 F W 1000 1000 O\n\n"
			"in mx6, two board full duplex R/W test (flowcontrol disable)"
			"	  ./mxc_uart_stress_test.out /dev/ttymxc4 115200 D D 1000 1000 O\n\n"
			"in mx6, two board full duplex R/W test (flowcontrol enable)"
			"	  ./mxc_uart_stress_test.out /dev/ttymxc4 115200 F D 1000 1000 O\n\n"
			,
			argv[0]);
		return 0;
	}

	if ((argv[3][0] & (~0x20))=='F' || (argv[3][0] & (~0x20))=='f')
		flow_control = 1;
	if ((argv[4][0] & (~0x20))=='L' || (argv[4][0] & (~0x20))=='l')
		loopflag = 1;

	CHUNKS = atoi(argv[5]);
	if (CHUNKS < 0) {
		printf("error chunks, %d\n", CHUNKS);
		print_result(argv);
		return 1;
	}

	CHUNK_SIZE = atoi(argv[6]);
	if (CHUNK_SIZE > CHUNK_SIZE_DEFAULT) {
		printf("Chunk size %d not supported, using default %d\n",
			CHUNK_SIZE, CHUNK_SIZE_DEFAULT);
		CHUNK_SIZE = CHUNK_SIZE_DEFAULT;
	}

	if ((argv[7][0] & (~0x20))=='O' || (argv[7][0] & (~0x20))=='o')
		log_out = 1;

	fd = init_uart(argv[1], &ti);
	if (fd < 0) {
		print_result(argv);
		return fd;
	}

	ret = set_flow_control(fd, &ti, flow_control);
	if (ret) {
		close(fd);
		print_result(argv);
		return ret;
	}

	ret = set_speed(fd, &ti, atoi(argv[2]));
	if (ret) {
		close(fd);
		print_result(argv);
		return ret;
	}

	tcflush(fd, TCIOFLUSH);

	op = argv[4][0] & (~0x20);
	if (op == 'L' || op == 'l')
		ret = real_op_loop(fd);
	else if (op == 'D' || op == 'd')
		ret = real_op_duplex(fd);
	else
		ret = real_op(fd, op);

	close(fd);
	print_result(argv);
	return ret;
}
