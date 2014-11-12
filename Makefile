CC=gcc

CFLAGS=-c -Wall -Wextra -O0 -I deps/picotcp/build/include -I deps/netmap/sys -I include
CFLAGS+=`pkg-config --cflags opencv`
CFLAGS+=-ggdb

LDFLAGS=-L deps/picotcp/build/lib -lpicotcp
LDFLAGS+=`pkg-config --libs opencv` -lm

SOURCES=src/videostream.c src/client-udp.c src/client-tcp.c src/server-tcp.c src/server-udp.c
OBJECTS=$(SOURCES:.c=.o)

EXECUTABLE_SERVER_TCP=nm-picotcp-server-tcp
EXECUTABLE_SERVER_UDP=nm-picotcp-server-udp
EXECUTABLE_PICOSERVER_UDP=picotcp-server-udp
EXECUTABLE_CLIENT_TCP=nm-picotcp-client-tcp
EXECUTABLE_CLIENT_UDP=nm-picotcp-client-udp
EXECUTABLE_PICOCLIENT_UDP=picotcp-client-udp
all: $(SOURCES) $(EXECUTABLE_CLIENT_UDP) $(EXECUTABLE_CLIENT_TCP) $(EXECUTABLE_SERVER_UDP) $(EXECUTABLE_SERVER_TCP) $(EXECUTABLE_PICOCLIENT_UDP) $(EXECUTABLE_PICOSERVER_UDP)

.c.o:
	$(CC) $(CFLAGS) $< -o $@

$(EXECUTABLE_SERVER_TCP): src/server-tcp.o src/videostream.o
	$(CC) src/videostream.o $< $(LDFLAGS) -o $@

$(EXECUTABLE_SERVER_UDP): src/server-udp.o src/videostream.o
	$(CC) src/videostream.o $< $(LDFLAGS) -o $@

$(EXECUTABLE_CLIENT_TCP): src/client-tcp.o
	$(CC) $< $(LDFLAGS) -o $@

$(EXECUTABLE_CLIENT_UDP): src/client-udp.o
	$(CC) $< $(LDFLAGS) -o $@

$(EXECUTABLE_PICOSERVER_UDP): src/picoserver-udp.o src/videostream.o
	$(CC) src/videostream.o $< $(LDFLAGS) -o $@

$(EXECUTABLE_PICOCLIENT_UDP): src/picoclient-udp.o
	$(CC) $< $(LDFLAGS) -o $@


deps: deps/picotcp deps/netmap
	cd deps/picotcp;      make IPV6=0 NAT=0 MCAST=0 IPFILTER=0 DNS_CLIENT=0 SNTP_CLIENT=0 DHCP_CLIENT=0 DHCP_SERVER=0 HTTP_CLIENT=0 HTTP_SERVER=0 OLSR=0 SLAACV4=0 IPFRAG=0 DEBUG=1 TCP=1 UDP=1
	cd deps/netmap/LINUX; make NODRIVERS=1

clean:
	@rm $(OBJECTS)
	@rm $(EXECUTABLE_CLIENT_UDP)
	@rm $(EXECUTABLE_CLIENT_TCP)
	@rm $(EXECUTABLE_SERVER_UDP)
	@rm $(EXECUTABLE_SERVER_TCP)
	@rm $(EXECUTABLE_PICOSERVER_TCP)
	@rm $(EXECUTABLE_PICOSERVER_TCP)


depsclean:
	cd deps/picotcp;      make clean
	cd deps/netmap/LINUX; make clean

