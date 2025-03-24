
#include "../phase2/headers/initial.h"

//NOTE  Since address translation is not implemented until the Support Level, uTLB_RefillHandler is a place holder function whose code is provided
// Un evento di TLB-Refill si verifica quando l'hardware non trova una corrispondenza nella Translation Lookaside Buffer (TLB) durante la traduzione di un indirizzo virtuale
extern void uTLB_RefillHandler();
//NOTE - handler generale per tutte le eccezioni (e interruzioni) diverse dagli eventi di TLB-Refill
extern void exceptionHandler();
//NOTE - funzione punto di partenza per il primo processo creato durante inizializzazione del sistema
extern void test();

//entry point del sistema operativo
void main(){
    //nucleus initialization
    initialize();

    //Load interval timer 100ms
    LDIT(PSECOND);

    //istantiate a first process
    //NOTE - non so cosa facciano metà delle seguenti righe - ho adattato da p2test.c e altro
    first_process_pcb = allocPcb();
    RAMTOP(first_process_pcb->p_s.reg_sp);
    first_process_pcb->p_s.pc_epc = (memaddr)test; //sono abbastanza sicuro non ci vada "first_process_pcb" ma whatever
    first_process_pcb->p_s.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M;
    first_process_pcb->p_s.mie = MIE_ALL;
    process_count++;

    schedule();
    


}

static void initialize(){
    // Pass Up Vector for Processor 0
  passupvector_t *passUpVec = (passupvector_t *) PASSUPVECTOR;
  passUpVec->tlb_refill_handler = (memaddr) uTLB_RefillHandler;
  passUpVec->tlb_refill_stackPtr = (memaddr) KERNELSTACK;
  passUpVec->exception_handler = (memaddr) exceptionHandler;
  passUpVec->exception_stackPtr = (memaddr) KERNELSTACK;

  // level 2 structures
  initPcbs();
  initASL();

  // initialize variables
  process_count = 0;
  waiting_count = 0;
  current_process = NULL;
  mkEmptyProcQ(&ready_queue);
  for (int i = 0; i < MAXDEV; i++) {
    // external
    mkEmptyProcQ(&external_blocked_list[0][i]);
    mkEmptyProcQ(&external_blocked_list[1][i]);
    mkEmptyProcQ(&external_blocked_list[2][i]);
    mkEmptyProcQ(&external_blocked_list[3][i]);
    // terminal
    mkEmptyProcQ(&terminal_blocked_list[0][i]);
    mkEmptyProcQ(&terminal_blocked_list[1][i]);
  }
  mkEmptyProcQ(&pseudoclock_blocked_list);
  currentState = (state_t *)BIOSDATAPAGE;
  stateCauseReg = &currentState->cause;
}