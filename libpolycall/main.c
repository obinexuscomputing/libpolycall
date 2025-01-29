// main.c - PPI-Focused PolyCall Implementation
#include "polycall.h"
#include "polycall_protocol.h"
#include "polycall_state_machine.h"
#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define PPI_VERSION "1.0.0"
#define MAX_ENDPOINTS 16
#define MAX_PROGRAMS 8

typedef struct {
    NetworkProgram* programs[MAX_PROGRAMS];
    size_t program_count;
    polycall_context_t pc_ctx;
    bool running;
} PPI_Runtime;

// Global runtime instance
static PPI_Runtime g_runtime = {0};

// Forward declarations
static void handle_signal(int sig);
static void cleanup_runtime(void);
static bool initialize_runtime(void);
static void on_network_receive(NetworkEndpoint* endpoint, NetworkPacket* packet);
static void on_network_connect(NetworkEndpoint* endpoint);
static void on_network_disconnect(NetworkEndpoint* endpoint);
static void on_protocol_handshake(polycall_protocol_context_t* ctx);
static void on_protocol_auth(polycall_protocol_context_t* ctx, const char* credentials);
static void on_protocol_command(polycall_protocol_context_t* ctx, const char* command, size_t length);
static void on_protocol_error(polycall_protocol_context_t* ctx, const char* error);

// Signal handler implementation
static void handle_signal(int sig) {
    printf("\nReceived signal %d, shutting down...\n", sig);
    g_runtime.running = false;
}

// Runtime cleanup
static void cleanup_runtime(void) {
    for (size_t i = 0; i < g_runtime.program_count; i++) {
        if (g_runtime.programs[i]) {
            net_cleanup_program(g_runtime.programs[i]);
            free(g_runtime.programs[i]);
        }
    }
    
    if (g_runtime.pc_ctx) {
        polycall_cleanup(g_runtime.pc_ctx);
    }
    
    memset(&g_runtime, 0, sizeof(g_runtime));
}

// Initialize PPI runtime
static bool initialize_runtime(void) {
    // Initialize PolyCall context
    polycall_config_t config = {
        .flags = 0,
        .memory_pool_size = 1024 * 1024, // 1MB
        .user_data = NULL
    };
    
    if (polycall_init_with_config(&g_runtime.pc_ctx, &config) != POLYCALL_SUCCESS) {
        fprintf(stderr, "Failed to initialize PolyCall context\n");
        return false;
    }
    
    // Create default network program
    NetworkProgram* program = calloc(1, sizeof(NetworkProgram));
    if (!program) {
        fprintf(stderr, "Failed to allocate network program\n");
        return false;
    }
    
    // Initialize network program
    net_init_program(program);
    
    // Set up network handlers
    program->handlers.on_receive = on_network_receive;
    program->handlers.on_connect = on_network_connect;
    program->handlers.on_disconnect = on_network_disconnect;
    
    // Add program to runtime
    g_runtime.programs[g_runtime.program_count++] = program;
    
    // Set up signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    g_runtime.running = true;
    return true;
}

// Network event handlers
static void on_network_receive(NetworkEndpoint* endpoint, NetworkPacket* packet) {
    if (!endpoint || !packet || !packet->data) return;
    
    polycall_protocol_context_t* proto_ctx = NULL;
    if (endpoint->user_data) {
        proto_ctx = (polycall_protocol_context_t*)endpoint->user_data;
    } else {
        // Create new protocol context
        polycall_protocol_config_t config = {
            .callbacks = {
                .on_handshake = on_protocol_handshake,
                .on_auth_request = on_protocol_auth,
                .on_command = on_protocol_command,
                .on_error = on_protocol_error
            },
            .flags = 0,
            .max_message_size = 4096,
            .timeout_ms = 5000,
            .user_data = NULL
        };
        
        proto_ctx = malloc(sizeof(polycall_protocol_context_t));
        if (!proto_ctx) {
            fprintf(stderr, "Failed to allocate protocol context\n");
            return;
        }
        
        if (!polycall_protocol_init(proto_ctx, g_runtime.pc_ctx, endpoint, &config)) {
            fprintf(stderr, "Failed to initialize protocol context\n");
            free(proto_ctx);
            return;
        }
        
        endpoint->user_data = proto_ctx;
    }
    
    // Process received packet
    if (!polycall_protocol_process(proto_ctx, packet->data, packet->size)) {
        fprintf(stderr, "Failed to process protocol message\n");
    }
}

static void on_network_connect(NetworkEndpoint* endpoint) {
    if (!endpoint) return;
    printf("New connection from %s:%d\n", 
           endpoint->address, 
           endpoint->port);
}

static void on_network_disconnect(NetworkEndpoint* endpoint) {
    if (!endpoint) return;
    
    printf("Connection closed from %s:%d\n", 
           endpoint->address, 
           endpoint->port);
           
    if (endpoint->user_data) {
        polycall_protocol_context_t* proto_ctx = (polycall_protocol_context_t*)endpoint->user_data;
        polycall_protocol_cleanup(proto_ctx);
        free(proto_ctx);
        endpoint->user_data = NULL;
    }
}

// Protocol event handlers
static void on_protocol_handshake(polycall_protocol_context_t* ctx) {
    if (!ctx) return;
    printf("Protocol handshake received\n");
    polycall_protocol_complete_handshake(ctx);
}

static void on_protocol_auth(polycall_protocol_context_t* ctx, const char* credentials) {
    if (!ctx || !credentials) return;
    printf("Authentication request received\n");
    polycall_protocol_authenticate(ctx, credentials, strlen(credentials));
}

static void on_protocol_command(polycall_protocol_context_t* ctx, const char* command, size_t length) {
    if (!ctx || !command || length == 0) return;
    printf("Received command: %.*s\n", (int)length, command);
    // Process command based on PPI requirements
}

static void on_protocol_error(polycall_protocol_context_t* ctx, const char* error) {
    if (!ctx || !error) return;
    fprintf(stderr, "Protocol error: %s\n", error);
}

// Main program entry
int main(void) {
    printf("PPI-Focused PolyCall Runtime v%s\n", PPI_VERSION);
    
    if (!initialize_runtime()) {
        fprintf(stderr, "Failed to initialize runtime\n");
        return 1;
    }
    
    printf("Runtime initialized. Press Ctrl+C to exit.\n");
    
    // Main event loop
    while (g_runtime.running) {
        for (size_t i = 0; i < g_runtime.program_count; i++) {
            if (g_runtime.programs[i]) {
                net_run(g_runtime.programs[i]);
            }
        }
    }
    
    cleanup_runtime();
    printf("Runtime shutdown complete.\n");
    return 0;
}