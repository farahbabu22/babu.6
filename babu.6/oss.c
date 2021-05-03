#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <stdio.h>
#include "oss.h"

/*
author: Farah Babu
email: fbkzx@umsystem.edu
hoare id: babu
*/

union semun {
   int              val;
   struct semid_ds *buf;
   unsigned short  *array;
   struct seminfo  *__buf;
};

//OPs to wait and release a semaphore
static struct sembuf WAIT = {.sem_num=0, .sem_flg=0, .sem_op=-1};
static struct sembuf POST = {.sem_num=0, .sem_flg=0, .sem_op=1};

//ids for shared memory and semaphore set
static int shmid = -1;
static int semid = -1;

struct oss* ossCreate(const int flags){

	shmid = shmget(ftok("oss.c", 1123), sizeof(struct oss), flags);
	if (shmid == -1) {
		perror("shmget");
		return NULL;
	}

	semid = semget(ftok("oss.c", 2223), NSEMS, flags);
	if(semid == -1){
		perror("semget");
		return NULL;
	}

  if(flags & IPC_CREAT){
    int i;
    unsigned short val[NSEMS];
  	union semun un;

    //set all semaphores to 1 (unlocked)
  	for(i=0; i < NSEMS; ++i){
      val[i] = 1;
    }
    val[daemonSem] = 0; //daemon sem is locked

    un.array = val;
  	if(semctl(semid, -1, SETALL, un) == -1){
  		perror("semctl");
			return NULL;
  	}
  }

  struct oss* oss = (struct oss*)shmat(shmid, (void *)0, 0);
	if (oss == (void *)-1) {
		perror("shmat");
		return NULL;
	}

  return oss;
}

void ossDestroy(struct oss * oss){
	shmdt(oss);
  shmctl(shmid, IPC_RMID, NULL);
  semctl(semid, 0, IPC_RMID);
}

int ossWait(const int n){

  WAIT.sem_num = n;
  if (semop(semid, &WAIT, 1) == -1) {
	   perror("semop");
	   return -1;
	}

  return 0;
}

int ossPost(const int n){

  POST.sem_num = n;
  if (semop(semid, &POST, 1) == -1) {
     perror("semop");
     return -1;
  }

  return 0;
}
