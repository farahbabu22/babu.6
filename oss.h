#ifndef OSS_H
#define OSS_H

#include "process.h"
#include "clock.h"



//How big are our bitmaps
#define framemapSize ((frameCount / sizeof(char)) + 1)
#define procmapSize  ((runLimit / sizeof(char))+ 1)

//Semaphore for each user, for oss and for daemon
#define NSEMS 		 (runLimit + 1 + 1)
#define daemonSem (NSEMS - 1)

struct oss {

	//frames
	struct frame  frames[frameCount];
	unsigned char framemap[framemapSize];

	//processes
	struct process procs[runLimit];
	unsigned char procmap[procmapSize];

	//fifo queue of frame indexes, with loaded pages
	unsigned short fifo[frameCount];
	unsigned short fifoIndex;

	//statistics
	struct memCounters 	mstat;
	struct procCounters	pstat;

	//the clock
  struct clock 	clock;
};

struct oss* ossCreate(const int flags);
void ossDestroy(struct oss*);

//for locking and unlocking the semaphores
int ossWait(const int id);
int ossPost(const int id);
#endif
