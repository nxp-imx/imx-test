/*
 * Copyright 2017 NXP.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <asm/types.h>
#include <linux/types.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include "cec-funcs.h"
#include "cec.h"

struct cec_node {
	int fd_cec;
	int vendor;
	int hdmi_id;
	int cmd;
};

int process_cmdline(struct cec_node *node, int argc, char **argv)
{
	int i;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-i") == 0) {
			node->hdmi_id = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-v") == 0) {
			node->vendor = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-c") == 0) {
			node->cmd = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-h") == 0) {
			printf("*****************************************\n");
			printf("mx8_cec_test -i <hdmi port> -v <tv brand> -c <test cmd>\n");
			printf("Port: which hdmi port connected in TV\n");
			printf("       default port is 4\n");
			printf("brand: default is sony\n");
			printf("       brand = 0,  SONY\n");
			printf("       brand = 1,  LG\n");
			printf("CMD:  Only support two cmd\n");
			printf("       192: 0xc0, start arc\n");
			printf("       54:  0x36,  TV standby\n");
			printf("*****************************************\n");
			return -1;
		}
	}
	return 0;
}


int cec_transmit(struct cec_node *node,  struct cec_msg *msg)
{
	int ret;
	char from   = cec_msg_initiator(msg);
	char to     = cec_msg_destination(msg);
	char opcode = cec_msg_opcode(msg);

	printf("send  from %x to %x, %x\n", from, to, opcode);

	ret = ioctl(node->fd_cec, CEC_TRANSMIT, msg);
	if (ret < 0)
		printf("msg %d failed\n", msg->msg[1]);

	return ret;
}

int processmsg(struct cec_node *node, struct cec_msg *msg)
{
	int ret = 0;
	struct cec_msg tx_msg;

	switch (msg->msg[1]) {

	case CEC_MSG_SET_OSD_STRING:
		break;

	case CEC_MSG_GIVE_DEVICE_POWER_STATUS:
		cec_msg_set_reply_to(&tx_msg, msg);

		cec_msg_report_power_status(&tx_msg, CEC_OP_POWER_STATUS_ON);

		return cec_transmit(node, &tx_msg);

	case CEC_MSG_GIVE_DECK_STATUS:
		cec_msg_set_reply_to(&tx_msg, msg);

		cec_msg_deck_status(&tx_msg, CEC_OP_DECK_INFO_PLAY);

		return cec_transmit(node, &tx_msg);

	case CEC_MSG_SYSTEM_AUDIO_MODE_REQUEST:
		cec_msg_set_reply_to(&tx_msg, msg);

		cec_msg_set_system_audio_mode(&tx_msg, CEC_OP_SYS_AUD_STATUS_OFF);

		return cec_transmit(node, &tx_msg);

	case CEC_MSG_GIVE_SYSTEM_AUDIO_MODE_STATUS:
		cec_msg_set_reply_to(&tx_msg, msg);

		cec_msg_system_audio_mode_status(&tx_msg, CEC_OP_SYS_AUD_STATUS_ON);

		return cec_transmit(node, &tx_msg);

	case CEC_MSG_GIVE_AUDIO_STATUS:
		cec_msg_set_reply_to(&tx_msg, msg);

		cec_msg_report_audio_status(&tx_msg, 0, 100);

		return cec_transmit(node, &tx_msg);

	case CEC_MSG_REQUEST_ARC_INITIATION:
		cec_msg_set_reply_to(&tx_msg, msg);

		cec_msg_initiate_arc(&tx_msg, 1);

		cec_transmit(node, &tx_msg);

		break;

	case CEC_MSG_REPORT_ARC_INITIATED:
		break;

	case CEC_MSG_VENDOR_COMMAND:
		break;

	case CEC_MSG_DEVICE_VENDOR_ID:
		break;
	}

	return ret;
}

int main(int argc, char *argv[])
{
	int    fd_cec = 0;
	char   cec_device[12] = "/dev/cec0";
	struct cec_caps caps;
	struct cec_log_addrs log_addrs;
	struct cec_msg msg, tx_msg;
	struct cec_node node;
	short  phyaddr;
	int    mode = CEC_MODE_INITIATOR | CEC_MODE_FOLLOWER;
	int    ret;
	int  time = 0;

	node.hdmi_id = 4;
	node.vendor  = 0;
	node.cmd  = 0xc0;

	if (process_cmdline(&node, argc, argv) < 0)
		return -1;

	node.fd_cec = open(cec_device, O_RDWR, 0);
	if (node.fd_cec < 0) {
		printf("Unable to open %s\n", cec_device);
		return -1;
	}

	ret = ioctl(node.fd_cec, CEC_ADAP_G_CAPS, &caps);
	if (ret < 0) {
		printf("get cec capss failed, %d\n", ret);
		return -1;
	}

	printf("device[%s], name[%s], LA[%d]\n", caps.driver, caps.name, caps.available_log_addrs);

	ret = ioctl(node.fd_cec, CEC_ADAP_G_PHYS_ADDR, &phyaddr);
	if (ret)
		printf("get phyaddr failed\n");

	printf("phy %x\n", phyaddr);

	phyaddr = node.hdmi_id << 12;
	ret = ioctl(node.fd_cec, CEC_ADAP_S_PHYS_ADDR, &phyaddr);
	if (ret < 0) {
		printf("set cec phy addr failed, %d\n", ret);
		return -1;
	}

	printf("set phy addr success\n");

	if (node.vendor == 0)
		log_addrs.vendor_id = 0xABCD;
	else
		log_addrs.vendor_id = 0xe091;

	log_addrs.cec_version = CEC_OP_CEC_VERSION_1_4;
	log_addrs.primary_device_type[0] = CEC_OP_PRIM_DEVTYPE_AUDIOSYSTEM;
	log_addrs.log_addr_type[0] = CEC_LOG_ADDR_TYPE_AUDIOSYSTEM;
	log_addrs.log_addr[0] = 0x5;
	log_addrs.num_log_addrs = 1;
	log_addrs.osd_name[0] = 'f';
	log_addrs.osd_name[1] = 's';
	log_addrs.osd_name[2] = 'l';
	log_addrs.osd_name[3] = '\0';
	log_addrs.all_device_types[0] = CEC_OP_PRIM_DEVTYPE_AUDIOSYSTEM;
	log_addrs.features[0][0] =  0x2;
	log_addrs.features[0][1] =  0x2;

	ret = ioctl(node.fd_cec, CEC_ADAP_S_LOG_ADDRS, &log_addrs);
	if (ret < 0)
		printf("set cec log addr failed\n");
	else
		printf("set log_addr success\n");

	ret = ioctl(node.fd_cec, CEC_S_MODE, &mode);
	if (ret)
		printf("cec_s_mode failed\n");

	switch (node.cmd) {
	case 0xc0:
		while (1) {

			msg.timeout = 1;
			ret = ioctl(node.fd_cec, CEC_RECEIVE, &msg);
			if (ret) {
				if (node.vendor != 0 && time == 0) {
					tx_msg.msg[0] = 0x50;
					cec_msg_initiate_arc(&tx_msg, 1);
					cec_transmit(&node, &tx_msg);
					time++;
				}
				continue;
			}
			char from = cec_msg_initiator(&msg);
			char to   = cec_msg_destination(&msg);
			char opcode = cec_msg_opcode(&msg);

			printf("Received  from %x to %x, %x, %x\n", from, to, opcode, msg.msg[2]);
			processmsg(&node, &msg);

		}
		break;

	case 0x36:
		tx_msg.msg[0] = 0x50;
		cec_msg_standby(&tx_msg);
		cec_transmit(&node, &tx_msg);
		break;
	default:
		break;
	}

	if (fd_cec)
		close(fd_cec);
	return 0;
}
