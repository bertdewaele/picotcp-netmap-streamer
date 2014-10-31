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
#include <pico_socket_udp.h>

#include <cv.h>
#include <highgui.h>
#include <getopt.h>
#include <time.h>

#define MTU_UDP_ETH	1472
#define NUMBER_OF_IMG_ATTRIB 4
#define UDP_RECV_BUF_SIZE 1024*64
#define REQUEST_STRING "request stream"

//// TEST

static uint32_t peer= 0;
static uint16_t port= 0;

struct pico_ip_mreq mreq_multicast = {{0},{0}};
//#define DEBUG_STREAM
//// END TEST


#ifdef DEBUG_STREAM
#define DEBUG(x) printf x
#else
#define DEBUG(x) do {} while (0)
#endif

static unsigned char* payload_ptr = NULL;
static unsigned char* raw_data = NULL;
static unsigned char* data_ptr = NULL;
static unsigned char* end_ptr = NULL;
static uint32_t WIDTH= 0, HEIGHT= 0, NCHANN= 0, DEPTH= 0, IMAGE_SIZE=0;
static uint8_t new_connect=1;

static CvSize frame_size= { 0 };
static IplImage *img;

static int is_retrieving_img = 0;
static char* window_name="VIDEO STREAMER";

struct {
	char *if_mac;
	char *if_name;
	char *if_addr;
	char *port;
} config;

void setup_udp_app();
void pico_netmap_destroy(struct pico_device *dev);
struct pico_device *pico_netmap_create(char *interface, char *name, uint8_t *mac);

static void
free_resources(void)
{
	if (raw_data) {
		free(raw_data);
		raw_data = NULL;
	}
}

static void
handle_image_buffer(void)
{
	cvSetData(img, (void*)raw_data, WIDTH*NCHANN);
	cvShowImage(window_name, img);
	cvWaitKey(1);
}

int
recv_udpimg(struct pico_socket* s) {

	//int bytes = pico_socket_read(s, (void*)data_ptr, MTU_UDP_ETH);
	int bytes = pico_socket_recvfrom(s, (void*)data_ptr, MTU_UDP_ETH,  &mreq_multicast.mcast_group_addr.addr , &port);
	if(bytes < 0) {
		printf("Pico socket read failed. error = %i\n", pico_err);
	}

	DEBUG(("Bytes received = %i\n", bytes));
	data_ptr += bytes;
	/////////
	char *end="ENDING";
	if (bytes == strlen(end))
		{
			return 0; // hack

			*data_ptr='\n';
			
			//printf("data ptr: '%s'\n",data_ptr);
			
			if(strncmp(data_ptr, end, strlen(end)) == 0) {
				printf("end received");
				return 0;
			}
		}
	//////////
	return data_ptr < end_ptr ? 1 : 0;
}

void init_image_attrib(struct pico_socket *s)
{
	uint16_t image_attrib[NUMBER_OF_IMG_ATTRIB];
	int bytes = -1;

	bytes = pico_socket_recvfrom(s, (void*)image_attrib, NUMBER_OF_IMG_ATTRIB*sizeof(image_attrib[0]), &peer, &port);
	//bytes = pico_socket_read(s, (void*)image_attrib, NUMBER_OF_IMG_ATTRIB*sizeof(image_attrib[0]));
	if(bytes < 0) {
		printf("Pico socket read failed. error = %i\n", pico_err);
		exit(-1);
	}

	WIDTH=image_attrib[0];
	HEIGHT=image_attrib[1];
	NCHANN=image_attrib[2];
	DEPTH=image_attrib[3];

	printf("\n\nimg WIDTH: %i\n", WIDTH);
	printf("img HEIGHT: %i\n", HEIGHT);
	printf("img nchannels: %i\n", NCHANN);
	printf("img depth: %i\n\n", DEPTH);

	frame_size.width = WIDTH;
	frame_size.height = HEIGHT;
	IMAGE_SIZE= WIDTH*HEIGHT*NCHANN;


	img = cvCreateImageHeader(frame_size, DEPTH, NCHANN);
}


void
cb_udpconnect(uint16_t ev, struct pico_socket *s) {
	
	if (ev & PICO_SOCK_EV_ERR) {
		printf("Socket error received: %s. Bailing out.\n", strerror(pico_err));
		free_resources();
		exit(1);
	}

	if (ev & PICO_SOCK_EV_RD) {
		if (new_connect) {
			init_image_attrib(s);

			///////////testing
			int ret= -1;

			struct pico_ip4 inaddr_dst, inaddr_link;

			pico_string_to_ipv4("224.7.7.7", &inaddr_dst.addr);
			pico_string_to_ipv4("10.0.0.100", &inaddr_link.addr);
			mreq_multicast.mcast_group_addr = inaddr_dst;
			mreq_multicast.mcast_link_addr = inaddr_link;
			ret = pico_socket_setoption(s, PICO_IP_ADD_MEMBERSHIP, &mreq_multicast);
			if (ret < 0){
				printf("add membership error\n");
				printf("error: %s\n", strerror(pico_err));
				exit(-1);

			}

			//////////end testing

			new_connect= 0;

			raw_data = (unsigned char*) malloc(IMAGE_SIZE);

			end_ptr = raw_data + IMAGE_SIZE;
			payload_ptr = raw_data;
			data_ptr = raw_data;
		}




		if (!is_retrieving_img) {
			data_ptr = raw_data;
			is_retrieving_img = 1;
		}

		if (!recv_udpimg(s)) {
			handle_image_buffer();
			is_retrieving_img = 0;
		}
	}

}



void
setup_udp_app() {
	struct pico_socket *client_socket;
	struct pico_ip4 address;
	uint16_t port;
	int ret;

	port = short_be(atoi(config.port));
	bzero(&address, sizeof(address));

	client_socket = pico_socket_open(PICO_PROTO_IPV4, PICO_PROTO_UDP, &cb_udpconnect);
	if (!client_socket) {
		printf("cannot open socket: %s", strerror(pico_err));
		exit(1);
	}

	int rec = UDP_RECV_BUF_SIZE;
	pico_setsockopt_udp(client_socket, PICO_SOCKET_OPT_RCVBUF, (void*)&rec);

	ret = pico_socket_bind(client_socket, &address, &port);
	if (ret < 0) {
		printf("cannot bind socket to port %u: %s", short_be(port), strerror(pico_err));
		exit(1);
	}

	pico_string_to_ipv4(config.if_addr,  &address.addr);

	ret = pico_socket_connect(client_socket, &address.addr, port);
	if (ret != 0) {
		printf("cannot connect on port %u", short_be(port));
		exit(1);
	}

	cvNamedWindow(window_name, CV_WINDOW_AUTOSIZE);

	char* request = REQUEST_STRING;
	int bytes = pico_socket_send(client_socket, (void*)request, strlen(request));
	if (bytes < 0) {
		printf("Could not send peer info\n");
		exit(-2);
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

	pico_string_to_ipv4("10.0.0.100",  &addr.addr);
	pico_string_to_ipv4("255.255.255.0", &netm.addr);
	pico_ipv4_link_add(dev, addr, netm);
}

int
main(int argc, char *argv[]) {
	char c= 0;
	int option_index= 0;
	int req_arg_count=0;
	printf("starting....\n");

	static struct option long_options[]=
		{
			{"if_name", required_argument, 0, 'i'},
			{"if_mac", required_argument, 0, 'm'},
			{"if_addr", required_argument, 0, 'a'},
			{"port", required_argument, 0, 'p'},
			{0, 0, 0, 0}
		};

	while ( (c = getopt_long (argc, argv, "i:m:a:p:",
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
				default:
					abort ();
				}
		}
	if (req_arg_count !=4)
		{
			printf("Not enough required arguments: if_name if_mac if_addr port\n");
			exit(-1);
		}
	printf("args parsed\n");


	init_picotcp();

	setup_udp_app();

	pico_stack_loop();

	free_resources();

	return 0;
}
