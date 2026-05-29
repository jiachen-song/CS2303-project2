/**
 * thread_pool.c - Student implementation scaffold
 *
 * The public API and queue helpers are fixed. You still need to:
 * - define struct thread_pool
 * - FIFO task-queue operations
 * - choose the shared state and wake-up protocol your pool needs
 * - pool initialization and rollback on failure
 * - worker synchronization and lifecycle
 * - wait / destroy semantics
 *
 * You may add private static helpers in this file.
 */

#include <pthread.h>
#include <stdlib.h>

#include "task_queue.h"

struct thread_pool {
    pthread_t *threads;//struct
    int worker_count;
    task_queue tasks;

    pthread_mutex_t lock;
    pthread_cond_t has_task_cond;
    pthread_cond_t is_idle;
    /*
     * TODO: add the shared state your design needs.
     *
     * A workable design must let submit(), worker threads, wait(), and destroy()
     * agree on:
     * - whether queued work exists,
     * - whether work is currently executing,
     * - whether shutdown has started,
     * - how sleepers are woken when those facts change.
     */
    int running_tasks;
    int shutdown;
    //int unused;
};

void task_queue_init(task_queue *queue) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
}

/* TODO A: append one task at the tail while preserving head/tail/count. */
void task_queue_push(task_queue *queue, task *new_task) {
    //(void)queue;
    //(void)new_task;
    if(queue->count==0){
        queue->head=new_task;
        queue->tail=new_task;
    }
    else{
        queue->tail->next=new_task;
        queue->tail=new_task;
    }
    queue->count++;
    return;
}

/* TODO A: pop one task from the head and repair tail when the queue becomes empty. */
task *task_queue_pop(task_queue *queue) {
    //(void)queue;
    if(queue->count==0){
        return NULL;
    }
    task* res=queue->head;
    queue->head=res->next;
    queue->count--;
    if(queue->count==0){
        queue->tail=NULL;
    }
    return res;
}

void task_queue_clear(task_queue *queue) {
    task *next_task;

    while ((next_task = task_queue_pop(queue)) != NULL) {
        free(next_task);
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
}

/*
 * TODO B: implement the worker loop.
 *
 * Required behavior:
 * - wait while no task is available
 * - exit cleanly after shutdown
 * - do not hold the synchronization lock that protects shared state
 *   while running the task
 * - after shutdown starts, do not begin any new queued task
 * - maintain shared state so wait/destroy semantics work
 */
void *thread_pool_worker_main(void *pool_arg) {
    //(void)pool_arg;
    thread_pool *pool=pool_arg;
    while(1){
        pthread_mutex_lock(&pool->lock);
        while(pool->tasks.count==0 && !pool->shutdown){//4.3 requires while to avoid spurious wakeup
            pthread_cond_wait(&pool->has_task_cond,&pool->lock);
        }

        //destory
        if(pool->shutdown ){
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        //pop task
        task* t=task_queue_pop(&pool->tasks);
        if(t!=NULL){
            pool->running_tasks++;
        }
        pthread_mutex_unlock(&pool->lock);

        //finish task
        if(t!=NULL){
            t->task_fn(t->task_arg);
            free(t);
        }
        //update state
        pthread_mutex_lock(&pool->lock);
        pool->running_tasks--;
        if(pool->running_tasks==0 && pool->tasks.count==0){
            pthread_cond_broadcast(&pool->is_idle);
        }
        pthread_mutex_unlock(&pool->lock);
    }
    return NULL;
}

/*
 * TODO B: create the thread pool.
 *
 * You need:
 * - argument validation
 * - pool / thread-array allocation
 * - queue initialization
 * - initialization of the synchronization state your design needs
 * - worker creation
 * - rollback if any step fails after earlier steps succeeded
 */
thread_pool *thread_pool_create(int worker_count) {
    //(void)worker_count;
    if(worker_count<=0){
        return NULL;
    }
    //init pool
    thread_pool* pool=calloc(1,sizeof(thread_pool));
    if(pool==NULL){
        return NULL;
    }
    pool->worker_count=worker_count;
    task_queue_init(&pool->tasks);
    //init state,use 4.4 to rollback
    if(pthread_mutex_init(&pool->lock,NULL)!=0){
        goto c3;
    }
    if(pthread_cond_init(&pool->has_task_cond,NULL)!=0){
        goto c2;
    }
    if(pthread_cond_init(&pool->is_idle,NULL)!=0){
        goto c1;
    }
    
    pool->threads=calloc(worker_count,sizeof(pthread_t));
    if(pool->threads==NULL){
        goto cleanup;
    }
    //create worker
    for(int i=0;i<worker_count;i++){
        if(pthread_create(&pool->threads[i],NULL,thread_pool_worker_main,pool)!=0){
            pool->worker_count=i;
            thread_pool_destroy(pool);
            //goto cleanup;
            return NULL;
        }
    }
    return pool;

cleanup:
    pthread_cond_destroy(&pool->is_idle);
c1:
    pthread_cond_destroy(&pool->has_task_cond);
c2:
    pthread_mutex_destroy(&pool->lock);
c3:
    free(pool);
    return NULL;
}

/*
 * TODO B: submit a task safely.
 *
 * Required behavior:
 * - reject invalid input
 * - reject a pool that is already shutting down
 * - wake a waiting worker after a successful push
 * - submission may happen from inside a running worker task
 */
int thread_pool_submit(thread_pool *pool, thread_pool_task_fn task_fn, void *task_arg) {
    // (void)pool;
    // (void)task_fn;
    // (void)task_arg;
    if(pool==NULL || task_fn==NULL){
        return -1;
    }
    //init new task
    task* new_task=calloc(1,sizeof(task));
    if(new_task==NULL){
        return -1;
    }
    new_task->task_fn=task_fn;
    new_task->task_arg=task_arg;
    new_task->next=NULL;
    //push task
    pthread_mutex_lock(&pool->lock);
    if(pool->shutdown){
        free(new_task);
        pthread_mutex_unlock(&pool->lock);
        return -1;
    }
    task_queue_push(&pool->tasks, new_task);
    pthread_cond_signal(&pool->has_task_cond);
    pthread_mutex_unlock(&pool->lock);
    return 0;
}

/*
 * TODO C: block until the pool is idle.
 *
 * Idle means:
 * - the queue is empty
 * - no worker is still executing a task
 */
void thread_pool_wait(thread_pool *pool) { 
    //(void)pool;
    if(pool==NULL){
        return;
    }

    pthread_mutex_lock(&pool->lock);
    while(pool->tasks.count!=0 || pool->running_tasks!=0){//wait running tasks alse
        pthread_cond_wait(&pool->is_idle,&pool->lock);
    }
    pthread_mutex_unlock(&pool->lock);

}

/*
 * TODO D: destroy the pool.
 *
 * Required behavior:
 * - let already-started tasks finish
 * - drop not-yet-started tasks
 * - free resources after all workers exit
 */
void thread_pool_destroy(thread_pool *pool) {
    if (pool == NULL) {
        return;
    }
    //set shutdown flag and wake all workers
    pthread_mutex_lock(&pool->lock);
    pool->shutdown=1;
    pthread_cond_broadcast(&pool->has_task_cond);
    pthread_mutex_unlock(&pool->lock);  
    //wait for all workers to exit
    for(int i=0;i<pool->worker_count;i++){
        pthread_join(pool->threads[i],NULL);
    }

    task_queue_clear(&pool->tasks);
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->is_idle);
    pthread_cond_destroy(&pool->has_task_cond);
    free(pool->threads);
    free(pool);

    /* Keep an explicit reference so the starter still builds with -Werror. */
    //(void)task_queue_clear;
}
