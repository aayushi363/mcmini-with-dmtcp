#include <pthread.h>
#include "GMAL.h"
#include "GMALWrappers.h"

#define THREAD_NUM 5

pthread_barrier_t barrier;
pthread_t thread[THREAD_NUM];

void * thread_doit(void *unused)
{
    gmal_pthread_barrier_wait(&barrier);
    return nullptr;
}
int main(int argc, char* argv[])
{
    gmal_init();
    gmal_pthread_barrier_init(&barrier, NULL, THREAD_NUM);
    for(int i = 0; i < THREAD_NUM; i++) {
        gmal_pthread_create(&thread[i], NULL, &thread_doit, NULL);
    }

    gmal_pthread_barrier_wait(&barrier);

    for(int i = 0; i < THREAD_NUM; i++) {
        gmal_pthread_join(thread[i], NULL);
    }
    return 0;
}