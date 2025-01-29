
#include "network.h"
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>  // For close
   
#endif

#include <fcntl.h>  // For fcntl

// Initialize client state
void net_init_client_state(ClientState* state) {
    pthread_mutex_init(&state->lock, NULL);
    state->is_active = false;
    state->socket_fd = 0;
    memset(&state->addr, 0, sizeof(state->addr));
}

// Clean up client state
void net_cleanup_client_state(ClientState* state) {
    pthread_mutex_lock(&state->lock);
    if (state->socket_fd > 0) {
        close(state->socket_fd);
        state->socket_fd = 0;
    }
    state->is_active = false;
    pthread_mutex_unlock(&state->lock);
    pthread_mutex_destroy(&state->lock);
}

bool net_is_port_in_use(uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return true;  // Error on the safe side
    
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };
    
    int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
    
    return result < 0;
}

// Attempt to release port
bool net_release_port(uint16_t port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;
    
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };
    
    // Set SO_REUSEADDR
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Attempt to bind and immediately close
    int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
    
    // Small delay to ensure port is released
    usleep(100000);  // 100ms
    
    return result >= 0;
}

// Update net_init to include port checking and cleanup
bool net_init(NetworkEndpoint* endpoint) {
    if (!endpoint) return false;
    
    // Check if port is in use
    if (net_is_port_in_use(endpoint->port)) {
        printf("Port %d is in use, attempting to release...\n", endpoint->port);
        if (!net_release_port(endpoint->port)) {
            printf("Failed to release port %d\n", endpoint->port);
            printf("Please try:\n");
            printf("1. Wait a few seconds and try again\n");
            printf("2. Use a different port with -p option\n");
            printf("3. Or manually kill the process using the port:\n");
            printf("   sudo lsof -i :%d\n", endpoint->port);
            printf("   sudo kill <PID>\n");
            return false;
        }
        printf("Successfully released port %d\n", endpoint->port);
    }
    
    pthread_mutex_init(&endpoint->lock, NULL);
    pthread_mutex_lock(&endpoint->lock);
    
    // Create socket
    endpoint->socket_fd = socket(AF_INET, 
        endpoint->protocol == NET_TCP ? SOCK_STREAM : SOCK_DGRAM, 
        0);
    
    if (endpoint->socket_fd < 0) {
        perror("Socket creation failed");
        pthread_mutex_unlock(&endpoint->lock);
        pthread_mutex_destroy(&endpoint->lock);
        return false;
    }

    // Set socket options
    int opt = 1;
    if (setsockopt(endpoint->socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(endpoint->socket_fd);
        pthread_mutex_unlock(&endpoint->lock);
        pthread_mutex_destroy(&endpoint->lock);
        return false;
    }
    
    // Configure address
    endpoint->addr.sin_family = AF_INET;
    endpoint->addr.sin_port = htons(endpoint->port);
    endpoint->addr.sin_addr.s_addr = INADDR_ANY;
    
    // For server endpoints
    if (endpoint->role == NET_SERVER) {
        if (bind(endpoint->socket_fd, (struct sockaddr*)&endpoint->addr, sizeof(endpoint->addr)) < 0) {
            perror("Bind failed");
            close(endpoint->socket_fd);
            pthread_mutex_unlock(&endpoint->lock);
            pthread_mutex_destroy(&endpoint->lock);
            return false;
        }
        
        if (endpoint->protocol == NET_TCP) {
            if (listen(endpoint->socket_fd, NET_MAX_CLIENTS) < 0) {
                perror("Listen failed");
                close(endpoint->socket_fd);
                pthread_mutex_unlock(&endpoint->lock);
                pthread_mutex_destroy(&endpoint->lock);
                return false;
            }
        }
    }

    pthread_mutex_unlock(&endpoint->lock);
    return true;
}

// Update net_close to ensure complete cleanup
void net_close(NetworkEndpoint* endpoint) {
    if (!endpoint) return;
    
    pthread_mutex_lock(&endpoint->lock);
    
    if (endpoint->socket_fd > 0) {
        // Set linger to ensure complete socket shutdown
        struct linger ling = {1, 0};  // Immediate shutdown
        setsockopt(endpoint->socket_fd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
        
        shutdown(endpoint->socket_fd, SHUT_RDWR);  // Shutdown both directions
        close(endpoint->socket_fd);
        endpoint->socket_fd = 0;
    }
    
    pthread_mutex_unlock(&endpoint->lock);
    pthread_mutex_destroy(&endpoint->lock);
}

// Send data through network endpoint
ssize_t net_send(NetworkEndpoint* endpoint, NetworkPacket* packet) {
    if (!endpoint || !packet) return -1;
    
    ssize_t result;
    pthread_mutex_lock(&endpoint->lock);
    result = send(endpoint->socket_fd, packet->data, packet->size, packet->flags);
    pthread_mutex_unlock(&endpoint->lock);
    return result;
}

// Receive data through network endpoint
ssize_t net_receive(NetworkEndpoint* endpoint, NetworkPacket* packet) {
    if (!endpoint || !packet) return -1;
    
    ssize_t result;
    pthread_mutex_lock(&endpoint->lock);
    result = recv(endpoint->socket_fd, packet->data, packet->size, packet->flags);
    pthread_mutex_unlock(&endpoint->lock);
    return result;
}

// Add client to program
bool net_add_client(NetworkProgram* program, int socket_fd, struct sockaddr_in addr) {
    if (!program) return false;
    
    bool added = false;
    pthread_mutex_lock(&program->clients_lock);
    
    for (int i = 0; i < NET_MAX_CLIENTS; i++) {
        pthread_mutex_lock(&program->clients[i].lock);
        if (!program->clients[i].is_active) {
            program->clients[i].socket_fd = socket_fd;
            program->clients[i].addr = addr;
            program->clients[i].is_active = true;
            added = true;
            pthread_mutex_unlock(&program->clients[i].lock);
            break;
        }
        pthread_mutex_unlock(&program->clients[i].lock);
    }
    
    pthread_mutex_unlock(&program->clients_lock);
    return added;
}

// Remove client from program
void net_remove_client(NetworkProgram* program, int socket_fd) {
    if (!program) return;
    
    pthread_mutex_lock(&program->clients_lock);
    
    for (int i = 0; i < NET_MAX_CLIENTS; i++) {
        pthread_mutex_lock(&program->clients[i].lock);
        if (program->clients[i].is_active && program->clients[i].socket_fd == socket_fd) {
            close(program->clients[i].socket_fd);
            program->clients[i].is_active = false;
            program->clients[i].socket_fd = 0;
        }
        pthread_mutex_unlock(&program->clients[i].lock);
    }
    
    pthread_mutex_unlock(&program->clients_lock);
}

// Initialize network program
void net_init_program(NetworkProgram* program) {
    if (!program) return;
    
    pthread_mutex_init(&program->clients_lock, NULL);
    program->running = true;
    
    for (int i = 0; i < NET_MAX_CLIENTS; i++) {
        net_init_client_state(&program->clients[i]);
    }
}

// Clean up network program
void net_cleanup_program(NetworkProgram* program) {
    if (!program) return;
    
    pthread_mutex_lock(&program->clients_lock);
    program->running = false;
    
    for (int i = 0; i < NET_MAX_CLIENTS; i++) {
        net_cleanup_client_state(&program->clients[i]);
    }
    
    pthread_mutex_unlock(&program->clients_lock);
    pthread_mutex_destroy(&program->clients_lock);
}

// Run network program
void net_run(NetworkProgram* program) {
    if (!program || !program->running) return;

    fd_set readfds;
    struct timeval tv = {
        .tv_sec = 1,  // 1 second timeout
        .tv_usec = 0
    };

    // Setup file descriptors
    FD_ZERO(&readfds);
    int max_fd = program->endpoints[0].socket_fd;
    FD_SET(max_fd, &readfds);

    // Add active clients
    pthread_mutex_lock(&program->clients_lock);
    for (int i = 0; i < NET_MAX_CLIENTS; i++) {
        pthread_mutex_lock(&program->clients[i].lock);
        if (program->clients[i].is_active) {
            int fd = program->clients[i].socket_fd;
            FD_SET(fd, &readfds);
            if (fd > max_fd) max_fd = fd;
        }
        pthread_mutex_unlock(&program->clients[i].lock);
    }
    pthread_mutex_unlock(&program->clients_lock);

    // Wait for activity with timeout
    int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);
    
    if (activity < 0) {
        if (errno != EINTR) {
            perror("select error");
        }
        return;
    }

    // Handle new connections
    if (FD_ISSET(program->endpoints[0].socket_fd, &readfds)) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int new_socket = accept(program->endpoints[0].socket_fd,
                              (struct sockaddr*)&client_addr,
                              &addr_len);

        if (new_socket >= 0) {
            // Set socket to non-blocking mode
            int flags = fcntl(new_socket, F_GETFL, 0);
            if (flags >= 0) {
                fcntl(new_socket, F_SETFL, flags | O_NONBLOCK);
            }

            // Add client
            if (net_add_client(program, new_socket, client_addr)) {
                NetworkEndpoint client_endpoint = {
                    .socket_fd = new_socket,
                    .addr = client_addr,
                    .phantom = program->phantom
                };
                
                if (program->handlers.on_connect) {
                    program->handlers.on_connect(&client_endpoint);
                }
            } else {
                close(new_socket);
            }
        }
    }

    // Handle client data
    pthread_mutex_lock(&program->clients_lock);
    for (int i = 0; i < NET_MAX_CLIENTS; i++) {
        pthread_mutex_lock(&program->clients[i].lock);
        if (program->clients[i].is_active &&
            FD_ISSET(program->clients[i].socket_fd, &readfds)) {
            
            char buffer[NET_BUFFER_SIZE];
            ssize_t bytes_read = recv(program->clients[i].socket_fd,
                                    buffer,
                                    sizeof(buffer) - 1,
                                    0);

            if (bytes_read <= 0) {
                // Handle disconnection
                NetworkEndpoint client_endpoint = {
                    .socket_fd = program->clients[i].socket_fd,
                    .addr = program->clients[i].addr,
                    .phantom = program->phantom
                };

                if (program->handlers.on_disconnect) {
                    program->handlers.on_disconnect(&client_endpoint);
                }
                
                net_remove_client(program, program->clients[i].socket_fd);
            } else {
                // Handle received data
                NetworkEndpoint client_endpoint = {
                    .socket_fd = program->clients[i].socket_fd,
                    .addr = program->clients[i].addr,
                    .phantom = program->phantom
                };

                NetworkPacket packet = {
                    .data = buffer,
                    .size = bytes_read,
                    .flags = 0
                };

                if (program->handlers.on_receive) {
                    program->handlers.on_receive(&client_endpoint, &packet);
                }
            }
        }
        pthread_mutex_unlock(&program->clients[i].lock);
    }
    pthread_mutex_unlock(&program->clients_lock);
}