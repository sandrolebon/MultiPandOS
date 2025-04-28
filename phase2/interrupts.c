#include <uriscv/liburiscv.h>
#include "../headers/const.h"
#include "../headers/types.h"
#include "../phase1/headers/pcb.h"
extern pcb_PTR current_process[NCPU];
extern struct list_head ready_queue;
extern state_t *currentState;

/**
* Gestione dell'interrupt di un dispositivo specifico
* @param line: linea di interrupt
*/
static void deviceHandler(int line) {
    unsigned int deviceBitmap;
    int deviceNumber = -1;
  
    // Prendi il device bitmap per questa linea
    deviceBitmap = ((devregarea_t *)RAMBASEADDR)->interrupt_dev[line - IL_DISK];
  
    // Trova quale device ha lanciato l'interrupt
    for (int i = 0; i < 8; i++) {
        if (deviceBitmap & (1 << i)) {
            deviceNumber = i;
            break;
        }
    }
  
    if (deviceNumber == -1) {
        PANIC(); // qualcosa è andato storto
    }
  
    devreg_t *devReg = &((devregarea_t *)RAMBASEADDR)->devreg[line - IL_DISK][deviceNumber];
    unsigned int status;
  
    if (line == IL_TERMINAL) {
        // Terminali gestiscono separatamente ricezione e trasmissione
        if (devReg->term.recv_status & DEV0ON) {
            status = devReg->term.recv_status;
            devReg->term.recv_command = ACK;
        } else {
            status = devReg->term.transm_status;
            devReg->term.transm_command = ACK;
        }
    } else {
        status = devReg->dtp.status;
        devReg->dtp.command = ACK;
    }
  
    // Risveglia il processo bloccato su quel device
    pcb_PTR p = removeBlocked((int *)&dev_semaph[(line - IL_DISK) * 8 + deviceNumber]);
    if (p != NULL) {
        p->p_semAdd = NULL;
        waiting_count--;
        p->p_s.gpr[2] = status; // restituisco lo status nel registro v0
        insertProcQ(&ready_queue, p);
    }
  }
  

static void interruptHandler() {
    int cpu_id = getPRID();
    unsigned int cause = *stateCauseReg;
  
    if (cause & LOCALTIMERINT) {
        // Timer Locale scaduto: Preempt processo
        current_process[cpu_id]->p_s = *currentState; // salva stato
        insertProcQ(&ready_queue, current_process[cpu_id]);
        current_process[cpu_id] = NULL;
        schedule();
    }
  
    if (cause & TIMERINTERRUPT) {
        // Interval Timer scaduto: PseudoClock tick
        LDIT(PSECOND); // ricarica Interval Timer
  
        // Risveglia tutti i processi bloccati sul semaforo PseudoClock
        pcb_PTR p;
        while ((p = removeBlocked((int *)&dev_semaph[NRSEMAPHORES - 1])) != NULL) {
            p->p_semAdd = NULL;
            insertProcQ(&ready_queue, p);
            waiting_count--;
        }
    }
  
    // Gestione Interrupt di Dispositivo
    for (int line = IL_DISK; line <= IL_TERMINAL; line++) {
        if (CAUSE_IP_GET(cause, line)) {
            // c'è un interrupt su questa linea
            deviceHandler(line);
        }
    }
  }