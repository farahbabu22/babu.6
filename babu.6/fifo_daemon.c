#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/shm.h>
#include <sys/types.h>

#include "oss.h"

/*
author: Farah Babu
email: fbkzx@umsystem.edu
hoare id: babu
*/

static struct oss *oss = NULL;
static FILE *logFile = NULL;

static void bitUnset(unsigned char *map, const int size, const unsigned int x){
  const unsigned char byte = x / 8, bit = x % 8;

  if(byte >= size){
    fprintf(stderr, "fifo_daemon: Out of bounds\n");
    exit(1);
  }

  map[byte] &= ~(1 << bit);
}

static void frameClear(const unsigned char framei){
	struct frame *fr = &oss->frames[framei];
  fr->dirty = 0;
  fr->pagei = pageCount;
  fr->useri = startLimit;

	bitUnset(oss->framemap, framemapSize, framei);
}

static void fifoShift(const int x){
  int i;
  for(i=x+1; i < frameCount; i++){
    oss->fifo[i-1] = oss->fifo[i];
  }
  oss->fifoIndex--;
}

static int fifoPop(const int framei){
  int i;
  for(i=0; i < frameCount; i++){
    if(oss->fifo[i] == framei){
      break;
    }
  }
  fifoShift(i);
  return i;
}

static void clockInc(struct clock* clk, const unsigned int sec, const unsigned int ns){

  clk->seconds += sec;
  const unsigned int sum = clk->nanoseconds + ns;
  if(sum > 1000000000){
		clk->seconds++;
		clk->nanoseconds = 1000000000 - sum;
	}else{
    clk->nanoseconds = sum;
  }
}

static int fifoEviction(){
  int i;

  //lock the memory
	if(ossWait(0) < 0){
    return -1;
  }

  //determine how much pages to evict
  int n = ((float) oss->fifoIndex / 100.0f) * 5.0f;
  fprintf(logFile, "Daemon: Trying to free %d pages\n", n);

  //oldest pages are at front of the queue
  for(i=0; i < oss->fifoIndex; i++){

    const int framei = oss->fifo[i];
    struct frame * frame = &oss->frames[framei];

    if(frame->pagei != pageCount){
      struct page * page = &oss->procs[frame->useri].vm[frame->pagei];
      if(frame->loading){
        fprintf(logFile, "Daemon: Skipping loading frame %d\n", framei);

      }else if(page->pReferenced){ //if page is valid, drop the bit
        page->pReferenced = 0;
        fprintf(logFile, "Daemon: Invalidated P%d page %d\n", frame->useri, frame->pagei);
        n--;
      }else{

        //unload the page
        frameClear(framei);
        fifoPop(framei);

        fprintf(logFile, "Daemon: Unloaded P%d page %d\n", frame->useri, frame->pagei);
        n--;

        //save the page on disk
        clockInc(&oss->clock, 0, swapTime*1000);
      }
    }

    if(n < 0){
      break;
    }
  }

  //unlock the memory
	if(ossPost(0) < 0){
    return -1;
  }
  return 0;
}


int main(void){

	struct oss *oss = ossCreate(0);
	if(oss == NULL){
    fprintf(stderr, "Daemon: Failed to create oss\n");
		return 1;
	}

  logFile = fopen("fifo_daemon.log", "w");
  if(logFile == NULL){
    perror("fopen");
    return 1;
  }

  fprintf(logFile,"Daemon: Started at time %i.%i\n", oss->clock.seconds, oss->clock.nanoseconds);

  while(1){
    //wait for master to kick us
    if(ossWait(daemonSem) < 0){
      break;
    }

    //clear some pages
    if(fifoEviction() < 0){
      break;
    }
  }
	shmdt(oss);

  fprintf(logFile, "Daemon: Stopped at time %i.%i\n", oss->clock.seconds, oss->clock.nanoseconds);
  fclose(logFile);

  return EXIT_SUCCESS;
}
