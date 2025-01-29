#include "polycall_state_machine.h"
#include "polycall.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
// Utility functions
static inline uint32_t calculate_state_checksum(const PolyCall_State* state) {
    uint32_t checksum = 0;
    const uint8_t* data = (const uint8_t*)state;
    size_t size = offsetof(PolyCall_State, checksum);
    
    for (size_t i = 0; i < size; i++) {
        checksum = (checksum << 8) | (checksum >> 24);
        checksum += data[i];
    }
    return checksum;
}

static inline void update_state_timestamp(PolyCall_State* state) {
    state->timestamp = (uint64_t)time(NULL);
    state->version++;
}
/* Static helper functions */
static inline uint32_t calculate_state_checksum(const PolyCall_State* state) {
    uint32_t checksum = 0;
    const uint8_t* data = (const uint8_t*)state;
    size_t size = offsetof(PolyCall_State, checksum);
    
    for (size_t i = 0; i < size; i++) {
        checksum = (checksum << 8) | (checksum >> 24);
        checksum += data[i];
    }
    return checksum;
}

static inline void update_state_timestamp(PolyCall_State* state) {
    state->timestamp = (uint64_t)time(NULL);
    state->version++;
}

/* Core state machine functions */

polycall_sm_status_t polycall_sm_create_with_integrity(
    polycall_context_t ctx, 
    PolyCall_StateMachine** sm,
    PolyCall_StateIntegrityCheck integrity_check
) {
    if (!ctx || !sm) return POLYCALL_SM_ERROR_INVALID_CONTEXT;
    
    *sm = (PolyCall_StateMachine*)calloc(1, sizeof(PolyCall_StateMachine));
    if (!*sm) return POLYCALL_SM_ERROR_NOT_INITIALIZED;
    
    (*sm)->ctx = ctx;
    (*sm)->is_initialized = true;
    (*sm)->integrity_check = integrity_check;
    (*sm)->diagnostics.last_verification = (uint64_t)time(NULL);
    (*sm)->machine_checksum = 0;
    
    return POLYCALL_SM_SUCCESS;
}

void polycall_sm_destroy(PolyCall_StateMachine* sm) {
    if (sm) {
        /* Clear sensitive data before freeing */
        memset(sm, 0, sizeof(PolyCall_StateMachine));
        free(sm);
    }
}

/* State management functions */

polycall_sm_status_t polycall_sm_add_state(
    PolyCall_StateMachine* sm,
    const char* name,
    PolyCall_StateAction on_enter,
    PolyCall_StateAction on_exit,
    bool is_final
) {
    if (!sm || !sm->is_initialized || !name) 
        return POLYCALL_SM_ERROR_INVALID_STATE;
    
    if (sm->num_states >= POLYCALL_MAX_STATES) 
        return POLYCALL_SM_ERROR_MAX_STATES_REACHED;

    PolyCall_State* state = &sm->states[sm->num_states];
    
    /* Initialize state */
    strncpy(state->name, name, POLYCALL_MAX_NAME_LENGTH - 1);
    state->name[POLYCALL_MAX_NAME_LENGTH - 1] = '\0';
    state->on_enter = on_enter;
    state->on_exit = on_exit;
    state->is_final = is_final;
    state->id = sm->num_states;
    state->version = 1;
    state->is_locked = false;

    update_state_timestamp(state);
    state->checksum = calculate_state_checksum(state);
    
    sm->num_states++;
    return POLYCALL_SM_SUCCESS;
}

/* Transition management functions */

polycall_sm_status_t polycall_sm_add_transition(
    PolyCall_StateMachine* sm,
    const char* name,
    unsigned int from_state,
    unsigned int to_state,
    PolyCall_StateAction action,
    bool (*guard_condition)(const PolyCall_State*, const PolyCall_State*)
) {
    if (!sm || !sm->is_initialized || !name) 
        return POLYCALL_SM_ERROR_INVALID_TRANSITION;
    
    if (sm->num_transitions >= POLYCALL_MAX_TRANSITIONS) 
        return POLYCALL_SM_ERROR_MAX_TRANSITIONS_REACHED;
    
    if (from_state >= sm->num_states || to_state >= sm->num_states) 
        return POLYCALL_SM_ERROR_INVALID_STATE;

    PolyCall_Transition* transition = &sm->transitions[sm->num_transitions];
    
    /* Initialize transition */
    strncpy(transition->name, name, POLYCALL_MAX_NAME_LENGTH - 1);
    transition->name[POLYCALL_MAX_NAME_LENGTH - 1] = '\0';
    transition->from_state = from_state;
    transition->to_state = to_state;
    transition->action = action;
    transition->guard_condition = guard_condition;
    transition->is_valid = true;

    sm->num_transitions++;
    return POLYCALL_SM_SUCCESS;
}

polycall_sm_status_t polycall_sm_execute_transition(
    PolyCall_StateMachine* sm,
    const char* transition_name
) {
    if (!sm || !sm->is_initialized || !transition_name) 
        return POLYCALL_SM_ERROR_INVALID_TRANSITION;

    /* Find the requested transition */
    PolyCall_Transition* transition = NULL;
    for (unsigned int i = 0; i < sm->num_transitions; i++) {
        if (strcmp(sm->transitions[i].name, transition_name) == 0) {
            transition = &sm->transitions[i];
            break;
        }
    }

    if (!transition || !transition->is_valid) {
        sm->diagnostics.failed_transitions++;
        return POLYCALL_SM_ERROR_INVALID_TRANSITION;
    }

    PolyCall_State* from_state = &sm->states[transition->from_state];
    PolyCall_State* to_state = &sm->states[transition->to_state];

    /* Check state locks and guard conditions */
    if (from_state->is_locked || to_state->is_locked) 
        return POLYCALL_SM_ERROR_STATE_LOCKED;
    
    if (transition->guard_condition && 
        !transition->guard_condition(from_state, to_state)) {
        sm->diagnostics.failed_transitions++;
        return POLYCALL_SM_ERROR_INVALID_TRANSITION;
    }

    /* Execute transition actions */
    if (from_state->on_exit) from_state->on_exit(sm->ctx);
    if (transition->action) transition->action(sm->ctx);
    if (to_state->on_enter) to_state->on_enter(sm->ctx);

    /* Update state machine */
    sm->current_state = transition->to_state;
    update_state_timestamp(to_state);

    return POLYCALL_SM_SUCCESS;
}

/* Integrity verification functions */

polycall_sm_status_t polycall_sm_verify_state_integrity(
    PolyCall_StateMachine* sm,
    unsigned int state_id
) {
    if (!sm || !sm->is_initialized) 
        return POLYCALL_SM_ERROR_NOT_INITIALIZED;
    
    if (state_id >= sm->num_states) 
        return POLYCALL_SM_ERROR_INVALID_STATE;

    PolyCall_State* state = &sm->states[state_id];
    uint32_t current_checksum = calculate_state_checksum(state);

    if (current_checksum != state->checksum) {
        sm->diagnostics.integrity_violations++;
        return POLYCALL_SM_ERROR_INTEGRITY_CHECK_FAILED;
    }

    if (sm->integrity_check && !sm->integrity_check(state)) {
        sm->diagnostics.integrity_violations++;
        return POLYCALL_SM_ERROR_INTEGRITY_CHECK_FAILED;
    }

    return POLYCALL_SM_SUCCESS;
}

/* State locking functions */

polycall_sm_status_t polycall_sm_lock_state(
    PolyCall_StateMachine* sm,
    unsigned int state_id
) {
    if (!sm || !sm->is_initialized) 
        return POLYCALL_SM_ERROR_NOT_INITIALIZED;
    
    if (state_id >= sm->num_states) 
        return POLYCALL_SM_ERROR_INVALID_STATE;

    sm->states[state_id].is_locked = true;
    update_state_timestamp(&sm->states[state_id]);
    return POLYCALL_SM_SUCCESS;
}

polycall_sm_status_t polycall_sm_unlock_state(
    PolyCall_StateMachine* sm,
    unsigned int state_id
) {
    if (!sm || !sm->is_initialized) 
        return POLYCALL_SM_ERROR_NOT_INITIALIZED;
    
    if (state_id >= sm->num_states) 
        return POLYCALL_SM_ERROR_INVALID_STATE;

    sm->states[state_id].is_locked = false;
    update_state_timestamp(&sm->states[state_id]);
    return POLYCALL_SM_SUCCESS;
}

/* Snapshot and restoration functions */

polycall_sm_status_t polycall_sm_create_state_snapshot(
    const PolyCall_StateMachine* sm,
    unsigned int state_id,
    PolyCall_StateSnapshot* snapshot
) {
    if (!sm || !sm->is_initialized || !snapshot) 
        return POLYCALL_SM_ERROR_NOT_INITIALIZED;
    
    if (state_id >= sm->num_states) 
        return POLYCALL_SM_ERROR_INVALID_STATE;

    const PolyCall_State* state = &sm->states[state_id];
    memcpy(&snapshot->state, state, sizeof(PolyCall_State));
    snapshot->timestamp = (uint64_t)time(NULL);
    snapshot->checksum = calculate_state_checksum(state);

    return POLYCALL_SM_SUCCESS;
}

polycall_sm_status_t polycall_sm_restore_state_from_snapshot(
    PolyCall_StateMachine* sm,
    const PolyCall_StateSnapshot* snapshot
) {
    if (!sm || !sm->is_initialized || !snapshot) 
        return POLYCALL_SM_ERROR_NOT_INITIALIZED;
    
    if (snapshot->state.id >= sm->num_states) 
        return POLYCALL_SM_ERROR_INVALID_STATE;

    PolyCall_State* state = &sm->states[snapshot->state.id];
    
    if (state->is_locked) 
        return POLYCALL_SM_ERROR_STATE_LOCKED;
    
    if (state->version != snapshot->state.version) 
        return POLYCALL_SM_ERROR_VERSION_MISMATCH;

    memcpy(state, &snapshot->state, sizeof(PolyCall_State));
    update_state_timestamp(state);

    return POLYCALL_SM_SUCCESS;
}

/* Version and diagnostic functions */

polycall_sm_status_t polycall_sm_get_state_version(
    const PolyCall_StateMachine* sm,
    unsigned int state_id,
    unsigned int* version
) {
    if (!sm || !sm->is_initialized || !version) 
        return POLYCALL_SM_ERROR_NOT_INITIALIZED;
    
    if (state_id >= sm->num_states) 
        return POLYCALL_SM_ERROR_INVALID_STATE;
    
    *version = sm->states[state_id].version;
    return POLYCALL_SM_SUCCESS;
}

polycall_sm_status_t polycall_sm_get_state_diagnostics(
    const PolyCall_StateMachine* sm,
    unsigned int state_id,
    PolyCall_StateDiagnostics* diagnostics
) {
    if (!sm || !sm->is_initialized || !diagnostics) 
        return POLYCALL_SM_ERROR_NOT_INITIALIZED;
    
    if (state_id >= sm->num_states) 
        return POLYCALL_SM_ERROR_INVALID_STATE;

    const PolyCall_State* state = &sm->states[state_id];
    
    diagnostics->state_id = state->id;
    diagnostics->creation_time = state->timestamp;
    diagnostics->last_modified = state->timestamp;
    diagnostics->is_locked = state->is_locked;
    diagnostics->current_checksum = state->checksum;
    diagnostics->transition_count = 0;  /* Updated in future implementation */
    diagnostics->integrity_check_count = 0;  /* Updated in future implementation */

    return POLYCALL_SM_SUCCESS;
}