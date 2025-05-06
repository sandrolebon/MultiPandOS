#include <uriscv/liburiscv.h>
#include <string.h>
#include <stdbool.h>
#include "../headers/const.h"
#include "../headers/types.h"
#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"

extern pcb_PTR current_process[NCPU];
extern int dev_semaph[NRSEMAPHORES];
extern struct list_head pseudoclock_blocked_list;
extern int process_count;
extern int waiting_count;
extern int global_lock;
extern struct list_head ready_queue;
extern state_t *currentState;
extern unsigned int *stateCauseReg;
extern void schedule();
extern void interruptHandler(); //Gestisce gli interrupt hardware (timer, dispositivi)
static void syscallHandler(); //Gestisce le syscall (NSYS1-NSYS7)
static void programTrapHandler(); //Gestisce errori software, istruzioni privilegiate
void exceptionHandler(); //Riceve qualsiasi eccezione o interrupt; smista in base al tipo
void destroyProcess(pcb_t *p);
void terminateProgeny(pcb_t *p);
void passUpOrDie(int exceptionType, int cpu_id); //Passa l'eccezione al processo padre o termina il processo corrente
int isInPCBFree_h(pcb_t *p);
extern int debug;

void terminateProcess(pcb_t *p) {
    outChild(p);
    terminateProgeny(p);
    destroyProcess(p);
  }
  
  /**
   * Elimina tutti i processi figli di p
   * 
   * @param p processo di cui eliminare i figli
   */
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
        waiting_count--;
      }
      freePcb(p);
      process_count--;
    }
  }

/**
 * Passa la gestione dell'eccezione al Support Level o termina il processo
 * @param exceptionType PGFAULTEXCEPT (0) o GENERALEXCEPT (1)
 * @param cpu_id ID della CPU dove è avvenuta l'eccezione
 */
void passUpOrDie(int exceptionType, int cpu_id) {
    /* === 1. Verifiche di sicurezza === */
    if (cpu_id < 0 || cpu_id >= NCPU || !current_process[cpu_id] || !currentState) {
        PANIC(); // Stato o processo non validi
    }

    // Pass up
    if(current_process[cpu_id]->p_supportStruct != NULL) {
        unsigned int stackPtr, status, progCounter;
        stackPtr = current_process[cpu_id]->p_supportStruct->sup_exceptContext[exceptionType].stackPtr;
        status = current_process[cpu_id]->p_supportStruct->sup_exceptContext[exceptionType].status;
        progCounter = current_process[cpu_id]->p_supportStruct->sup_exceptContext[exceptionType].pc;

        LDCXT(stackPtr, status, progCounter);
    }
    // Or die
    else {
        terminateProcess(current_process[cpu_id]);
        current_process[cpu_id] = NULL;
        schedule();
    }
}



/**
 * System Call Handler - Gestisce tutte le syscall del kernel
 * Gestisce tutte le syscall obbligatorie (NSYS1-NSYS9)
 */
static void syscallHandler() {
    const int cpu_id = getPRID();
    
    /* === 1. Verifiche di sicurezza === */
    if (!currentState || !current_process[cpu_id]) {
        PANIC();
    }

    pcb_PTR proc = current_process[cpu_id];
    currentState->pc_epc += 4; // Avanza PC

    /* === 2. Estrazione parametri === */
    const int syscall_num = (int)currentState->gpr[24]; // a0
    const bool kernel_mode = (currentState->status & MSTATUS_MPP_MASK) != MSTATUS_MPP_U;

    /* === 3. Syscall privilegiate (negative) === */
    if (syscall_num < 0) {
        /* --- Controllo privilegi --- */
        if (!kernel_mode) {
            currentState->cause = PRIVINSTR;
            programTrapHandler(cpu_id);
            return;
        }

        ACQUIRE_LOCK((volatile unsigned int*)&global_lock);

        switch (syscall_num) {
            /* --- NSYS1: CREATEPROCESS (-1) --- */
            case CREATEPROCESS: {
                state_t *init_state = (state_t *)currentState->gpr[25]; // a1
                support_t *support = (support_t *)currentState->gpr[27]; // a3
                
                pcb_PTR new_proc = allocPcb();
                if (!new_proc) {
                    currentState->gpr[2] = NOPROC; // v0 = -1
                } else {
                    memcpy(&new_proc->p_s, init_state, sizeof(state_t));
                    new_proc->p_time = 0;
                    new_proc->p_semAdd = NULL;
                    new_proc->p_supportStruct = support;

                    insertProcQ(&ready_queue, new_proc);
                    insertChild(proc, new_proc);
                    process_count++;
                    
                    currentState->gpr[2] = 0; // v0 = 0 (success)
                }
                break;
            }

            /* --- NSYS2: TERMPROCESS (-2) --- */
            case TERMPROCESS: {
                int target_pid = (int)currentState->gpr[25]; // a1
                pcb_PTR target = NULL;
            
                /* --- Caso 1: Termina il processo corrente --- */
                if (target_pid == 0) {
                    target = proc;
                }
                /* --- Caso 2: Termina un child diretto (se esiste) --- */
                else {
                    // Cerca solo tra i figli diretti
                    struct list_head *pos;
                    list_for_each(pos, &proc->p_child) {
                        pcb_PTR child = container_of(pos, pcb_t, p_list);
                        if (child->p_pid == target_pid) {
                            target = child;
                            break;
                        }
                    }
                    if (!target) {
                        PANIC(); // PID non trovato tra i figli
                        __builtin_unreachable();
                    }
                }
            
                /* --- Esecuzione terminazione --- */
                terminateProcess(target);
                RELEASE_LOCK((volatile unsigned int*)&global_lock);
                schedule();
                __builtin_unreachable();
            }
            
            

            /* --- NSYS3: PASSEREN (-3) --- */
            case PASSEREN: {
                int *semaddr = (int *)currentState->gpr[25]; // a1
                int old_val = *semaddr;
                *semaddr = old_val - 1;

                if (old_val <= 0) {
                    proc->p_s = *currentState;
                    proc->p_semAdd = semaddr;
                    insertBlocked(semaddr, proc);
                    current_process[cpu_id] = NULL;
                    waiting_count++;

                    RELEASE_LOCK((volatile unsigned int*)&global_lock);
                    schedule();
                    __builtin_unreachable();
                }
                break;
            }

            /* --- NSYS4: VERHOGEN (-4) --- */
            case VERHOGEN: {
                int *semaddr = (int *)currentState->gpr[25]; // a1
                *semaddr += 1;

                if (*semaddr <= 0) {
                    pcb_PTR unblocked = removeBlocked(semaddr);
                    if (unblocked) {
                        insertProcQ(&ready_queue, unblocked);
                    }
                }
                break;
            }

            /* --- NSYS5: DOIO (-5) --- */
            case DOIO: {
                int *cmd_addr = (int *)currentState->gpr[25]; // a1
                int command = currentState->gpr[26]; // a2
                int *dev_addr = (int *)currentState->gpr[27]; // a3

                proc->p_s = *currentState;
                proc->p_semAdd = dev_addr;
                insertBlocked(dev_addr, proc);
                current_process[cpu_id] = NULL;
                waiting_count++;

                *cmd_addr = command; // Scrive sul device DOPO il blocco
                
                RELEASE_LOCK((volatile unsigned int*)&global_lock);
                schedule();
                __builtin_unreachable();
            }

            /* --- NSYS6: GETTIME (-6) --- */
            case GETTIME: {
                cpu_t now, start = proc->p_s.gpr[5]; // t0 = start time
                STCK(now);
                currentState->gpr[2] = proc->p_time + (now - start);
                break;
            }

            /* --- NSYS7: CLOCKWAIT (-7) --- */
            case CLOCKWAIT: {
                proc->p_s = *currentState;
                proc->p_semAdd = &dev_semaph[NRSEMAPHORES-1]; // Semaforo pseudo-clock
                insertBlocked(proc->p_semAdd, proc);
                current_process[cpu_id] = NULL;
                waiting_count++;

                RELEASE_LOCK((volatile unsigned int*)&global_lock);
                schedule();
                __builtin_unreachable();
            }

            /* --- NSYS8: GETSUPPORTPTR (-8) --- */
            case GETSUPPORTPTR: {
                currentState->gpr[2] = (unsigned int)proc->p_supportStruct;
                break;
            }

            /* --- NSYS9: GETPROCESSID (-9) --- */
            case GETPROCESSID: {
                int get_parent = (int)currentState->gpr[25]; // a1
                currentState->gpr[2] = get_parent ? proc->p_parent->p_pid : proc->p_pid;
                break;
            }

            default:
                RELEASE_LOCK((volatile unsigned int*)&global_lock);
                programTrapHandler(cpu_id);
                return;
        }

        RELEASE_LOCK((volatile unsigned int*)&global_lock);
        LDST(currentState);
        return;
    }

    /* === 4. Syscall standard (positive) === */
    passUpOrDie(GENERALEXCEPT, cpu_id);
}






/**
 * Gestore dei Program Trap (eccezioni software/privilegi)
 * 
 * Cosa gestisce:
 * - Istruzioni illegali
 * - Accessi invalidi alla memoria
 * - Syscall privilegiate eseguite in user-mode
 * 
 * Comportamento:
 * 1. Termina il processo colpevole e tutta la sua discendenza
 * 2. Richiama lo scheduler
 * 
 * Note:
 * - Deve essere chiamato con current_process[cpu_id] non NULL
 * - Non ritorna al chiamante (transizione a schedule())
 */
static void programTrapHandler() {
    int cpu_id = getPRID();
    
    /* 1. Controllo di sicurezza (non dovrebbe mai fallire) */
    if (current_process[cpu_id] == NULL) {
        PANIC();  // Caso anomalo: trap senza processo corrente
    }

    /* 3. Terminazione del processo (e di tutta la sua discendenza) */
    ACQUIRE_LOCK((volatile unsigned int*)&global_lock);  // Protegge ready_queue, process_count, etc.
    terminateProcess(current_process[cpu_id]);
    RELEASE_LOCK((volatile unsigned int*)&global_lock);

    /* 4. Passa il controllo allo scheduler */
    schedule();
       /* 5. Punto irraggiungibile (schedule() non ritorna) 
       __builtin_unreachable(); */ //TODO carcola che dipsìk propone chist ma manco l'implementa, non so se ci sia una cosa analoga, forse LDST????
}


/**
 * Termina un processo e tutta la sua discendenza (albero di processi)
 * //NOTE - porcoddueee fa 
 * @param proc Processo da terminare (non NULL)
 * @pre: global_lock deve essere già acquisito dal chiamante
 * @post: Tutti i PCB dell'albero sono deallocati
 * 
 * Comportamento:
 * 1. Termina ricorsivamente tutti i figli
 * 2. Rimuove il processo dalle code/strutture del kernel
 * 3. Aggiorna i contatori globali (process_count, waiting_count)
 * 4. Rilascia le risorse (PCB, semafori, etc.)
 * 
 * Note:
 * - Deve essere chiamato con il global_lock acquisito
 * - Gestisce sia processi running che bloccati/ready
 */
// void terminateProcess(pcb_PTR proc) {
//     const int cpu_id = getPRID();
    
//     /* === 1. Verifiche di sicurezza === */
//     if (proc == NULL || proc->p_list.next == NULL) {
//         PANIC(); // PCB corrotto o già deallocato
//     }

//     /* === 2. Terminazione ricorsiva figli === */
//     while (!list_empty(&proc->p_child)) {
//         pcb_PTR figlio = container_of(proc->p_child.next, pcb_t, p_list);
//         terminateProcess(figlio); // Lock già acquisito
//     }

//     /* === 3. Rimozione da strutture kernel (atomica) === */
//     if (proc->p_semAdd != NULL) {
//         /* --- Caso bloccato su semaforo --- */
//         pcb_PTR rimosso = outBlocked(proc);
//         if (rimosso) {
//             // Patch: Macro per identificare tipo semaforo
//             #define IS_DEV_SEM(sem)
//                 ((sem) >= &dev_semaph[0] && (sem) <= &dev_semaph[NRSEMAPHORES-1])
            
//             if (IS_DEV_SEM(proc->p_semAdd)) {
//                 waiting_count--; // Semaforo dispositivo
//             } else {
//                 (*(proc->p_semAdd))++; // Operazione V atomica
//             }
//         }
//     } else if (!list_empty(&proc->p_list)) {
//         /* --- Caso ready --- */
//         outProcQ(&ready_queue, proc);
//     }

//     /* === 4. Pulizia stato processo === */
//     if (proc == current_process[cpu_id]) {
//         current_process[cpu_id] = NULL;
//         // Patch: Aggiorna tempo CPU prima della rimozione
//         updateProcessTime(cpu_id);
//     }

//     /* === 5. Aggiornamento contatori globali === */
//     process_count--; // Atomico (lock già acquisito)

//     /* === 6. Deallocazione sicura === */
//     memset(proc, 0, sizeof(pcb_t)); // Azzera memoria per sicurezza
//     freePcb(proc);
// }


/**
 * Main exception dispatcher - Corrected version with non-overlapping cases
 */
void exceptionHandler() { //TODO - ma ci sta dichiarare così la roba? Che cazzo ho scritto?
    int cpu_id = getPRID();
        switch((getCAUSE() & GETEXECCODE) >> CAUSESHIFT) {
            case IOINTERRUPTS:
                // External Device Interrupt
                interruptHandler();
                break;
            case 1 ... 3:
                // TLB Exception
                // uTLB_RefillHandler();
                passUpOrDie(PGFAULTEXCEPT, cpu_id);
                break;
            case 4 ... 7:
                // Program Traps p1: Address and Bus Error Exception
                passUpOrDie(GENERALEXCEPT, cpu_id);
                break;
            case SYSEXCEPTION: 
                // Syscalls
                syscallHandler();
                break;
            case 9 ... 12:
                // Breakpoint Calls, Program Traps p2
                passUpOrDie(GENERALEXCEPT, cpu_id);
                break;
            default: 
                // Wrong ExcCode
                passUpOrDie(GENERALEXCEPT, cpu_id);
                break;
        }
}
