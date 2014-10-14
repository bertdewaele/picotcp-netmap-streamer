CC=gcc

CFLAGS=-c -Wall -Wextra -Os -I deps/picotcp/build/include -I deps/netmap/sys -I include
CFLAGS+=`pkg-config --cflags opencv`

LDFLAGS=-L deps/picotcp/build/lib -lpicotcp
LDFLAGS+=`pkg-config --libs opencv` -lm

SOURCES=src/videostream.c src/nm-picotcp.c
OBJECTS=$(SOURCES:.c=.o)

EXECUTABLE_SERVER=nm-picotcp-server
EXECUTABLE_CLIENT=nm-picotcp-client

all: $(SOURCES) $(EXECUTABLE_SERVER) $(EXECUTABLE_CLIENT)

$(EXECUTABLE_SERVER): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

$(EXECUTABLE_CLIENT): src/client.o
	$(CC) $< $(LDFLAGS) -o $@

deps: deps/picotcp deps/netmap
	cd deps/picotcp;      make IPV6=0 NAT=0 MCAST=0 IPFILTER=0 DNS_CLIENT=0 SNTP_CLIENT=0 DHCP_CLIENT=0 DHCP_SERVER=0 HTTP_CLIENT=0 HTTP_SERVER=0 OLSR=0 SLAACV4=0 IPFRAG=0 DEBUG=0
	cd deps/netmap/LINUX; make

clean:
	@rm $(EXECUTABLE_SERVER)
	@rm $(EXECUTABLE_CLIENT)
	@rm $(OBJECTS)

depsclean:
	cd deps/picotcp;      make clean
	cd deps/netmap/LINUX; make clean
