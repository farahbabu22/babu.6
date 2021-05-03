#ifndef PROCESS_H
#define PROCESS_H

#include <sys/types.h>
#include <unistd.h>

#include "vm.h"

/*
author: Farah Babu
email: fbkzx@umsystem.edu
hoare id: babu
*/

//how many processes to run at once
#define runLimit 18
//maximum processes to start
#define startLimit 40

//process states
enum state { sREADY=1, sSWAP, sTERMINATED, sSTATES};

struct process {
	pid_t	pid;
	unsigned int id;
	enum state state;

	struct reference	reference;

	struct page	vm[pageCount];	//process virtual memory
};

//Processes statistics
struct procCounters {
	int started;
  	int terminated;
};

#endif
