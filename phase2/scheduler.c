#include "../phase2/headers/scheduler.h"

extern int process_count;
extern int waiting_count;
extern int global_lock;
extern pcb_PTR current_process[NCPU];
extern struct list_head ready_queue;

/**
 * Carica un processo per essere mandato in run, altrimenti blocca l'esecuzione.
 * <p>Salvare lo stato, rimuovere il processo precedente ecc viene svolto dal chiamante
 */
void schedule() {
  //Con il LOCK garantisco accesso esclusivo alla coda dei processi pronti "ready_queue"
  ACQUIRE_LOCK(&global_lock);
  // dispatch the next process
  current_process[cpu_id] = removeProcQ(&ready_queue);

  if (current_process != NULL) {
    // load the PLT
    setTIMER(TIMESLICE * (*((cpu_t *)TIMESCALEADDR)));
    // perform Load Processor State
    RELEASE_LOCK(&global_lock); 
    LDST(&current_process[cpu_id]); //Load Processor State (LDST) sul processore, carica lo stato salvato nel PCB del processo selezionato, permettendone la ripresa dell'esecuzione
  } else if (process_count == 1) {
    // only SSI in the system
    HALT();
  } else if (process_count > 0 && waiting_count > 0) {
    // waiting for an interrupt
    setSTATUS((IECON | IMON) & (~TEBITON));
    WAIT();
  } else if (process_count > 0 && waiting_count == 0) {
    // deadlock
    PANIC();
  }
}