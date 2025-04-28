#include <uriscv/liburiscv.h>
#include "../headers/const.h"
#include "../headers/types.h"
#include "../phase1/headers/pcb.h"
extern pcb_PTR current_process[NCPU];
extern struct list_head ready_queue;
extern state_t *currentState;

//TODO - adattare questa IvanFunction seguendo sez.7.2 specs
void PLTInterruptHandler() {
    int cpu_id = getPRID();
    //Riconoscere il PLT interrupt caricando il timer con un nuovo valore usando setTIMER;
    copyRegisters(current_process[cpu_id]->p_s, currentState); //Copiare il processor state della CPU corrente al tempo dell'exception nel PCB (p_s) del Current Process della CPU corrente;
    current_process[cpu_id]->p_time += TIMESLICE;
    insertProcQ(&ready_queue, current_process); //Piazzare il Current Process nella Ready Queue; traslare il Current Process da running a ready;
    current_process[cpu_id] = NULL;
    schedule(); //Chiamare lo Scheduler;
}

