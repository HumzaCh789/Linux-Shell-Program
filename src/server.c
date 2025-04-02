/* server.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_CLIENTS FD_SETSIZE
#define BUFFER_SIZE 1024

// Use a volatile flag to control the main loop when a termination signal is received.
volatile sig_atomic_t server_running = 1;

// Signal handler for SIGTERM or SIGINT to gracefully shut down the server.
void handle_sigterm(int signum) {
    (void)signum;
    server_running = 0;
}

// Helper function to set file descriptors to non-blocking mode.
void set_non_blocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl(F_GETFL)");
        exit(EXIT_FAILURE);
    }
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl(F_SETFL)");
        exit(EXIT_FAILURE);
    }
}

/*
 * run_server: Starts a non-blocking server on the given port.
 *
 * The server:
 *  - Accepts multiple client connections.
 *  - Assigns each client an ID (client1:, client2:, etc).
 *  - Prints every message on the server console (prefixed with the client ID).
 *  - Broadcasts the message to all connected clients.
 *  - Checks immediately if a client sends the special command "\connected"
 *    and responds with the number of connected clients.
 */
void run_server(int port) {
    int listen_fd, new_socket, max_fd, activity;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len;
    char buffer[BUFFER_SIZE];

    // Arrays to store client sockets and their corresponding IDs.
    int client_sockets[MAX_CLIENTS] = {0};
    int client_ids[MAX_CLIENTS] = {0};
    int client_counter = 0;  // This counter is used to assign successive IDs.

    // Set SIGTERM handler so that the server shuts down gracefully.
    struct sigaction sa;
    sa.sa_handler = handle_sigterm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    // Optionally, you can also catch SIGINT if you want the server to shutdown on CTRL+C.
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // Create a listening socket.
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Set the listening socket to non-blocking mode.
    set_non_blocking(listen_fd);

    // Allow immediate reuse of the address.
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // Configure the server address structure.
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Bind to all available interfaces.
    server_addr.sin_port = htons(port);

    // Bind the socket to the specified port.
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // Start listening for client connections.
    if (listen(listen_fd, 10) < 0) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    //printf("Server started on port %d\n", port);

    // Main server loop.
    while (server_running) {
        fd_set readfds;
        FD_ZERO(&readfds);

        // Add the listening socket.
        FD_SET(listen_fd, &readfds);
        max_fd = listen_fd;

        // Add all active client sockets to the read file descriptor set.
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sock = client_sockets[i];
            if (sock > 0)
                FD_SET(sock, &readfds);
            if (sock > max_fd)
                max_fd = sock;
        }

        // Wait indefinitely until some file descriptor becomes readable.
        activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            break;
        }

        // Check for an incoming connection on the listening socket.
        if (FD_ISSET(listen_fd, &readfds)) {
            addr_len = sizeof(client_addr);
            new_socket = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
            if (new_socket < 0) {
                if (errno != EWOULDBLOCK && errno != EAGAIN)
                    perror("accept");
            } else {
                // Set the new client socket to non-blocking mode.
                set_non_blocking(new_socket);
                // Assign the new client an ID.
                client_counter++;
                int assigned_id = client_counter;

                // Add the new socket into the first available slot.
                int added = 0;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (client_sockets[i] == 0) {
                        client_sockets[i] = new_socket;
                        client_ids[i] = assigned_id;
                        added = 1;
                        break;
                    }
                }
                if (!added) {
                    // If no free slot is found, the server is too busy.
                    fprintf(stderr, "Max clients reached. Refusing connection from %s:%d\n",
                            inet_ntoa(client_addr.sin_addr),
                            ntohs(client_addr.sin_port));
                    close(new_socket);
                } else {
                    printf("New connection from %s:%d, assigned client%d:\n",
                           inet_ntoa(client_addr.sin_addr),
                           ntohs(client_addr.sin_port),
                           assigned_id);
                    // Optionally, send a welcome message along with the client's ID.
                    char id_message[64];
                    snprintf(id_message, sizeof(id_message), "You are client%d:\n", assigned_id);
                    send(new_socket, id_message, strlen(id_message), 0);
                }
            }
        }

        // Process incoming data on client sockets.
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sock = client_sockets[i];
            // If this slot is active and marked as readable.
            if (sock > 0 && FD_ISSET(sock, &readfds)) {
                int bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
                if (bytes_read <= 0) {
                    // The client disconnected or an error occurred.
                    if (bytes_read == 0) {
                        printf("Client%d: disconnected\n", client_ids[i]);
                    } else {
                        perror("recv");
                    }
                    close(sock);
                    client_sockets[i] = 0;
                    client_ids[i] = 0;
                } else {
                    // Null-terminate the received message.
                    buffer[bytes_read] = '\0';
                    // If the message is the special command "\connected",
                    // respond only to the requesting client with the count.
                    if (strncmp(buffer, "\\connected", strlen("\\connected")) == 0) {
                        int connected_clients = 0;
                        for (int j = 0; j < MAX_CLIENTS; j++) {
                            if (client_sockets[j] > 0)
                                connected_clients++;
                        }
                        char count_msg[64];
                        snprintf(count_msg, sizeof(count_msg),
                                 "Number of connected clients: %d\n",
                                 connected_clients);
                        send(sock, count_msg, strlen(count_msg), 0);
                    } else {
                        // Prepend the client ID to the message.
                        char composed_message[BUFFER_SIZE + 64];
                        snprintf(composed_message, sizeof(composed_message),
                                 "client%d: %s", client_ids[i], buffer);

                        // Print the message on the server console.
                        printf("%s", composed_message);

                        // Broadcast the message to all connected clients.
                        for (int j = 0; j < MAX_CLIENTS; j++) {
                            if (client_sockets[j] > 0) {
                                if (send(client_sockets[j], composed_message, strlen(composed_message), 0) < 0) {
                                    perror("send");
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Clean up: Close all client sockets and the listening socket.
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] > 0) {
            close(client_sockets[i]);
            client_sockets[i] = 0;
        }
    }
    close(listen_fd);
    printf("Server shutting down.\n");
}
