#include <uriscv/liburiscv.h>
#include "../phase1/headers/pcb.h"
#include "../headers/const.h"

extern int process_count;
extern int waiting_count;
extern int global_lock;
extern pcb_PTR current_process[NCPU];
extern struct list_head ready_queue;

/**
 * Carica un processo per essere mandato in run, altrimenti blocca l'esecuzione.
 * Salvare lo stato, rimuovere il processo precedente ecc viene svolto dal chiamante
 */
void schedule() {
  //Con il LOCK garantisco accesso esclusivo alla coda dei processi pronti "ready_queue"
  ACQUIRE_LOCK(&global_lock);
  int cpu_id = getPRID(); // ottengo l'ID della CPU corrente
  // dispatch the next process
  pcb_PTR next_process = removeProcQ(&ready_queue); //Rimuovere il PCB da head  della Ready Queue e immagazzinare il puntatore al PCB nel campo Current Process della CPU corrente.
  RELEASE_LOCK(&global_lock); 
  if (next_process != NULL) {
    // Assegna il processo rimosso all'elemento current_process corrispondente alla CPU corrente
    current_process[cpu_id] = next_process;
    setTIMER(TIMESLICE * (*((cpu_t *)TIMESCALEADDR)));   // load the PLT
    LDST(&current_process[cpu_id]->p_s); //Load Processor State (LDST) sul processore, carica lo stato salvato nel PCB del processo selezionato, permettendone la ripresa dell'esecuzione
  } else if (process_count == 0) {
        HALT();//Consider this a job well done! 
        } else if (process_count > 0 && waiting_count > 0) {
          // waiting for an interrupt
          //Before executing the WAIT instruction, the Scheduler must first set the mie register to enable interrupts and either disable the PLT (also through the mie register) using:
          setMIE(MIE_ALL & ~MIE_MTIE_MASK); //Disabilita il Local Timer (MTIE) ma lascia abilitati gli interrupt di device esterni (disco, terminale, clock)
          unsigned int status = getSTATUS();
          status |= MSTATUS_MIE_MASK; //bilita il bit globale di abilitazione interrupt: senza questo bit a 1, nessun interrupt può interrompere la CPU anche se i singoli interrupt sono abilitati.
          setSTATUS(status); // enable global interrupts and timer interrupt (MTIP)
          WAIT();
          } else if (process_count > 0 && waiting_count == 0) {
              // deadlock
              PANIC();
            }
}
