#include <uriscv/liburiscv.h>
#include <string.h>
#include "../headers/const.h"
#include "../headers/types.h"
#include "../phase1/headers/pcb.h"
extern pcb_PTR current_process[NCPU];
extern struct list_head ready_queue;
extern state_t *currentState;
extern int waiting_count;
extern unsigned int *stateCauseReg;
extern int dev_semaph[NRSEMAPHORES];
extern int global_lock;
extern void updateProcessTime(int cpu_id);
static void handlePLT(int cpu_id);
static void handleIntervalTimer(void);
void interruptHandler();
#include <uriscv/cpu.h>

/* ------------------------------------------------------------------
   Gestione della linea‑1  (Processor Local Timer – PLT)
   ------------------------------------------------------------------ */
   static void handlePLT(int cpu_id)
   {
       /* 1. Aggiorna il tempo CPU del PCB corrente */
       updateProcessTime(cpu_id);
   
       /* 2. Salva lo stato corrente nel PCB e rimetti in ready queue */
       if (current_process[cpu_id] != NULL) {
           current_process[cpu_id]->p_s = *currentState;
           insertProcQ(&ready_queue, current_process[cpu_id]);
       }
   
       /* 3. Azzeriamo current_process e ricarichiamo il PLT */
       current_process[cpu_id] = NULL;
       setTIMER(TIMESLICE);                /* 5 ms per nuovo time‑slice */
   
       /* 4. Chiamata allo scheduler (non ritorna) */
       schedule();
   }
   
   /* ------------------------------------------------------------------
      Gestione della linea‑2  (Interval Timer – Pseudo‑clock tick)
      ------------------------------------------------------------------ */
   static void handleIntervalTimer(void)
   {
       /* 1. Ricarica l’Interval Timer con 100 ms */
       LDIT(PSECOND);
   
       /* 2. Sveglia tutti i PCB bloccati sul semaforo pseudo‑clock */
       pcb_PTR p;
       while ((p = removeBlocked(&dev_semaph[NRSEMAPHORES-1])) != NULL) {
           p->p_semAdd = NULL;
           insertProcQ(&ready_queue, p); 
           waiting_count--;
       }
       dev_semaph[NRSEMAPHORES-1] = 0;     /* reset semaforo */
   }

/**
* Gestione dell'interrupt di un dispositivo specifico
* @param line: linea di interrupt
*/
static void deviceHandler(int line)
{
    devregarea_t *devarea = (devregarea_t *)RAMBASEADDR;
    unsigned int bitmap   = devarea->interrupt_dev[line - IL_DISK];

    /* scorri i device 0…7 della linea */
    for (int dev = 0; dev < 8; dev++) { //NOTE 8 = 	Numero di sub‑device collegati a ciascuna linea d’interrupt 3‑7 (disk0‑7, flash0‑7, …).
        if (!(bitmap & (1 << dev))) continue;          /* nessun interrupt */

        devreg_t *d = &devarea->devreg[line - IL_DISK][dev];
        unsigned int status;

        if (line == IL_TERMINAL) {
            /* priorità: trasmissione > ricezione */
            if (d->term.transm_status & DEV0ON) {
                status = d->term.transm_status;
                d->term.transm_command = ACK;
            } else {
                status = d->term.recv_status;
                d->term.recv_command  = ACK;
            }
        } else {                                       /* disk, flash, ecc. */
            status = d->dtp.status;
            d->dtp.command = ACK;
        }

        /* sblocca eventuale PCB in attesa su semaforo del device */
        int semIndex = (line - IL_DISK) * 8 + dev;
        pcb_PTR p = removeBlocked(&dev_semaph[semIndex]);
        if (p != NULL) {
            p->p_semAdd     = NULL;
            p->p_s.gpr[2]   = status;                  /* v0 <- status dev */
            waiting_count--;
            insertProcQ(&ready_queue, p);
        }
    }
}

  void interruptHandler(void)
  {
    int  cpu   = getPRID();
    unsigned int cause = getCAUSE();          /* mcause */

    /* ---------- Linea 1: PLT ---------- */
    if (cause & LOCALTIMERINT) {
        handlePLT(cpu);                       /* pre‑emzione  */
    }

    /* ---------- Linea 2: Interval Timer / Pseudo‑clock ---------- */
    if (cause & TIMERINTERRUPT) {
        handleIntervalTimer();                /* tick 100 ms  */
    }

    /* ---------- Linee 3‑7: periferiche ---------- */
    for (int line = IL_DISK; line <= IL_TERMINAL; line++) {
        if (CAUSE_IP_GET(cause, line)) {      /* bit IP(line) == 1 ? */
            deviceHandler(line);              /* gestisci TUTTI i dev di quella linea */
        }
    }
  
      /* al termine si ritorna al processo corrente (se esiste)
         o lo scheduler ha già preso controllo dall’interno
         di handlePLT / handleIntervalTimer / deviceHandler */
  }
  