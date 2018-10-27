#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include "queue.h"
#include "sem.h"
#include "thread.h"

struct semaphore {
	int count;
	queue_t queue;
};

sem_t sem_create(size_t count)
{
	sem_t newSemaphore = (sem_t)malloc(sizeof(struct semaphore));
	if (newSemaphore == NULL){
		return NULL;
	}
	newSemaphore->count = count;
	newSemaphore->queue = queue_create();
	if (newSemaphore->queue == NULL){
		return NULL;
	}
	return newSemaphore;
}

int sem_destroy(sem_t sem)
{
	if (sem == NULL || 
	   (queue_length(sem->queue) > 0) ) {
		return -1;
	}
	queue_destroy(sem->queue);
	free(sem);
	return 0;
}

int sem_down(sem_t sem)
{
	enter_critical_section();
	if (sem == NULL){
		return -1;
	}
	while (sem->count == 0){
		queue_enqueue(sem->queue,(void*)pthread_self()); // Enqueue the ID of the current thread.
		thread_block();
	}
	sem->count = (sem->count - 1);
	exit_critical_section();
	return 0;
}

int sem_up(sem_t sem)
{
	enter_critical_section();
	if (sem == NULL){
		return -1;
	}
	sem->count = (sem->count + 1);
	if (queue_length(sem->queue) > 0){
		pthread_t dequeuedThreadID;              // Dequeue the oldest thread that's waiting and unblock it.
		queue_dequeue(sem->queue,(void*)&dequeuedThreadID);
		thread_unblock(dequeuedThreadID);
	}
	exit_critical_section();
	return 0;
}

