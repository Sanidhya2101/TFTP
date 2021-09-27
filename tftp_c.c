#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>



const int mxbufsize = 550; 
const int timelimit = 5; 
const int mxdatainpkt = 512; //max data in a packet
const int mxfsize = 100; // max length of filename
const int mx_pkts = 99;  // max packets
const int try_limit = 3; // max packet resending tries



char *send_to_port;

void s_to_i(char *f, int n) 
{
	if (n == 0) 
		f[0] = '0', f[1] = '0', f[2] = '\0';
	else if (n % 10 > 0 && n / 10 == 0) 
	{
		char c = n + '0';
		f[0] = '0', f[1] = c, f[2] = '\0';
	} 
	else if (n % 100 > 0 && n / 100 == 0) 
	{
		char c2 = (n % 10) + '0';
		char c1 = (n / 10) + '0';
		f[0] = c1, f[1] = c2, f[2] = '\0';
	} 
	else 
		f[0] = '9', f[1] = '9', f[2] = '\0';
}//helper function


void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


int check_timeout(int sockfd, char *buf, struct sockaddr_storage their_addr, socklen_t addr_len) {
	fd_set fds;
	int n;
	struct timeval tv;

	// set up the file descriptor set
	FD_ZERO(&fds);
	FD_SET(sockfd, &fds);

	// set up the struct timeval for the timeout
	tv.tv_usec = 0;
	tv.tv_sec = timelimit;

	// wait until timeout or data received
	n = select(sockfd + 1, &fds, NULL, NULL, &tv);
	if (n == 0)
	{
		printf("TIMEOUT\n");
		return -2;//TIMEOUT
	} else if (n == -1)
	{
		printf("ERROR\n");
		return -1; // error
	}

	return recvfrom(sockfd, buf, mxbufsize - 1 , 0, (struct sockaddr *)&their_addr, &addr_len);
}

int main(int argc, char* argv[]) {

	if (argc != 5)
	{
		printf("PROTOTYPE: ./tftp_c <SERVER> <SERVERPORTNO> GET/PUT <FILENAME>\n");
		return 1;
	}


	int sockfd;
	int rv;
	int bytes;
	char buf[mxbufsize];
	char s[INET6_ADDRSTRLEN];
	struct sockaddr_storage their_addr;
	socklen_t addr_len;


	struct addrinfo hints, *servinfo;
	char *server = argv[1]; //SERVER ADDRESS
	send_to_port = argv[2]; //PORT NO.

	int st = -1;
	if (strcmp(argv[3], "GET") == 0)
		st = 0;
	else if (strcmp(argv[3], "PUT") == 0)
		st = 1;

	char *file = argv[4]; //FILE NAME


	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	if ((rv = getaddrinfo(server, send_to_port, &hints, &servinfo)) != 0)
	{
		printf("CLIENT: GETADDRINFO: %s\n", gai_strerror(rv));
		return 1;
	}

	struct addrinfo *p;
	for (p = servinfo; p != NULL; p = p->ai_next)
	{
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
		{
			perror("CLIENT: SOCKET");
			continue;
		}
		break;
	}//LOOPING THRU LIST SET BY GETADDRINFO

	if (p == NULL)
	{
		printf("CLIENT: FAILED TO BIND\n");
		return 1;
	}


	switch (st)
	{
	case 0:
	{
		;
		/**************************SENDING RRQ************************************/


		/*********************MAKING RRQ PACKET******************/
		char *msg = malloc(2 + strlen(file));
		memset(msg, 0, sizeof(msg));
		strcat(msg, "01"); //OPCODE FOR IDENTIFYING RRQ PACKETS
		strcat(msg, file);
		/******************RRQ PACKET MADE*******************/

		if ((bytes = sendto(sockfd, msg, strlen(msg), 0, p->ai_addr, p->ai_addrlen)) == -1)
		{
			printf("CLIENT: SENDTO(ERROR), EXITING......\n");
			return 1;
		}
		printf("CLIENT: SENT %d BYTES!!\n", bytes);

		/**************************RRQ SENT***************************************/

		char prev_recv_msg[mxbufsize];
		strcpy(prev_recv_msg, "");
		char prev_ACK[10];
		strcpy(prev_ACK, msg);

		/*******************FILE RELATED OPERATIONS********************************/
		char filename[mxfsize];
		strcpy(filename, file);
		strcat(filename, "_GET");

		FILE *fp = fopen(filename, "wb");
		if (fp == NULL)
		{
			printf("CLIENT: ERROR OPENING FILE!! EXITING....\n");
			return 1;
		}
		/*****************FILE OPENED AT CLIENT SIDE********************************/

		int cursor;
		do
		{
			/***********************RECEIVING FILE************************************/
			addr_len = sizeof(their_addr);
			if ((bytes = recvfrom(sockfd, buf, mxbufsize - 1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1)
			{
				printf("CLIENT: RECVFROM(ERROR) EXITING.............\n");
				return 1;
			}
			printf("CLIENT: PACKET RECEIVED!!\n");
			printf("CLIENT: PACKET SIZE: %d BYTES\n", bytes);
			buf[bytes] = '\0';

			/*********************ERROR PACKET CHECK**********************************/
			if (buf[0] == '0' && buf[1] == '5')
			{
				printf("CLIENT: ERROR: %s, EXITING .......\n", buf);
				return 1;
			}
			/*************************************************************************/

			/**************************RESENDING ACK**************************************/
			if (strcmp(buf, prev_recv_msg) == 0)
			{
				sendto(sockfd, prev_ACK, strlen(prev_ACK), 0, (struct sockaddr *)&their_addr, addr_len);
				continue;
			}
			/***************************ACK RESENT*****************************************/

			/************************WRITING TO FILE***********************************/
			strcpy(prev_recv_msg, buf);
			cursor = strlen(buf + 4);
			fwrite(buf + 4, sizeof(char), cursor, fp);
			/**********************PACKET DATA WRITTEN TO FILE*************************/

			/********************ACKNOWLEDGMENT FOR DATA******************************/
			char block[3];
			strncpy(block, buf + 2, 2);
			block[2] = '\0';

			/*****MAKING ACK PACKET******************/
			char *ack_msg = malloc(2 + strlen(block));
			memset(ack_msg, 0, sizeof(ack_msg));
			strcat(ack_msg, "04"); //OPCODE FOR IDENTIFYING ACK PACKETS
			strcat(ack_msg, block);
			/*******ACK PACKET MADE******************/


			if ((bytes = sendto(sockfd, ack_msg, strlen(ack_msg), 0, p->ai_addr, p->ai_addrlen)) == -1)
			{
				printf("CLIENT ACK: SENDTO(ERROR) EXITING...........\n");
				return 1;
			}
			printf("CLIENT: SENT %d BYTES\n", bytes);
			strcpy(prev_ACK, ack_msg);

			/*******************ACKNOWLEDGMENT TO DATA SENT***************************/
		} while (cursor == mxdatainpkt);

		/******************************FILE RECEIVED**********************/

		fclose(fp);
		printf("FILE(s) DOWNLOADED: %s\n", filename);
		break;
	}
	case 1:
	{
		;
		/***********************SENDING WRQ**********************************/


		/***********************MAKING WRQ PACKET***********************************/
		char *msg = malloc(2 + strlen(file));
		memset(msg, 0, sizeof(msg));
		strcat(msg, "02");//OPCODE FOR IDENTIFYING WRQ PACKET
		strcat(msg, file);
		/***********************WRQ PACKET MADE*************************************/

		if ((bytes = sendto(sockfd, msg, strlen(msg), 0, p->ai_addr, p->ai_addrlen)) == -1)
		{
			printf("CLIENT: SENDTO(ERROR) EXITING........\n");
			return 1;
		}
		printf("CLIENT: SENT %d BYTES!!\n", bytes);

		/***********************WRQ SENT*************************************/

		char *prev_msg = msg;

		/***********************WATING FOR ACK(WRQ)********************************/
		addr_len = sizeof(their_addr);
		for (int t = 0; t <= try_limit; ++t)
		{
			if (t == try_limit)
			{
				printf("CLIENT: MAX NUMBER OF TRIES REACHED!!\n EXITING...........\n");
				return 1;
			}

			if ((bytes = check_timeout(sockfd, buf, their_addr, addr_len)) == -2) //TIMEOUT
			{
				printf("CLIENT: TRY NO %d\n", t + 1);
				int temp_bytes;
				if ((temp_bytes = sendto(sockfd, prev_msg, strlen(prev_msg), 0, p->ai_addr, p->ai_addrlen)) == -1)
				{
					printf("CLIENT ACK: SENDTO(ERROR) EXITING........\n");
					return 1;
				}
				printf("CLIENT: SENT %d BYTES AGAIN\n", temp_bytes);
				continue;
			}
			else if (bytes == -1) //ERROR
			{
				printf("CLIENT: RECVFROM(ERROR) EXITING............\n");
				return 1;
			}
			else //RECEIVED
				break;

		}
		buf[bytes] = '\0';
		/**************************ACK RECEIVED******************************************/

		printf("CLIENT: PACKET RECEIVED!!\n");
		printf("CLIENT: PACKET SIZE IS %d BYTES!!\n", bytes);

		if (buf[0] == '0' && buf[1] == '4')
		{
			FILE *fp = fopen(file, "rb");
			if (fp == NULL || access(file, F_OK) == -1)
			{
				printf("CLIENT: FILE %s NOT PRESENT\n", file);
				return 1;
			}
			/****************CALCULATING THE SIZE OF FILE***********************************/
			fseek(fp, 0, SEEK_END);
			int total = ftell(fp);
			fseek(fp, 0, SEEK_SET);
			int rem = total;
			if (rem == 0)
				++rem;
			else if (rem % mxdatainpkt == 0)
				--rem;
			/********************************************************************************/

			int block = 1;

			while (rem > 0)
			{
				char temp[mxdatainpkt + 5];
				if (rem <= mxdatainpkt)
				{
					fread(temp, rem, sizeof(char), fp);
					temp[rem] = '\0';
					rem = 0;
				}
				else
				{
					fread(temp, mxdatainpkt, sizeof(char), fp);
					rem -= (mxdatainpkt);
					temp[mxdatainpkt] = '\0';
				}

				/******************************SENDING DATA PACKET*********************************/

				char tempc[3];
				s_to_i(tempc, block);


				char *msg = malloc(4 + strlen(temp));
				memset(msg, 0, sizeof(msg));
				strcat(msg, "03"); //OPCODE FOR IDENTIFYING DATA PACKET
				strcat(msg, tempc);
				strcat(msg, temp);

				if ((bytes = sendto(sockfd, msg, strlen(msg), 0, p->ai_addr, p->ai_addrlen)) == -1)
				{
					printf("CLIENT: SENDTO(ERROR) EXITING..............\n");
					return 1;
				}
				printf("CLIENT: SENT %d BYTES!!\n", bytes);
				prev_msg = msg;
				/*****************************DATA PACKET SENT*************************************/


				/******************************WAITING FOR ACK*************************************/
				for (int t = 0; t < try_limit; ++t)
				{
					if (t == try_limit)
					{
						printf("CLIENT: MAX NUMBER OF TRIES REACHED\n");
						return 1;
					}

					if ((bytes = check_timeout(sockfd, buf, their_addr, addr_len)) == -2) //TIMEOUT
					{
						printf("CLIENT: try no. %d\n", t + 1);
						int temp_bytes;
						if ((temp_bytes = sendto(sockfd, prev_msg, strlen(prev_msg), 0, p->ai_addr, p->ai_addrlen)) == -1)
						{
							printf("CLIENT ACK: SENDTO(ERROR) EXITING....\n");
							return 1;
						}
						printf("CLIENT: SENT %d BYTES AGAIN!!\n", temp_bytes);
						continue;
					}
					else if (bytes == -1) //ERROR
					{
						printf("CLIENT: RECVFROM(ERROR) EXITING........\n");
						return 1;
					}
					else
						break;
				}
				printf("CLIENT: RECEIVED PACKET!!\n");
				printf("CLIENT: PACKET SIZE IS %d BYTES!!\n", bytes);
				buf[bytes] = '\0';
				/********************ACK RECEIVED FOR DATA PACKET SENT*******************************/

				//ERROR PACKET??
				if (buf[0] == '0' && buf[1] == '5')
				{
					printf("CLIENT: RECEIVED ERROR PACKET!! EXITING..........\n");
					return 1;
				}

				++block;
				if (block > mx_pkts)
					block = 1;
			}

			fclose(fp);
		}
		else
		{
			printf("CLIENT: EXPECTED ACK, RECEIVED: %s\n", buf);
			return 1;
		}
		break;
	}
	default:
		printf("PROTOTYPE: ./tftp_c <SERVER> <SERVERPORTNO> GET/PUT <FILENAME>\n");
		return 1;
	}

	freeaddrinfo(servinfo);
	close(sockfd);

	return 0;
}
