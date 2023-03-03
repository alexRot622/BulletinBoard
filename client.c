#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "utils.h"

static struct sockaddr_in serv_addr;
static char id[ID_LEN];
static int sockfd;

void usage()
{
	fprintf(stderr, "Usage: ./subscriber <ID_CLIENT> <IP_SERVER> <PORT_SERVER>\n");
	exit(0);
}

// Parse the command and send a packet to the server, if necessary
// Returns the sub_type of the sent packet. If cmd is "exit", returns 0
int exec_command(char *cmd) {
    char *word = strtok(cmd, " \n");

    if (strcmp("subscribe", word) == 0) {
        char *topic;
        int sf;

        word = strtok(NULL, " \n");
        if (word == NULL)
            return -1;
        topic = strdup(word);

        word = strtok(NULL, " \n");
        if (word == NULL)
            return -1;
        sf = atoi(word);

        send_sub(sockfd, id, SUB_SUB, sf, topic);

        free(topic);
        return SUB_SUB;
    } else if (strcmp("unsubscribe", word) == 0) {
        char *topic;

        word = strtok(NULL, " \n");
        if (word == NULL)
            return -1;
        topic = strdup(word);

        send_sub(sockfd, id, SUB_UNSUB, topic);

        free(topic);
        return SUB_UNSUB;
    } else if (strcmp("exit", word) == 0) {
        send_sub(sockfd, id, SUB_LOGOUT);
        return 0;
    }

    return -1;
}

#define BUF_LEN 255

void loop() {
    static char buf[BUF_LEN];
    struct subhdr sub;
    int received;
    int running = 1;
    fd_set read_fds, temp_fds;

    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);
    FD_SET(0, &read_fds);

    while (running) {
        temp_fds = read_fds;

        int ret = select(sockfd + 1, &temp_fds, NULL, NULL, NULL);
        if (ret < 0) {
            send_sub(sockfd, id, SUB_LOGOUT);
            DIE(1, "Error during call to select\n");
        }

        // Get input from stdin
        if (FD_ISSET(0, &temp_fds)) {
            memset(buf, 0, BUF_LEN);
            fgets(buf, BUF_LEN - 1, stdin);

            switch (exec_command(buf)) {
                case SUB_SUB:
                    printf("Subscribed to topic.\n");
                    break;
                case SUB_UNSUB:
                    printf("Unsubscribed from topic.\n");
                    break;
                case 0:
                    running = 0;
                    break;
                default:
                    printf("Unknown command.\n");
            }
        } else if (FD_ISSET(sockfd, &temp_fds)) {
            memset(&sub, 0, sizeof(struct subhdr));
            received = recv_sub(sockfd, &sub);

            if (received < 0)
                running = 0;
            else if (received > 0) {
                if (sub.type == SUB_SERVDOWN)
                    running = 0;
                else if (sub.type == SUB_DATA) {
                    sub.un.data.s_ip.s_addr = ntohl(sub.un.data.s_ip.s_addr);
                    sub.un.data.s_port = ntohs(sub.un.data.s_port);

                    print_bulletin(sub.un.data.s_ip, sub.un.data.s_port,
                                   &sub.un.data.bulletin);
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    if (argc != 4)
        usage();

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    DIE(sockfd < 0, "Could not open socket\n");

    // Set ID and server IP/Port
    memcpy(id, argv[1], ID_LEN);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[3]));
    DIE(inet_aton(argv[2], &serv_addr.sin_addr) == 0,
        "Invalid server IP\n");

    int enable = 1;
    DIE(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0,
        "Could not set TCP socket options\n");

    // Disable Nagle's algorithm for TCP
    DIE(setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int)) < 0,
        "Could not set TCP socket options\n");

    // Connect to server
    DIE(connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0,
            "Could not connect to server\n");

    // Send log in request to server
    send_sub(sockfd, id, SUB_LOGIN);

    loop();

    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);

    return 0;
}
