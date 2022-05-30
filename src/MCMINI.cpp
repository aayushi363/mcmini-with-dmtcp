#include "MCMINI.h"
#include "MC_Private.h"
#include "MCSharedTransition.h"
#include "MCTransitionFactory.h"
#include "transitions/MCTransitionsShared.h"
#include <vector>

extern "C" {
    #include "mc_shared_cv.h"
    #include <cassert>
    #include <fcntl.h>
    #include <cstdio>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
}

/* The shmTransitionTypeInfo resides in shared memory */
/* The semaphores must also reside in shared memory as per the man page */

/* Synchronization primitives */
MC_THREAD_LOCAL tid_t tid_self = TID_INVALID;
pid_t cpid = -1;
mc_shared_cv (*threadQueue)[MAX_TOTAL_THREADS_IN_PROGRAM] = nullptr;
sem_t mc_pthread_create_binary_sem;

/*
 * Identifies the trace number of the model checker
 * Note that is we ever parallelized the program
 * this would be highly unsafe and would need to be atomic
 */
trid_t traceId = 0;
trid_t transitionId = 0;

/* Data transfer */
void *shmStart = nullptr;
MCSharedTransition *shmTransitionTypeInfo = nullptr;
void *shmTransitionData = nullptr;
const size_t shmAllocationSize =  sizeof(*threadQueue) + (sizeof(*shmTransitionTypeInfo) + MAX_SHARED_MEMORY_ALLOCATION);

/* Program state */
MCDeferred<MCState> programState;

MC_CTOR void
mc_init()
{
    mc_load_shadow_routines();
    mc_create_program_state();
    mc_initialize_shared_memory_region();
    mc_create_thread_sleep_points();

    MC_FATAL_ON_FAIL(__real_sem_init(&mc_pthread_create_binary_sem, 0, 0) == 0);

    MC_PROGRAM_TYPE program = mc_scheduler_main();
    if (MC_IS_SOURCE_PROGRAM(program)) return;

    mcprintf("***** Model checking completed! *****\n");
    mcprintf("Number of transitions: %lu\n", transitionId);
    mcprintf("Number of traces: %lu\n", traceId);
    __real_exit(EXIT_SUCCESS);
}

void
mc_create_program_state()
{
    // Registering tells mcmini what to do when a thread in the
    // target program encounters a specific type of transition;
    // viz. it describes how to create/find the shadow corresponding
    // to the transition in the target. If you don't register a transition
    // type with mcmini, it will simply ignore the transition (well it should,
    // if you still intercept the transition with a transparent wrapper and
    // wait as usual bad things will probably happen for now; ideally you'd
    // want to ignore the wrapper calling back to mcmini entirely)
    auto config = get_config_for_execution_environment();
    programState.Construct(config);
    programState->registerVisibleOperationType(typeid(MCThreadStart), &MCReadThreadStart);
    programState->registerVisibleOperationType(typeid(MCThreadCreate), &MCReadThreadCreate);
    programState->registerVisibleOperationType(typeid(MCThreadFinish), &MCReadThreadFinish);
    programState->registerVisibleOperationType(typeid(MCThreadJoin), &MCReadThreadJoin);
    programState->registerVisibleOperationType(typeid(MCThreadReachGoal), &MCReadThreadReachGoal);
    programState->registerVisibleOperationType(typeid(MCThreadRequestNewGoal), &MCReadThreadRequestNewGoal);
    programState->registerVisibleOperationType(typeid(MCThreadEnterGoalCriticalSection), &MCReadThreadEnterGoalCriticalSection);
    programState->registerVisibleOperationType(typeid(MCThreadExitGoalCriticalSection), &MCReadThreadExitGoalCriticalSection);
    programState->registerVisibleOperationType(typeid(MCMutexInit), &MCReadMutexInit);
    programState->registerVisibleOperationType(typeid(MCMutexUnlock), &MCReadMutexUnlock);
    programState->registerVisibleOperationType(typeid(MCMutexLock), &MCReadMutexLock);
    programState->registerVisibleOperationType(typeid(MCSemInit), &MCReadSemInit);
    programState->registerVisibleOperationType(typeid(MCSemPost), &MCReadSemPost);
    programState->registerVisibleOperationType(typeid(MCSemWait), &MCReadSemWait);
    programState->registerVisibleOperationType(typeid(MCSemEnqueue), &MCReadSemEnqueue);
    programState->registerVisibleOperationType(typeid(MCExitTransition), &MCReadExitTransition);
    programState->registerVisibleOperationType(typeid(MCBarrierEnqueue), &MCReadBarrierEnqueue);
    programState->registerVisibleOperationType(typeid(MCBarrierInit), &MCReadBarrierInit);
    programState->registerVisibleOperationType(typeid(MCBarrierWait), &MCReadBarrierWait);
    programState->registerVisibleOperationType(typeid(MCCondInit), &MCReadCondInit);
    programState->registerVisibleOperationType(typeid(MCCondSignal), &MCReadCondSignal);
    programState->registerVisibleOperationType(typeid(MCCondBroadcast), &MCReadCondBroadcast);
    programState->registerVisibleOperationType(typeid(MCCondWait), &MCReadCondWait);
    programState->registerVisibleOperationType(typeid(MCCondEnqueue), &MCReadCondEnqueue);
    programState->registerVisibleOperationType(typeid(MCGlobalVariableRead), &MCReadGlobalRead);
    programState->registerVisibleOperationType(typeid(MCGlobalVariableWrite), &MCReadGlobalWrite);
    programState->start();
}

MC_PROGRAM_TYPE
mc_scheduler_main()
{
    mc_register_main_thread();

    auto mainThread = programState->getThreadWithId(TID_MAIN_THREAD);
    auto initialTransition = MCTransitionFactory::createInitialTransitionForThread(mainThread);
    programState->setNextTransitionForThread(TID_MAIN_THREAD, initialTransition);

    MC_PROGRAM_TYPE program = mc_begin_target_program_at_main(false);
    if (MC_IS_SOURCE_PROGRAM(program))
        return MC_SOURCE_PROGRAM;

    mc_exhaust_threads(initialTransition);
    mc_exit_with_trace_if_necessary(traceId);
    // Possibly print

    program = mc_enter_gdb_debugging_session_if_necessary(traceId++);
    if (MC_IS_SOURCE_PROGRAM(program))
        return MC_SOURCE_PROGRAM;

    int curStateStackDepth = static_cast<int>(programState->getStateStackSize());
    int curTransitionStackDepth = static_cast<int>(programState->getTransitionStackSize());

    while (curStateStackDepth > 0) {

        std::shared_ptr<MCStateStackItem> sTop = programState->getStateItemAtIndex(curStateStackDepth - 1);
        if (sTop->hasThreadsToBacktrackOn()) {

            if (traceId % 1000 == 0) {
                printf("*** TRACE ID: %lu ***\n", traceId);
            }

            // TODO: We can be smart here and only run a thread
            // if it is not already in a sleep set or lock set (eventually)
            programState->reflectStateAtTransitionDepth(curTransitionStackDepth - 1);

            // DPOR ensures that any thread in the backtrack set is enabled in this state
            tid_t backtrackThread = sTop->popFirstThreadToBacktrackOn();
            std::shared_ptr<MCTransition> backtrackOperation = programState->getNextTransitionForThread(backtrackThread);

            program = mc_enter_gdb_debugging_session_if_necessary(traceId);
            if (MC_IS_SOURCE_PROGRAM(program))
                return MC_SOURCE_PROGRAM;

            program = mc_readvance_main(backtrackOperation);
            if (MC_IS_SOURCE_PROGRAM(program))
                return MC_SOURCE_PROGRAM;
            mc_exit_with_trace_if_necessary(traceId++);

            curStateStackDepth = static_cast<int>(programState->getStateStackSize());
            curTransitionStackDepth = static_cast<int>(programState->getTransitionStackSize());
        } else {
            curStateStackDepth--;
            curTransitionStackDepth--;
        }
    }
    return false;
}

void*
mc_create_shared_memory_region()
{
    //  If the region exists, then this returns a fd for the existing region.
    //  Otherwise, it creates a new shared memory region.
    char dpor[100];

    // FIXME: It's technically possible that the process ID could wrap around and be reused
    // so a race could ensue again if another mcmini took that (repeated) pid. But that's
    // unlikely
    snprintf(dpor, sizeof(dpor), "/DPOR-%s-%lu", getenv("USER"), (long)getpid());
    // This creates a file in /dev/shm/
    int fd = shm_open(dpor, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        if (errno == EACCES) {
            fprintf(stderr, "Shared memory region '%s' not owned by this process\n",
                    dpor);
        } else {
            perror("shm_open");
        }
        __real_exit(EXIT_FAILURE);
    }
    int rc = ftruncate(fd, shmAllocationSize);
    if (rc == -1) {
        perror("ftruncate");
        __real_exit(EXIT_FAILURE);
    }
    // We want stack at same address for each process.  Otherwise, a pointer
    //   to an address in the stack data structure will not work everywhere.
    //   Hopefully, this address is not already used.
    void *stack_address = (void *)0x4444000;
    void *shmStart = mmap(stack_address, shmAllocationSize,
                                        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
    if (shmStart == MAP_FAILED) {
        perror("mmap");
        __real_exit(EXIT_FAILURE);
    }
    // shm_unlink(dpor); // Don't unlink while child processes need to open this.
    fsync(fd);
    close(fd);
    return shmStart;
}

void
mc_initialize_shared_memory_region()
{
    void *shm = mc_create_shared_memory_region();
    void *threadQueueStart = shm;
    void *shmTransitionTypeInfoStart = (char*)threadQueueStart + sizeof(*threadQueue);
    void *shmTransitionDataStart = (char*)shmTransitionTypeInfoStart + sizeof(*shmTransitionTypeInfo);

    shmStart = shm;
    threadQueue = static_cast<typeof(threadQueue)>(threadQueueStart);
    shmTransitionTypeInfo = static_cast<typeof(shmTransitionTypeInfo)>(shmTransitionTypeInfoStart);
    shmTransitionData = shmTransitionDataStart;
}

void
mc_create_thread_sleep_points()
{
    for (int i = 0; i < MAX_TOTAL_THREADS_IN_PROGRAM; i++)
        mc_shared_cv_init(&(*threadQueue)[i]);
}

void
mc_reset_cv_locks()
{
    for (int i = 0; i < MAX_TOTAL_THREADS_IN_PROGRAM; i++) {
        mc_shared_cv_destroy(&(*threadQueue)[i]);
        mc_shared_cv_init(&(*threadQueue)[i]);
    }
}

void
sigusr1_handler_child(int sig)
{
    _Exit(0);
}

void
sigusr1_handler_scheduler(int sig)
{
    mc_child_kill();
    puts("******* Something went wrong in the source program... *******************");
    _exit(1);
}

MC_PROGRAM_TYPE
mc_spawn_child()
{
    // Ensure that a child does not already exist to prevent fork bombing
    MC_ASSERT(cpid == -1);

    pid_t childpid;
    if ( (childpid = fork()) < 0) {
        perror("fork");
        abort();
    }
    cpid = childpid;

    if (FORK_IS_CHILD_PID(childpid)) {
        signal(SIGUSR1, &sigusr1_handler_child);
        return MC_SOURCE_PROGRAM;
    } else {
        MC_FATAL_ON_FAIL(signal(SIGUSR1, &sigusr1_handler_scheduler) != SIG_ERR);
        return MC_SCHEDULER;
    }
}

MC_PROGRAM_TYPE
mc_spawn_child_following_transition_stack()
{
    mc_reset_cv_locks();
    MC_PROGRAM_TYPE program = mc_begin_target_program_at_main(false);

    if (MC_IS_SCHEDULER(program)) {
        const int transition_stack_height = programState->getTransitionStackSize();
        for (int i = 0; i < transition_stack_height; i++) {
            // NOTE: This is reliant on the fact
            // that threads are created in the same order
            // when we create them. This will always be consistent,
            // but we might need to look out for when a thread dies
            tid_t nextTid = programState->getThreadRunningTransitionAtIndex(i);
            mc_run_thread_to_next_visible_operation(nextTid);
        }
    } else {
        // We need to reset the concurrent system
        // for the child since, at the time this method
        // is invoked, it will have a complete copy of
        // the state the of system. But we need to
        // re-simulate the system by running the transitions
        // in the transition stack; otherwise, shadow resource
        // allocations will be off
        programState->reset();
        mc_register_main_thread();
    }

    return program;
}

MC_PROGRAM_TYPE
mc_begin_target_program_at_main(bool spawnDaemonThread)
{
    MC_PROGRAM_TYPE program = mc_spawn_child();
    if (MC_IS_SOURCE_PROGRAM(program)) {
        // NOTE: Technically, the child will be frozen
        // inside of dpor_init until it is scheduled. But
        // this is only a technicality: it doesn't actually
        // matter where the child spawns so long as it reaches
        // the actual source program
        tid_self = 0;

        // Note that the child process does
        // not need to re-map the shared memory
        // region as the parent has already done that

        // This is important to handle the case when the
        // main thread hits return 0; in that case, we
        // keep the process alive to allow the model checker to
        // continue working
        //
        //
        // NOTE: This does not handle the case where a
        // thread makes a call to exit(). This is a special case we need
        // to be able to handle
        //
        // NOTE!!: atexit handlers can be invoked when a dynamic
        // library is unloaded. In the transparent target, we need
        // to be able to handle this case gracefully
        MC_FATAL_ON_FAIL(atexit(&mc_exit_main_thread) == 0);

        if (spawnDaemonThread)
            mc_spawn_daemon_thread();

        thread_await_mc_scheduler_for_thread_start_transition();
    }
    return program;
}

void
mc_run_thread_to_next_visible_operation(tid_t tid)
{
    MC_ASSERT(tid != TID_INVALID);
    mc_shared_cv_ref cv = &(*threadQueue)[tid];
    mc_shared_cv_wake_thread(cv);
    mc_shared_cv_wait_for_thread(cv);
}

void
mc_child_kill()
{
    if (cpid == -1) return; // No child
    kill(cpid, SIGUSR1);
    waitpid(cpid, NULL, 0);
    cpid = -1;
}

void
mc_child_wait()
{
    MC_ASSERT(cpid != -1);
    waitpid(cpid, nullptr, 0);
}

void
mc_child_panic()
{
    pid_t schedpid = getppid();
    kill(schedpid, SIGUSR1);

    // The scheduler will kill the child
    // process before being able to leave this function
    waitpid(schedpid, nullptr, 0);
}

void
mc_exhaust_threads(std::shared_ptr<MCTransition> initialTransition)
{
    uint64_t debug_depth = programState->getTransitionStackSize();
    std::shared_ptr<MCTransition> t_next = initialTransition;
    do {
        debug_depth++;
        tid_t tid = t_next->getThreadId();
        mc_run_thread_to_next_visible_operation(tid);
        transitionId++;
        programState->simulateRunningTransition(t_next, shmTransitionTypeInfo, shmTransitionData);
        programState->dynamicallyUpdateBacktrackSets();

        {
            /* Check for data races */
            auto pendingTransitionForExecutingThread = programState->getPendingTransitionForThread(tid);
            if (programState->programHasADataRaceWithNewTransition(pendingTransitionForExecutingThread)) {
                mcprintf("*** DATA RACE DETECTED ***\n");
                programState->printTransitionStack();
                programState->printNextTransitions();
            }

            /* Check for starvation */
            if (!programState->programAchievedForwardProgressGoals(pendingTransitionForExecutingThread)) {
                mcprintf("*** FORWARD PROGRESS VIOLATION DETECTED ***\n");
                programState->printTransitionStack();
                programState->printNextTransitions();
                programState->printThreadExecutionDepths();
                programState->printForwardProgressViolations();
            }
        }

    } while ((t_next = programState->getFirstEnabledTransitionFromNextStack()) != nullptr);

    const bool programIsInDeadlock = programState->programIsInDeadlock();
    const bool programAchievedForwardProgressGoals = programState->programAchievedForwardProgressGoals();
    const bool programHasNoErrors = !programIsInDeadlock && programAchievedForwardProgressGoals;

    if (programIsInDeadlock) {
        puts("*** DEADLOCK DETECTED ***");
        programState->printTransitionStack();
        programState->printNextTransitions();

        if (programState->getConfiguration().stopAtFirstDeadlock) {
            mcprintf("*** Model checking completed! ***\n");
            mcprintf("Number of transitions: %lu\n", transitionId);
            __real_exit(0);
        }
    }

    if (!programAchievedForwardProgressGoals) {
        mcprintf("*** FORWARD PROGRESS VIOLATION DETECTED ***\n");
        programState->printTransitionStack();
        programState->printNextTransitions();
        programState->printThreadExecutionDepths();
        programState->printForwardProgressViolations();
    }

    if (programHasNoErrors) {
//        mcprintf("*** NO FAILURE DETECTED ***\n");
//        programState->printTransitionStack();
//        programState->printNextTransitions();
    }

    mc_child_kill();
}

MC_PROGRAM_TYPE
mc_readvance_main(std::shared_ptr<MCTransition> nextTransitionToTest)
{
    MC_PROGRAM_TYPE program = mc_spawn_child_following_transition_stack();
    if (MC_IS_SOURCE_PROGRAM(program)) return MC_SOURCE_PROGRAM;
    mc_exhaust_threads(nextTransitionToTest);
    return MC_SCHEDULER;
}

tid_t
mc_register_thread()
{
    tid_t newTid = programState->createNewThread();
    tid_self = newTid;
    return newTid;
}

tid_t
mc_register_main_thread()
{
    tid_t newTid = programState->createMainThread();
    tid_self = newTid;
    return newTid;
}

void
mc_report_undefined_behavior(const char *msg)
{
    mc_child_kill();
    fprintf(stderr,"\t Undefined Behavior Detected! \t\n"
            "\t ............................ \t\n"
            "\t The model checker aborted the execution because\t\n"
            "\t it detected undefined behavior\t\n"
            "\t ............................ \t\n"
            "\t %s \t\n",
    msg);
    programState->printTransitionStack();
    programState->printNextTransitions();
    exit(EXIT_FAILURE);
}

/* GDB Interface */

MC_PROGRAM_TYPE
mc_enter_gdb_debugging_session_if_necessary(trid_t trid)
{
    if (mc_should_enter_gdb_debugging_session_with_trace_id(trid))
        return mc_enter_gdb_debugging_session();
    return MC_SCHEDULER;
}

void
mc_exit_with_trace_if_necessary(trid_t trid)
{
    if (programState->isTargetTraceIdForStackContents(trid)) {
        programState->printTransitionStack();
        programState->printNextTransitions();
        mc_child_kill();
        __real_exit(0);
    }
}

bool
mc_should_enter_gdb_debugging_session_with_trace_id(trid_t trid)
{
    return programState->isTargetTraceIdForGDB(trid);
}

MC_PROGRAM_TYPE
mc_enter_gdb_debugging_session()
{
    MC_PROGRAM_TYPE program = mc_begin_target_program_at_main(true);
    if (MC_IS_SCHEDULER(program)) {
        mc_child_wait(); /* The daemon thread will take the place of the parent process */
        __real_exit(0);
    }

    return program;
}

void
mc_spawn_daemon_thread()
{
    /*
     * Make sure to copy the transition sequence since we
     * will eventually reset the `programState` before
     * rerunning the trace/schedule
     */
    auto trace = new std::vector<tid_t>();
    *trace = programState->getThreadIdTraceOfTransitionStack();

    pthread_t daemon;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    __real_pthread_create(&daemon, nullptr, &mc_daemon_thread_simulate_program, trace);
    pthread_attr_destroy(&attr);
}

void*
mc_daemon_thread_simulate_program(void *trace)
{
    programState->reset();
    programState->start();
    mc_register_main_thread();

    auto mainThread = programState->getThreadWithId(TID_MAIN_THREAD);
    auto initialTransition = MCTransitionFactory::createInitialTransitionForThread(mainThread);
    programState->setNextTransitionForThread(TID_MAIN_THREAD, initialTransition);

    auto tracePtr = static_cast<std::vector<tid_t>*>(trace);

    for (auto &tid : *tracePtr) {
        auto t_next = programState->getPendingTransitionForThread(tid);
        t_next->print();
        mc_run_thread_to_next_visible_operation(tid);
        programState->simulateRunningTransition(t_next, shmTransitionTypeInfo, shmTransitionData);
    }
    delete tracePtr;
    mc_child_panic();
    return nullptr; /* Ignored */
}

MCStateConfiguration
get_config_for_execution_environment()
{
    uint64_t maxThreadDepth = 7;//MC_STATE_CONFIG_THREAD_NO_LIMIT;
    trid_t gdbTraceNumber = MC_STATE_CONFIG_NO_TRACE;
    trid_t stackContentDumpTraceNumber = MC_STAT_CONFIG_NO_TRANSITION_STACK_DUMP;
    bool firstDeadlock = false;
    uint64_t extraLivenessTransitions = 8;

    /* Parse the max thread depth from the command line (if available) */
    char *maxThreadDepthChar = getenv(ENV_MAX_THREAD_DEPTH);
    char *gdbTraceNumberChar = getenv(ENV_DEBUG_AT_TRACE);
    char *stackContentDumpTraceNumberChar = getenv(ENV_PRINT_AT_TRACE);
    char *firstDeadlockChar = getenv(ENV_STOP_AT_FIRST_DEADLOCK);
    char *expectForwardProgressOfThreadsChar = getenv(ENV_CHECK_FORWARD_PROGRESS);

    // TODO: Sanitize arguments (check errors of strtoul)
    if (maxThreadDepthChar != nullptr)
        maxThreadDepth = strtoul(maxThreadDepthChar, nullptr, 10);

    if (gdbTraceNumberChar != nullptr)
        gdbTraceNumber = strtoul(gdbTraceNumberChar, nullptr, 10);

    if (stackContentDumpTraceNumberChar != nullptr)
        stackContentDumpTraceNumber = strtoul(stackContentDumpTraceNumberChar, nullptr, 10);

    if (firstDeadlockChar != nullptr)
        firstDeadlock = true;

    return {maxThreadDepth,
            gdbTraceNumber,
            stackContentDumpTraceNumber,
            firstDeadlock,
            extraLivenessTransitions,
    };
}
