#include "concurrent_system.h"
#include "common.h"
#include "hashtable.h"
#include "fail.h"

thread_local tid_t tid_self = TID_INVALID;

struct concurrent_system
{
    tid_t tid_next;                                         /* Next available thread slot */
    thread threads[MAX_TOTAL_THREADS_PER_SCHEDULE];         /* Thread identities for all threads in the system */

    mutid_t mut_next;                                       /* Next available mutex slot */
    hash_table_ref mutex_map;                               /* Maps pthread_mutex_t* to struct mutex* (pointer into the static array) */
    mutex locks[MAX_MUTEX_OBJECT_COUNT];                    /* Memory backing the mutexes in the map */

    int t_stack_top;                                        /* Points to the top of the transition stack */
    transition t_stack[MAX_VISIBLE_OPERATION_DEPTH];        /* *** TRANSITION STACK *** */
    transition t_next[MAX_TOTAL_THREADS_PER_SCHEDULE];      /* Storage for each next transition (next(s, p)) = for each thread */
    hash_table_ref transition_map;                          /* Maps `struct thread*` to `transition_ref` in `t_next` */

    int s_stack_top;                                        /* Points to the top of the state stack */
    state_stack_item s_stack[MAX_VISIBLE_OPERATION_DEPTH];  /* *** STATE STACK *** */

    // *** BACKTRACKING ***
    bool is_backtracking;                                   /* Whether or not the system is performing backtrack analysis*/
    int detached_t_top;                                     /* Points to the top of the transition stack in the current backtrack */
};

concurrent_system csystem; /* Global concurrent system for the program */

void
csystem_init(concurrent_system_ref ref)
{
    ref->tid_next = 0;
    ref->t_stack_top = -1;
    ref->s_stack_top = -1;
    ref->mut_next = -1;
    ref->mutex_map = hash_table_create();
    ref->transition_map = hash_table_create();

    hash_table_set_hash_function(ref->transition_map, (hash_function)thread_hash);
    hash_table_set_hash_function(ref->mutex_map, (hash_function)mutex_hash);

    // Push the initial first state (the starting state) onto the state stack explicitly
    csystem_grow_state_stack(&csystem);
}

void
csystem_reset(concurrent_system_ref ref)
{
    hash_table_destroy(ref->mutex_map);
    hash_table_destroy(ref->transition_map);
    csystem_init(ref);
}

tid_t
csystem_register_thread(concurrent_system_ref ref)
{
    tid_t self = ref->tid_next++;
    tid_self = self;
    thread_ref tself = &ref->threads[self];
    tself->arg = NULL;
    tself->start_routine = NULL;
    tself->owner = pthread_self();
    tself->tid = self;
    tself->is_alive = true;
    return self;
}

tid_t
csystem_register_main_thread(concurrent_system_ref ref)
{
    tid_t main = csystem_register_thread(ref);
    mc_assert(main == TID_MAIN_THREAD);
    return TID_MAIN_THREAD;
}

thread_ref
csystem_get_thread_with_tid(concurrent_system_ref ref, tid_t tid)
{
    if (tid == TID_INVALID) return NULL;
    return &ref->threads[tid];
}

inline thread_ref
csystem_get_thread_with_pthread(concurrent_system_ref ref, pthread_t *pthread)
{

}

inline mutex_ref
csystem_get_mutex_with_pthread(concurrent_system_ref ref, pthread_mutex_t *mutex)
{
    return (mutex_ref) hash_table_get(ref->mutex_map, (hash_t)mutex);
}

static void
csystem_virtually_apply_mutex_operation(concurrent_system_ref ref, mutex_operation_ref mutop)
{
    pthread_mutex_t *mutex = mutop->mutex.mutex;
    mutex_ref shadow = NULL;

    switch (mutop->type) {
    case MUTEX_INIT:;
            shadow = &ref->locks[++ref->mut_next];
            *shadow = mutop->mutex;
            hash_table_set_implicit(ref->mutex_map, mutex, shadow);
            break;
    case MUTEX_LOCK:;
            shadow = hash_table_get_implicit(ref->mutex_map, mutex);
            shadow->state = MUTEX_LOCKED;
            break;
        case MUTEX_UNLOCK:
            shadow = hash_table_get_implicit(ref->mutex_map, mutex);
            shadow->state = MUTEX_UNLOCKED;
            break;
        default:
        mc_unimplemented();
    }
}

static void
csystem_virtually_apply_thread_operation(concurrent_system_ref ref, thread_operation_ref mutop)
{

}

static void
csystem_virtually_apply_transition(concurrent_system_ref ref, transition_ref transition)
{
    // TODO: Report undefined behavior here

    switch (transition->operation.type) {
        case MUTEX:;
            csystem_virtually_apply_mutex_operation(ref, &transition->operation.mutex_operation);
            break;
        case THREAD_LIFECYCLE:;
            csystem_virtually_apply_thread_operation(ref, &transition->operation.thread_operation);
            break;
        default:
            mc_unimplemented();
    }
}

static void
csystem_virtually_revert_mutex_operation(concurrent_system_ref ref, mutex_operation_ref mutop)
{
    pthread_mutex_t *mutex = mutop->mutex.mutex;

    switch (mutop->type) {
        case MUTEX_INIT:;

            // Remove the mutex ONLY if we know it didn't already
            // exist. The `MUTEX_UNKNOWN` state defines any mutex whose
            // state is undefined. This differs from `MUTEX_DESTROYED`,
            // which is assigned after being destroyed. The contents
            // are still undefined, but we know why they are in the latter
            // case whereas in the former we don't. This distinction allows
            // use to tell when we have undefined behavior with an existing
            // mutex vs a new mutex entirely
            if (mutop->mutex.state == MUTEX_UNKNOWN) {
                hash_table_remove_implicit(ref->mutex_map, mutex);
                ref->mut_next--;
            }
            break;
        default:;
            mutex_ref shadow_mutex = csystem_get_mutex_with_pthread(ref, mutex);
            *shadow_mutex = mutop->mutex;
            break;
    }
}

static void
csystem_virtually_revert_thread_operation(concurrent_system_ref ref, thread_operation_ref top)
{

}

static void
csystem_virtually_revert_transition(concurrent_system_ref ref, transition_ref transition)
{
    transition_ref was_next = csystem_get_transition_slot_for_thread(ref, transition->thread);
    *was_next = *transition; /* t_top represents what executed BEFORE to reach the current next transition */

    switch (transition->operation.type) {
        case MUTEX:;
            csystem_virtually_revert_mutex_operation(ref, &transition->operation.mutex_operation);
            break;
        case THREAD_LIFECYCLE:;
            csystem_virtually_revert_thread_operation(ref, &transition->operation.thread_operation);
            break;
        default:
            mc_unimplemented();
    }
}

state_stack_item_ref
csystem_grow_state_stack(concurrent_system_ref ref)
{
    state_stack_item_ref s_top = &ref->s_stack[++ref->s_stack_top];
    s_top->backtrack_set = hash_set_create((hash_function) thread_hash);
    s_top->done_set = hash_set_create((hash_function) thread_hash);
    mc_assert(ref->s_stack_top <= MAX_VISIBLE_OPERATION_DEPTH);
    return s_top;
}

inline state_stack_item_ref
csystem_shrink_state_stack(concurrent_system_ref ref)
{
    mc_assert(ref->s_stack_top >= 0);
    state_stack_item_ref s_top = &ref->s_stack[ref->s_stack_top--];
    return s_top;
}

inline transition_ref
csystem_grow_transition_stack(concurrent_system_ref ref, thread_ref thread)
{
    transition_ref t_top = &ref->t_stack[++ref->t_stack_top];
    mc_assert(ref->t_stack_top <= MAX_VISIBLE_OPERATION_DEPTH);

    transition_ref thread_runs = csystem_get_transition_slot_for_thread(ref, thread);
    csystem_virtually_apply_transition(ref, thread_runs);

    // Copy the contents of the transition into the the top of the transition stack
    ref->t_stack[ref->t_stack_top] = *thread_runs;

    return t_top;
}

static inline transition_ref
csystem_grow_transition_stack_restore(concurrent_system_ref ref, transition_ref transition)
{
    transition_ref t_top = &ref->t_stack[++ref->t_stack_top];
    mc_assert(ref->t_stack_top <= MAX_VISIBLE_OPERATION_DEPTH);

    csystem_virtually_apply_transition(ref, transition);

    // Copy the contents of the transition into the the top of the transition stack
    ref->t_stack[ref->t_stack_top] = *transition;
    return t_top;
}

inline transition_ref
csystem_shrink_transition_stack(concurrent_system_ref ref)
{
    mc_assert(ref->t_stack_top >= 0);
    transition_ref t_top = &ref->t_stack[ref->t_stack_top--];
    csystem_virtually_revert_transition(ref, t_top);
    return t_top;
}

inline transition_ref
csystem_get_transition_slot_for_thread(concurrent_system_ref ref, csystem_local thread_ref thread)
{
    return csystem_get_transition_slot_for_tid(ref, thread->tid);
}

inline transition_ref
csystem_get_transition_slot_for_tid(concurrent_system_ref ref, csystem_local tid_t tid)
{
    mc_assert(tid != TID_INVALID);
    return &ref->t_next[tid];
}

int
csystem_copy_enabled_transitions(concurrent_system_ref ref, transition_ref tref_array)
{
    int thread_count = csystem_get_thread_count(ref);
    int enabled_thread_count = 0;

    for (tid_t tid = 0; tid < thread_count; tid++) {
        transition_ref tid_transition = csystem_get_transition_slot_for_tid(ref, tid);

        if (transition_enabled(tid_transition)) {
            tref_array[enabled_thread_count++] = *tid_transition;
        }
    }
    return enabled_thread_count;
}

transition_ref
csystem_run(concurrent_system_ref ref, thread_ref thread)
{
    csystem_grow_transition_stack(ref, thread);
    csystem_grow_state_stack(ref);
}

inline transition_ref
csystem_transition_stack_get_element(concurrent_system_ref ref, int i)
{
    mc_assert(i >= 0 && i < MAX_VISIBLE_OPERATION_DEPTH);
    return &ref->t_stack[i];
}

inline transition_ref
csystem_transition_stack_top(concurrent_system_ref ref)
{
    if (csystem_transition_stack_is_empty(ref)) return NULL;
    return csystem_transition_stack_get_element(ref, ref->t_stack_top);
}

inline state_stack_item_ref
csystem_state_stack_top(concurrent_system_ref ref)
{
    mc_assert(ref->s_stack_top >= 0 && ref->s_stack_top < MAX_VISIBLE_OPERATION_DEPTH);
    return &ref->s_stack[ref->s_stack_top];
}

void
csystem_copy_per_thread_transitions(concurrent_system_ref ref, transition_ref tref_array)
{
    int thread_count = csystem_get_thread_count(ref);
    int enabled_thread_index = 0;

    for (tid_t tid = 0; tid < thread_count; tid++) {
        transition_ref tid_transition = csystem_get_transition_slot_for_tid(ref, tid);
        tref_array[enabled_thread_index++] = *tid_transition;
    }
}

static void
__csystem_replace_per_thread_transitions_for_backtracking(concurrent_system_ref ref, transition_ref tref_array)
{
    int thread_count = csystem_get_thread_count(ref);

    for (tid_t tid = 0; tid < thread_count; tid++) {
        transition_ref tid_transition = csystem_get_transition_slot_for_tid(ref, tid);
        *tid_transition = tref_array[tid];
    }
}

transition_ref
csystem_get_first_enabled_transition(concurrent_system_ref ref)
{
    for (int i = 0; i < MAX_TOTAL_THREADS_PER_SCHEDULE; i++) {
        transition_ref t_next_i = &ref->t_next[i];
        if (transition_enabled(t_next_i)) return t_next_i;
    }
    return NULL;
}

inline int
csystem_get_thread_count(concurrent_system_ref ref)
{
    return (int)ref->tid_next;
}

inline int
csystem_state_stack_count(concurrent_system_ref ref)
{
    return ref->s_stack_top + 1;
}

inline int
csystem_transition_stack_count(concurrent_system_ref ref)
{
    return ref->t_stack_top + 1;
}

inline bool
csystem_state_stack_is_empty(concurrent_system_ref ref)
{
    return ref->s_stack_top < 0;
}

inline bool
csystem_transition_stack_is_empty(concurrent_system_ref ref)
{
    return ref->t_stack_top < 0;
}

void
csystem_start_backtrack(concurrent_system_ref ref)
{
    mc_assert(!ref->is_backtracking);
    ref->is_backtracking = true;
    ref->detached_t_top = ref->t_stack_top;
}

void
csystem_end_backtrack(concurrent_system_ref ref)
{
    mc_assert(ref->is_backtracking);
    ref->is_backtracking = false;

    // We only expect to look backwards. Looking forward
    // makes no sense when backtracking. "Back" is in the name
    // after all...
    mc_assert(ref->t_stack_top < ref->detached_t_top);

    const bool no_transitions_left = csystem_transition_stack_is_empty(ref);
    const int cur_top = no_transitions_left ? 0 : ref->t_stack_top;
    const int dest_top = ref->detached_t_top;

    // NOTE: The assumption is that while
    // is_backtracking = true, we don't clear out the
    // contents of the array t_stack. If, in the future,
    // this *is* done, this code will break. The solution
    // is to simply check if we're backtracking in the spot
    // where clearing the stack was added and don't clear
    // it out in the case we are backtracking
    for (int i = cur_top; i <= dest_top; i++) {
        transition_ref transition_i = &ref->t_stack[i];
        csystem_grow_transition_stack_restore(ref, transition_i);
    }
    ref->detached_t_top = -1;
}

bool
happens_before(concurrent_system_ref ref, int i, int j)
{
    transition_ref t_i = &ref->t_stack[i];
    transition_ref t_j = &ref->t_stack[j];
    return i <= j && transitions_dependent(t_i, t_j);
}

bool
happens_before_thread(concurrent_system_ref ref, int i, thread_ref p)
{
    for (int k = i; k <= ref->t_stack_top; k++) {
        transition_ref S_k = &ref->t_stack[k];
        if (happens_before(ref, i, k) && threads_equal(p, S_k->thread))
            return true;
    }
    return false;
}

bool
csystem_p_q_could_race(concurrent_system_ref ref, int i, thread_ref q, thread_ref p)
{
    mc_assert(ref->is_backtracking);
    for (int j = i + 1; j <= ref->t_stack_top; j++) {
        transition_ref S_j = &ref->t_stack[j];
        if (threads_equal(p, S_j->thread) && happens_before_thread(ref, j, p))
            return true;
    }
    return false;
}

void
csystem_dynamically_update_backtrack_sets(concurrent_system_ref ref)
{
    csystem_start_backtrack(ref);

    // TODO: Technically, only a single array is needed. The enabled transitions
    // can simply refer to entries in the `transitions_at_s_top` array. For now, this suffices
    static transition transitions_at_s_top[MAX_TOTAL_THREADS_PER_SCHEDULE];
    static transition enabled_transitions_s_top[MAX_TOTAL_THREADS_PER_SCHEDULE];
    static transition scratch_enabled_at_i[MAX_TOTAL_THREADS_PER_SCHEDULE];

    // Maps `thread_ref` to a `transition_ref` pointing into the static array `enabled_transitions_s_top`
    static hash_table_ref s_top_enabled_transition_map = NULL;

    if (s_top_enabled_transition_map == NULL) {
        s_top_enabled_transition_map = hash_table_create();
        hash_table_set_hash_function(s_top_enabled_transition_map, (hash_function) thread_hash);
    }
    hash_table_clear(s_top_enabled_transition_map);

    int thread_count = csystem_get_thread_count(ref);
    int t_stack_height = csystem_transition_stack_count(ref);

    // 1. Store transitions at this state for restoration after backtracking later
    csystem_copy_per_thread_transitions(ref, transitions_at_s_top);

    // 2. Map enabled threads to their enabled transitions in the current state
    {
        int transitions_s_top_index = 0;
        for (tid_t tid = 0; tid < thread_count; tid++) {
            thread_ref tid_thread = csystem_get_thread_with_tid(ref, tid);
            transition_ref tid_transition = csystem_get_transition_slot_for_thread(ref, tid_thread);

            if (transition_enabled(tid_transition)) {
                transition_ref static_slot = &enabled_transitions_s_top[transitions_s_top_index++];
                *static_slot = *tid_transition;

                hash_table_set_implicit(s_top_enabled_transition_map,
                                        tid_thread,
                                        static_slot);
            }
        }
    }

    for (int i = t_stack_height - 1; i >= 0; i--) {
        transition_ref S_i = csystem_shrink_transition_stack(ref);
        state_stack_item_ref s_top_i = csystem_state_stack_top(ref);

        if (hash_table_is_empty(s_top_enabled_transition_map))
            break; /* Don't need to keep going further into the transition stack if everything is processed */

        for (int tid = 0; tid < thread_count; tid++) {
            thread_ref thread_for_tid = csystem_get_thread_with_tid(ref, tid);

            // Transition in the latest state stack (at this point lost but whose enabled transitions were copied into the
            // array `enabled_transitions_s_top`)
            transition_ref enabled_transition_from_most_recent_state_maybe = NULL;
            if ((enabled_transition_from_most_recent_state_maybe = hash_table_get_implicit(s_top_enabled_transition_map, thread_for_tid)) != NULL) {

                bool found_max_i_condition = transitions_coenabled(S_i, enabled_transition_from_most_recent_state_maybe) &&
                                             transitions_dependent(S_i, enabled_transition_from_most_recent_state_maybe) &&
                                             !happens_before_thread(ref, i, thread_for_tid);
                if (found_max_i_condition) {
                    thread_ref p = enabled_transition_from_most_recent_state_maybe->thread;

                    int num_enabled_at_s_i = csystem_copy_enabled_transitions(ref, scratch_enabled_at_i);
                    for (int s_i_index = 0; s_i_index < num_enabled_at_s_i; s_i_index++) {
                        transition_ref q_transition = &scratch_enabled_at_i[s_i_index];
                        thread_ref q = q_transition->thread;

                        bool q_is_in_E = threads_equal(q, p) || csystem_p_q_could_race(ref, i, q, p);
                        if (q_is_in_E) {
                            hash_set_insert(s_top_i->backtrack_set, q);

                            // TODO: Only one q is needed. An optimization
                            // where we check the done set for q first would
                            // reduce backtracking

                            // We're done now
                            goto search_over;
                        }
                    }

                    // If we exit the loop without hitting the `goto`, we
                    // know that `E` is the empty set. In this case, we must
                    // add every enabled thread to the backtrack set
                    for (int s_i_index = 0; s_i_index < num_enabled_at_s_i; s_i_index++) {
                        transition_ref q_transition = &scratch_enabled_at_i[s_i_index];
                        thread_ref q = q_transition->thread;
                        hash_set_insert(s_top_i->backtrack_set, q);
                    }

                    search_over:
                    // Remove this thread from needing to be searched again
                    hash_table_remove_implicit(s_top_enabled_transition_map, thread_for_tid);
                }
            }
        }
    }
    csystem_end_backtrack(ref);
    __csystem_replace_per_thread_transitions_for_backtracking(ref, transitions_at_s_top);
}