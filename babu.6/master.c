#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>

#include "process.h"
#include "oss.h"

//on_reference statistics

/*
author: Farah Babu
email: fbkzx@umsystem.edu
hoare id: babu
*/

static struct oss *oss = NULL;

static unsigned int num_lines = 0;
static FILE *logFile = NULL;

#define masterLog "master.log"
#define maxLogLines 10000

//Shift the FIFO queue left
static void fifoShift(const int x){
  int i;
  for(i=x+1; i < frameCount; i++){
    oss->fifo[i-1] = oss->fifo[i];
  }
  oss->fifoIndex--;
}

//Add an frame index to FIFO queue
static void fifoPush(const int framei){

  if(oss->fifoIndex >= frameCount){
    fifoShift(0);
  }
  oss->fifo[oss->fifoIndex++] = framei;
}

//Remove a frame from FIFO queue
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

//Set a bit in bitmap
static void bitSet(unsigned char *map, const int size, const unsigned int x){
  const unsigned char byte = x / 8, bit = x % 8;

  if(byte >= size){
    fprintf(stderr, "master: bit out of bounds\n");
    exit(1);
  }

  map[byte] |= 1 << bit;
}

//Check the value of a bit in bitmap
static int bitTest(unsigned char *map, const int size, const unsigned int x){
  const unsigned char byte = x / 8, bit = x % 8;

  if(byte >= size){
    fprintf(stderr, "master: bit out of bounds\n");
    exit(1);
  }

  return ((map[byte] & (1 << bit)) >> bit);
}

//Unset a bit in bitmap
static void bitUnset(unsigned char *map, const int size, const unsigned int x){
  const unsigned char byte = x / 8, bit = x % 8;

  if(byte >= size){
    fprintf(stderr, "master: bit out of bounds\n");
    exit(1);
  }

  map[byte] &= ~(1 << bit);
}

//Search for unset bit in bitmap
static int bitSearch(unsigned char *map, const int size, const unsigned int max){
  unsigned int i;
  for(i=0; i < max; i++){
		if(bitTest(map, size, i) == 0)
      return i;
  }
  return size;
}

//Count the unset bits in bitmap
static int bitCount(unsigned char *map, const int size, const unsigned int max){
  unsigned int i, count = 0;
  for(i=0; i < max; i++){
		if(bitTest(map, size, i) == 0){
      ++count;
    }
  }
  return count;
}

//Increment clock
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

//Check if a clock is passed the mark
static int clockTest(const struct clock *clk, const struct clock *mark){
  if(mark->seconds < clk->seconds){
    return 1;
  }else if((mark->seconds == clk->seconds) && (mark->nanoseconds <= clk->nanoseconds)){
    return 1;
  }else{
    return 0;
  }
}

//Start a user process
static int userProcess(struct process * procs, const int id){
  char arg[10];

  const int p = bitSearch(oss->procmap, procmapSize, runLimit);
  if(p == procmapSize){  //if we don't have free process slot
    return 0;
  }

  //make the index argument
  sprintf(arg, "%i", p);

  procs[p].id		 = id;
  procs[p].state = sREADY;

  //start the process
  const pid_t pid = fork();
  if(pid == -1){
    perror("fork");
    return -1;
  }else if(pid == 0){

    execl("./user", "./user", arg, NULL);

    perror("execl");
    exit(EXIT_FAILURE);
  }else{  //parent

    procs[p].pid = pid;
    bitSet(oss->procmap, procmapSize, p);
  }

  return procs[p].id;
}

//Clear a process structure, after its terminated
static void clearProcess(struct process * procs, const unsigned int n){

  struct process * proc = &procs[n];
  proc->pid = 0;
  proc->id = 0;
  proc->state = sSTATES;
  bzero(&proc->reference, sizeof(struct reference));
  //vm stays!

  bitUnset(oss->procmap, procmapSize, n);
}

//Clear a frame
static void frameClear(const unsigned short framei){
	struct frame *fr = &oss->frames[framei];
  fr->dirty = 0;
  fr->pagei = pageCount;
  fr->useri = startLimit;

	bitUnset(oss->framemap, framemapSize, framei);
}

//Print the frames
static void printFrames(FILE * output){
  int i;
  fprintf(logFile, "Master: Current memory layout at %i.%i:\n", oss->clock.seconds, oss->clock.nanoseconds);
  num_lines++;
  fprintf(output, "\t\t\t\t\t\tOccupied\t\tRefByte\t\tDirtyBit\n");
  num_lines++;

	for(i=0; i < frameCount; i++){

    struct frame * frame = &oss->frames[i];

    const int ref_bit = (frame->pagei != pageCount)
                      ? oss->procs[frame->useri].vm[frame->pagei].referenced
                      : 0;

    fprintf(output, "Frame %3d\t%10s\t\t%7d\t%10d\n", i,
      (frame->pagei != pageCount) ? "Yes" : "No", ref_bit, frame->dirty);
	}
  num_lines += frameCount;
}

//Clear a page
static void pageClear(struct page * p){
  if(p->framei != frameCount){
    frameClear(p->framei);
  }
  p->framei = frameCount;
  p->referenced = 0;
}

//Clear the page table of a process
static void ptClear(struct page *pt){
  int i;
	for(i=0; i < pageCount; i++){
    pageClear(&pt[i]);
  }
}

//Exclude a page, using second chance algorithm
static int pageExclude(struct frame * frames){

  static unsigned short clock_hand = 0;  //second change hand

  while(1){

    struct frame * frame = &frames[clock_hand];

    //if frame has a page loaded
		if(frame->pagei != pageCount){
      struct page * page = &oss->procs[frame->useri].vm[frame->pagei];
      //if page is not on_reference
      if(page->referenced == 0){
        break;  //we have found the page to exclude
  		}else{
        //give second change to page
  			page->referenced = 0;
      }
    }
    ++clock_hand;
    clock_hand %= frameCount;
  }

  return clock_hand;
}

//Process a user reference, that generated a page fault
static int onFault(const unsigned char useri){

	oss->mstat.faults++;

  struct process * proc = &oss->procs[useri];
  const unsigned char pagei = PAGEI(proc->reference.addr);

  //check if we are low on frames
  const unsigned int free_frames = bitCount(oss->framemap, framemapSize, frameCount);
  if(free_frames < daemonLimit){
    ossPost(daemonSem);

    fprintf(logFile, "Master: Started FIFO eviction daemon, only %d frames are free\n", free_frames);
  }

  //search for a free frame
  unsigned short framei = bitSearch(oss->framemap, framemapSize, frameCount);
  if(framei == frameCount){  //if we don't have free frame

    //exclude one
	  framei = pageExclude(oss->frames);

    //remove the frame from FIFO queue
    fifoPop(framei);

    struct frame * frame = &oss->frames[framei];
    struct process * proc = &oss->procs[frame->useri];
	  struct page * page    = &proc->vm[frame->pagei];


    if(frame->dirty){

      fprintf(logFile, "Master: Dirty bit of frame %d set, adding additional time to the clock at %i.%i\n",
        page->framei, oss->clock.seconds, oss->clock.nanoseconds);
      num_lines++;

      clockInc(&oss->clock, 0, swapTime*1000);
    }

	  fprintf(logFile, "Master: Clearing frame %d and swapping P%d page %d at %i.%i\n",
	    page->framei, proc->id, pagei, oss->clock.seconds, oss->clock.nanoseconds);
    num_lines++;

	  framei = page->framei;
    pageClear(page);

	}else{
    fprintf(logFile, "Master: Using free frame %d for P%d page %d at %i.%i\n",
      framei, proc->id, pagei, oss->clock.seconds, oss->clock.nanoseconds);
    num_lines++;

  }

  //add the frame to FIFO queue
  fifoPush(framei);

  return framei;
}

//A process has reference to memory
static enum refState on_reference(const unsigned char useri){
  enum refState state = refError;

  struct process * proc = &oss->procs[useri];
  const unsigned char pagei = PAGEI(proc->reference.addr);
  struct page * page = &proc->vm[pagei];

  oss->mstat.refs++;

  //if address is invalid
  if(proc->reference.addr > (pageCount * pageSize)){

    fprintf(logFile, "Master: P%d requesting invalid address %d at %i.%i\n",
      proc->id, proc->reference.addr, oss->clock.seconds, oss->clock.nanoseconds);
    ++num_lines;

    oss->mstat.invalid++;

    return refError;
  }

  //if its a read
  if(proc->reference.rw == refRD){

    oss->mstat.reads++;
    fprintf(logFile, "Master: P%d requesting read of address %d at %i.%i\n",
      proc->id, proc->reference.addr, oss->clock.seconds, oss->clock.nanoseconds);
    ++num_lines;

  }else{  //if its a write

    oss->mstat.writes++;
    fprintf(logFile, "Master: P%d requesting write of address %d at %i.%i\n",
      proc->id, proc->reference.addr, oss->clock.seconds, oss->clock.nanoseconds);
    ++num_lines;
  }

  //if page is not loaded
  if(page->framei == frameCount){
    fprintf(logFile, "Master: Address %d is not in a frame, pagefault\n", proc->reference.addr);
    ++num_lines;

  	page->framei = onFault(useri);
		if(page->framei == frameCount){
			return refError;
		}
  	page->referenced = 1;  //after page is loaded into memory, raise the referenced bit


    //load the frame
    bitSet(oss->framemap, framemapSize, page->framei);
    oss->frames[page->framei].pagei = pagei;
    oss->frames[page->framei].useri = useri;

    proc->reference.swapClock = oss->clock;
    clockInc(&proc->reference.swapClock, 0, swapTime);

    //set the loading flag in frame
    oss->frames[page->framei].loading = 1;

		state = refSwap; //refrene goes to IO queue

  }else{  //page is loaded

    //increment the clock with time to access it
    clockInc(&oss->clock, 0, accessTime);

    if(proc->reference.rw == refRD){
  		fprintf(logFile, "Master: Address %d in frame %d, giving data to P%d at %i.%i\n", proc->reference.addr, page->framei, proc->id, oss->clock.seconds, oss->clock.nanoseconds);
      ++num_lines;

    }else{

  		fprintf(logFile, "Master: Address %d in frame %d, P%d writing data to frame at %i.%i\n",
  			proc->reference.addr, page->framei, proc->id, oss->clock.seconds, oss->clock.nanoseconds);
      ++num_lines;

      //set dirty flag
      oss->frames[page->framei].dirty = 1;
    }
    state = refSuccess;
  }

  return state;
}


//Check each process reference
static int forech_reference(){
	int i;
	for(i=0; i < runLimit; ++i){

		struct process * proc = &oss->procs[i];

    if(ossWait(i) < 0){
      return -1;
    }

		if( (proc->pid > 0) &&
        (proc->reference.state == refPending)){  //if we have a waiting reference

      proc->reference.state = on_reference(i);
    }
    if(ossPost(i) < 0){
      return -1;
    }

    //add request processing to clock
    clockInc(&oss->clock, 0, 100);
	}
  return 0;
}

//Check the swapping frames
static int forech_reference_swap(){
	int i, num_waiting = 0;

	for(i=0; i < runLimit; i++){

		struct process * proc = &oss->procs[i];

    if(ossWait(i) < 0){
      return -1;
    }

		if( (proc->pid > 0) &&
        (proc->reference.state == refSwap)){

      //if the swap time has come
      if(clockTest(&oss->clock, &proc->reference.swapClock) ){

        const unsigned char pagei = PAGEI(proc->reference.addr);
        struct page * page = &proc->vm[pagei];

        //drop the frame loading flag
        oss->frames[page->framei].loading = 0;

        proc->reference.state = on_reference(i);
      }else{
        num_waiting++;
      }
    }
    if(ossPost(i) < 0){
      return -1;
    }

    //add request processing to clock
    clockInc(&oss->clock, 0, 100);
	}

  //to avoid deadlock ,we have to check if all users are waiting for a swap
  if(num_waiting == runLimit){
    fprintf(logFile, "Master: All waiting, increasing clock at time %i:%i\n", oss->clock.seconds, oss->clock.nanoseconds);
    clockInc(&oss->clock, 1, 0);
  }

  return 0;
}

//Check which users terminated
static int foreach_terminated(){
  int i;
  for(i=0; i < runLimit; i++){

		struct process * proc = &oss->procs[i];
    if(ossWait(i) < 0){
      return -1;
    }

    if(proc->state == sTERMINATED){

      oss->pstat.terminated++;

      fprintf(logFile,"Master P%d terminated at time %i:%i\n",
            proc->id, oss->clock.seconds, oss->clock.nanoseconds);
      num_lines++;

      clearProcess(oss->procs, i);
      ptClear(proc->vm);
    }
    if(ossPost(i) < 0){
      return -1;
    }
  }
  return 0;
}

//Run the master loop and monitor for references from users
static void monitor_references(const int num_users){
  int i;
  struct clock print_clock = oss->clock;
  struct clock user_clock  = oss->clock;

  while(oss->pstat.terminated < num_users){

    if( (clockTest(&oss->clock, &user_clock) == 1) ||
        (oss->pstat.started < num_users) ){

      const int proc_id = userProcess(oss->procs, ++oss->pstat.started);
      if(proc_id < 0){
        break;
      }else if (proc_id > 0){
        fprintf(logFile, "Master: Generated process with PID %u at time %i.%i\n", proc_id, oss->clock.seconds, oss->clock.nanoseconds);
        num_lines++;
      }

      //next user will be started after 1.5 seconds
      clockInc(&user_clock, 1, 500*1000);
    }

    if( (forech_reference_swap() < 0) ||
        (forech_reference() < 0) ||
        (foreach_terminated() < 0) ){
      break;
    }

    if(clockTest(&oss->clock, &print_clock)){
			printFrames(logFile);
      print_clock.seconds += 1;
    }

  	if(num_lines >= maxLogLines){
  		fprintf(logFile,"Master: Log is getting too big at time %i.%i\n", oss->clock.seconds, oss->clock.nanoseconds);
  		logFile = freopen("/dev/null", "w", logFile);
  	}

    //advance time with random nanoseconds in range [0; 1000]
    clockInc(&oss->clock, 0, rand() % 1000);
  }

  /* stop all processes, that are still running */
  for(i=0; i < runLimit; i++){
    if(oss->procs[i].pid > 0){
      oss->procs[i].state = sTERMINATED;
      kill(oss->procs[i].pid, SIGTERM);
      waitpid(oss->procs[i].pid, NULL, 0);
    }
  }
}

//Create the oss shared memory and semaphores
static int init_oss(){
  oss = ossCreate(IPC_CREAT | IPC_EXCL | S_IRWXU);
  if(oss == NULL){
    return -1;
  }
	srand(getpid());

  bzero(oss, sizeof(struct oss));
  return 0;
}

//Initialize the virtual oss memory
static void init_memory(){

  int i;
 	for(i=0; i < frameCount; i++){
 		frameClear(i);
 	}

 	for(i=0; i < runLimit; i++){
    clearProcess(oss->procs, i);
    ptClear(oss->procs[i].vm);
 	}
}

static void sig_handler(const int sig){

  oss->pstat.terminated = startLimit;

  fprintf(logFile, "Master: Interrupted by %d at time %i.%i\n", sig, oss->clock.seconds, oss->clock.nanoseconds);
  num_lines++;
}

//Start the fifo daemon
static pid_t start_fifo_daemon(){
  const pid_t pid = fork();
  if(pid == -1){
    perror("fork");
    return -1;
  }else if(pid == 0){

    execl("./fifo_daemon", "./fifo_daemon", 0, NULL);

    perror("execl");
    exit(EXIT_FAILURE);
  }
  return pid;
}

int main(const int argc, char * argv[]){
  int num_users = 20;
  pid_t daemon_pid = 0;

	if(argc >= 2){
    if(strcmp(argv[1], "-p") == 0){
      num_users = atoi(argv[2]);
      if(num_users > startLimit){
        num_users = startLimit;
      }
    }else{
      fprintf(stderr, "Usage: ./master [-p 20]\n");
      return -1;
    }
  }

  signal(SIGTERM, sig_handler);
  signal(SIGINT,  sig_handler);
  signal(SIGALRM, sig_handler);
  signal(SIGCHLD, SIG_IGN);

  logFile = fopen(masterLog, "w");
  if(logFile == NULL){
    perror("fopen");
    return 1;
  }

  if(init_oss() < 0){
    return 1;
  }
  init_memory();
  daemon_pid = start_fifo_daemon();
  if(daemon_pid < 0){
    return 1;
  }

  alarm(10);

  monitor_references(num_users);

  logFile = freopen(masterLog, "a", logFile);
  if(logFile == NULL){
    perror("fopen");
    return 1;
  }

  printFrames(logFile);

  fprintf(logFile, "Clock: %i:%i\n", oss->clock.seconds, oss->clock.nanoseconds);
  fprintf(logFile, "References (all, read, write, err): %u, %u, %u, %u\n", oss->mstat.refs, oss->mstat.reads, oss->mstat.writes, oss->mstat.invalid);
  fprintf(logFile, "Page fault per reference : %.2f%%\n", ((float)oss->mstat.faults / oss->mstat.refs) * 100);
  fprintf(logFile, "Seg fault per reference : %.2f%%\n", ((float)oss->mstat.invalid / oss->mstat.refs) * 100);
  fprintf(logFile, "Memory speed: %.2f references/second\n", (float) oss->mstat.refs / oss->clock.seconds);

  //stop the daemon
  kill(daemon_pid, SIGTERM);
  waitpid(daemon_pid, NULL, 0);

  ossDestroy(oss);

  fclose(logFile);


  return 0;
}
