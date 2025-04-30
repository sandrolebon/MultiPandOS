//NOTE con vergogna debbo puntualizzare che di mio c'è poco e niente qui
#include <uriscv/liburiscv.h>
#include <string.h>
#include "../headers/const.h"
#include "../headers/types.h"
#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"

extern pcb_PTR current_process[NCPU];
extern int dev_semaph[NRSEMAPHORES];
extern struct list_head pseudoclock_blocked_list;
extern int process_count;
extern int waiting_count;
extern int cpu_id;
extern struct list_head ready_queue;
extern state_t *currentState;
extern unsigned int *stateCauseReg;
extern void schedule();
extern void interruptHandler(); //Gestisce gli interrupt hardware (timer, dispositivi)
static void syscallHandler(); //Gestisce le syscall (NSYS1-NSYS7)
static void programTrapHandler(); //Gestisce errori software, istruzioni privilegiate
void exceptionHandler(); //Riceve qualsiasi eccezione o interrupt; smista in base al tipo
void terminateProcess(pcb_PTR proc);
void passUpOrDie(int exceptionType, int cpu_id); //Passa l'eccezione al processo padre o termina il processo corrente

void passUpOrDie(int exceptionType, int cpu_id) {
    if (current_process[cpu_id]->p_supportStruct != NULL) {
        support_t *support = current_process[cpu_id]->p_supportStruct;
        support->sup_exceptState[exceptionType] = *currentState;
        LDST(&(support->sup_exceptContext[exceptionType]));
    } else {
        terminateProcess(current_process[cpu_id]);
        schedule();
    }
}

//TODO - scegliere un approccio: o tremila funzioni o all in switch
cpu_t GETTIMEsys() {
    int cpu_id = getPRID();
    cpu_t now;
    STCK(now);
    cpu_t start = current_process[cpu_id]->p_s.gpr[5]; // gpr[5] (t0) contiene p_s_time: inizio dell’attuale time slice
    return current_process[cpu_id]->p_time + (now - start);
}

/* --------------------------------------------------------------------
   Aggiorna p_time del processo che sta girando sulla CPU indicata
   usando gpr[5] --> p_s_time   (vedi discussione precedente)
   ------------------------------------------------------------------ */
static inline void updateProcessTime(int cpu_id)
{
    cpu_t now; STCK(now);                          /* TOD clock attuale */
    pcb_PTR p = current_process[cpu_id];
    if (p == NULL) return;                         /* nessun processo   */
    cpu_t start = p->p_s.gpr[5];                   /* slice start       */
    p->p_time += (now - start);                    /* accumula tempo    */
}


/**
 * PASSERENsys: simula operazione P (DOWN) su semaforo.
 * @param semaddr: indirizzo del semaforo
 * @return 1 se il processo è stato bloccato, 0 altrimenti
 
//TODO - prob eliminabile
 int PASSERENsys(int *semaddr) {
  int cpu_id = getPRID();
  (*semaddr)--; // decrementa semaforo

  if ((*semaddr) < 0) {
      // semaforo negativo => blocca il processo corrente
      updateProcessTime(cpu_id);
      current_process[cpu_id]->p_s = *currentState; // salva stato corrente nel PCB
      current_process[cpu_id]->p_semAdd = semaddr;  // associa il semaforo al processo

      insertBlocked(semaddr, current_process[cpu_id]);

      current_process[cpu_id] = NULL;

      return 1; // processo bloccato
  }

  return 0; // processo NON bloccato
}
*/ 

static void syscallHandler() {
  int cpu_id = getPRID();
  pcb_PTR proc = current_process[cpu_id];

  // Avanza il Program Counter per evitare di ripetere la syscall
  currentState->pc_epc += 4;

  unsigned int syscall_num = currentState->gpr[24]; // a0 contiene il numero della syscall

  switch (syscall_num) {
      case CREATEPROCESS: {
          pcb_PTR newProc = allocPcb();
          if (newProc == NULL) {
              currentState->gpr[2] = (unsigned int)NOPROC; // v0 <- -1 (errore)
          } else {
              newProc->p_s = *((state_t *)currentState->gpr[25]); // a1 = stato iniziale
              newProc->p_time = 0;
              newProc->p_semAdd = NULL;
              newProc->p_supportStruct = (support_t *)currentState->gpr[26]; // a2 = supporto, può essere NULL
              insertProcQ(&ready_queue, newProc);
              newProc->p_parent = proc;
              list_add_tail(&newProc->p_list, &proc->p_child);
              process_count++;
              currentState->gpr[2] = 0; // v0 <- 0 (successo)
          }
          break;
      }

      case TERMPROCESS: {
          terminateProcess(proc);
          schedule(); // Non ritorniamo più da qui
          break;
      }

      case PASSEREN: {
        int *semaddr = (int*)currentState->gpr[25];
    
        (*semaddr)--;
        if (*semaddr < 0) {
            /* dovrà bloccare -> aggiorna tempo, inserisci su ASL, schedule() */
            updateProcessTime(cpu_id);
            current_process[cpu_id]->p_s = *currentState;
            current_process[cpu_id]->p_semAdd = semaddr;
            insertBlocked(semaddr, current_process[cpu_id]);
            current_process[cpu_id] = NULL;
            waiting_count++;
            schedule();                /* ← NON TORNA */
        }
        /* se non blocca, esce dallo switch
           e finirà nell’LDST comune in coda */
        break;
    }
    
      case VERHOGEN: {
          int *semaddr = (int *)currentState->gpr[25]; // a1
          (*semaddr)++; // incrementa semaforo

         if ((*semaddr) <= 0) {
            // c'è qualcuno bloccato su questo semaforo: sveglialo
            pcb_PTR unblocked = removeBlocked(semaddr);

        if (unblocked != NULL) {
          unblocked->p_semAdd = NULL; // processo ora non più bloccato
          insertProcQ(&ready_queue, unblocked);
      }
  }
          break;
      }
      
      case DOIO: {
        int  *cmdAddr = (int *)currentState->gpr[25];  /* a1 */
        int   command =           currentState->gpr[26];  /* a2 */
        int  *devAddr = (int *)currentState->gpr[27];  /* a3 */
    
        updateProcessTime(cpu_id);                      /* ← nuovo passo */
    
        current_process[cpu_id]->p_s = *currentState;
        current_process[cpu_id]->p_semAdd = devAddr;
        insertBlocked(devAddr, current_process[cpu_id]);
    
        current_process[cpu_id] = NULL;
        waiting_count++;
    
        *cmdAddr = command;                             /* scrivi comando */
        schedule();                                     /* non ritorna    */
    }
            
      case GETTIME: {
          cpu_t time = GETTIMEsys();
          currentState->gpr[2] = (unsigned int)time;
          break;
      }

      case CLOCKWAIT: {
        updateProcessTime(cpu_id);                        /* ← nuovo passo */
    
        current_process[cpu_id]->p_s = *currentState;
        current_process[cpu_id]->p_semAdd = &dev_semaph[NRSEMAPHORES-1];
        insertBlocked(&dev_semaph[NRSEMAPHORES-1], current_process[cpu_id]);
    
        current_process[cpu_id] = NULL;
        waiting_count++;
        schedule();                                       /* non ritorna   */
        /* (mai LDST qui) */
        
    }
    

      case GETSUPPORTPTR: {
          currentState->gpr[2] = (unsigned int)(proc->p_supportStruct);
          break;
      }

      default: {
          // Syscall sconosciuta: comportati come un Program Trap
          programTrapHandler();
          break;
      }
  }/* -----------------------------------------------------------------
    Se siamo arrivati qui significa che:
    - la syscall NON ha bloccato il processo
    - NON lo ha terminato
    -> dobbiamo restituire il controllo all’utente
   ----------------------------------------------------------------- */
  LDST(currentState); 
}

/**
 * Program Trap Handler
 */
static void programTrapHandler() {
    // Terminate the process that caused illegal instruction
    int cpu_id = getPRID();
    terminateProcess(current_process[cpu_id]);
    schedule();
}

void terminateProcess(pcb_PTR proc) {
  pcb_PTR child;
  int thisCpu = getPRID();

  /* se proc è quello in esecuzione su questa CPU,
     aggiorno p_time PRIMA di toccarlo               */
  if (proc == current_process[thisCpu]) {
      updateProcessTime(thisCpu);                 
      current_process[thisCpu] = NULL;
  }

  // Termina ricorsivamente tutti i figli
  while (!list_empty(&proc->p_child)) {
      child = container_of(proc->p_child.next, pcb_t, p_list);
      terminateProcess(child);
  }

  // Se il processo è bloccato su un semaforo
  if (proc->p_semAdd != NULL) {
      pcb_PTR removed = outBlocked(proc);
      if (removed != NULL) {
          if ((int)(proc->p_semAdd) >= (int)&dev_semaph[0] && (int)(proc->p_semAdd) < (int)&dev_semaph[NRSEMAPHORES]) {
              // Era bloccato su un semaforo di DEVICE o CLOCK
              waiting_count--;
            } else if (proc->p_semAdd == &dev_semaph[NRSEMAPHORES-1]) {
                         waiting_count--;                 /* pseudo-clock    */
                     } else {
              // Era bloccato su semafori normali: faccio V
              (*(proc->p_semAdd))++;
          }
      }} else {
  }

  // Se era nella ready queue
  if (proc == current_process[getPRID()]) {
      updateProcessTime(cpu_id);
      current_process[getPRID()] = NULL;
  } else {
      outProcQ(&ready_queue, proc);
  }

  // Aggiorna contatore processi
  process_count--;
  if (proc == current_process[getPRID()]) {
    cpu_t now;
    STCK(now);
    cpu_t start = proc->p_s.gpr[5];
    proc->p_time += (now - start);
}
  // Libera il PCB
  freePcb(proc);
}


void exceptionHandler() { //fa il dispatch generale: smista le eccezioni verso il giusto gestore
   /* 
   CONTROLLO BY CHATGPT
   unsigned int mcause = getCAUSE();
bool isInterrupt = (mcause >> 31) & 0x1;   /* MSB 
unsigned int exccode = mcause & 0x1F;      /* low 5 bits 

if (isInterrupt) {
    interruptHandler();
    return;
}*/
    
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

