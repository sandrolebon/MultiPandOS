
#include "../phase2/headers/initial.h"

//NOTE  Since address translation is not implemented until the Support Level, uTLB_RefillHandler is a place holder function whose code is provided
// Un evento di TLB-Refill si verifica quando l'hardware non trova una corrispondenza nella Translation Lookaside Buffer (TLB) durante la traduzione di un indirizzo virtuale
extern void uTLB_RefillHandler();
//NOTE - handler generale per tutte le eccezioni (e interruzioni) diverse dagli eventi di TLB-Refill
extern void exceptionHandler();
//NOTE - funzione punto di partenza per il primo processo creato durante inizializzazione del sistema
extern void test(); //pare che questa cosa dell'extern serva per rendere la funzione visibile al linker

//entry point del sistema operativo
void main(){
    //nucleus initialization
    initialize();

    //Load interval timer 100ms
    LDIT(PSECOND);

    //istantiate a first process
    //NOTE - "p_s" è lo stato del processore e 
    first_process_pcb = allocPcb();
    first_process_pcb->p_s.mie = MIE_ALL; //abilito il bit "Machine Interrupt Enable"; MIE_ALL abilita tutti gli interrupt
    first_process_pcb->p_s.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M; //abilito tutti interrupt assegnando un valore che abbia il bit corrispondente a MIE impostato a 1, MSTATUS_MIE_MASK, incluso tramite un'operazione OR bit a bit; MPP - Machine Previous Privilege: indica la modalità in cui il processore stava operando prima di entrare in un'eccezione; all'inizio lo impostiamo in modalità machine (kernel), quindi primo processo avrà privilegi massimi
    RAMTOP(first_process_pcb->p_s.reg_sp); //imposto lo stack pointer ad indirizzo RAMPTOP (quindi la cima della memoria RAM disponibile per lo stack del kernel, che crescerà poi verso indirizzi inferiori)
    first_process_pcb->p_s.pc_epc = (memaddr)test; //il program counter pc_epc conterrà l'indirizzo della prima istruzione che il processo deve eseguire 
    first_process_pcb->p_s. = (memaddr)test;
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
  global_lock = 0;
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