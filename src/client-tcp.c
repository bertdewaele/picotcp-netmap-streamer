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
#include <pico_socket_tcp.h>

#include <cv.h>
#include <highgui.h>

//#define WIDTH (320*2)
//#define HEIGHT (240*2)

#define WIDTH (320)
#define HEIGHT (240)
#define IMAGE_SIZE (WIDTH*HEIGHT)

#ifdef DEBUG_STREAM
#define DEBUG(x) printf(x)
#else 
#define DEBUG(x) do {} while (0)
#endif

static unsigned char* raw_data = NULL; 
static unsigned char* data_ptr = NULL;
static unsigned char* end_ptr = NULL;

static const CvSize frame_size = {.width = WIDTH, .height = HEIGHT};
static IplImage *img;

static int flag = 0;

static int is_retrieving_img = 0;
static char* window_name="VIDEO STREAMER";

struct {
	char *if_mac;
	char *if_name;
	char *if_addr;
	char *port;
} config;

void setup_tcp_app();
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
deferred_exit(pico_time __attribute__((unused)) now, void *arg) {
	
	if (arg) {
		free(arg);
		arg = NULL;
	}

	free_resources();
	exit(0);
}

static void
handle_imagebuffer(void)
{
  	cvSetData(img, (void*)raw_data, WIDTH);
  	cvShowImage(window_name, img);
  	cvWaitKey(1);
}

int
recv_tcpimg(struct pico_socket* s) {

	int bytes = pico_socket_read(s, (void*)data_ptr, (int)(end_ptr-data_ptr));

	DEBUG(("bytes received = %i\n", bytes));

  	if(bytes < 0) {
  	  	printf("Pico socket read failed. error = %i\n", pico_err);
  	}

  	data_ptr += bytes;

  	if (data_ptr < end_ptr)
  	  	return 1;
	
	return 0;
}

void
cb_tcpconnect(uint16_t ev, struct pico_socket *s) {
  
	if (ev & PICO_SOCK_EV_CONN) {
		printf("Socket connects...\n");
		img = cvCreateImageHeader(frame_size, IPL_DEPTH_8U, 1);
		cvNamedWindow(window_name, CV_WINDOW_AUTOSIZE);
	}
       
	if (ev & PICO_SOCK_EV_FIN) {
		printf("Socket closed. Exit normally. \n");
		pico_timer_add(2000, deferred_exit, NULL);
	}

	if (ev & PICO_SOCK_EV_ERR) {
		printf("Socket error received: %s. Bailing out.\n", strerror(pico_err));
		free_resources();		
		exit(1);
	}

	if (ev & PICO_SOCK_EV_CLOSE) {
		printf("Socket received close from peer.\n");
		free_resources();

		if (flag & PICO_SOCK_EV_RD) {
			pico_socket_shutdown(s, PICO_SHUT_WR);
			printf("SOCKET> Called shutdown write, ev = %d\n", ev);
		}
	}

	if (ev & PICO_SOCK_EV_RD) {

		if (!is_retrieving_img) {
			data_ptr = raw_data;
			is_retrieving_img = 1;
		}

		if (recv_tcpimg(s)) {
			flag|= PICO_SOCK_EV_RD;
		} else {
			flag &= (~PICO_SOCK_EV_RD);
			handle_imagebuffer();
			is_retrieving_img = 0;
		}
	}
}

void
setup_tcp_app() {
	struct pico_socket *client_socket;
	struct pico_ip4 address;
	uint16_t port;
	int ret, yes;

	yes = 1;
	port = short_be(atoi(config.port));
	bzero(&address, sizeof(address));

	client_socket = pico_socket_open(PICO_PROTO_IPV4, PICO_PROTO_TCP, &cb_tcpconnect);
	if (!client_socket) {
		printf("cannot open socket: %s", strerror(pico_err));
		exit(1);
	}

	pico_socket_setoption(client_socket, PICO_TCP_NODELAY, &yes);
	
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

	raw_data = (unsigned char*) malloc(IMAGE_SIZE);
	end_ptr = raw_data + IMAGE_SIZE;
	data_ptr = raw_data;

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

	if (argc < 4) {
    		printf("usage: %s if_name if_mac if_addr port\n", argv[0]);
    		exit(1);
  	}

  	config.if_name = argv[1];
  	config.if_mac  = argv[2];
  	config.if_addr = argv[3];
  	config.port    = argv[4];

  	init_picotcp();

  	setup_tcp_app();

  	pico_stack_loop();

	free_resources();

  	return 0;
}

