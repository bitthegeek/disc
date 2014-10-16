#ifndef _MY_SEM_H_

#define _MY_SEM_H_

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

int sem_init(int *semid, int val) {
	
	static int proj_id = 0;
	
	if (proj_id == 255) {
		printf("maximum semaphore number reached\n");
		return -1;
	} 
	if ((*semid = semget (ftok(__FILE__, ++proj_id), 1, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR)) == -1) {
		printf("cannot create semaphore\n");
		return -1;
	} 
	return semctl(*semid, 0, SETVAL, val); 
}

int sem_close (int semid) {
	return semctl(semid, 0, IPC_RMID, 0); 
}
	
void sem_up(int semid) {
	struct sembuf sops = {0, 0, 1};
	if (semop(semid, &sops, 1) == -1) {
		printf("sem_up failure\n");
	}
}

void sem_down(int semid) {

	struct sembuf sops = {0, 0, -1};
	
restart:
	if (semop(semid, &sops, 1) == -1) {
		if (errno == EINTR)
			goto restart;
		else {
			printf("sem_down failure, sleep forever\n");
			while(pause());
		}
	}
}

#endif
