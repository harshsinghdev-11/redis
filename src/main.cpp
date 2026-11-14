#include <iostream>
#include <cstdint>
#include <vector>
#include <poll.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <cstring>
#include <unistd.h>
#include <cassert>
#include <errno.h>
#include <cstdlib>
#include <cstdio>
#include <csignal>

#include "connection.h"
#include "utils.h"
#include "network.h"
#include "pubsub.h"

using namespace std;

static volatile sig_atomic_t g_running = 1;

static void sig_handler(int) {
    g_running = 0;
}

int main(int argc, char* argv[]) {
    // 1. Prevent SIGPIPE crash when clients or NGINX abruptly close connections
    signal(SIGPIPE, SIG_IGN);

    // 2. Register graceful termination signals (systemd / Ctrl+C)
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    std::cout << "Connection fd is: " << fd << "\n";
    if (fd < 0) {
        die("socket()");
    }

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    std::cout << "socket is configured\n";

    // Port: configurable via CLI argument, default 1234
    uint16_t port = 1234;
    if (argc > 1) {
        port = static_cast<uint16_t>(atoi(argv[1]));
    }

    // Bind to 127.0.0.1 (Loopback only)
    // Security: Only NGINX (or local processes on this EC2 instance) can connect.
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int rv = bind(fd, (const sockaddr*)&addr, sizeof(addr));
    if (rv) {
        die("bind()");
    }

    fd_set_nb(fd);

    rv = listen(fd, SOMAXCONN);
    if (rv) {
        die("listen()");
    }

    std::cout << "Redis server listening on 127.0.0.1:" << port << " (behind NGINX)\n";

    vector<Conn*> fd2Conn;
    vector<struct pollfd> poll_args;
    int count = 0;

    while (g_running) {
        poll_args.clear();
        count++;

        // Listening socket in first position
        struct pollfd pfd = {fd, POLLIN, 0};
        poll_args.push_back(pfd);

        for (Conn *conn : fd2Conn) {
            if (!conn) {
                continue;
            }
            struct pollfd pfd = {conn->fd, POLLERR, 0};
            if (conn->want_read) {
                pfd.events |= POLLIN;
            }
            if (conn->want_write) {
                pfd.events |= POLLOUT;
            }
            poll_args.push_back(pfd);
        }

        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);

        if (rv < 0 && errno == EINTR) {
            if (!g_running) break;
            continue;
        }
        if (rv < 0) {
            if (!g_running) break;
            die("poll");
        }

        if (poll_args[0].revents) {
            if (Conn *conn = handle_accept(fd)) {
                if (fd2Conn.size() <= (size_t)conn->fd) {
                    fd2Conn.resize(conn->fd + 1);
                }
                std::cout << "Client connected, fd=" << conn->fd << std::endl;
                fd2Conn[conn->fd] = conn;
            }
        }

        for (size_t i = 1; i < poll_args.size(); i++) {
            uint32_t ready = poll_args[i].revents;
            if (ready == 0) {
                continue;
            }

            Conn *conn = fd2Conn[poll_args[i].fd];
            if (!conn) {
                continue;
            }

            if (ready & POLLIN) {
                assert(conn->want_read);
                handle_read(conn);
            }

            if (!conn->want_close && conn->want_write && (ready & POLLOUT)) {
                handle_write(conn);
            }

            if ((ready & POLLERR) || conn->want_close) {
                pubsub_cleanup_conn(conn);
                (void)close(conn->fd);
                fd2Conn[conn->fd] = NULL;
                delete conn;
            }
        }
    }

    std::cout << "\nGracefully shutting down server...\n";
    for (Conn *conn : fd2Conn) {
        if (conn) {
            pubsub_cleanup_conn(conn);
            close(conn->fd);
            delete conn;
        }
    }
    close(fd);
    std::cout << "Server shutdown complete.\n";
    return 0;
}
