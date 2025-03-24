//NOTE - c&p from IVAN - ci sono sicuramente differenze causa multiproc....

#include "../phase2/headers/scheduler.h"

extern int process_count;
extern int waiting_count;
extern pcb_PTR current_process;
extern struct list_head ready_queue;

/**
 * Carica un processo per essere mandato in run, altrimenti blocca l'esecuzione.
 * <p>Salvare lo stato, rimuovere il processo precedente ecc viene svolto dal chiamante
 */
void schedule() {
  // dispatch the next process
  current_process = removeProcQ(&ready_queue);

  if (current_process != NULL) {
    // load the PLT
    setTIMER(TIMESLICE * (*((cpu_t *)TIMESCALEADDR)));
    // perform Load Processor State
    LDST(&current_process->p_s);
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