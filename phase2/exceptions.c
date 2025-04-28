//NOTE con vergogna debbo puntualizzare che di mio c'è poco e niente qui
#include <uriscv/liburiscv.h>
#include "../headers/const.h"
#include "../headers/types.h"
#include "../phase1/headers/pcb.h"

extern pcb_PTR current_process[NCPU];
extern int dev_semaph[NRSEMAPHORES];
extern struct list_head pseudoclock_blocked_list;
extern int process_count;
extern int waiting_count;
extern struct list_head ready_queue;
extern state_t *currentState;
extern unsigned int *stateCauseReg;
static void syscallHandler(); //Gestisce le syscall (NSYS1-NSYS7)
static void programTrapHandler(); //Gestisce errori software, istruzioni privilegiate
static void interruptHandler(); //Gestisce gli interrupt hardware (timer, dispositivi)
void exceptionHandler(); //Riceve qualsiasi eccezione o interrupt; smista in base al tipo

static void syscallHandler() { //egge a0 (dove secondo convenzione sta il numero della syscall).
  int cpu_id = getPRID();
  current_process[cpu_id]->p_s.pc_epc += 4; // Advance PC to avoid repeating syscall

  unsigned int syscall_num = currentState->reg_a0; // assuming syscall number is in a0

  switch (syscall_num) {
      case CREATEPROCESS:
        pcb_PTR newProc = allocPcb();
        if (newProc == NULL) { 
          currentState->reg_v0 = NOPROC;
        } else {
            newProc->p_s = *((state_t *)currentState->reg_a1);
            insertProcQ(&ready_queue, newProc);
            newProc->p_parent = current_process[cpu_id];
            list_add_tail(&newProc->p_list, &current_process[cpu_id]->p_child);
            process_count++;
            currentState->reg_v0 = (unsigned int)newProc;
        }
          break;
      case TERMPROCESS:
        terminateProcess(current_process[cpu_id]);
        scheduler(); // Non ritorneremo mai da qui
          break;
      case PASSEREN:
        int *semaddr = (int *)currentState->reg_a1;
        if (PASSERENsys(semaddr) == 1) {
          scheduler(); // processo bloccato, quindi scheduler
        }
          break;
      case VERHOGEN:
        int *semaddr = (int *)currentState->reg_a1;
         VERHOGENsys(semaddr);
          break;
      case DOIO:
          // TODO: implement DoIO syscall
          break;
      case GETTIME:
          // TODO: implement GetTime syscall
          break;
      case CLOCKWAIT:
          // TODO: implement ClockWait syscall
          break;
      default:
          // Invalid syscall -> program trap
          programTrapHandler();
          break;
  }
}

static void interruptHandler() {
  // TODO: identify source (device, clock...) and handle appropriately
}

/**
 * Program Trap Handler
 */
static void programTrapHandler() {
    // Terminate the process that caused illegal instruction
    int cpu_id = getPRID();
    terminateProcess(current_process[cpu_id]);
    scheduler();
}


/**
 * Elimina il processo p dal sistema e lo ripone nella lista dei free pcb
 * 
 * @param p processo da rimuovere dalle code
 */
 void destroyProcess(pcb_t *p) {
    if (!isInPCBFree_h(p)) {
      // lo cerco nella ready queue
      if (outProcQ(&ready_queue, p) == NULL) {
        // se non è nella ready lo cerco tra i bloccati per lo pseudoclock
        int found = FALSE;
        if (outProcQ(&pseudoclock_blocked_list, p) == NULL) {
          // se non è per lo pseudoclock, magari è bloccato per un altro interrupt
          // for (int i = 0; i < MAXDEV && found == FALSE; i++) {
          //   if (outProcQ(&external_blocked_list[0][i], p) != NULL) found = TRUE;
          //   if (outProcQ(&external_blocked_list[1][i], p) != NULL) found = TRUE;
          //   if (outProcQ(&external_blocked_list[2][i], p) != NULL) found = TRUE;
          //   if (outProcQ(&external_blocked_list[3][i], p) != NULL) found = TRUE;
          //   if (outProcQ(&terminal_blocked_list[0][i], p) != NULL) found = TRUE;
          //   if (outProcQ(&terminal_blocked_list[1][i], p) != NULL) found = TRUE;
          // }
        } else {
          found = TRUE;
        }
        // contatore diminuito solo se era bloccato per DOIO o PseudoClock
        if (found) waiting_count--;
      }
      freePcb(p);
      process_count--;
    }
  }

void terminateProgeny(pcb_t *p) {
  while (!emptyChild(p)) {
    // rimuove il primo figlio
    pcb_t *child = removeChild(p);
    // se è stato rimosso con successo, elimina ricorsivamente i suoi figli
    if (child != NULL) {
      terminateProgeny(child);
      // dopo aver eliminato i figli, distrugge il processo
      destroyProcess(child);
    }
  }
}

void terminateProcess(pcb_t *p) {
    outChild(p);
    terminateProgeny(p);
    destroyProcess(p);
  }

/**
 * Gestore Trap
 * 
 * @param indexValue indica se si tratta di un PGFAULTEXCEPT o di un GENERALEXCEPT
 */
void passUpOrDie(int indexValue, int cpu_id) {
    
    // Pass up
    if((current_process[cpu_id])->p_supportStruct != NULL) {
        
        unsigned int stackPtr, status, progCounter;
        stackPtr = current_process[cpu_id]->p_supportStruct->sup_exceptContext[indexValue].stackPtr;
        status = current_process[cpu_id]->p_supportStruct->sup_exceptContext[indexValue].status;
        progCounter = current_process[cpu_id]->p_supportStruct->sup_exceptContext[indexValue].pc;

        LDCXT(stackPtr, status, progCounter);
    }
    // Or die
    else {
        terminateProcess(current_process);
        current_process [cpu_id]= NULL;
        schedule();
    }
}

void exceptionHandler() { //fa il dispatch generale: smista le eccezioni verso il giusto gestore
    switch((getCAUSE() & GETEXECCODE) >> CAUSESHIFT) {
        case IOINTERRUPTS:
            // External Device Interrupt
            interruptHandler();
            break;
        case 1 ... 3:
            // TLB Exception
            // uTLB_RefillHandler();
            passUpOrDie(PGFAULTEXCEPT, getPRID());
            break;
        case 4 ... 7:
            // Program Traps p1: Address and Bus Error Exception
            passUpOrDie(GENERALEXCEPT, getPRID());
            break;
        case SYSEXCEPTION: 
            // Syscalls
            syscallHandler();
		    break;
        case 9 ... 12:
            // Breakpoint Calls, Program Traps p2
            passUpOrDie(GENERALEXCEPT, getPRID());
            break;
        default: 
            // Wrong ExcCode
            passUpOrDie(GENERALEXCEPT, getPRID());
            break;
	}
}

