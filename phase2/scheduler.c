#include <uriscv/liburiscv.h>
#include <string.h>
#include "../phase1/headers/pcb.h"
#include "../headers/const.h"

extern int process_count;
extern int waiting_count;
extern int global_lock;
extern pcb_PTR current_process[NCPU];
extern struct list_head ready_queue;
extern void print(char *msg);
extern int debug;
extern void klog_print(char *str);
int cpu_id;

/**
 * Scheduler - Seleziona il prossimo processo da eseguire
 * 
 * Comportamento:
 * 1. Prende un processo dalla ready_queue (se disponibile)
 * 2. Imposta timer e priorità
 * 3. Se la ready_queue è vuota, attende interrupt o gestisce deadlock
 * 
 * Note:
 * - Usa gpr[5] (t0) per memorizzare l'istante di inizio timeslice
 * - Gestisce correttamente TPR (Task Priority Register)
 */
void schedule() {
    cpu_id = getPRID();
    // dispatch the next process
    current_process[cpu_id] = removeProcQ(&ready_queue);
    //HALT();
    if (current_process[cpu_id]!= NULL) {
        // load the PLT
        setTIMER(TIMESLICE * (*((cpu_t *)TIMESCALEADDR)));
        // perform Load Processor State
        LDST(&current_process[cpu_id]->p_s);
    } else if (process_count > 0 && waiting_count > 0) {
        // waiting for an interrupt
        setSTATUS((IECON | IMON) & (~TEBITON));
        WAIT();
    } else if (process_count > 0 && waiting_count == 0) {
        // deadlock
        PANIC();
    }
  }