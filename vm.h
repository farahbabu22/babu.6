#ifndef VM_H
#define VM_H

#include "clock.h"

/*
author: Farah Babu
email: fbkzx@umsystem.edu
hoare id: babu
*/

//access to memory and to disk
#define accessTime 10
#define swapTime (14 * 1000)

//memory dimensions (Swapping)
#define pageSize	1024
#define pageCount	32
#define frameCount 256



//% of free frames, below which we kick the daemon
#define daemonLimit (frameCount / 10)

//get page index
#define PAGEI(addr) (addr / pageSize)

enum refRW	{
  refRD=0, //read reference
  refWR    //write reference
};

struct page {
	unsigned short framei;       //frame index
	unsigned char pReferenced:1;  //page is pReferenced
};

struct frame {
	unsigned char pagei;   //page index
	unsigned char useri;   //user index

  unsigned char dirty:1;    //page is dirty
  unsigned char loading:1;  //if frame is being loaded from device
};

enum refState {
  refSuccess=0,
  refError,
  refPending,
  refSwap
};

struct reference {
	unsigned int addr;   //address being referenced
	unsigned char rw:1;  //read or write

	enum refState state;

  struct clock swapClock;  //time when page will be loaded
};

//Memory statistics
struct memCounters {
  unsigned int reads;
  unsigned int writes;
  unsigned int refs;
  unsigned int faults;
  unsigned int invalid;
};

#endif
