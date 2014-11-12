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
#define MTU_UDP_ETH	1472
#define REQUEST_STRING "request stream"
#define REQUEST_LENGTH (sizeof(REQUEST_STRING))

static unsigned char* raw_image_data = NULL;
static unsigned char* data_ptr = NULL;
static unsigned char* end_ptr = NULL;

static uint32_t peer= 0;
static uint16_t port= 0;

#ifdef DEBUG_STREAM
#define DEBUG(x) printf x
#else
#define DEBUG(x) do {} while (0)
#endif


struct {
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


void push_packets_to_network(void)
{
			int count= 10;

			while( --count>0)
				{
					pico_stack_tick();
				}
}


int
send_udpimg(struct pico_socket* s) {
	while(data_ptr < end_ptr)
		{
			pico_stack_tick();

			int bytes = pico_socket_sendto(s, (void*)data_ptr, MTU_UDP_ETH, &peer, port);
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

uint8_t check_if_valid_request_and_init_peerinfo(struct pico_socket *s)
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

		if (!check_if_valid_request_and_init_peerinfo(s))
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

			send_udpimg(s);

			push_packets_to_network();

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

void
init_picotcp() {
	pico_stack_init();
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
					printf("Unexpected argument: %c\nAborting...\n",c);		
					abort ();
				}
		}
	if (req_arg_count !=3)
		{
			printf("Not enough required arguments: --if_addr/-a --port/-p --cam_device/-d (--scale_factor/-s --color_disable/-c)\n");
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
