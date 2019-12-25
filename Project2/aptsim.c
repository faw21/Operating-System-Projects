#include <linux/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "sem.h"
#include <sys/mman.h>

//Arguments
int m; //number of tenant 
int k; //number of agent
int pt; // probability of a tenant immediately following another tenant
int dt; // delay in seconds when a tenant does not immediately follow another tenant
int st; //random seed for the tenant arrival process
int pa; // probability of an agent immediately following another agent
int da; //delay in seconds when an agent does not immediately follow another agent
int sa; // random seed for the agent arrival process
int start_time; //the benchmark of the time


struct each_agent {
    int curr_serving; // current number of tenant whom the agent is serving
    int total_served; // the total number of tenants the agent has served
};

struct cs1550_sem * apt_door; //lock of the apartment, used by agents
struct cs1550_sem * tenant; // agent wait for tenant
struct cs1550_sem * agent; // tenant wait for agent
struct cs1550_sem * agent_info_lock; // the lock to modify the agent_info
struct cs1550_sem * tenant_action; // ensure only on tenant enter critical section 
struct cs1550_sem * agent_waits; // agent will wait on this semaphore after open the apt
struct each_agent * agent_info; // the each_agent struct to store info of how many tenants has the agent served



//void down(struct cs1550_sem *sem) {
//    syscall(__NR_cs1550_down, sem);
//}

//void up(struct cs1550_sem *sem) {
//    syscall(__NR_cs1550_up, sem);
//}

void agentArrives(int index) {
    printf("Agent %d arrives at time %d.\n", index, time(NULL)-start_time);
    down(apt_door); // wait for the apartment
    down(tenant); // wait for the first tenant
}

void openApt(int index) {
    down(agent_info_lock); 
    printf("Agent %d opens the apartment for inspection at time %d.\n", index, time(NULL)-start_time);
    agent_info->curr_serving = 0;
    agent_info->total_served = 0;

    up(agent); // tell the first tenant the agent is ready
    up(agent_info_lock);
    down(agent_waits); // wait until the last tenant calls up(agent_waits)
}

void agentLeaves(int index) { 
    down(agent_info_lock);
    agent_info->curr_serving = 0;
    agent_info->total_served = 0;
    printf("Agent %d leaves the apartment at time %d.\nThe apartment is now empty.\n", index, time(NULL)-start_time);
    up(agent_info_lock);
    up(apt_door); // let other agent to enter the apartment
}

void tenantArrives(int index) {
    printf("Tenant %d arrives at time %d.\n", index, time(NULL)-start_time);
    down(tenant_action); 
    down(agent_info_lock); // lock to check the agent_info
    if (agent_info->curr_serving == 0 || agent_info->total_served >= 10) {
        up(tenant); // tell agent there is tenant waiting
        up(agent_info_lock); // finish checking
        down(agent); // wait for agent
        down(agent_info_lock); 
        agent_info->curr_serving++;
        agent_info->total_served++;
        up(agent_info_lock);
    } else { // if the apt is open and can be entered
        agent_info->curr_serving++;
        agent_info->total_served++;
        up(agent_info_lock); 
    }
    up(tenant_action); // release the lock to let other tenant try to enter the house
}

void viewApt(int index){
    down(agent_info_lock);//ensure no tenant can modify the number in agent_info when printing the following message
    printf("Tenant %d inspects the apartment at time %d.\n", index, time(NULL)-start_time);
    up(agent_info_lock);
    sleep(2);
} 

void tenantLeaves(int index) {
    down(agent_info_lock);
    agent_info->curr_serving--;
    if (agent_info->curr_serving == 0) { // if the tenant is the last tenant in the apartment
        up(agent_waits);                    // tell the agent you can leave as well
    }
    printf("Tenant %d leaves the apartment at time %d.\n", index, time(NULL)-start_time);
    up(agent_info_lock);
}

void print_error(){
    printf("Invalid arguments!\n");
    printf("usage: ./aptsim -m %%d -k %%d -pt %%d -dt %%d -pa %%d -da %%d\n");
    exit(1);
}

int main(int argc, char ** argv) {

    //Shared memory
    apt_door = mmap(NULL,sizeof(struct cs1550_sem), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
    tenant = mmap(NULL,sizeof(struct cs1550_sem), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
    agent = mmap(NULL,sizeof(struct cs1550_sem), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
    agent_info_lock = mmap(NULL,sizeof(struct cs1550_sem), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
    tenant_action = mmap(NULL,sizeof(struct cs1550_sem), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
    agent_waits = mmap(NULL,sizeof(struct cs1550_sem), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);
    agent_info = mmap(NULL,sizeof(struct each_agent), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);

    //Semaphore initialization
    apt_door->value = 1; 
    tenant->value = 0; 
    agent->value = 0;
    agent_info_lock->value = 1;
    tenant_action->value = 1;
    agent_waits->value = 0;

    //check the arguments
    if (argc != 17){
        print_error();
    }
    else{
        if (strcmp(argv[1], "-m")==0){ 
            m = atoi(argv[2]);
        }
        else print_error();
        if (strcmp(argv[3], "-k")==0){
            k = atoi(argv[4]);
        }
        else print_error();
        if (strcmp(argv[5], "-pt")==0){
            pt = atoi(argv[6]);
        }
        else print_error();
        if (strcmp(argv[7], "-dt")==0){
            dt = atoi(argv[8]);
        }
        else print_error();
        if (strcmp(argv[9], "-st")==0){
            st = atoi(argv[10]);
        }
        else print_error();
        if (strcmp(argv[11], "-pa")==0){
            pa = atoi(argv[12]);
        }
        else print_error();
        if (strcmp(argv[13], "-da")==0){
            da = atoi(argv[14]);
        }
        else print_error();
        if (strcmp(argv[15], "-sa")==0){
            sa = atoi(argv[16]);
        }
        else print_error();
        
    }

    //Start
    printf("The apartment is now empty.\n");
    start_time = time(NULL);
    if (fork() == 0) { 
        int i;
        for (i=0; i<k; i++) { //Generates agents
            if (fork() == 0) { 
                agentArrives(i);
                openApt(i);
                agentLeaves(i);
                exit(0);
            } 
            srand(sa);//use -sa as the seed
            if (rand() % 100 >= pa) { // wait for "-da" time if the second agent is not following the first one
                sleep(da);
            } 
        }

        for (i=0; i<k; i++) {
            wait(NULL);
        }
    }
    else{ 
        if (fork() == 0){ 
            int i;
            for (i=0; i<m; i++) {// Generates tenants
                if (fork() == 0) {
                    tenantArrives(i);
                    viewApt(i);
                    tenantLeaves(i);
                    exit(0);
                } 
                srand(st);//use -st as the seed
                if (rand() % 100 >= pt) { // wait for "-dt" time if the second agent is not following the first one
                    sleep(dt);
                } 
            }

            for (i=0; i<m; i++) {
                wait(NULL);
            }   
        }
        else{
            // the parent process wait for all producer processes terminates
            wait(NULL);
            wait(NULL);
        }
    }

}
