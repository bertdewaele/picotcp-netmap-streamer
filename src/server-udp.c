/*
 * Copyright (C) 2014 jibi <jibi@paranoici.org>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#define _GNU_SOURCE
#define NETMAP_WITH_LIBS

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <net/netmap_user.h>
#include <netinet/ether.h>
#include <sys/ioctl.h>

#include <pico_ipv4.h>
#include <pico_stack.h>
#include <pico_socket.h>

#include <cv.h>
#include <highgui.h>
#include <videostream.h>
#include <time.h>
#include <getopt.h>

#define BSIZE   2048
#define NUMBER_OF_ATTRIB 4
#define REQUEST_STRING "request stream"
#define REQUEST_LENGTH (sizeof(REQUEST_STRING))

static unsigned char* raw_image_data = NULL;
static unsigned char* data_ptr = NULL;
static unsigned char* end_ptr = NULL;

static uint32_t peer= 0;
static uint16_t port= 0;

static uint8_t new_connect=1;

static uint32_t WIDTH= -1, HEIGHT= -1, NCHANN= -1, DEPTH= -1, IMAGE_SIZE=-1;

#ifdef DEBUG_STREAM
#define DEBUG(x) printf x
#else
#define DEBUG(x) do {} while (0)
#endif


struct {
	char *if_mac;
	char *if_name;
	char *if_addr;
	char *port;
	int cam_device;
	double scale_factor;
	int color_disable;
} config;

void setup_udp_app();
void pico_netmap_destroy(struct pico_device *dev);
struct pico_device *pico_netmap_create(char *interface, char *name, uint8_t *mac);

void
free_resources(void)
{
	if(raw_image_data){
		free(raw_image_data);
	}

	clean_up_stream();
}


int
send_udpimg(struct pico_socket* s) {

	while(data_ptr < end_ptr)
		{
			pico_stack_tick();

			int bytes = pico_socket_sendto(s, (void*)data_ptr, (int)(end_ptr-data_ptr), &peer, port);
			//DEBUG("packet #%i send.\n", ++packet_counter);

			if(bytes < 0) {
				printf("Pico socket write failed. error = %i\n", pico_err);
				break;
			}

			//DEBUG("%i bytes sent.\n", bytes);

			data_ptr += bytes;

		}
	return (data_ptr == end_ptr)?1:0;
}


void send_image_info(struct pico_socket *s)
{

	IplImage* temp= grab_image(config.scale_factor, config.color_disable);
	int bytes=-1;
	uint16_t image_attrib[NUMBER_OF_ATTRIB];

	image_attrib[0]= temp->width;
	image_attrib[1]= temp->height;
	image_attrib[2]= temp->nChannels;
	image_attrib[3]= temp->depth;

	bytes= pico_socket_sendto(s, (void*)image_attrib, NUMBER_OF_ATTRIB*sizeof(image_attrib[0]), &peer, port);
	if(bytes < 0) {
		printf("Pico socket write failed. error = %i\n", pico_err);
		exit(-1);
	}

	printf("\n\nimg WIDTH: %i\n", temp->width);
	printf("img HEIGHT: %i\n", temp->height);
	printf("img nchannels: %i\n", temp->nChannels);
	printf("img depth: %i\n\n", temp->depth);


	printf("info about img sent\n");
}

uint8_t is_valid_request(struct pico_socket *s)
{
	char recvbuf[REQUEST_LENGTH+1]; // +1 for terminator
	int read=-1;

	read = pico_socket_recvfrom(s, recvbuf, REQUEST_LENGTH, &peer, &port);
	if (read < 0)
		{

			printf("Pico socket recvfrom failed. error = %i\n", pico_err);
			printf("pico_err: %s\n", strerror(pico_err));
			return -1;
		}

	recvbuf[read]='\n';


	if (!(strncmp(recvbuf, REQUEST_STRING, strlen(REQUEST_STRING)) == 0))
		{
			printf("Wrong request string\n");
			return -1;
		}
	return 1;
}


void
cb_udpconnect(uint16_t ev, struct pico_socket *s) {

	if (ev & PICO_SOCK_EV_ERR) {
		printf("Socket error received: %s. Bailing out.\n", strerror(pico_err));
		exit(1);
	}

	if (ev & PICO_SOCK_EV_RD) {
		int imgsize= 0;
		int r=-1;

		if (!is_valid_request(s))
			{
				printf("Bad request\n");
				exit(-1);
			}

		send_image_info(s);
		printf("Sending stream....\n");

		do{
			raw_image_data = grab_raw_data(config.scale_factor, config.color_disable, &imgsize);

			if(!raw_image_data) {
				exit(-1);
			}

			data_ptr = raw_image_data;
			end_ptr = raw_image_data + imgsize;

			r= send_udpimg(s);


			int count= 10;

			while( --count>0)
				{
					pico_stack_tick();
				}
			//exit(0); // only send one image

		}
		while(1);
	}
}

void
setup_udp_app() {
	struct pico_socket *listen_socket;
	struct pico_ip4 address;
	uint16_t port;
	int ret;

	port = short_be(atoi(config.port));
	bzero(&address, sizeof(address));

	listen_socket = pico_socket_open(PICO_PROTO_IPV4, PICO_PROTO_UDP, &cb_udpconnect);
	if (!listen_socket) {
		printf("cannot open socket: %s", strerror(pico_err));
		exit(1);
	}

	ret = pico_socket_bind(listen_socket, &address, &port);
	if (ret < 0) {
		printf("cannot bind socket to port %u: %s", short_be(port), strerror(pico_err));
		exit(1);
	}


	return;
}

struct pico_device_netmap {
	struct pico_device dev;
	struct nm_desc *conn;
};

int
pico_netmap_send(struct pico_device *dev, void *buf, int len) {
	struct pico_device_netmap *netmap = (struct pico_device_netmap *) dev;

	return nm_inject(netmap->conn, buf, len);
}

void
pico_dev_netmap_cb(u_char *u, const struct nm_pkthdr *h, const uint8_t *buf) {
	struct pico_device *dev = (struct pico_device *) u;

	pico_stack_recv(dev, (uint8_t *) buf, (uint32_t) h->len);
}

int
pico_netmap_poll(struct pico_device *dev, int loop_score) {
	struct pico_device_netmap *netmap;
	struct pollfd fds;

	netmap     = (struct pico_device_netmap *) dev;
	fds.fd     = NETMAP_FD(netmap->conn);
	fds.events = POLLIN;

	do {
		if (poll(&fds, 1, 0) <= 0) {
			return loop_score;
		}

		loop_score -= nm_dispatch(netmap->conn, loop_score, pico_dev_netmap_cb, (u_char *) netmap);
	} while(loop_score > 0);

	return 0;
}

void
pico_netmap_destroy(struct pico_device *dev) {
	struct pico_device_netmap *netmap = (struct pico_device_netmap *) dev;

	nm_close(netmap->conn);
}

struct pico_device *
pico_netmap_create(char *interface, char *name, uint8_t *mac) {
	struct pico_device_netmap *netmap;
	char   ifname[IFNAMSIZ + 7];

	netmap = PICO_ZALLOC(sizeof(struct pico_device_netmap));
	if (!netmap) {
		return NULL;
	}

	if (pico_device_init((struct pico_device *)netmap, name, mac)) {
		pico_netmap_destroy((struct pico_device *)netmap);
		return NULL;
	}

	sprintf(ifname, "netmap:%s", interface);

	netmap->dev.overhead = 0;
	netmap->conn         = nm_open(ifname, NULL, 0, 0);

	if (! netmap->conn) {
		pico_netmap_destroy((struct pico_device *)netmap);
		return NULL;
	}

	netmap->dev.send    = pico_netmap_send;
	netmap->dev.poll    = pico_netmap_poll;
	netmap->dev.destroy = pico_netmap_destroy;

	return (struct pico_device *) netmap;
}

void
init_picotcp() {
	struct ether_addr mac;
	struct pico_device *dev = NULL;
	struct pico_ip4 addr;
	struct pico_ip4 netm;

	ether_aton_r(config.if_mac, &mac);

	pico_stack_init();

	dev = pico_netmap_create(config.if_name, "eth_if", (uint8_t *) &mac);

	pico_string_to_ipv4(config.if_addr,  &addr.addr);
	pico_string_to_ipv4("255.255.255.0", &netm.addr);
	pico_ipv4_link_add(dev, addr, netm);
}

int
main(int argc, char *argv[]) {
	char c= 0;
	int option_index= 0;
	int req_arg_count=0;
	printf("starting....\n");

	//default init of optional args
	config.scale_factor=1.0;
	config.color_disable=0;

	static struct option long_options[]=
		{
			{"if_name", required_argument, 0, 'i'},
			{"if_mac", required_argument, 0, 'm'},
			{"if_addr", required_argument, 0, 'a'},
			{"port", required_argument, 0, 'p'},

			{"cam_device", required_argument, 0, 'd'},
			{"scale_factor", required_argument, 0, 's'},
			{"color_disable", required_argument, 0, 'c'},
			{0, 0, 0, 0}


		};

	while ( (c = getopt_long (argc, argv, "i:m:a:p:d:s:c:",
							  long_options, &option_index)) != -1)
		{

			switch (c)
				{
				case 'i':
					config.if_name = optarg;
					req_arg_count++;
					break;

				case 'm':
					config.if_mac  = optarg;
					req_arg_count++;
					break;

				case 'a':
					config.if_addr = optarg;
					req_arg_count++;
					break;

				case 'p':
					req_arg_count++;
					config.port    = optarg;
					break;

				case 'd':
					config.cam_device = strtol(optarg, NULL, 10);
					req_arg_count++;
					break;

				case 's':
					sscanf(optarg, "%lf", &(config.scale_factor));
					break;

				case 'c':
					config.color_disable = strtol(optarg, NULL, 10);
					break;

				default:
					abort ();
				}
		}
	if (req_arg_count !=5)
		{
			printf("Not enough required arguments: --if_name --if_mac --if_addr --port --cam_device (--scale_factor --color_disable)\n");
			exit(-1);
		}
	printf("args parsed\n");

	if (setup_capture(config.cam_device, config.scale_factor, config.color_disable))
		exit(-2);

	init_picotcp();
	setup_udp_app();

	printf("started.\n");
	pico_stack_loop();

	free_resources();

	return 0;
}
