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

int main(const int argc, char * const argv[]){

	unsigned int ref_count = 900 + (rand() % 200);	//1000 +/- 100;

	if(argc != 2){
		fprintf(stderr, "Usage: user <index>\n");
		return -1;
	}

	//get our index
	const int id = atoi(argv[1]);

	//create the oss pointer
	struct oss *oss = ossCreate(0);
	if(oss == NULL){
		return 1;
	}

	//get our process structure
	struct process *proc = &oss->procs[id];

	srand(getpid());

	while(1){
		int addr;

		if((rand() % 100) < 2){	//2% chance to generate invalid address
			//make an address outside of our virtual space
			addr = (pageCount*pageSize) + 1;
		}else{
			//generate a random page and offset, to make an address
			addr = ((rand() % pageCount) * pageSize) + (rand() % pageSize);
		}

		// 65 % read priority
		enum refRW rw = ((rand() % 100) < 65) ? refRD : refWR;

		if(ossWait(id) < 0)
			break;

		//check if we should terminate
		if(--ref_count <= 0){
			//if our terminate flag is set
			if(proc->state == sTERMINATED){
				ossPost(id);
				break;
			}
			//update counter for next check
			ref_count = 900 + (rand() % 200);	//1000 +/- 100
		}

		//fill the reference
		proc->reference.addr = addr;
		proc->reference.rw	 = rw;
		proc->reference.state   = refPending;

		//wait until its processed
		while(proc->reference.state == refPending){
			if(ossPost(id) < 0){
				break;
			}

			//usleep(5);
			if(ossWait(id) < 0)
				break;
		}
		if(ossPost(id) < 0)
			break;

		if(proc->reference.state == refError){
			break;
		}

	}

	ossWait(id);
	proc->state = sTERMINATED;
	ossPost(id);

	shmdt(oss);

  return EXIT_SUCCESS;
}
