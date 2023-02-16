/*
Roll No.: 21CS60R40

Instructions to run the program

1. install ncurses library using command
		sudo apt-get install libncurses-dev

2. To compile program use
		gcc 21CS60R40_ass6_task_1.c -lncurses
*/
#include<stdio.h>
#include<unistd.h>
#include<sys/ipc.h>
#include<stdlib.h>
#include<sys/wait.h>
#include<sys/shm.h>
#include<sys/types.h>
#include<sys/sem.h>
#include<time.h>
#include <ncurses.h>
#define NFLOORS 5
#define NLIFTS 4
#define NPEOPLE 10
#define P(s) semop(s, &pop, 1) 
#define V(s) semop(s, &vop, 1)

//name of each person
char names[11] = {'A','B','C','D','E','F','G','H','I','J','\0'};

//loop is indicates main function to terminate.
int loop = 1;

// 1-UP 0-DOWN
typedef struct {
	int waiting[2]; 				//(shared memory)waiting[0]: waiting to go down waiting[1]: waiting to go up
	int arrow;						//(semaphore) arrow[0]: down arrow, arrow[1] uparrow 
	int lifts_on_floor[2][NLIFTS];	//(shared memory)track the lifts that are available on that floor.
	int people_waiting_arr[2][NPEOPLE];	//track the people who are waiting to go up(1) or down(0);
	int not_waiting[NPEOPLE]; //track the people who have not decided their destination.
} floor_info;

typedef struct {
	int no; 		//id of the lift.
	int position; 	//current position of variable
	int dir; 		//direction of the lift 0: down 1:up
	int people_in_lift;	//counts the total number of people in lift.
	int stops[NFLOORS]; //number of people waiting for perticular floor to arrive.
	int stopsem; //floor semaphore on which people process wait. 
	int lift_arr[NPEOPLE]; //track people present in the lift.
} lift_info;

typedef struct {
	int person_id;	//person id
	int curr_floor;	//current floor of person
	int dest_floor;	//destination floor of person
	int lift_no;	//id of the lift choosen by the person.
} person_info;


floor_info *floors;
lift_info *lifts;
person_info *person;

pid_t lift_process[NLIFTS];
pid_t person_process[NPEOPLE];

//SIGINT signal handler to perform cleanup operations.
void handler(int sig){ 
  int i, j;
  char *arg_ipcrm[] = {0};
  for(i = 0; i < NPEOPLE; i++){
  	kill(person_process[i], SIGTERM);
  }
  for(j = 0; j < NLIFTS; j++){
  	kill(lift_process[j], SIGTERM);
  }
  if(fork() == 0){
  	execvp("ipcrm",arg_ipcrm);
  }
  wait(NULL);
  loop = 0;
}

int main(){
	int i = 0,k = 0;

	//this semaphore ensures mutual exclusion when updating number of people waiting on each floor.
	int semid_waiting_update[NFLOORS];

	//this semaphore ensures mutual exclusion when updating floor lifts_on_floor array.
	int semid_lift_update[NFLOORS];

	//this semaphore ensures mutual exclusion when updating the stop array in lift.
	int stop_arr_sem[NLIFTS];

	//sembuf definations
	struct sembuf pop,vop;
	pop.sem_flg = SEM_UNDO;
	vop.sem_flg = SEM_UNDO;
	
	//shared memory ids.
	int shmid_floor_info;
	int shmid_lift_info;
	int shmid_person_info;

	//allocating the floor info shared memory
	shmid_floor_info = shmget(IPC_PRIVATE,NFLOORS*sizeof(floor_info),0777|IPC_CREAT);
	floors = (floor_info*)shmat(shmid_floor_info,0,0);

	//allocating the lift info shared memory
	shmid_lift_info = shmget(IPC_PRIVATE,NLIFTS*sizeof(lift_info),0777|IPC_CREAT);
	lifts = (lift_info*)shmat(shmid_lift_info,0,0);

	//allocating the person info shared memory
	shmid_person_info = shmget(IPC_PRIVATE,NPEOPLE*sizeof(person_info),0777|IPC_CREAT);
	person = (person_info*)shmat(shmid_person_info,0,0);

	
	
	for(i = 0; i < NFLOORS; i++){
		floors[i].arrow = semget(IPC_PRIVATE, 2, 0777|IPC_CREAT);
		semctl(floors[i].arrow , 0, SETVAL, 0);
		semctl(floors[i].arrow , 1, SETVAL, 0);
		
		semid_waiting_update[i]  = semget(IPC_PRIVATE,2,0777|IPC_CREAT);
		semctl(semid_waiting_update[i],0,SETVAL,1);
		semctl(semid_waiting_update[i],1,SETVAL,1);

		semid_lift_update[i] = semget(IPC_PRIVATE,2,0777|IPC_CREAT);
		semctl(semid_lift_update[i],0,SETVAL,1);
		semctl(semid_lift_update[i],1,SETVAL,1);
		
	}

	
	for(i = 0; i < NLIFTS; i++){
		lifts[i].no = i;

		//initial position and direction of each lift.
		lifts[i].position = i;
		lifts[i].dir = i%2;


		stop_arr_sem[i] = semget(IPC_PRIVATE,1,0777|IPC_CREAT);
		semctl(stop_arr_sem[i],0,SETVAL,1);

		lifts[i].stopsem = semget(IPC_PRIVATE,NFLOORS,0777|IPC_CREAT);
		semctl(lifts[i].stopsem,0,SETVAL,0);
		semctl(lifts[i].stopsem,1,SETVAL,0);

		//initializing the lifts_on_floor array.
		floors[lifts[i].position].lifts_on_floor[lifts[i].dir][i] = 1;	
	}



	for(i = 0; i < NPEOPLE; i++){
		person[i].person_id = i;
		person[i].curr_floor = i%NFLOORS;
		person[i].lift_no = -1;
		person[i].dest_floor = -1;
	}

	for(i = 0; i < NPEOPLE; i++){
		if((person_process[i] = fork()) == 0){
			while(1){

				pop.sem_num = 0;
				pop.sem_op = -1;
				vop.sem_num = 0;
				vop.sem_op = 1;

				//selecting the destination floor.
				srand(time(0));
				while((person[i].dest_floor = rand()%NFLOORS) == person[i].curr_floor);
				

				//determine the direction
				int direction = (person[i].dest_floor - person[i].curr_floor) > 0? 1 : 0;
				
				while(1){					
					pop.sem_num = direction;
					pop.sem_op = -1;
					vop.sem_num = direction;
					vop.sem_op = 1;

					//update waiting variable based on direction.
					P(semid_waiting_update[person[i].curr_floor]);
					floors[person[i].curr_floor].waiting[direction]++;
					V(semid_waiting_update[person[i].curr_floor]);
					floors[person[i].curr_floor].not_waiting[i]--;
					floors[person[i].curr_floor].people_waiting_arr[direction][i]++;

					//waiting on arrow(up/down)
					P(floors[person[i].curr_floor].arrow);

					//looking for the lifts that are available.
					P(semid_lift_update[person[i].curr_floor]);
					int k;
					for(k = 0; k < NLIFTS; k++){
						if(floors[person[i].curr_floor].lifts_on_floor[direction][k] == 1){
							person[i].lift_no = k;
							break;
						}
					}
					V(semid_lift_update[person[i].curr_floor]);

					//if found the lift break else loop.
					if(person[i].lift_no != -1){
						break;
					}
				}
				pop.sem_num = 0;
				pop.sem_op = -1;
				vop.sem_num = 0;
				vop.sem_op = 1;

				//decrement waiting array
				floors[person[i].curr_floor].people_waiting_arr[direction][i]--;
				
				//increment the lift array
				lifts[person[i].lift_no].lift_arr[i]=1;
				
				//update number of people in lift and stop array in lift.
				P(stop_arr_sem[person[i].lift_no]);
				(lifts[person[i].lift_no].people_in_lift)++;
				lifts[person[i].lift_no].stops[person[i].dest_floor]++;
				V(stop_arr_sem[person[i].lift_no]);

				//waiting on stop semaphore of dest floor.
				pop.sem_num = person[i].dest_floor;
				pop.sem_op = -1;
				P(lifts[person[i].lift_no].stopsem);

				//get out of lift.
				lifts[person[i].lift_no].lift_arr[i]=0;
				
				//update current floor.
				person[i].curr_floor = person[i].dest_floor;
				person[i].lift_no = -1;
				
				person[i].dest_floor = -1;
				
				//go to waiting array
				floors[person[i].curr_floor].not_waiting[i] = 1;

				//sleep for some time
				sleep(3+i);
			}
		}
	}
	

	for(i = 0; i < NLIFTS; i++){
		if((lift_process[i] = fork()) == 0){
			while(1){
				sleep(2);
				int temp = 0;
				pop.sem_num = 0;
				pop.sem_op = -1;
				vop.sem_num = 0;
				vop.sem_op = 1;

				//release the people waiting on current floor.
				P(stop_arr_sem[lifts[i].no]);
				if(lifts[i].people_in_lift != 0){
					temp = semctl(lifts[i].stopsem,lifts[i].position,GETNCNT);
					lifts[i].people_in_lift -= temp;
					vop.sem_num = lifts[i].position;
					vop.sem_op = temp;
					V(lifts[i].stopsem);
					vop.sem_num = 0;
					vop.sem_op = 1;	
				}
				V(stop_arr_sem[lifts[i].no]);

				//check whether top floor/ bottom floor and update direction accordingly
				if(lifts[i].position == NFLOORS-1){
						lifts[i].dir = 0;
				}
				else if(lifts[i].position == 0){
						lifts[i].dir = 1;
				}
				

				pop.sem_num = lifts[i].dir;
				pop.sem_op = -1;
				vop.sem_num = lifts[i].dir;
				vop.sem_op = 1;
				

				int flag = 0;

				//wakeup the people waiting to go up/down depending upon the direction.
				P(semid_waiting_update[lifts[i].position]);
				if(floors[lifts[i].position].waiting[lifts[i].dir] != 0){
					flag = 1;
					P(semid_lift_update[lifts[i].position]);
					(floors[lifts[i].position].lifts_on_floor[lifts[i].dir][i])=1;
					V(semid_lift_update[lifts[i].position]);
					temp = semctl(floors[lifts[i].position].arrow,lifts[i].dir,GETNCNT);
					floors[lifts[i].position].waiting[lifts[i].dir] -= temp;
					vop.sem_op = temp;
					V(floors[lifts[i].position].arrow);
					vop.sem_op = 1;
				}
				V(semid_waiting_update[lifts[i].position]);

				if(flag){

					//sleep for some time while people are getting in.
					sleep(3);
					P(semid_lift_update[lifts[i].position]);
					floors[lifts[i].position].lifts_on_floor[lifts[i].dir][i]=0;
					V(semid_lift_update[lifts[i].position]);
				}

				//if all the floors above or below are empty the change the direction.
				P(stop_arr_sem[lifts[i].no]);
				if(lifts[i].people_in_lift == 0){
					V(stop_arr_sem[lifts[i].no]);
					if((lifts[i].position != NFLOORS-1) && (lifts[i].position != 0)){
						int p = 0;
						int all_empty = 1;
						if(lifts[i].dir){
							for(p = lifts[i].position+1;p<NFLOORS;p++){
								if(semctl(floors[p].arrow,lifts[i].dir,GETNCNT) != 0 || semctl(floors[p].arrow,!lifts[i].dir,GETNCNT) != 0){
									all_empty = 0;
								}
							}
						}
						else{
							for(p = lifts[i].position-1;p>=0;p--){
								if(semctl(floors[p].arrow,lifts[i].dir,GETNCNT) != 0 || semctl(floors[p].arrow,!lifts[i].dir,GETNCNT) != 0){
									all_empty = 0;
								}
							}
						}

						if(all_empty){
							lifts[i].dir = !lifts[i].dir;
						}
					}

				}
				else{
					V(stop_arr_sem[lifts[i].no]);
				}


				//lift is moving
				sleep(4+i);		
				
				//change the current position when reached the destination.
				if(lifts[i].dir){
					
					(lifts[i].position)++;
							
				}
				else{
	
					(lifts[i].position)--;
					
				}	
				

			}
		}
	}

	
	
	int a,b;
	initscr ();
	curs_set (0);
	signal(SIGINT, handler); 
	while(loop){	
		
		//print floor information (waiting array, waiting to go up, waiting to go down arrays)
		for(k = NFLOORS-1; k >= 0; k--){
			printw("Floor: %d\n",k);
			addch(ACS_UARROW);
			printw(" = [");
			for(a = 0; a < NPEOPLE; a++){
				//people waiting to go up
				if(floors[k].people_waiting_arr[1][a] == 1){
					printw("(%c,%d)",names[a],person[a].dest_floor);
				}
			}
			printw("]\n");
			addch(ACS_DARROW);
			printw(" = [");
			for(b = 0; b < NPEOPLE; b++){
				//people waiting to go down
				if(floors[k].people_waiting_arr[0][b] == 1){
					printw("(%c,%d)",names[b],person[b].dest_floor);
				}
			}
			printw("]\n");
			printw("[");
			for(a = 0; a < NPEOPLE; a++){
				//people waiting to go up
				if(floors[k].not_waiting[a] == 1){
					printw("%c ",names[a]);
				}
			}
			printw("]\n\n");

		}

		//print the names of people waiting in the lift.
		for(k = 0; k < NLIFTS; k++){
			//y = max - floor*4
			move(20-(lifts[k].position*5),(k+1)*30);
			printw("[");
			
			for(a = 0; a < NPEOPLE; a++){
				if(lifts[k].lift_arr[a] == 1){
					printw("(%c,%d)",names[a],person[a].dest_floor);
				}
			}
			printw("]");
			

		}

		refresh();
        sleep(1);
        clear();
		
	}
	endwin();
}
