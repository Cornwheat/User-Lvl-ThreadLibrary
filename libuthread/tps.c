#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "queue.h"
#include "thread.h"
#include "tps.h"

typedef struct TPS* TPS_T;
typedef struct page* page_T;

struct TPS
{
	pthread_t threadID;  // Thread associated with memory page
	page_T memoryPage;   // Points to page struct
};

struct page
{
	int referenceCount;  // How many threads refer to the page
	void* memory;	     // pointer to block of TPS_SIZE memory (4096)
};

queue_t TPSLibrary = NULL;


// Find and return a TPS by it's Page address (dequeues from library)
TPS_T findTPSbyPage(page_T memoryPage)
{
	enter_critical_section();
	int TPSLength = queue_length(TPSLibrary);
	for (int queueIndex = 0; queueIndex < TPSLength; queueIndex++){
	TPS_T dequeuedTPS;
	queue_dequeue(TPSLibrary,(void*)&dequeuedTPS);
	if (dequeuedTPS->memoryPage == memoryPage){   // Found ID already in queue
		exit_critical_section();
		return dequeuedTPS;
	}
	queue_enqueue(TPSLibrary,dequeuedTPS);
	}
	exit_critical_section();
	return NULL;
}

static void segv_handler(int sig, siginfo_t *si, void *context)
{
	void *p_fault = (void*)((uintptr_t)si->si_addr & ~(TPS_SIZE - 1));
	TPS_T check = findTPSbyPage((page_T)p_fault);
	if (check != NULL){
		queue_enqueue(TPSLibrary, check);
		fprintf(stderr, "TPS protection error!\n");
	}
  	signal(SIGSEGV, SIG_DFL);
  	signal(SIGBUS, SIG_DFL);
  	raise(sig);
}

int tps_init(int segv)
{
	if (segv){
		struct sigaction sa;
        	sigemptyset(&sa.sa_mask);
        	sa.sa_flags = SA_SIGINFO;
        	sa.sa_sigaction = segv_handler;
        	sigaction(SIGBUS, &sa, NULL);
        	sigaction(SIGSEGV, &sa, NULL);
	}
	if(TPSLibrary != NULL){					// TPS Library has already been initialized
		return -1;
	}
	TPSLibrary = queue_create();
	return 0;
}

// Find and return a TPS by it's TID (dequeues from library)
TPS_T find_TPS(pthread_t TID)
{
	enter_critical_section();
	int TPSLength = queue_length(TPSLibrary);
	for (int queueIndex = 0; queueIndex < TPSLength; queueIndex++){
		TPS_T dequeuedTPS;
		queue_dequeue(TPSLibrary,(void*)&dequeuedTPS);
		if (dequeuedTPS->threadID == TID){		// Found ID already in queue
			exit_critical_section();
			return dequeuedTPS;
		}
		queue_enqueue(TPSLibrary,dequeuedTPS);
	}
	exit_critical_section();
	return NULL;
}

int tps_create(void)
{
	pthread_t newThreadID = pthread_self();

	TPS_T newTPS = find_TPS(newThreadID);
	if (newTPS != NULL){			 		// Thread already has a TPS
		queue_enqueue(TPSLibrary, (void*)newTPS);       // re-enqueue the TPS we found
		return -1;
	}
	newTPS = (TPS_T)malloc(sizeof(struct TPS));
	page_T newPage = (page_T)malloc(sizeof(struct page));
	void* newMemory = mmap(NULL,TPS_SIZE,PROT_NONE,MAP_SHARED | MAP_ANONYMOUS,-1,0);
	newTPS->threadID = newThreadID;
	newTPS->memoryPage = newPage;
	newTPS->memoryPage->memory = newMemory;
	newTPS->memoryPage->referenceCount = 1;
	queue_enqueue(TPSLibrary, (void*)newTPS);
	return 0;
}

int tps_destroy(void)
{
	pthread_t currentThreadID = pthread_self();
	TPS_T targetTPS = find_TPS(currentThreadID);
	if(targetTPS == NULL){					// Current thread does not have a TPS
		return -1;
	}
	enter_critical_section();
	targetTPS->memoryPage->referenceCount--;
	if (targetTPS->memoryPage->referenceCount < 1){
		munmap(targetTPS->memoryPage,TPS_SIZE);
		free(targetTPS->memoryPage);
	}
	free(targetTPS);
	exit_critical_section();
	return 0;
}

int tps_read(size_t offset, size_t length, char *buffer)
{
	if(buffer == NULL){
		return -1;
	}
	pthread_t currentThreadID = pthread_self();
	TPS_T readTPS = find_TPS(currentThreadID);
	if(readTPS == NULL){					// Current thread does not have a TPS
		return -1;
	}
	enter_critical_section();
	mprotect(readTPS->memoryPage->memory,TPS_SIZE,PROT_READ);       // Allow page to be read
	memcpy(buffer,(readTPS->memoryPage->memory + offset),length);
	mprotect(readTPS->memoryPage->memory,TPS_SIZE,PROT_NONE);
	queue_enqueue(TPSLibrary, (void*)readTPS);
	exit_critical_section();
	return 0;
}

int tps_write(size_t offset, size_t length, char *buffer)
{
	if(buffer == NULL){
	return -1;
	}
	pthread_t currentThreadID = pthread_self();
	TPS_T writeTPS = find_TPS(currentThreadID);
	if(writeTPS == NULL){					// Current thread does not have a TPS
		return -1;
	}

	enter_critical_section();
	if (writeTPS->memoryPage->referenceCount > 1){		// More than 1 TPS referring to page; split and make new clone
		writeTPS->memoryPage->referenceCount--;
		page_T newPage = (page_T)malloc(sizeof(struct page));
		void* newMemory = mmap(NULL,TPS_SIZE,PROT_WRITE,MAP_SHARED | MAP_ANONYMOUS,-1,0); 
		newPage->memory = newMemory;
		newPage->referenceCount = 1;
		mprotect(writeTPS->memoryPage->memory,TPS_SIZE,PROT_READ); 
		memcpy(newPage->memory, writeTPS->memoryPage->memory,TPS_SIZE);
		mprotect(writeTPS->memoryPage->memory,TPS_SIZE,PROT_NONE);
		mprotect(newPage->memory,TPS_SIZE,PROT_NONE);
		writeTPS->memoryPage = newPage;			// writeTPS created new memory page with same memory as original
	}
	mprotect(writeTPS->memoryPage->memory,TPS_SIZE,PROT_WRITE);	// Allow page to be written to
	memcpy((writeTPS->memoryPage->memory + offset),buffer,length);
	mprotect(writeTPS->memoryPage->memory,TPS_SIZE,PROT_NONE);
	queue_enqueue(TPSLibrary, (void*)writeTPS);
	exit_critical_section();
	return 0;
}

int tps_clone(pthread_t tid)
{
	pthread_t currentThreadID = pthread_self();
	TPS_T cloneTPS = find_TPS(currentThreadID);
	if (cloneTPS != NULL){			 		  // Current thread already has a TPS
		queue_enqueue(TPSLibrary, (void*)cloneTPS);       // re-enqueue the TPS we found
		return -1;
	}

	TPS_T originalTPS = find_TPS(tid);
	if(originalTPS == NULL) {				  // Cloned thread does not have a TPS
		return -1;
	}

	enter_critical_section();
	cloneTPS = (TPS_T)malloc(sizeof(struct TPS));
	cloneTPS->threadID = currentThreadID;
	cloneTPS->memoryPage = originalTPS->memoryPage;
	cloneTPS->memoryPage->referenceCount++;

	queue_enqueue(TPSLibrary, (void*)originalTPS);
	queue_enqueue(TPSLibrary, (void*)cloneTPS);
	exit_critical_section();
	return 0;
}