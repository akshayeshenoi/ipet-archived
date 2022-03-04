/*
 * See LICENSE for licensing information
 */

#include "pcap_replay.h"

#define MAGIC 0xFFEEDDCC

const gchar* USAGE = "USAGE: <node-type> <server-host> <server-port> <pcap_client_ip> <pcap_nw_addr> <pcap_nw_mask> <timeout> <pcap_trace1> <pcap_trace2>..\n";

/* pcap_activateClient() is called when the epoll descriptor has an event for the client */
void _pcap_activateClient(Pcap_Replay* pcapReplay, gint sd, uint32_t event) {
	pcapReplay->slogf(G_LOG_LEVEL_DEBUG, __FUNCTION__, "Activate client!");
 
	char receivedPacket[MTU];
	struct epoll_event ev;
	ssize_t numBytes;

	/* Save a pointer to the packet to send */
	Custom_Packet_t *pckt_to_send = pcapReplay->nextPacket;

	/* Process event */ 
	if (sd == pcapReplay->client.tfd_sendtimer && (event & EPOLLIN)) { // time to send the next packet
		pcapReplay->slogf(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "Sending packet!");
		Custom_Packet_t* pckt_to_send = pcapReplay->nextPacket;

		// we only send the packet meta, not the actual packet
		int meta_packet_size = 4;
		unsigned char message[meta_packet_size];

		if (pckt_to_send->proto == _TCP_PROTO) {
			// indicate that this is a TCP packet
			message[0] = 'T';
			// indicate if it is outgoing (0) or incoming (1) in the second byte
			message[1] = (pckt_to_send->outgoing == TRUE) ? 'O' : 'I';

			// indicate payload size in the next 2 bytes (shouldn't be greater than 1500)
			// note little endianness
			for (int i = 0; i < 2; i++) {
				message[i+2] = (unsigned char) (pckt_to_send->payload_size >> (i*8));
			}

			numBytes = send(pcapReplay->client.server_sd_tcp, message, meta_packet_size, 0);
		}
		else if(pckt_to_send->proto == _UDP_PROTO) {
			// do the same thing as above for now TODO
			// indicate that this is a UDP packet
			message[0] = 'U';
			// indicate if it is outgoing or incoming in the second byte
			message[1] = (pckt_to_send->outgoing == TRUE) ? 'O' : 'I';

			// indicate payload size in the next 2 bytes (shouldn't be greater than 1500)
			// note endianness?
			for (int i = 0; i < 2; i++) {
				message[i+2] = (unsigned char) (pckt_to_send->payload_size >> (i*8));
			}

			numBytes = send(pcapReplay->client.server_sd_tcp, message, meta_packet_size, 0);
		}

		/* log result */
		if(numBytes > 0) {
			pcapReplay->slogf(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
					"Successfully sent a '%d' (bytes) packet to the server", numBytes);
		} else if(numBytes == 0) {
			/* What is this TODO */
			pcapReplay->slogf(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
						"The last packet to send was an ACK. Skipped sending.", numBytes);
		} else {
			pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Unable to send message!");
		}

		//  now prepare next packet
		if(!get_next_packet(pcapReplay)) {
			/* No packet found! */
			pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "No packet found! Terminating..");
			shutdown_client(pcapReplay);

			exit(0);
		}

		struct timespec timeToWait;
		timeval_subtract (&timeToWait, &pckt_to_send->timestamp, &pcapReplay->nextPacket->timestamp);

		// sleep for timeToWait time 
		struct itimerspec itimerspecWait;
		itimerspecWait.it_interval.tv_nsec = 0;
		itimerspecWait.it_interval.tv_sec = 0;
		itimerspecWait.it_value = timeToWait;
		if (timerfd_settime(pcapReplay->client.tfd_sendtimer, 0, &itimerspecWait, NULL) < 0) {
			pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Can't set timerFD");
			exit(1);
		}

		pcapReplay->slogf(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "Sleeping for %d %d!", timeToWait.tv_sec, timeToWait.tv_nsec);

		free(pckt_to_send);

	}

	else if(sd == pcapReplay->client.server_sd_tcp && (event & EPOLLIN)) { // receive a message from the server
		memset(receivedPacket, 0, (size_t)MTU);
		numBytes = recv(sd, receivedPacket, (size_t)MTU, 0);

		/* log result */
		if(numBytes > 0) {
			pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
						"Successfully received a TCP packet from server: %d bytes", numBytes);
		} else if(numBytes==0) {
			/* The connection have been closed by the distant peer. Terminate */
			pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
						"Server closed connection? Shutting down..");
			shutdown_client(pcapReplay);
		} else{
			pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Unable to receive message");
		}
	}

	else if (sd == pcapReplay->client.server_sd_udp && (event & EPOLLIN)) { /* data on a listening socket means a new UDP message */
		/* TODO this is useless for now */
		/*  Prepare to receive message */
		memset(receivedPacket, 0, (size_t)MTU);
		u_int serverLen = sizeof(pcapReplay->client.serverAddr);
		numBytes = recvfrom(pcapReplay->client.server_sd_udp, receivedPacket, (size_t)MTU, 0, 
							(struct sockaddr *)&pcapReplay->client.serverAddr, &serverLen);

		/* log result */
		if(numBytes > 0) {
			pcapReplay->slogf(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
						"Successfully received a UDP packet from the client: %d bytes", numBytes);
		} else if(numBytes == 0) {
			/* What is this TODO */
			pcapReplay->slogf(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
						"The last packet to send was an ACK. Skipped sending.", numBytes);
		} else {
			pcapReplay->slogf(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
						"Unable to recvfrom");
			exit(1);
		}
	}

	/*  If the timeout is reached, close the plugin ! */
	GDateTime* dt = g_date_time_new_now_local();
	if(g_date_time_to_unix(dt) >= pcapReplay->timeout) {
		pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__,  "Timeout reached!");
		shutdown_client(pcapReplay);
	}
}

gboolean pcap_StartClient(Pcap_Replay* pcapReplay) {
	g_assert(pcapReplay && (pcapReplay->magic == MAGIC));

	/* use epoll to asynchronously watch events for all of our sockets */
	pcapReplay->ed = epoll_create(1);
	if(pcapReplay->ed == -1) {
		pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error in main epoll_create");
		close(pcapReplay->ed);
		return FALSE;
	}

	/* create the client socket and get a socket descriptor */
	pcapReplay->client.server_sd_tcp = socket(AF_INET, SOCK_STREAM, 0);
	if(pcapReplay->client.server_sd_tcp == -1) {
		pcapReplay->slogf(G_LOG_LEVEL_ERROR, __FUNCTION__,
					"Unable to start control socket: error in socket");
		return FALSE;
	}

	/* get the server ip address */
	if(g_ascii_strncasecmp(pcapReplay->serverHostName->str, "localhost", 9) == 0) {
		pcapReplay->serverIP = htonl(INADDR_LOOPBACK);
	} else {
		struct addrinfo* info;
		int ret = getaddrinfo(pcapReplay->serverHostName->str, NULL, NULL, &info);
		if(ret < 0) {
			pcapReplay->slogf(G_LOG_LEVEL_ERROR, __FUNCTION__,
					"Unable to getaddrinfo() on hostname \"%s\"", pcapReplay->serverHostName->str);
			return FALSE;
		}

		pcapReplay->serverIP = ((struct sockaddr_in*)(info->ai_addr))->sin_addr.s_addr;
		freeaddrinfo(info);
	}

	/* our client socket address information for connecting to the server */
	struct sockaddr_in serverAddress;
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = pcapReplay->serverIP;
	serverAddress.sin_port = pcapReplay->serverPortTCP;

	/* connect to server. since we are blocking, we expect this to return only after connect */
	gint res = connect(pcapReplay->client.server_sd_tcp, (struct sockaddr *) &serverAddress, sizeof(serverAddress));
	if (res == -1) {
		pcapReplay->slogf(G_LOG_LEVEL_ERROR, __FUNCTION__, "Unable to start control socket: error in connect");
		return FALSE;
	}

	// make socket non blocking now
	res = fcntl(pcapReplay->client.server_sd_tcp, F_SETFL, fcntl(pcapReplay->client.server_sd_tcp, F_GETFL, 0) | O_NONBLOCK);
	if (res == -1){
		pcapReplay->slogf(G_LOG_LEVEL_ERROR, __FUNCTION__, "Error converting to nonblock");
	}

	pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Connected to server!");

	/* Tell Epoll to watch this socket */
	_pcap_epoll(pcapReplay, EPOLL_CTL_ADD, EPOLLIN, pcapReplay->client.server_sd_tcp);

	// send first message to server: deviceid
	send(pcapReplay->client.server_sd_tcp, pcapReplay->deviceid->str, pcapReplay->deviceid->len, 0);

	return TRUE;
}

/* The pcap_replay_new() function creates a new instance of the pcap replayer plugin 
 * The instance can either be a server waiting for a client or a client connecting to the pcap server. */
Pcap_Replay* pcap_replay_new(gint argc, gchar* argv[], PcapReplayLogFunc slogf) {
	/* Expected args:
		./pcap_replay-exe <deviceid> <node-type> <server-host> <server-port> <pcap_client_ip> <pcap_nw_addr> <pcap_nw_mask> <timeout> <pcap_trace1> <pcap_trace2>.. 
		node-type: client | server
	*/
	g_assert(slogf);
	gboolean is_instanciation_done = FALSE; 
	gint arg_idx = 1;

	Pcap_Replay* pcapReplay = g_new0(Pcap_Replay, 1);

	pcapReplay->magic = MAGIC;
	pcapReplay->slogf = slogf;
	pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
					"Creating a new instance of the pcap replayer plugin:");

	// set device id first
	pcapReplay->deviceid = g_string_new(argv[arg_idx++]);

	const GString* nodeType = g_string_new(argv[arg_idx++]); // client or server ?
	const GString* client_str = g_string_new("client");
	const GString* server_str = g_string_new("server");
	const GString* client_server_str = g_string_new("client-server");

	/* Get the remote server name & port */
	pcapReplay->serverHostName = g_string_new(argv[arg_idx++]);
	pcapReplay->serverPortTCP = (in_port_t) htons(atoi(argv[arg_idx++]));

	// Get client IP addr used in the pcap file
	if(inet_aton(argv[arg_idx++], &pcapReplay->client_IP_in_pcap) == 0) {
		pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
					"Cannot get the client IP used in pcap file : Err in the arguments ");
		pcap_replay_free(pcapReplay);
		return NULL;
	}

	// Get local network address
	if(inet_aton(argv[arg_idx++], &pcapReplay->pcap_local_nw_addr) == 0) {
		pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
					"Cannot get the client IP used in pcap file : Err in the arguments ");
		pcap_replay_free(pcapReplay);
		return NULL;
	}

	// Get local network address mask (2^(32 - mask))
	pcapReplay->pcap_local_nw_mask = (guint32) 1 << (32 - atoi(argv[arg_idx++]));

	// Get the timeout of the experiment
	GDateTime* dt = g_date_time_new_now_local();
	pcapReplay->timeout = atoi(argv[arg_idx++]) + g_date_time_to_unix(dt);

	// Get pcap paths and then open the file using pcap_open()
	// akshaye: We are just going to read one file but I don't want to mess anything up
	pcapReplay->nmb_pcap_file = argc - arg_idx;
	// We open all the pcap file here in order to know directly if there is an error ;)
	// The paths of the pcap file are stored in a queue as well as the pcap_t pointers.
	// Path & pcap_t pointers are stored in the same order !
	pcapReplay->pcapFilePathQueue = g_queue_new();
	pcapReplay->pcapStructQueue = g_queue_new();

	for(gint i=arg_idx; i < arg_idx+pcapReplay->nmb_pcap_file ;i++) {
		// Open the pcap file 
		pcap_t *pcap = NULL;
		char ebuf[PCAP_ERRBUF_SIZE];
		if ((pcap = pcap_open_offline(argv[i], ebuf)) == NULL) {
			pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
					"Unable to open the pcap file : %s", argv[i]);
			return NULL;
		} else {
			pcapReplay->slogf(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
					"Pcap file opened (%s) ",argv[i]);
		}
		//Add the file paths & pcap_t pointer to the queues
		g_queue_push_tail(pcapReplay->pcapFilePathQueue, g_string_new(argv[i]));
		g_queue_push_tail(pcapReplay->pcapStructQueue, pcap);
	}

	// Attach the first pcap_t struct to the instance state
	// The pcap files are used in the order the appear in arguments
	pcapReplay->pcap = (pcap_t*) g_queue_peek_head(pcapReplay->pcapStructQueue);

	/* If the first argument is equal to "client" 
	 * Then create a new client instance of the  pcap replayer plugin */
	if(g_string_equal(nodeType,client_str)) {
		pcapReplay->isClient = TRUE;
		pcapReplay->isServer = FALSE;
		// Start the client (socket,connect)
		if(!pcap_StartClient(pcapReplay)) {
			pcap_replay_free(pcapReplay);
			return NULL;
		} 
	}

	/* If the first argument is equal to "server" 
	 * Then also create a new CLIENT instance of the pcap replayer plugin, but while playing the pcap,
	 * only forward the server side of things.
	 */
	else if(g_string_equal(nodeType,server_str)) {
		pcapReplay->isClient = FALSE;
		pcapReplay->isServer = TRUE;
		// Start the client (socket,connect)
		if(!pcap_StartClient(pcapReplay)) {
			pcap_replay_free(pcapReplay);
			return NULL;
		}
	}

	/* If the first argument is equal to "client-server" 
	 * Then also create a new CLIENT instance of the pcap replayer plugin, but while playing the pcap,
	 * forward both server and client packets.
	 */
	else if(g_string_equal(nodeType,client_server_str)) {
		pcapReplay->isClient = TRUE;
		pcapReplay->isServer = TRUE;
		// Start the client (socket,connect)
		if(!pcap_StartClient(pcapReplay)) {
			pcap_replay_free(pcapReplay);
			return NULL;
		}
	}
	else {
		pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
					"First argument is not equals to either 'client' | 'server'. Exiting!");
		pcap_replay_free(pcapReplay);
		return NULL;
	}
	is_instanciation_done = TRUE;

	// get first packet and start timer
	if (!get_next_packet(pcapReplay)) {
		// If there is no packet matching the IP.source & IP.dest & port.dest, then exits !
		is_instanciation_done=FALSE;
		pcap_replay_free(pcapReplay);
		pcapReplay->slogf(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"Cannot find one packet (in the pcap file) matching the IPs/Ports arguments ");
		return NULL;
	}

	// create timerfd and start sending right away
	pcapReplay->client.tfd_sendtimer = timerfd_create(CLOCK_MONOTONIC, 0);

	struct itimerspec itimerspecWait;
	itimerspecWait.it_interval.tv_nsec = 0;
	itimerspecWait.it_interval.tv_sec = 0;
	itimerspecWait.it_value.tv_nsec = 1;
	itimerspecWait.it_value.tv_sec = 0;
	if (timerfd_settime(pcapReplay->client.tfd_sendtimer, 0, &itimerspecWait, NULL) < 0) {
		pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Can't set timerFD");
		exit(1);
	}

	// finally monitor by epoll
	struct epoll_event ev;
	memset(&ev, 0, sizeof(struct epoll_event));
	ev.events = EPOLLIN;
	ev.data.fd = pcapReplay->client.tfd_sendtimer;
	epoll_ctl(pcapReplay->ed, EPOLL_CTL_ADD, pcapReplay->client.tfd_sendtimer, &ev);
	pcapReplay->slogf(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "Epoll timer created!");

	// Free the Strings used for comparaison
	g_string_free((GString*)nodeType, TRUE);
	g_string_free((GString*)client_str, TRUE);
	g_string_free((GString*)server_str, TRUE);
	g_string_free((GString*)client_server_str, TRUE);

	if(!is_instanciation_done) {
		//pcap_replay_free(pcapReplay);
		pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
				"Cannot instanciate the pcap plugin ! Exiting plugin");
		pcap_replay_free(pcapReplay);
		return NULL;
	}

	return pcapReplay;
}

void pcap_replay_ready(Pcap_Replay* pcapReplay) {
	g_assert(pcapReplay && (pcapReplay->magic == MAGIC));

	/* Collect the events that are ready 
	 * Then activate client or server with corresponding events (EPOLLIN &| EPOLLOUT)*/
	struct epoll_event epevs[100];
	gint nfds = epoll_wait(pcapReplay->ed, epevs, 100, 0);

	if(nfds == -1) {
		pcapReplay->slogf(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "error in epoll_wait");
	} else {
		for(gint i = 0; i < nfds; i++) {
			gint d = epevs[i].data.fd;
			uint32_t e = epevs[i].events;
			if(d == pcapReplay->client.server_sd_tcp || d == pcapReplay->client.server_sd_udp || d == pcapReplay->client.tfd_sendtimer) {
				_pcap_activateClient(pcapReplay, d, e);
			}
		}
	}
}

void _pcap_epoll(Pcap_Replay* pcapReplay, gint operation, guint32 events, int sd) {
	g_assert(pcapReplay && (pcapReplay->magic == MAGIC));

	struct epoll_event ev;
	memset(&ev, 0, sizeof(struct epoll_event));
	ev.events = events;
	ev.data.fd = sd;

	gint res = epoll_ctl(pcapReplay->ed, operation, sd, &ev);
	if(res == -1) {
		pcapReplay->slogf(G_LOG_LEVEL_ERROR, __FUNCTION__, "Error in epoll_ctl");
	}
}

gint pcap_replay_getEpollDescriptor(Pcap_Replay* pcapReplay) {
	g_assert(pcapReplay && (pcapReplay->magic == MAGIC));
	return pcapReplay->ed;
}

gboolean pcap_replay_isDone(Pcap_Replay* pcapReplay) {
	g_assert(pcapReplay && (pcapReplay->magic == MAGIC));
	return pcapReplay->isDone;
}

void pcap_replay_free(Pcap_Replay* pcapReplay) {
	g_assert(pcapReplay && (pcapReplay->magic == MAGIC));

	if(pcapReplay->ed) {
		close(pcapReplay->ed);
	}
	if(pcapReplay->client.server_sd_tcp) {
		close(pcapReplay->client.server_sd_tcp);
	}
	if(pcapReplay->server.sd_tcp) {
		close(pcapReplay->server.sd_tcp);
	}
	if(pcapReplay->server.sd_udp) {
		close(pcapReplay->server.sd_udp);
	}
	if(pcapReplay->serverHostName) {
		g_string_free(pcapReplay->serverHostName, TRUE);
	}
	while(!g_queue_is_empty(pcapReplay->pcapStructQueue)) {
		pcap_t * pcap = g_queue_pop_head(pcapReplay->pcapStructQueue);
		if(pcap) {
			pcap_close(pcap);
		}
	}
	while(!g_queue_is_empty(pcapReplay->pcapFilePathQueue)) {
		GString * s = g_queue_pop_head(pcapReplay->pcapFilePathQueue);
		if(s) {
			g_string_free(s,TRUE);
		}
	}
	g_queue_free(pcapReplay->pcapStructQueue);
	g_queue_free(pcapReplay->pcapFilePathQueue);
	pcapReplay->magic = 0;
	g_free(pcapReplay);
}

gboolean get_next_packet(Pcap_Replay* pcapReplay) {
	/* Get first the first pcap packet matching the IP:PORT received in argv 
	 * Example : 
	 * If in the pcap file the client have the IP:Port address 192.168.1.2:5555 
	 * and the server have the IP:Port address 192.168.1.3:80. 
	 * Then, if the plugin is instanciated as a client, the client needs to resend
	 * the packet with ip.source=192.168.1.2 & ip.destination=192.168.1.3 & port.dest=80
	 * to the remote server.
	 * On the contrary, if the plugin is instanciated as a server, the server needs to wait
	 * for a client connection. When the a client is connected, it starts to resend packets 
	 * with ip.source=192.168.1.3 & ip.dest=192.168.1.2 & port.dest=5555 */

	struct pcap_pkthdr *header;
	const u_char *pkt_data;
	int size = 0;
	gboolean exists = FALSE;

	//tcp info
	const struct sniff_ethernet *ethernet; /* The ethernet header */
	const struct sniff_ip *ip; /* The IP header */
	const struct sniff_tcp *tcp; /* The TCP header */
	u_int size_ip_header;
	u_int size_tcp_header;
	u_int size_payload;
	char *payload;

	_PROTO proto;

	while((size = pcap_next_ex(pcapReplay->pcap, &header, &pkt_data)) >= 0) {
		// There exists a next packet in the pcap file
		// Retrieve header information
		ethernet = (struct sniff_ethernet*)(pkt_data);
		// ensure we are dealing with an ipv4 packet
		if (ethernet->ether_type != 8) {
			continue;
		}

		ip = (struct sniff_ip*)(pkt_data + SIZE_ETHERNET);
		size_ip_header = IP_HL(ip)*4;
		// ensure that we are dealing with tcp
		// or if we're interested in tunneling udp over tcp in the case of vpn
		if (ip->ip_p == '\x06') {
			// tcp
			proto = _TCP_PROTO;
			tcp = (struct sniff_tcp*)(pkt_data + SIZE_ETHERNET + size_ip_header);
			size_tcp_header = TH_OFF(tcp)*4;
			// extract the payload
			payload = (char *)(pkt_data + SIZE_ETHERNET + size_ip_header + size_tcp_header);
			size_payload = ntohs(ip->ip_len) - (size_ip_header + size_tcp_header);

			if (size_payload <= 0) {
				// does not have any payload, probably an ACK or keep alive
				continue;
			}
		}
		else if (ip->ip_p == '\x11') {
			// udp
			// ipet should deal with UDP as well, but the functionality is not coded yet
			// so we assume it is a tcp packet
			proto = _UDP_PROTO;

			payload = (char *)(pkt_data + SIZE_ETHERNET + size_ip_header + UDP_HEADER_SIZE);
			size_payload = ntohs(ip->ip_len) - (size_ip_header + UDP_HEADER_SIZE);
		}
		else {
			// not of interest
			continue;
		}

		// pcap client scenario
		if(pcapReplay->isClient) {
			// next packet must be one with ip.src == client_IP_in_pcap.s_addr
			if(ip->ip_src.s_addr == pcapReplay->client_IP_in_pcap.s_addr) {
				// AND ip.dst must not be a local address (e.g., should not belong to 192.168.0.0/16)
				// or (ip.dst < pcapReplay->local_net_addr OR ip.dst > pcapReplay->local_net_addr + pcapReplay->local_net_mask)
				if( ntohl(ip->ip_dst.s_addr) < ntohl(pcapReplay->pcap_local_nw_addr.s_addr)
					|| ntohl(ip->ip_dst.s_addr) > ntohl(pcapReplay->pcap_local_nw_addr.s_addr) + pcapReplay->pcap_local_nw_mask) {

					pcapReplay->nextPacket = g_new0(Custom_Packet_t, 1);
					pcapReplay->slogf(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "Found at least one matching packet in next_packet. Destination -> %s",
						inet_ntoa(ip->ip_dst));
					exists = TRUE;
					pcapReplay->nextPacket->timestamp.tv_sec = header->ts.tv_sec;
					pcapReplay->nextPacket->timestamp.tv_usec = header->ts.tv_usec;
					pcapReplay->nextPacket->payload_size = size_payload;
					pcapReplay->nextPacket->payload = payload;
					pcapReplay->nextPacket->proto = proto;
					pcapReplay->nextPacket->outgoing = TRUE;

					break;
				}
			}	
		} 
		// pcap server scenario 
		if(pcapReplay->isServer) {
			// ip.src must not be a local address (e.g., should not belong to 192.168.0.0/16)
			// or (ip.src < pcapReplay->local_net_addr OR ip.src > pcapReplay->local_net_addr + pcapReplay->local_net_mask)
			if(ntohl(ip->ip_src.s_addr) < ntohl(pcapReplay->pcap_local_nw_addr.s_addr)
				|| ntohl(ip->ip_src.s_addr) > ntohl(pcapReplay->pcap_local_nw_addr.s_addr) + pcapReplay->pcap_local_nw_mask) {
				// AND next packet must be one with ip.dst == client_IP_in_pcap.s_addr (reverse flow) 
				if(ip->ip_dst.s_addr == pcapReplay->client_IP_in_pcap.s_addr) {

					pcapReplay->nextPacket = g_new0(Custom_Packet_t, 1);
					pcapReplay->slogf(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "Found at least one matching packet in next_packet. Destination -> %s",
						inet_ntoa(ip->ip_dst));
					exists = TRUE;
					pcapReplay->nextPacket->timestamp.tv_sec = header->ts.tv_sec;
					pcapReplay->nextPacket->timestamp.tv_usec = header->ts.tv_usec;
					pcapReplay->nextPacket->payload_size = size_payload;
					pcapReplay->nextPacket->payload = payload;
					pcapReplay->nextPacket->proto = proto;
					pcapReplay->nextPacket->outgoing = FALSE;

					break;
				}
			}
		}
	}
	return exists;
}

void deinstanciate(Pcap_Replay* pcapReplay, gint sd) {
	epoll_ctl(pcapReplay->ed, EPOLL_CTL_DEL, sd, NULL);
	close(sd);
	pcapReplay->client.server_sd_tcp = 0;
	pcapReplay->isDone = TRUE;
	pcapReplay->slogf(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
					"Plugin deinstanciated, exiting plugin !");
}

int timeval_subtract (struct timespec *result, struct timeval *y, struct timeval *x)
{
	/* Perform the carry for the later subtraction by updating y. */
	if (x->tv_usec < y->tv_usec) {
		int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
		y->tv_usec -= 1000000 * nsec;
		y->tv_sec += nsec;
	}
	if (x->tv_usec - y->tv_usec > 1000000) {
		int nsec = (x->tv_usec - y->tv_usec) / 1000000;
		y->tv_usec += 1000000 * nsec;
		y->tv_sec -= nsec;																																																																		
	}

	/* Compute the time remaining to wait.
	 * tv_usec is certainly positive. */
	result->tv_sec = x->tv_sec - y->tv_sec;
	result->tv_nsec = (x->tv_usec - y->tv_usec)*1000;

	/* Return 1 if result is negative. */
	return x->tv_sec < y->tv_sec;
}

gboolean shutdown_client(Pcap_Replay* pcapReplay) {
	epoll_ctl(pcapReplay->ed, EPOLL_CTL_DEL, pcapReplay->client.server_sd_tcp, NULL);
	epoll_ctl(pcapReplay->ed, EPOLL_CTL_DEL, pcapReplay->client.server_sd_udp, NULL);
	epoll_ctl(pcapReplay->ed, EPOLL_CTL_DEL, pcapReplay->client.tfd_sendtimer, NULL);

	close(pcapReplay->client.server_sd_tcp);
	close(pcapReplay->client.server_sd_udp);
	close(pcapReplay->client.tfd_sendtimer);

	pcapReplay->isDone = TRUE;
	pcapReplay->slogf(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "Plugin exiting!");

	return FALSE;
}
