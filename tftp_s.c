#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define mxbufsize 550 // get sockaddr, IPv4 or IPv6:
#define mxdatainpkt 512 // maximum data size that can be sent on one packet
#define mxfsize 100 // maximum length of file name supported
#define mx_pkts 99 // maximum number of file packets
#define try_limit 3 // maximum number of tries if packet t out
#define timelimit 5 // in seconds


// converts block number to length-2 string
void s_to_i(char *f, int n) {
	if (n == 0) {
		f[0] = '0', f[1] = '0', f[2] = '\0';
	} else if (n % 10 > 0 && n / 10 == 0) {
		char c = n + '0';
		f[0] = '0', f[1] = c, f[2] = '\0';
	} else if (n % 100 > 0 && n / 100 == 0) {
		char c2 = (n % 10) + '0';
		char c1 = (n / 10) + '0';
		f[0] = c1, f[1] = c2, f[2] = '\0';
	} else {
		f[0] = '9', f[1] = '9', f[2] = '\0';
	}
}

// makes data packet
char* make_data_pack(int block, char *data) {
	char *packet;
	char temp[3];
	s_to_i(temp, block);
	packet = malloc(4 + strlen(data));
	memset(packet, 0, sizeof packet);
	strcat(packet, "03");//opcode
	strcat(packet, temp);
	strcat(packet, data);
	return packet;
}

// makes ACK packet
char* make_ack(char* block) {
	char *packet;
	packet = malloc(2 + strlen(block));
	memset(packet, 0, sizeof packet);
	strcat(packet, "04");//opcode
	strcat(packet, block);
	return packet;
}

// makes ERR packet
char* make_err(char *errcode, char* errmsg) {
	char *packet;
	packet = malloc(4 + strlen(errmsg));
	memset(packet, 0, sizeof packet);
	strcat(packet, "05");//opcode
	strcat(packet, errcode);
	strcat(packet, errmsg);
	return packet;
}


char *listening_on_port;

void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

//CHECKS FOR TIMEOUT
int check_timeout(int sockfd, char *buf, struct sockaddr_storage their_addr, socklen_t addr_len) {
	fd_set fds;
	int n;
	struct timeval tv;

	// set up the file descriptor set
	FD_ZERO(&fds);
	FD_SET(sockfd, &fds);

	// set up the struct timeval for the timeout
	tv.tv_sec = timelimit;
	tv.tv_usec = 0;

	// wait until timeout or data received
	n = select(sockfd + 1, &fds, NULL, NULL, &tv);
	if (n == 0) {
		printf("timeout\n");
		return -2; // timeout!
	} else if (n == -1) {
		printf("error\n");
		return -1; // error
	}

	return recvfrom(sockfd, buf, mxbufsize - 1 , 0, (struct sockaddr *)&their_addr, &addr_len);
}

int main(int argc, char* argv[]) {
	if (argc != 2) { // CHECKS IF args ARE VALID
		fprintf(stderr, "USAGE: tftp_s listeningportno\n");
		exit(1);
	}
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int bytes;
	struct sockaddr_storage their_addr;
	char buf[mxbufsize];
	socklen_t addr_len;
	char s[INET6_ADDRSTRLEN];


	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	listening_on_port = argv[1];
	if ((rv = getaddrinfo(NULL, listening_on_port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "SERVER: getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("SERVER: socket");
			continue;
		}
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("SERVER: bind");
			continue;
		}
		break;
	}
	if (p == NULL) {
		fprintf(stderr, "SERVER: failed to bind socket\n");
		return 2;
	}
	freeaddrinfo(servinfo);

	printf("SERVER: waiting to recvfrom...\n");
	//===========CONFIGURATION OF SERVER - ENDS===========


	//===========MAIN IMPLEMENTATION - STARTS===========

	//WAITING FOR FIRST REQUEST FROM CLIENT - RRQ/WRQ
	addr_len = sizeof their_addr;
	if ((bytes = recvfrom(sockfd, buf, mxbufsize - 1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
		perror("SERVER: recvfrom");
		exit(1);
	}
	printf("SERVER: got packet from %s\n", inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s));
	printf("SERVER: packet is %d bytes long\n", bytes);
	buf[bytes] = '\0';
	printf("SERVER: packet contains \"%s\"\n", buf);

	if (buf[0] == '0' && buf[1] == '1') { //READ REQUEST
		//CHECKING IF FILE EXISTS
		char filename[mxfsize];
		strcpy(filename, buf + 2);

		FILE *fp = fopen(filename, "rb");
		if (fp == NULL || access(filename, F_OK) == -1) { //SENDING ERROR PACKET - FILE NOT FOUND
			fprintf(stderr, "SERVER: file '%s' does not exist, sending error packet\n", filename);
			char *e_msg = make_err("02", "ERROR_FILE_NOT_FOUND");
			printf("%s\n", e_msg);
			sendto(sockfd, e_msg, strlen(e_msg), 0, (struct sockaddr *)&their_addr, addr_len);
			exit(1);
		}

		//STARTING TO SEND FILE
		int block = 1;
		fseek(fp, 0, SEEK_END);
		int total = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		int rem = total;
		if (rem == 0)
			++rem;
		else if (rem % mxdatainpkt == 0)
			--rem;

		while (rem > 0) {
			//READING FILE
			char temp[mxdatainpkt + 5];
			if (rem > mxdatainpkt) {
				fread(temp, mxdatainpkt, sizeof(char), fp);
				temp[mxdatainpkt] = '\0';
				rem -= (mxdatainpkt);
			} else {
				fread(temp, rem, sizeof(char), fp);
				temp[rem] = '\0';
				rem = 0;
			}

			//SENDING - DATA PACKET
			char *ack_msg = make_data_pack(block, temp);
			if ((bytes = sendto(sockfd, ack_msg, strlen(ack_msg), 0, (struct sockaddr *)&their_addr, addr_len)) == -1) {
				perror("SERVER ACK: sendto");
				exit(1);
			}
			printf("SERVER: sent %d bytes\n", bytes);

			//WAITING FOR ACKNOWLEDGEMENT - DATA PACKET
			int t;
			for (t = 0; t <= try_limit; ++t) {
				if (t == try_limit) {
					printf("SERVER: MAX NUMBER OF TRIES REACHED\n");
					exit(1);
				}

				bytes = check_timeout(sockfd, buf, their_addr, addr_len);
				if (bytes == -1) { //error
					perror("SERVER: recvfrom");
					exit(1);
				} else if (bytes == -2) { //timeout
					printf("SERVER: try no. %d\n", t + 1);
					int temp_bytes;
					if ((temp_bytes = sendto(sockfd, ack_msg, strlen(ack_msg), 0, p->ai_addr, p->ai_addrlen)) == -1) {
						perror("SERVER: ACK: sendto");
						exit(1);
					}
					printf("SERVER: sent %d bytes AGAIN\n", temp_bytes);
					continue;
				} else { //valid
					break;
				}
			}
			printf("SERVER: got packet from %s\n", inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s));
			printf("SERVER: packet is %d bytes long\n", bytes);
			buf[bytes] = '\0';
			printf("SERVER: packet contains \"%s\"\n", buf);

			++block;
			if (block > mx_pkts)
				block = 1;
		}
		fclose(fp);
	} else if (buf[0] == '0' && buf[1] == '2') { //WRITE REQUEST
		//SENDING ACKNOWLEDGEMENT
		char *message = make_ack("00");
		char prev_recv_msg[mxbufsize]; strcpy(prev_recv_msg, buf);
		char prev_ACK[10]; strcpy(prev_ACK, message);
		if ((bytes = sendto(sockfd, message, strlen(message), 0, (struct sockaddr *)&their_addr, addr_len)) == -1) {
			perror("SERVER ACK: sendto");
			exit(1);
		}
		printf("SERVER: sent %d bytes\n", bytes);

		char filename[mxfsize];
		strcpy(filename, buf + 2);
		strcat(filename, "_server");

		if (access(filename, F_OK) != -1) { //SENDING ERROR PACKET - DUPLICATE FILE
			fprintf(stderr, "SERVER: file %s already exists, sending error packet\n", filename);
			char *e_msg = make_err("06", "ERROR_FILE_ALREADY_EXISTS");
			sendto(sockfd, e_msg, strlen(e_msg), 0, (struct sockaddr *)&their_addr, addr_len);
			exit(1);
		}

		FILE *fp = fopen(filename, "wb");
		if (fp == NULL || access(filename, W_OK) == -1) { //SENDING ERROR PACKET - ACCESS DENIED
			fprintf(stderr, "SERVER: file %s access denied, sending error packet\n", filename);
			char *e_msg = make_err("05", "ERROR_ACCESS_DENIED");
			sendto(sockfd, e_msg, strlen(e_msg), 0, (struct sockaddr *)&their_addr, addr_len);
			exit(1);
		}

		int cursor;
		do {
			//RECEIVING FILE - PACKET DATA
			if ((bytes = recvfrom(sockfd, buf, mxbufsize - 1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
				perror("SERVER: recvfrom");
				exit(1);
			}
			printf("SERVER: got packet from %s\n", inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s));
			printf("SERVER: packet is %d bytes long\n", bytes);
			buf[bytes] = '\0';
			printf("SERVER: packet contains \"%s\"\n", buf);

			//SENDING LAST ACK AGAIN - AS IT HAS NOT REACHED
			if (strcmp(buf, prev_recv_msg) == 0) {
				sendto(sockfd, prev_ACK, strlen(prev_ACK), 0, (struct sockaddr *)&their_addr, addr_len);
				continue;
			}

			//WRITING FILE
			cursor = strlen(buf + 4);
			fwrite(buf + 4, sizeof(char), cursor, fp);
			strcpy(prev_recv_msg, buf);

			//SENDING ACKNOWLEDGEMENT - PACKET DATA
			char block[3];
			strncpy(block, buf + 2, 2);
			block[2] = '\0';
			char *ack_msg = make_ack(block);
			if ((bytes = sendto(sockfd, ack_msg, strlen(ack_msg), 0, (struct sockaddr *)&their_addr, addr_len)) == -1) {
				perror("SERVER ACK: sendto");
				exit(1);
			}
			printf("SERVER: sent %d bytes\n", bytes);
			strcpy(prev_ACK, ack_msg);
		} while (cursor == mxdatainpkt);
		printf("NEW FILE: %s SUCCESSFULLY MADE\n", filename);
		fclose(fp);
	} else {
		fprintf(stderr, "INVALID REQUEST\n");
		exit(1);
	}
	//===========MAIN IMPLEMENTATION - ENDS===========


	close(sockfd);

	return 0;
}
