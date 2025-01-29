// demo.c - Simple test program
#include "path/to/polycall.h"
#include "path/to/polycall_state_machine.h"
#include <stdio.h>

// Needed for actual implementation
struct polycall_context {
    int dummy;
};

polycall_status_t polycall_init_with_config(polycall_context_t* ctx, const polycall_config_t* config) {
    *ctx = malloc(sizeof(struct polycall_context));
    return POLYCALL_SUCCESS;
}

void polycall_cleanup(polycall_context_t ctx) {
    free(ctx);
}

void on_init(polycall_context_t ctx) {
    printf("System initialized\n");
}

void on_ready(polycall_context_t ctx) {
    printf("System ready\n");
}

void on_running(polycall_context_t ctx) {
    printf("System running\n");
}

int main() {
    polycall_context_t ctx;
    PolyCall_StateMachine* sm;
    polycall_config_t config = {0};

    if (polycall_init_with_config(&ctx, &config) != POLYCALL_SUCCESS) {
        printf("Failed to initialize PolyCall\n");
        return 1;
    }

    if (polycall_sm_create_with_integrity(ctx, &sm, NULL) != POLYCALL_SM_SUCCESS) {
        printf("Failed to create state machine\n");
        polycall_cleanup(ctx);
        return 1;
    }

    polycall_sm_add_state(sm, "INIT", on_init, NULL, false);
    polycall_sm_add_state(sm, "READY", on_ready, NULL, false);
    polycall_sm_add_state(sm, "RUNNING", on_running, NULL, false);

    polycall_sm_add_transition(sm, "init_to_ready", 0, 1, NULL, NULL);
    polycall_sm_add_transition(sm, "ready_to_running", 1, 2, NULL, NULL);

    PolyCall_StateDiagnostics diag;
    polycall_sm_verify_state_integrity(sm, 0);
    polycall_sm_execute_transition(sm, "init_to_ready");
    polycall_sm_get_state_diagnostics(sm, 1, &diag);
    
    polycall_sm_verify_state_integrity(sm, 1);
    polycall_sm_execute_transition(sm, "ready_to_running");
    polycall_sm_get_state_diagnostics(sm, 2, &diag);

    polycall_sm_destroy(sm);
    polycall_cleanup(ctx);
    return 0;
}