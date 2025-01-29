#include "polycall.h"
#include "polycall_protocol.h"
#include "polycall_state_machine.h"
#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define PPI_VERSION "1.0.0"
#define MAX_INPUT 256
#define HISTORY_SIZE 10
#define MAX_ENDPOINTS 16
#define MAX_PROGRAMS 8

// Global state
typedef struct {
    NetworkProgram* programs[MAX_PROGRAMS];
    size_t program_count;
    polycall_context_t pc_ctx;
    PolyCall_StateMachine* state_machine;
    char command_history[HISTORY_SIZE][MAX_INPUT];
    int history_count;
    PolyCall_StateSnapshot snapshots[POLYCALL_MAX_STATES];
    bool has_snapshot[POLYCALL_MAX_STATES];
#ifdef _WIN32
    bool wsaInitialized;
#endif
    bool running;
} PPI_Runtime;

static PPI_Runtime g_runtime = {0};

// State machine callbacks
static void on_init(polycall_context_t ctx) {
    (void)ctx;
    printf("State callback: System initialized\n");
}

static void on_ready(polycall_context_t ctx) {
    (void)ctx;
    printf("State callback: System ready\n");
}

static void on_running(polycall_context_t ctx) {
    (void)ctx;
    printf("State callback: System running\n");
}

static void on_paused(polycall_context_t ctx) {
    (void)ctx;
    printf("State callback: System paused\n");
}

static void on_error(polycall_context_t ctx) {
    (void)ctx;
    printf("State callback: System error\n");
    g_runtime.running = false;
}

// Helper functions
static void add_to_history(const char* command) {
    if (g_runtime.history_count < HISTORY_SIZE) {
        strncpy(g_runtime.command_history[g_runtime.history_count++], command, MAX_INPUT - 1);
    } else {
        memmove(g_runtime.command_history[0], g_runtime.command_history[1], 
                (HISTORY_SIZE - 1) * MAX_INPUT);
        strncpy(g_runtime.command_history[HISTORY_SIZE - 1], command, MAX_INPUT - 1);
    }
}

static void print_help(void) {
    printf("\nPolyCall CLI Commands:\n");
    printf("Network Commands:\n");
    printf("  start_network          - Start network services\n");
    printf("  stop_network           - Stop network services\n");
    printf("  list_endpoints         - List all network endpoints\n");
    printf("  list_clients          - List connected clients\n");
    
    printf("\nState Machine Commands:\n");
    printf("  init                  - Initialize the state machine\n");
    printf("  add_state NAME        - Add a new state\n");
    printf("  add_transition NAME FROM TO - Add a transition\n");
    printf("  execute NAME          - Execute a transition\n");
    printf("  lock STATE_ID         - Lock a state\n");
    printf("  unlock STATE_ID       - Unlock a state\n");
    printf("  verify STATE_ID       - Verify state integrity\n");
    printf("  snapshot STATE_ID     - Create state snapshot\n");
    printf("  restore STATE_ID      - Restore from snapshot\n");
    printf("  diagnostics STATE_ID  - Get state diagnostics\n");
    
    printf("\nMiscellaneous Commands:\n");
    printf("  list_states          - List all states\n");
    printf("  list_transitions     - List all transitions\n");
    printf("  history              - Show command history\n");
    printf("  status              - Show system status\n");
    printf("  help                - Show this help message\n");
    printf("  quit                - Exit the program\n");
}

static void list_states(void) {
    if (!g_runtime.state_machine) {
        printf("State machine not initialized\n");
        return;
    }

    printf("\nStates:\n");
    for (unsigned int i = 0; i < g_runtime.state_machine->num_states; i++) {
        printf("  %u: %s (locked: %s)\n", 
               i, 
               g_runtime.state_machine->states[i].name, 
               g_runtime.state_machine->states[i].is_locked ? "yes" : "no");
    }
}

static void list_transitions(void) {
    if (!g_runtime.state_machine) {
        printf("State machine not initialized\n");
        return;
    }

    printf("\nTransitions:\n");
    for (unsigned int i = 0; i < g_runtime.state_machine->num_transitions; i++) {
        printf("  %s: %u -> %u\n", 
               g_runtime.state_machine->transitions[i].name,
               g_runtime.state_machine->transitions[i].from_state,
               g_runtime.state_machine->transitions[i].to_state);
    }
}

static void show_history(void) {
    printf("\nCommand History:\n");
    for (int i = 0; i < g_runtime.history_count; i++) {
        printf("  %d: %s\n", i + 1, g_runtime.command_history[i]);
    }
}

static void list_endpoints(void) {
    for (size_t i = 0; i < g_runtime.program_count; i++) {
        NetworkProgram* program = g_runtime.programs[i];
        if (program && program->endpoints) {
            printf("\nProgram %zu Endpoints:\n", i);
            for (size_t j = 0; j < program->count; j++) {
                NetworkEndpoint* ep = &program->endpoints[j];
                printf("  Endpoint %zu: %s:%d (%s)\n",
                       j,
                       ep->address,
                       ep->port,
                       ep->protocol == NET_TCP ? "TCP" : "UDP");
            }
        }
    }
}

static void list_clients(void) {
    for (size_t i = 0; i < g_runtime.program_count; i++) {
        NetworkProgram* program = g_runtime.programs[i];
        if (program) {
            printf("\nProgram %zu Clients:\n", i);
            pthread_mutex_lock(&program->clients_lock);
            for (int j = 0; j < NET_MAX_CLIENTS; j++) {
                pthread_mutex_lock(&program->clients[j].lock);
                if (program->clients[j].is_active) {
                    printf("  Client %d: Connected\n", j);
                }
                pthread_mutex_unlock(&program->clients[j].lock);
            }
            pthread_mutex_unlock(&program->clients_lock);
        }
    }
}

static void show_status(void) {
    printf("\nSystem Status:\n");
    printf("  State Machine: %s\n", g_runtime.state_machine ? "Initialized" : "Not initialized");
    printf("  Network Programs: %zu\n", g_runtime.program_count);
    printf("  Running: %s\n", g_runtime.running ? "Yes" : "No");
    
    if (g_runtime.state_machine) {
        printf("  Current State: %u\n", g_runtime.state_machine->current_state);
    }
    
    list_endpoints();
    list_clients();
}

// Initialize runtime
static bool initialize_runtime(void) {
#ifdef _WIN32
    // Initialize Windows Sockets
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "Failed to initialize Winsock\n");
        return false;
    }
    g_runtime.wsaInitialized = true;
#endif

    // Initialize PolyCall context
    polycall_config_t config = {
        .flags = 0,
        .memory_pool_size = 1024 * 1024,
        .user_data = NULL
    };

    if (polycall_init_with_config(&g_runtime.pc_ctx, &config) != POLYCALL_SUCCESS) {
        fprintf(stderr, "Failed to initialize PolyCall context\n");
        return false;
    }

    // Initialize state machine
    if (polycall_sm_create_with_integrity(g_runtime.pc_ctx, &g_runtime.state_machine, NULL) 
        != POLYCALL_SM_SUCCESS) {
        fprintf(stderr, "Failed to create state machine\n");
        return false;
    }

    // Add default states
    polycall_sm_add_state(g_runtime.state_machine, "INIT", on_init, NULL, false);
    polycall_sm_add_state(g_runtime.state_machine, "READY", on_ready, NULL, false);
    polycall_sm_add_state(g_runtime.state_machine, "RUNNING", on_running, NULL, false);
    polycall_sm_add_state(g_runtime.state_machine, "PAUSED", on_paused, NULL, false);
    polycall_sm_add_state(g_runtime.state_machine, "ERROR", on_error, NULL, true);

    g_runtime.running = true;
    return true;
}

// Cleanup runtime
static void cleanup_runtime(void) {
    for (size_t i = 0; i < g_runtime.program_count; i++) {
        if (g_runtime.programs[i]) {
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
        polycall_cleanup(g_runtime.pc_ctx);
        g_runtime.pc_ctx = NULL;
    }

#ifdef _WIN32
    if (g_runtime.wsaInitialized) {
        WSACleanup();
        g_runtime.wsaInitialized = false;
    }
#endif
}
// Add these handlers to your main.c before the main() function

static void on_network_receive(NetworkEndpoint* endpoint, NetworkPacket* packet) {
    if (!endpoint || !packet || !packet->data) return;
    
    printf("Received data: %.*s\n", (int)packet->size, (char*)packet->data);
    
    // Echo back for now
    NetworkPacket response = {
        .data = packet->data,
        .size = packet->size,
        .flags = 0
    };
    
    net_send(endpoint, &response);
}

static void on_network_connect(NetworkEndpoint* endpoint) {
    printf("\nNew connection from %s:%d\n> ", 
           endpoint->address, 
           endpoint->port);
    fflush(stdout);
}

static void on_network_disconnect(NetworkEndpoint* endpoint) {
    printf("\nClient disconnected from %s:%d\n> ", 
           endpoint->address, 
           endpoint->port);
    fflush(stdout);
}


// Main program
int main(void) {
    char input[MAX_INPUT];
    char *command, *arg1, *arg2, *arg3;
    
    printf("PolyCall CLI v%s - Type 'help' for commands\n", PPI_VERSION);

    if (!initialize_runtime()) {
        fprintf(stderr, "Failed to initialize runtime\n");
        return 1;
    }

    while (g_runtime.running) {
        printf("\n> ");
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }

        // Remove newline
        input[strcspn(input, "\n")] = 0;
        
        // Skip empty lines
        if (strlen(input) == 0) {
            continue;
        }

        add_to_history(input);

        // Parse command and arguments
        command = strtok(input, " ");
        arg1 = strtok(NULL, " ");
        arg2 = strtok(NULL, " ");
        arg3 = strtok(NULL, " ");

        if (!command) continue;

        if (strcmp(command, "quit") == 0) {
            break;
        } else if (strcmp(command, "help") == 0) {
            print_help();
    // Modify the start_network command in main() to set these handlers:
} else if (strcmp(command, "start_network") == 0) {
    NetworkProgram* program = calloc(1, sizeof(NetworkProgram));
    if (program) {
        net_init_program(program);
        if (program->endpoints && program->count > 0) {
            // Set up handlers
            program->handlers.on_receive = on_network_receive;
            program->handlers.on_connect = on_network_connect;
            program->handlers.on_disconnect = on_network_disconnect;
            
            g_runtime.programs[g_runtime.program_count++] = program;
            printf("Network services started\n");
        } else {
            free(program);
            printf("Failed to start network services\n");
        }
    }
        } else if (strcmp(command, "stop_network") == 0) {
            for (size_t i = 0; i < g_runtime.program_count; i++) {
                if (g_runtime.programs[i]) {
                    net_cleanup_program(g_runtime.programs[i]);
                    free(g_runtime.programs[i]);
                    g_runtime.programs[i] = NULL;
                }
            }
            g_runtime.program_count = 0;
            printf("Network services stopped\n");
        } else if (strcmp(command, "list_endpoints") == 0) {
            list_endpoints();
        } else if (strcmp(command, "list_clients") == 0) {
            list_clients();
        } else if (strcmp(command, "list_states") == 0) {
            list_states();
        } else if (strcmp(command, "list_transitions") == 0) {
            list_transitions();
        } else if (strcmp(command, "history") == 0) {
            show_history();
        } else if (strcmp(command, "status") == 0) {
            show_status();
        } else if (strcmp(command, "add_state") == 0) {
            if (!g_runtime.state_machine) {
                printf("State machine not initialized\n");
                continue;
            }
            if (!arg1) {
                printf("Usage: add_state NAME\n");
                continue;
            }
            if (polycall_sm_add_state(g_runtime.state_machine, arg1, NULL, NULL, false) 
                == POLYCALL_SM_SUCCESS) {
                printf("State '%s' added successfully\n", arg1);
            } else {
                printf("Failed to add state\n");
            }
        }
        // ... Handle other state machine commands similarly ...
        else {
            printf("Unknown command. Type 'help' for available commands\n");
        }
    }

    cleanup_runtime();
    printf("Goodbye!\n");
    return 0;
}