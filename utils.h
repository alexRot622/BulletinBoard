#ifndef _UTILS_H
#define _UTILS_H 1

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#define DIE(assertion, call_description)	\
	do {									\
		if (assertion) {					\
			fprintf(stderr, "(%s, %d): ",	\
					__FILE__, __LINE__);	\
			perror(call_description);		\
			exit(EXIT_FAILURE);				\
		}									\
	} while(0)

// Data types for bulletin / subscriber packets,
// as shown in the homework description

struct sign_int {
    uint8_t sign;
    uint32_t i;
} __attribute__((packed));
typedef struct sign_int sign_int_t;

struct short_real {
    uint16_t mod;
} __attribute__((packed));
typedef struct short_real short_real_t;

struct real {
    uint8_t sign;
    uint32_t mod;
    uint8_t exp;
} __attribute__((packed));
typedef struct real real_t;

#define BULLETIN_INT 0
#define BULLETIN_SHORT_REAL 1
#define BULLETIN_FLOAT 2
#define BULLETIN_STRING 3

#define TOPIC_LEN 50
#define ID_LEN 10
#define MSG_LEN 1500

// definition of a bulletin packet
struct bulletinhdr {
    char topic[TOPIC_LEN];
    uint8_t type;
    union {
        sign_int_t i;
        short_real_t sr;
        real_t r;
        char s[MSG_LEN];
    } un;
} __attribute__((packed));

void print_bulletin(struct in_addr ip, uint16_t port, struct bulletinhdr *bulletin);

#define SUB_SUB 1
#define SUB_UNSUB 2
#define SUB_DATA 3
#define SUB_LOGIN 4
#define SUB_LOGOUT 5
#define SUB_SERVDOWN 6

// definition of a subscription packet
struct subhdr {
    uint16_t len; // Needed for packet separation when sent over TCP
    char id[ID_LEN];
    uint8_t type;
    union {
        // Structure for handling subscriptions
        struct {
            uint8_t sf;
            char topic[TOPIC_LEN];
        } sub;
        // Structure for sending information about a topic
        struct {
            uint16_t s_port;
            struct in_addr s_ip;
            struct bulletinhdr bulletin;
        } data;
    } un;
} __attribute__((packed));

// Send a subscription packet with a specified type.
// Calls for sending SUB_SUB packets must include the store flag and the topic.
// Calls for sending SUB_UNSUB packets must contain the topic.
// Calls for sending SUB_DATA must contain a port, an IP address
// and the address of a bulletin message
ssize_t send_sub(int sockfd, char *id, int sub_type, ...);
// Receive a subscription packet.
ssize_t recv_sub(int sockfd, struct subhdr *msg);

#endif
