#define _GNU_SOURCE
#include "GMALSharedLibraryWrappers.h"
#include "GMALMutexTransitionWrappers.h"
#include "GMALSemaphoreTransitionWrappers.h"
#include "GMALThreadTransitionWrappers.h"

typeof(&pthread_create) pthread_create_ptr;
typeof(&pthread_join) pthread_join_ptr;
typeof(&pthread_mutex_init) pthread_mutex_init_ptr;
typeof(&pthread_mutex_lock) pthread_mutex_lock_ptr;
typeof(&pthread_mutex_unlock) pthread_mutex_unlock_ptr;
typeof(&sem_wait) sem_wait_ptr;
typeof(&sem_post) sem_post_ptr;
typeof(&sem_init) sem_init_ptr;
typeof(&exit) exit_ptr;

void gmal_load_shadow_routines()
{
#if GMAL_SHARED_LIBRARY
    pthread_create_ptr = dlsym(RTLD_NEXT, "pthread_create");
    pthread_join_ptr = dlsym(RTLD_NEXT, "pthread_join");
    pthread_mutex_init_ptr = dlsym(RTLD_NEXT, "pthread_mutex_init");
    pthread_mutex_lock_ptr = dlsym(RTLD_NEXT, "pthread_mutex_lock");
    pthread_mutex_unlock_ptr = dlsym(RTLD_NEXT, "pthread_mutex_unlock");
    sem_wait_ptr = dlsym(RTLD_NEXT, "sem_wait");
    sem_post_ptr = dlsym(RTLD_NEXT, "sem_post");
    sem_init_ptr = dlsym(RTLD_NEXT, "sem_init");
    exit_ptr = dlsym(RTLD_NEXT, "exit");
#else
    pthread_create_ptr = &pthread_create;
    pthread_join_ptr = &pthread_join;
    pthread_mutex_init_ptr = &pthread_mutex_init;
    pthread_mutex_lock_ptr = &pthread_mutex_lock;
    pthread_mutex_unlock_ptr = &pthread_mutex_unlock;
    sem_post_ptr = &sem_post;
    sem_wait_ptr = &sem_wait;
    sem_init_ptr = &sem_init;
    exit_ptr = &exit;
#endif
}

#if GMAL_SHARED_LIBRARY

int
pthread_create(pthread_t *pthread, const pthread_attr_t *attr, void*(*routine)(void*), void *arg)
{
    return gmal_pthread_create(pthread, attr, routine, arg);
}

int
pthread_join(pthread_t pthread, void **result)
{
    return gmal_pthread_join(pthread, result);
}

int
pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *mutexattr)
{
    return gmal_pthread_mutex_init(mutex, mutexattr);
}

int
pthread_mutex_lock(pthread_mutex_t *mutex)
{
    return gmal_pthread_mutex_lock(mutex);
}

int
pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    return gmal_pthread_mutex_unlock(mutex);
}

int
sem_init(sem_t *sem, int pshared, unsigned int value)
{
    return gmal_sem_init(sem, pshared, value);
}

int
sem_post(sem_t *sem)
{
    return gmal_sem_post(sem);
}

int
sem_wait(sem_t *sem)
{
    return gmal_sem_wait(sem);
}

void
exit(int status)
{
    gmal_exit(status);
}
#endif