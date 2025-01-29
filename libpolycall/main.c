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
    PolyCall_StateMachine* state_machine;
    bool running;
} PPI_Runtime;

// Global runtime instance
static PPI_Runtime g_runtime = {0};

// State machine callbacks
static void on_init_state(polycall_context_t ctx) {
    DEBUG_PRINT("Entered INIT state");
}

static void on_ready_state(polycall_context_t ctx) {
    DEBUG_PRINT("Entered READY state");
}

static void on_error_state(polycall_context_t ctx) {
    DEBUG_PRINT("Entered ERROR state");
    g_runtime.running = false;
}

// Initialize state machine
static bool init_state_machine(void) {
    if (polycall_sm_create_with_integrity(g_runtime.pc_ctx, &g_runtime.state_machine, NULL) 
        != POLYCALL_SM_SUCCESS) {
        return false;
    }
    
    // Add states
    polycall_sm_add_state(g_runtime.state_machine, "INIT", on_init_state, NULL, false);
    polycall_sm_add_state(g_runtime.state_machine, "READY", on_ready_state, NULL, false);
    polycall_sm_add_state(g_runtime.state_machine, "ERROR", on_error_state, NULL, true);
    
    // Add transitions
    polycall_sm_add_transition(g_runtime.state_machine, "to_ready", 0, 1, NULL, NULL);
    polycall_sm_add_transition(g_runtime.state_machine, "to_error", 1, 2, NULL, NULL);
    
    return true;
}

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
    
    if (g_runtime.state_machine) {
        polycall_sm_destroy(g_runtime.state_machine);
        g_runtime.state_machine = NULL;
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
    memset(&g_runtime, 0, sizeof(g_runtime));
    
    // Initialize PolyCall context
    polycall_config_t config = {
        .flags = 0,
        .memory_pool_size = 1024 * 1024,
        .user_data = NULL
    };
    
    DEBUG_PRINT("Initializing PolyCall context");
    if (polycall_init_with_config(&g_runtime.pc_ctx, &config) != POLYCALL_SUCCESS) {
        fprintf(stderr, "Failed to initialize PolyCall context\n");
        return false;
    }
    
    // Initialize state machine
    DEBUG_PRINT("Initializing state machine");
    if (!init_state_machine()) {
        fprintf(stderr, "Failed to initialize state machine\n");
        cleanup_runtime();
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
    
    // Verify network initialization
    if (!program->endpoints || program->count == 0) {
        fprintf(stderr, "Network initialization failed\n");
        free(program);
        cleanup_runtime();
        return false;
    }
    
    // Add program to runtime
    g_runtime.programs[g_runtime.program_count++] = program;
    
    // Set up signal handlers
    DEBUG_PRINT("Setting up signal handlers");
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // Transition to ready state
    if (polycall_sm_execute_transition(g_runtime.state_machine, "to_ready") != POLYCALL_SM_SUCCESS) {
        fprintf(stderr, "Failed to transition to ready state\n");
        cleanup_runtime();
        return false;
    }
    
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
            NetworkProgram* program = g_runtime.programs[i];
            if (program && program->endpoints && program->count > 0) {
                // Verify endpoint is still valid
                if (program->endpoints[0].socket_fd <= 0) {
                    DEBUG_PRINT("Invalid socket detected, transitioning to error state");
                    polycall_sm_execute_transition(g_runtime.state_machine, "to_error");
                    continue;
                }
                net_run(program);
            }
        }
    }
    
    cleanup_runtime();
    printf("Runtime shutdown complete.\n");
    return 0;
}