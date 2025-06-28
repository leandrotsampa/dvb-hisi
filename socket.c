/*
 * Copyright (C) Leandro Tavares de Melo <leandrotsampa@yahoo.com.br>, All rights reserved.
 */

#include <dvb_hisi.h>
#include <linux/dns_resolver.h>

#define T_A     1  // IPv4 Address
#define T_NS    2  // Nameserver
#define T_CNAME 5  // Canonical Name
#define T_SOA   6  // Start of Authority Zone
#define T_PTR   12 // Domain Name Pointer
#define T_MX    15 // Mail Server

#define DNS_SIZE 65536

static unsigned int dns_servers[] = { 0xd043dede,   /* 208.67.222.222 - OpenDNS */
                                      0x08080808,   /* 8.8.8.8        - Google DNS */
                                      0xd043dcdc,   /* 208.67.220.220 - OpenDNS */
                                      0x08080404 }; /* 8.8.4.4        - Google DNS */

/*
 * DNS header structure
 */
struct DNS_HEADER {
	unsigned short id; // identification number

	unsigned char rd :1; // recursion desired
	unsigned char tc :1; // truncated message
	unsigned char aa :1; // authoritive answer
	unsigned char opcode :4; // purpose of message
	unsigned char qr :1; // query/response flag

	unsigned char rcode :4; // response code
	unsigned char cd :1; // checking disabled
	unsigned char ad :1; // authenticated data
	unsigned char z :1; // its z! reserved
	unsigned char ra :1; // recursion available

	unsigned short q_count; // number of question entries
	unsigned short ans_count; // number of answer entries
	unsigned short auth_count; // number of authority entries
	unsigned short add_count; // number of resource entries
};

/*
 * Constant sized fields of query structure
 */
struct QUESTION {
	unsigned short qtype;
	unsigned short qclass;
};

#pragma pack(push, 1)
struct R_DATA {
	unsigned short type;
	unsigned short _class;
	unsigned int ttl;
	unsigned short data_len;
};
#pragma pack(pop)

/*
 * Pointers to resource record contents
 */
struct RES_RECORD {
	unsigned char *name;
	struct R_DATA *resource;
	unsigned char *rdata;
};

/*
 * This will extract wwwgooglecom and convert back to www.google.com
 */
static unsigned char *read_host_name(unsigned char *reader, unsigned char *buffer, int *count) {
	int i;
	int j;
	unsigned int offset;
	unsigned int p = 0;
	unsigned int jumped = 0;
	unsigned char *name = vmalloc(256);

	*count  = 1;
	name[0] = '\0';

	//read the names in wwwgooglecom format
	while (*reader != 0) {
		if (*reader >= 192) {
			offset = (*reader)*256 + *(reader+1) - 49152; //49152 = 11000000 00000000 ;)
			reader = buffer + offset - 1;
			jumped = 1; //we have jumped to another location so counting wont go up!
		} else {
			name[p++] = *reader;
		}

		reader = reader+1;

		if (jumped == 0)
			*count = *count + 1; //if we havent jumped to another location then we can count up
	}

	name[p] = '\0'; //string complete
	if (jumped == 1)
		*count = *count + 1; //number of steps we actually moved forward in the packet

	// now convert wwwgooglecom to www.google.com
	for (i = 0; i < (int)strlen((const char*)name); i++) {
		p = name[i];
		for (j = 0; j < (int)p; j++) {
			name[i] = name[i+1];
			i = i + 1;
		}
		name[i] = '.';
	}

	if (i > 0)
		name[i-1] = '\0'; //remove the last dot

	return name;
}

/*
 * This will convert www.google.com to wwwgooglecom
 */
static void format_to_dns_name(unsigned char *dns, unsigned char *host) {
	int i;
	int lock = 0;
	strcat((char*)host, ".");

	for (i = 0; i < strlen((char*)host); i++) {
		if (host[i] == '.') {
			*dns++ = i-lock;

			for (; lock < i; lock++)
				*dns++=host[lock];

			lock++; //or lock=i+1;
		}
	}

	*dns++='\0';
}

int socket_read(struct socket *sock, void *buf, size_t length, unsigned long flags) {
	struct msghdr msg;
	struct kvec vec;

	msg.msg_name       = 0;
	msg.msg_namelen    = 0;
	msg.msg_control    = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags      = flags;

	vec.iov_base = buf;
	vec.iov_len = length;

	return kernel_recvmsg(sock, &msg, &vec, 1, length, flags);
}

int socket_write(struct socket *sock, void *buf, size_t length, unsigned long flags) {
	struct msghdr msg;
	struct kvec vec;

	msg.msg_name       = 0;
	msg.msg_namelen    = 0;
	msg.msg_control    = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags      = flags;

	vec.iov_base = buf;
	vec.iov_len  = length;

	return kernel_sendmsg(sock, &msg, &vec, 1, length);
}

t_host *gethostbyname(unsigned char *host) {
	size_t length;
	unsigned char *qname;
	struct sockaddr_in addr;
	struct RES_RECORD answers[20];
	int c_server = 0;
	char *svrIP = NULL;
	t_host *hEntry = NULL;
	unsigned char *url = NULL;
	struct socket *conn = NULL;
	struct DNS_HEADER *dns = NULL;
	struct QUESTION *qinfo = NULL;
	unsigned char *buf = NULL;
	unsigned int u32TimeOut = 1000; /* Timeout: 1s */
	unsigned int u32TimeSpan = 0;

	// Try resolve using kernel dns_resolver (CONFIG_DNS_RESOLVER)
	if (dns_query(NULL, (char *)host, strlen(host), NULL, &svrIP, NULL) > 0) {
		if ((hEntry = vmalloc(sizeof(t_host)))) {
			char arr[4];
			int a, b, c, d;
			sscanf(svrIP, "%d.%d.%d.%d", &a, &b, &c, &d);
			arr[0] = a; arr[1] = b; arr[2] = c; arr[3] = d;

			hEntry->name = host;
			hEntry->addr.s_addr = *(unsigned long *)arr;
		}

		return hEntry;
	}

	if (!(buf = vmalloc(DNS_SIZE))) {
		return hEntry;
	} else {
		url = kstrdup(host, GFP_KERNEL);
		if (!url) {
			vfree(buf);
			return hEntry;
		}
	}

	// Set the DNS structure to standard queries
	memset(buf, 0, DNS_SIZE);
	dns = (struct DNS_HEADER *)&buf[0];
	dns->id = (unsigned short)htons(0 /*getpid()*/);
	dns->qr = 0; // This is a query
	dns->opcode = 0; // This is a standard query
	dns->aa = 0; // Not Authoritative
	dns->tc = 0; // This message is not truncated
	dns->rd = 1; // Recursion Desired
	dns->ra = 0; // Recursion not available! hey we dont have it (lol)
	dns->z = 0;
	dns->ad = 0;
	dns->cd = 0;
	dns->rcode = 0;
	dns->q_count = htons(1); // We have only 1 question
	dns->ans_count = 0;
	dns->auth_count = 0;
	dns->add_count = 0;

	// Point to the query portion
	qname =(unsigned char*)&buf[sizeof(struct DNS_HEADER)];

	format_to_dns_name(qname, url);
	qinfo =(struct QUESTION*)&buf[sizeof(struct DNS_HEADER) + (strlen((const char*)qname) + 1)]; //fill it

	qinfo->qtype = htons(T_A); // Type of the query , A , MX , CNAME , NS etc
	qinfo->qclass = htons(1); // It's internet (lol)

	/* UDP packet for DNS queries */
	if (sock_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP, &conn) < 0)
		goto exit;

	for (; c_server < sizeof(dns_servers) / sizeof(unsigned int); c_server++) {
		int ret;

		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons(53);
		addr.sin_addr.s_addr = htonl(dns_servers[c_server]);

		/* Connect to DNS Server */
		ret = conn->ops->connect(conn, (struct sockaddr *)&addr, sizeof(addr), O_RDWR);
		if (!(ret && (ret != -EINPROGRESS)))
			break;
	}

	length = sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION);
	if (socket_write(conn, (void *)buf, length, MSG_DONTWAIT) != length)
		goto exit;

	while (u32TimeSpan < u32TimeOut) {
		if (!skb_queue_empty(&conn->sk->sk_receive_queue)) {
			int i;
			int j;
			int recv;
			int stop = 0;
			unsigned char *reader = NULL;

			memset(buf, 0, DNS_SIZE);
			recv = socket_read(conn, (void *)buf, DNS_SIZE, MSG_DONTWAIT);
			if (recv <= 0)
				break;

			dns = (struct DNS_HEADER *)&buf[0];
			reader = &buf[sizeof(struct DNS_HEADER) + (strlen((const char*)qname)+1) + sizeof(struct QUESTION)]; // move ahead of the dns header and the query field

			// Start reading answers
			for (i = 0; i < ntohs(dns->ans_count); i++) {
				answers[i].name = read_host_name(reader, buf, &stop);
				reader = reader + stop;

				answers[i].resource = (struct R_DATA*)(reader);
				reader = reader + sizeof(struct R_DATA);

				if (ntohs(answers[i].resource->type) == 1) { // If its an ipv4 address
					answers[i].rdata = (unsigned char*)vmalloc(ntohs(answers[i].resource->data_len));

					for (j = 0; j < ntohs(answers[i].resource->data_len); j++)
						answers[i].rdata[j]=reader[j];

					answers[i].rdata[ntohs(answers[i].resource->data_len)] = '\0';
					reader = reader + ntohs(answers[i].resource->data_len);
				} else {
					answers[i].rdata = read_host_name(reader, buf, &stop);
					reader = reader + stop;
				}
			}

			for (i = 0; i < ntohs(dns->ans_count); i++) {
				if (ntohs(answers[i].resource->type) == T_A) { // IPv4 Address
					long *p = (long*)answers[i].rdata;

					if (!(hEntry = vmalloc(sizeof(t_host))))
						goto exit;

					hEntry->name = answers[i].name;
					hEntry->addr.s_addr = (*p); // working without ntohl
					break;
				}
			}

			break;
		} else {
			mdelay(10);
			u32TimeSpan += 10;
		}
	}

exit:
	vfree(buf);
	kfree(url);
	return hEntry;
}