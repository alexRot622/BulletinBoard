#include <iostream>
#include <unordered_map>
#include <vector>

#include <unistd.h>
#include <cstring>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "utils.h"

#define MAX_CLIENTS 1000

class Server {
 public:
    Server(int port) {
        running = true;

        memset(serv_id, 0, ID_LEN);

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_aton("127.0.0.1", &(server_addr.sin_addr));

        // Create UDP and TCP sockets
        udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        DIE(udp_sockfd < 0, "Could not open UDP socket\n");

        tcp_sockfd = socket(AF_INET, SOCK_STREAM, 0);
        DIE(tcp_sockfd < 0, "Could not open TCP socket\n");

        FD_ZERO(&read_fds);
    }

    int start() {
        // Bind UDP and TCP sockets
        shutdown_if(bind(udp_sockfd, (struct sockaddr *) &server_addr, sizeof(struct sockaddr)) < 0,
            "Could not bind UDP socket.\n");

        shutdown_if(bind(tcp_sockfd, (struct sockaddr *) &server_addr, sizeof(struct sockaddr)) < 0,
            "Could not bind TCP socket.\n");

        int enable = 1;
        shutdown_if(setsockopt(udp_sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0,
            "Could not set UDP socket options\n");
        shutdown_if(setsockopt(tcp_sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0,
            "Could not set TCP socket options\n");

        // Disable Nagle's algorithm for TCP
        shutdown_if(setsockopt(tcp_sockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int)) < 0,
            "Could not set TCP socket options\n");

        shutdown_if(listen(tcp_sockfd, MAX_CLIENTS) < 0, "Error during listen\n");

        FD_SET(udp_sockfd, &read_fds);
        FD_SET(tcp_sockfd, &read_fds);
        FD_SET(0, &read_fds);

        fdmax = std::max(udp_sockfd, tcp_sockfd);

        running = true;
        return loop();
    }

    // Shutdown all connected TCP clients and close the UDP and TCP ports
    void shutdown_ports() const {
        // Send shutdown message to all subscribers
        for (const auto &it : subscribers) {
            auto sub = it.second;

            if (sub.online)
                send_sub(sub.sockfd, (char *) serv_id, SUB_SERVDOWN);
        }

        shutdown(udp_sockfd, SHUT_RDWR);
        close(udp_sockfd);

        shutdown(tcp_sockfd, SHUT_RDWR);
        close(tcp_sockfd);
    }

 private:
    // Structure that contains a bulletin message
    // and information regarding its source
    struct bulletin_from {
        uint16_t port;
        uint32_t ip;
        bulletinhdr bulletin{};

        bulletin_from(uint16_t port, uint32_t ip, bulletinhdr *bulletin) {
            this->port = port;
            this->ip = ip;
            memcpy(&this->bulletin, bulletin, sizeof(bulletinhdr));
        }
    };

    struct subscriber {
        int32_t sockfd{};
        bool online{};
        std::vector<std::pair<bool, std::string>> subscriptions;

        // Find the subscription with the provided topic name
        // If the topic name is not found, returns nullptr
        std::pair<bool, std::string> *find_subscription(const std::string& topic) {
            for (auto &it : subscriptions)
                if (it.second == topic)
                    return &it;

            return nullptr;
        }

        // Remove the subscription with the provided topic name
        // if the topic name is found, returns 0. Otherwise, returns -1
        int remove_subscription(const std::string& topic) {
            for (auto it = subscriptions.begin(); it != subscriptions.end(); ++it)
                if (it->second == topic) {
                    subscriptions.erase(it);
                    return 0;
               }

            return -1;
        }
    };

    std::unordered_map<std::string, std::vector<bulletin_from>> topics;
    std::unordered_map<std::string, subscriber> subscribers;

    struct sockaddr_in server_addr{};
    char serv_id[ID_LEN];
    fd_set read_fds;

    int fdmax;
    int udp_sockfd;
    int tcp_sockfd;

    bool running;

    int loop() {
        struct bulletinhdr bulletin{};
        struct sockaddr_in udp_addr{};
        fd_set temp_fds;
        socklen_t len;
        int received;

        static const size_t buf_len = 255;
        static char buf[buf_len];

        while (running) {
            temp_fds = read_fds;
            shutdown_if(select(fdmax + 1, &temp_fds, NULL, NULL, NULL) < 0,
                "Error during call to select\n");

            // Check for input from stdin
            if(FD_ISSET(0, &temp_fds)) {
                memset(buf, 0, buf_len);
                fgets(buf, buf_len - 1, stdin);
                //Select first word, remove newline
                strtok(buf, " \n");

                if (strcmp("exit", buf) == 0) {
                    running = false;
                    break;
                }
            }

            // Check for messages from UDP clients
            if(FD_ISSET(udp_sockfd, &temp_fds)) {
                len = sizeof(sockaddr_in);
                received = recvfrom(udp_sockfd, &bulletin, sizeof(bulletinhdr), 0,
                                    (struct sockaddr *) &udp_addr, &len);

                if (received < 0) {
                    fprintf(stderr, "Error while receiving data from client.\n");
                    running = false;
                    break;
                }

                if (received > 0) {
                    topics[bulletin.topic].emplace_back(ntohs(udp_addr.sin_port),
                                                      ntohl(udp_addr.sin_addr.s_addr),
                                                      &bulletin);

                    int store = send_to_subscribers(bulletin.topic);

                    // If the message received is not needed by any
                    // subscribers who are currently offline, don't keep it
                    if (store == 0)
                        topics[bulletin.topic].pop_back();
                }
            }

            listen_for_subscribers(&temp_fds);

            memset(&bulletin, 0, sizeof(bulletinhdr));
        }

        return received;
    }

    void shutdown_if(bool condition, const std::string errmsg) const {
        if (condition) {
            shutdown_ports();
            DIE(1, errmsg.c_str());
        }
    }

    // Send all the stored messages for a subscriber
    int send_stored_messages(const struct subscriber *sub) {
        int sent = 0;

        for (const auto &it : sub->subscriptions) {
            // Skip over topics where store flag is not set
            if (!it.first)
                continue;

            const auto &msgs = topics[it.second];
            for (const auto &msg : msgs)
                send_sub(sub->sockfd, (char *) serv_id, SUB_DATA,
                         (msg.port), htonl(msg.ip),
                         &msg.bulletin);

            sent += topics[it.second].size();
            topics[it.second].clear();
        }

        return sent;
    }

    int send_to_subscribers(const std::string &topic) {
        int store = 0;

        bulletin_from &last_msg = topics[topic].back();
        for (auto &it : subscribers) {
            struct subscriber *sub = &it.second;
            auto entry = sub->find_subscription(topic);

            if (entry == nullptr)
                continue;

            if (sub->online) {
                send_sub(sub->sockfd, (char *) serv_id, SUB_DATA,
                         last_msg.port, htonl(last_msg.ip),
                         &last_msg.bulletin);
            } else if (entry->first) {
                // Store and forward flag is set, the last message
                // should be kept for when the client is online
                store++;
            }
        }

        return store;
    }

    void listen_for_subscribers(fd_set *fds) {
        static std::vector<int> subscriber_fds;
        static struct subhdr msg;
        static struct sockaddr_in cli_addr{};
        static socklen_t len;
        char *msg_id;
        int newsockfd;

        // Check for new connections on the TCP port
        if (FD_ISSET(tcp_sockfd, fds)) {
            // New connection incoming, accept it
            newsockfd = accept(tcp_sockfd, NULL, NULL);
            shutdown_if(newsockfd < 0, "Could not accept TCP client\n");

            FD_SET(newsockfd, &read_fds);
            if (newsockfd > fdmax)
                fdmax = newsockfd;

            // Store the subscriber in new_subscribers until the ID is obtained
            subscriber_fds.push_back(newsockfd);
            return;
        }

        std::vector<int> temp_fds;
        // Get login messages from new subscribers
        for (auto sockfd : subscriber_fds) {
            if (FD_ISSET(sockfd, fds)) {
                len = sizeof(struct sockaddr_in);
                getpeername(sockfd, (struct sockaddr *) &cli_addr, &len);

                int received = recv_sub(sockfd, &msg);
                msg_id = strndup(msg.id, ID_LEN);

                shutdown_if(received < 0, "Error while listening for "
                                  "log in message\n");
                if (received == 0) {
                    free(msg_id);
                    FD_CLR(sockfd, fds);
                    continue;
                }

                if (msg.type != SUB_LOGIN) {
                    free(msg_id);
                    continue;
                }

                if (subscribers.count(msg_id) > 0) {
                    auto *sub = &subscribers[msg_id];
                    if (sub->online) {
                        printf("Client %.10s already connected.\n",
                               msg_id);
                        send_sub(sockfd, (char *) serv_id, SUB_SERVDOWN);
                        free(msg_id);
                        break;
                    }

                    sub->online = true;
                    sub->sockfd = sockfd;

                    send_stored_messages(sub);
                } else {
                    struct subscriber sub;

                    sub.online = true;
                    sub.sockfd = sockfd;

                    subscribers.emplace(msg_id, sub);
                }
                printf("New client %.10s connected from %s:%u\n",
                       msg_id, inet_ntoa(cli_addr.sin_addr), cli_addr.sin_port);
                FD_CLR(sockfd, fds);
                free(msg_id);
            } else temp_fds.push_back(sockfd);
        }
        subscriber_fds.clear();
        subscriber_fds = temp_fds;

        // Listen for packets from online subscribers
        for (auto &it : subscribers) {
            auto id = it.first;
            auto *sub = &it.second;
            int sockfd = sub->sockfd;

            if (FD_ISSET(sockfd, fds)) {
                int received = recv_sub(sockfd, &msg);
                msg_id = strndup(msg.id, ID_LEN);

                shutdown_if(received < 0, "Error while listening for "
                                   "message from subscriber\n");
                if (received == 0) {
                    free(msg_id);
                    FD_CLR(sockfd, fds);
                    continue;
                }

                switch (msg.type) {
                    case SUB_SUB: {
                        if (strncmp(id.c_str(), msg_id, ID_LEN) != 0)
                            break;

                        sub->subscriptions.emplace_back(
                                msg.un.sub.sf == 1,
                                std::string(msg.un.sub.topic));
                        break;
                    }
                    case SUB_UNSUB:
                        if (strncmp(id.c_str(), msg_id, ID_LEN) != 0)
                            break;

                        sub->remove_subscription(msg.un.sub.topic);
                        break;
                    case SUB_LOGIN:
                        printf("Client %.10s already connected.\n",
                               msg_id);
                        send_sub(sockfd, (char *) serv_id, SUB_SERVDOWN);
                        break;
                    case SUB_LOGOUT:
                        subscribers[msg_id].online = false;
                        printf("Client %.10s disconnected.\n", msg_id);
                        break;
                    default:
                        printf("Unsupported message type %d.\n", msg.type);
                        break;
                }
                free(msg_id);
            }
        }
    }
};

void usage()
{
    fprintf(stderr, "Usage: ./server <PORT>\n");
    exit(0);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

    if (argc < 2)
        usage();

    int port = atoi(argv[1]);
    DIE(port < 0 || port > 65535, "Provided port is not valid\n");

    auto server = new Server(port);
    server->start();
    server->shutdown_ports();
    delete server;
}
