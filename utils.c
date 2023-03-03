#include "utils.h"

#include <string.h>
#include <stdarg.h>
#include <arpa/inet.h>

static uint32_t uint_pow(uint32_t n, uint8_t exp) {
    if (exp == 0)
        return 1;
    if (exp == 1)
        return n;

    uint32_t p = uint_pow(n, exp / 2);
    if (exp % 2 == 0)
        return p * p;
    else
        return p * p * n;
}

void print_bulletin(struct in_addr ip, uint16_t port, struct bulletinhdr *bulletin) {
    uint32_t r, exp;
    uint16_t sr;

    printf("%s:%d - %.50s - ", inet_ntoa(ip), port, bulletin->topic);

    switch (bulletin->type) {
        case (BULLETIN_INT):
            printf("INT - ");
            if (bulletin->un.i.sign)
                printf("%c", '-');
            printf("%u\n", ntohl(bulletin->un.i.i));
            break;
        case (BULLETIN_SHORT_REAL):
            sr = ntohs(bulletin->un.sr.mod);

            printf("SHORT_REAL - %u.", sr / 100);
            if(sr % 100 < 10)
                printf("0");
            printf("%u\n", sr % 100);
            break;
        case (BULLETIN_FLOAT):
            r = ntohl(bulletin->un.r.mod);
            exp = uint_pow(10, bulletin->un.r.exp);

            printf("FLOAT - ");
            if (bulletin->un.r.sign)
                printf("%c", '-');
            printf("%u.", r / exp);
            while(exp / 10 > r) {
                printf("0");
                exp /= 10;
            }
            printf("%u\n", r % exp);
            break;
        case (BULLETIN_STRING):
            printf("STRING - %.1500s\n", bulletin->un.s);
            break;
        default:
            printf("UNKNOWN DATA TYPE\n");
    }
}

ssize_t bulletin_len(const struct bulletinhdr *bulletin) {
    ssize_t len = TOPIC_LEN + 1;

    switch (bulletin->type) {
        case BULLETIN_INT:
            len += sizeof(sign_int_t);
            break;
        case BULLETIN_SHORT_REAL:
            len += sizeof(short_real_t);
            break;
        case BULLETIN_FLOAT:
            len += sizeof(real_t);
            break;
        case BULLETIN_STRING:
            len += strnlen(bulletin->un.s, MSG_LEN);
            break;
        default:
            len = -1;
    }

    return len;
}

ssize_t send_sub(int sockfd, char *id, int sub_type, ...) {
    static const ssize_t header_len = ID_LEN + 3;
    static struct subhdr msg;
    va_list valist;

    msg.type = sub_type;
    memcpy(msg.id, id, 10);

    switch (sub_type) {
        case SUB_SUB: {
            uint8_t sf;
            char *topic;

            va_start(valist, sub_type);
            sf = va_arg(valist, int);
            topic = va_arg(valist, char *);

            msg.len = header_len + sizeof(msg.un.sub);
            msg.un.sub.sf = sf;
            memcpy(msg.un.sub.topic, topic, TOPIC_LEN);

            va_end(valist);
            break;
        }
        case SUB_UNSUB: {
            char *topic;

            va_start(valist, sub_type);
            topic = va_arg(valist, char *);

            msg.len = header_len + sizeof(msg.un.sub);
            memcpy(msg.un.sub.topic, topic, TOPIC_LEN);

            va_end(valist);
            break;
        }
        case SUB_DATA: {
            struct bulletinhdr *bulletin;
            ssize_t bul_len;
            uint16_t port;
            uint32_t ip;

            va_start(valist, sub_type);
            port = va_arg(valist, int);
            ip = va_arg(valist, int);
            bulletin = va_arg(valist, struct bulletinhdr *);
            bul_len = bulletin_len(bulletin);
            msg.len = header_len + bul_len + sizeof(msg.un.data) -
                    sizeof(struct bulletinhdr);

            msg.un.data.s_port = htons(port);
            msg.un.data.s_ip.s_addr = htonl(ip);
            memcpy(&msg.un.data.bulletin, bulletin, bul_len);

            va_end(valist);
            break;
        }
        default:
            msg.len = header_len;
    }

    msg.len = htons(msg.len);

    send(sockfd, &msg, ntohs(msg.len), 0);
    return ntohs(msg.len);
}

ssize_t recv_sub(int sockfd, struct subhdr *msg) {
    // Peek for the header containing the length of the message
    recv(sockfd, msg, sizeof(uint16_t), MSG_PEEK);

    // Receive the message
    return recv(sockfd, msg, ntohs(msg->len), MSG_WAITALL);
}