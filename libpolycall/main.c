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
#define DEBUG_PRINT(fmt, ...) fprintf(stderr, "DEBUG: %s:%d: " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)

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

// Signal handler implementation
static void handle_signal(int sig) {
    DEBUG_PRINT("Received signal %d", sig);
    g_runtime.running = false;
}

// Runtime cleanup
static void cleanup_runtime(void) {
    DEBUG_PRINT("Cleaning up runtime");
    
    for (size_t i = 0; i < g_runtime.program_count; i++) {
        if (g_runtime.programs[i]) {
            DEBUG_PRINT("Cleaning up program %zu", i);
            net_cleanup_program(g_runtime.programs[i]);
            free(g_runtime.programs[i]);
            g_runtime.programs[i] = NULL;
        }
    }
    
    if (g_runtime.pc_ctx) {
        DEBUG_PRINT("Cleaning up PolyCall context");
        polycall_cleanup(g_runtime.pc_ctx);
        g_runtime.pc_ctx = NULL;
    }
    
    g_runtime.program_count = 0;
    g_runtime.running = false;
}

// Initialize PPI runtime
static bool initialize_runtime(void) {
    DEBUG_PRINT("Initializing runtime");

    // Clear runtime structure
    memset(&g_runtime, 0, sizeof(g_runtime));
    
    // Initialize PolyCall context
    polycall_config_t config = {
        .flags = 0,
        .memory_pool_size = 1024 * 1024, // 1MB
        .user_data = NULL
    };
    
    DEBUG_PRINT("Initializing PolyCall context");
    if (polycall_init_with_config(&g_runtime.pc_ctx, &config) != POLYCALL_SUCCESS) {
        fprintf(stderr, "Failed to initialize PolyCall context\n");
        return false;
    }
    
    // Verify context was created
    if (!g_runtime.pc_ctx) {
        fprintf(stderr, "PolyCall context is NULL after initialization\n");
        return false;
    }
    
    // Create default network program
    DEBUG_PRINT("Creating network program");
    NetworkProgram* program = calloc(1, sizeof(NetworkProgram));
    if (!program) {
        fprintf(stderr, "Failed to allocate network program\n");
        cleanup_runtime();
        return false;
    }
    
    // Initialize network program
    DEBUG_PRINT("Initializing network program");
    net_init_program(program);
    
    // Add program to runtime
    if (g_runtime.program_count >= MAX_PROGRAMS) {
        fprintf(stderr, "Maximum number of programs reached\n");
        free(program);
        cleanup_runtime();
        return false;
    }
    
    g_runtime.programs[g_runtime.program_count++] = program;
    
    // Set up signal handlers
    DEBUG_PRINT("Setting up signal handlers");
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    g_runtime.running = true;
    DEBUG_PRINT("Runtime initialization complete");
    return true;
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
    DEBUG_PRINT("Entering main loop");
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