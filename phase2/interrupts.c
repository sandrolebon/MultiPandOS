#include <uriscv/liburiscv.h>
#include <string.h>
#include "../headers/const.h"
#include "../headers/types.h"
#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
extern pcb_PTR current_process[NCPU];
extern struct list_head ready_queue;
extern state_t *currentState;
extern int waiting_count;
extern unsigned int *stateCauseReg;
extern int dev_semaph[NRSEMAPHORES];
extern int global_lock;
extern void updateProcessTime(int cpu_id);
extern void schedule();
static void handlePLT(int cpu_id);
static void handleIntervalTimer(void);
void interruptHandler();
#include <uriscv/cpu.h>

/* ------------------------------------------------------------------
   Gestione della linea‑1  (Processor Local Timer – PLT)
   ------------------------------------------------------------------ */
/**
 * Gestore del Processor Local Timer (PLT)
 * 
 * Comportamento:
 * 1. Salva il tempo CPU usato dal processo corrente
 * 2. Rimette il processo in ready_queue
 * 3. Ricarica il timer per il prossimo timeslice
 * 4. Chiama lo scheduler
 * 
 * @param cpu_id ID della CPU dove è scattato l'interrupt
 */
static void handlePLT(int cpu_id) {
    /* === 1. Verifica preliminare === */
    if (cpu_id < 0 || cpu_id >= NCPU || !current_process[cpu_id] || !currentState) {
        PANIC();
    }

    /* === 2. Salvataggio stato e tempi === */
    updateProcessTime(cpu_id);  // Aggiorna p_time
    current_process[cpu_id]->p_s = *currentState;

    /* === 3. Reinserimento in ready_queue === */
    ACQUIRE_LOCK((volatile unsigned int*)&global_lock);
    insertProcQ(&ready_queue, current_process[cpu_id]);
    current_process[cpu_id] = NULL;
    RELEASE_LOCK((volatile unsigned int*)&global_lock);

    /* === 4. Ricarica timer e schedulazione === */
    setTIMER(TIMESLICE * (*(cpu_t *)TIMESCALEADDR));  // Scaling corretto
    schedule();
    __builtin_unreachable();
}

   
   /* ------------------------------------------------------------------
      Gestione della linea‑2  (Interval Timer – Pseudo‑clock tick)
      ------------------------------------------------------------------ */
   /**
 * Gestore dell'Interval Timer (Pseudo-clock a 100ms)
 * 
 * Comportamento:
 * 1. Ricarica il timer per il prossimo tick
 * 2. Sveglia tutti i processi bloccati sul semaforo pseudo-clock
 * 3. Resetta il semaforo
 */
static void handleIntervalTimer(void) {
    /* === 1. Ricarica il timer === */
    LDIT(PSECOND * (*(cpu_t *)TIMESCALEADDR));  // Scaling corretto

    /* === 2. Gestione processi bloccati === */
    ACQUIRE_LOCK((volatile unsigned int*)&global_lock);
    pcb_PTR p;
    while ((p = removeBlocked(&dev_semaph[NRSEMAPHORES-1])) != NULL) {
        if (p->p_semAdd != &dev_semaph[NRSEMAPHORES-1]) {
            PANIC();  // PCB inconsistente
        }
        p->p_semAdd = NULL;
        insertProcQ(&ready_queue, p);
        waiting_count--;
    }
    
    /* === 3. Reset sicuro del semaforo === */
    dev_semaph[NRSEMAPHORES-1] = 0;
    RELEASE_LOCK((volatile unsigned int*)&global_lock);
}

/**
 * Gestore degli interrupt di dispositivo (linee 3-7)
 * 
 * @param line Linea d'interrupt (IL_DISK=3, IL_FLASH=4, ..., IL_TERMINAL=7)
 * 
 * Comportamento:
 * 1. Legge la mappa degli interrupt per la linea specificata
 * 2. Per ogni dispositivo con interrupt pendente:
 *    a) Gestisce priorità terminale (trasmissione > ricezione)
 *    b) Invia ACK al dispositivo
 *    c) Sblocca il PCB in attesa sul semaforo corrispondente
 * 
 * Strutture dati:
 * - devregarea_t: Mappa dispositivi in RAMBASEADDR (0x10000000)
 * - dev_semaph: Array di semafori (indice calcolato come line*8 + dev)
 */
static void deviceHandler(int line) {
    /* === 1. Verifica preliminare === */
    if (line < IL_DISK || line > IL_TERMINAL) {
        PANIC(); // Linea interrupt non valida
    }

    /* === 2. Accesso alla mappa dispositivi === */
    devregarea_t *devarea = (devregarea_t *)RAMBASEADDR;
    unsigned int bitmap = devarea->interrupt_dev[line - IL_DISK];

    /* === 3. Scansione dispositivi sulla linea === */
    for (int dev = 0; dev < 8; dev++) {
        if (!(bitmap & (1 << dev))) continue; // Ignora dispositivi senza interrupt

        devreg_t *d = &devarea->devreg[line - IL_DISK][dev];
        unsigned int status;

        /* --- 3a. Gestione prioritaria per terminali --- */
        if (line == IL_TERMINAL) {
            // Priorità trasmissione > ricezione
            if (d->term.transm_status & DEV0ON) {
                status = d->term.transm_status;
                d->term.transm_command = ACK;
            } else {
                status = d->term.recv_status;
                d->term.recv_command = ACK;
            }
        } 
        /* --- 3b. Altri dispositivi (disk, flash, etc.) --- */
        else {
            status = d->dtp.status;
            d->dtp.command = ACK;
        }

        /* === 4. Sblocco PCB in attesa === */
        int semIndex = (line - IL_DISK) * 8 + dev;
        pcb_PTR p = removeBlocked(&dev_semaph[semIndex]);
        
        if (p != NULL) {
            if (p->p_semAdd != &dev_semaph[semIndex]) {
                PANIC(); // PCB inconsistente
            }
            p->p_semAdd = NULL;
            p->p_s.gpr[2] = status; // Imposta stato dispositivo in v0
            waiting_count--;
            
            ACQUIRE_LOCK((volatile unsigned int*)&global_lock);
            insertProcQ(&ready_queue, p);
            RELEASE_LOCK((volatile unsigned int*)&global_lock);
        }
    }
}



void interruptHandler(void) { //NOTE -  ma serve il void nei parametri??
    const int cpu = getPRID();
    const unsigned int cause = getCAUSE();
    
    /* === 1. Verifica preliminare === */
    if (cpu < 0 || cpu >= NCPU || !currentState) {
        PANIC();
    }

    /* === 2. Interrupt PLT (Priorità massima) === */
    if ((cause & LOCALTIMERINT) && (cause & MIP_MTIP_MASK)) {
        handlePLT(cpu); // Gestione pre-emption
        return; // Non controllare altri interrupt dopo PLT
    }

    /* === 3. Interval Timer (Priorità media) === */
    if ((cause & TIMERINTERRUPT) && (cause & MIP_MTIP_MASK)) {
        handleIntervalTimer(); // Tick 100ms
        return; // Non controllare periferiche dopo timer
    }

    /* === 4. Periferiche (Priorità bassa) === */
    for (int line = IL_DISK; line <= IL_TERMINAL; line++) {
        if (CAUSE_IP_GET(cause, line)) {
            // Patch: Lock per gestione atomica
            ACQUIRE_LOCK((volatile unsigned int*)&global_lock);
            deviceHandler(line); 
            RELEASE_LOCK((volatile unsigned int*)&global_lock);
        }
    }
}
  