#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    // wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    
    struct timespec wanted_sleep;
    struct timespec remaining_sleep;
    struct thread_data *thread_func_args = (struct thread_data *) thread_param;

    wanted_sleep.tv_sec  = thread_func_args->wait_before_obtaining / 1000;
    wanted_sleep.tv_nsec = (thread_func_args->wait_before_obtaining % 1000) * 1000000;
    if (0 != nanosleep(&wanted_sleep, &remaining_sleep)) goto RETURN_ERR;
    
    // obtain mutex
    if (0 != pthread_mutex_lock(thread_func_args->mutex)) goto RETURN_ERR;

    // sleep again
    wanted_sleep.tv_sec  = thread_func_args->wait_before_release / 1000;
    wanted_sleep.tv_nsec = (thread_func_args->wait_before_release % 1000) * 1000000;
    if (0 != nanosleep(&wanted_sleep, &remaining_sleep)) goto RETURN_ERR;
    
    // free mutex
    if (0 != pthread_mutex_unlock(thread_func_args->mutex)) goto RETURN_ERR;
    
    thread_func_args->thread_complete_success= true;
    RETURN_ERR:
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    bool success = false;
    /**
     * allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    struct thread_data *thread_func_args;
    thread_func_args = (struct thread_data*) malloc(sizeof(struct thread_data));
    if (NULL == thread_func_args) return false;
    thread_func_args->mutex = mutex;
    thread_func_args->wait_before_obtaining = wait_to_obtain_ms;
    thread_func_args->wait_before_release = wait_to_release_ms;

    if (0 == pthread_create(thread, NULL, &threadfunc, (void*) thread_func_args)) {
        success = true;
    }
    // freeing is done by the joiner thread
    //free(thread_func_args);
    return success;
}

