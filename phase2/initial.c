
#include <uriscv/liburiscv.h>
#include <string.h>
#include "../headers/const.h"
#include "../headers/types.h"
#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"

//NOTE  Since address translation is not implemented until the Support Level, uTLB_RefillHandler is a place holder function whose code is provided
// Un evento di TLB-Refill si verifica quando l'hardware non trova una corrispondenza nella Translation Lookaside Buffer (TLB) durante la traduzione di un indirizzo virtuale
extern void uTLB_RefillHandler();
//NOTE - handler generale per tutte le eccezioni (e interruzioni) diverse dagli eventi di TLB-Refill
extern void exceptionHandler();
//NOTE - funzione punto di partenza per il primo processo creato durante inizializzazione del sistema
extern void test(); //pare che questa cosa dell'extern serva per rendere la funzione visibile al linker
extern void schedule(); //TODO - uguale a sotto zioponi
static void initialize(); //TODO not sure sia dichiarata così
int dev_semaph[NRSEMAPHORES]; //con questo array gestisco i Device Semaphores
int process_count;  // number of processes started but not yet terminated
int waiting_count; // number of soft-blocked processes 
int global_lock; // Un intero che può assumere solo i valori 0 e 1, utilizzato per la sincronizzazione tra diverse istanze del Nucleo in esecuzione su CPU differenti
pcb_PTR current_process[NCPU]; //array volto a tenere traccia dei processi su ogni CPU
struct list_head ready_queue; //tailptr a queue of PCBs in ready state
passupvector_t* passupvector;
struct list_head pseudoclock_blocked_list;
state_t *currentState; //punta alla struttura che contiene lo stato del processore salvato al momento di un'eccezione o interrupt
unsigned int *stateCauseReg; //puntatore diretto al campo cause all'interno di quella struttura di stato, permettendo un accesso rapido al codice che identifica l'evento


void *memset (void *dest, register int val, register size_t len)
{
  register unsigned char *ptr = (unsigned char*)dest;
  while (len-- > 0)
    *ptr++ = val;
  return dest;
}
//entry point del sistema operativo
void main(){
    //nucleus initialization
    initialize();

    //Load interval timer 100ms
    LDIT(PSECOND);

    //istantiate a first process
    pcb_PTR next_process = allocPcb();
    next_process->p_s.mie = MIE_ALL; //abilito il bit "Machine Interrupt Enable"; MIE_ALL abilita tutti gli interrupt
    next_process->p_s.status |= MSTATUS_MPIE_MASK   /* prev-IE  */
                             | MSTATUS_MPP_M;     /* kernel   */
    RAMTOP(next_process->p_s.reg_sp); //imposto lo stack pointer ad indirizzo RAMPTOP (quindi la cima della memoria RAM disponibile per lo stack del kernel, che crescerà poi verso indirizzi inferiori)
    next_process->p_s.pc_epc = (memaddr)test; //il program counter pc_epc conterrà l'indirizzo della prima istruzione che il processo deve eseguire 
    STCK(next_process->p_s.gpr[5]); //contenere l’istante preciso in cui il processo inizia il time-slice
    next_process->p_time = 0; //azzero contatore tempo CPU usato dal processo
    next_process->p_semAdd = NULL; //il processo NON e' bloccato su alcun semaforo
    next_process->p_supportStruct = NULL; //il processo NON ha una struttura di supporto associata (serve solo a proc. di supporto avanzati)
    //inserisco il primo processo nella coda processi disponibili, aumento il contatore dei processi;
    insertProcQ(&ready_queue, next_process);   
    process_count++;
    
    schedule(); //chiamo lo scheduler
    
}


static void initialize() {
    // Pass Up Vector: Tabella di riferimento per il BIOS. Quando il processore rileva un'eccezione 
    // (esclusi alcuni casi gestiti direttamente dal BIOS) o un evento di TLB-Refill, 
    // il BIOS consulta il Pass Up Vector della CPU corrente per determinare 
    // a quale funzione del Nucleo passare il controllo e quale stack utilizzare 
    // per l'esecuzione di quel gestore 
    //TODO - check this shit out che non sono sicuro
  // 1. Correggiamo l'inizializzazione dei Pass Up Vector per CPU multiple
  passupvector = (passupvector_t *) PASSUPVECTOR;
  
  // CPU 0 (già corretto)
  passupvector->tlb_refill_handler = (memaddr) uTLB_RefillHandler;
  passupvector->exception_handler = (memaddr) exceptionHandler;
  passupvector->tlb_refill_stackPtr = (memaddr) KERNELSTACK;
  passupvector->exception_stackPtr = (memaddr) KERNELSTACK;

  // Per CPU 1-7 dobbiamo usare l'offset corretto nel Pass Up Vector
  for(int cpu_id = 1; cpu_id < NCPU; cpu_id++) {
      passupvector_t* cpu_passup = (passupvector_t*)(PASSUPVECTOR + (cpu_id * sizeof(passupvector_t)));
      
      cpu_passup->tlb_refill_handler = (memaddr) uTLB_RefillHandler;
      cpu_passup->exception_handler = (memaddr) exceptionHandler;
      cpu_passup->tlb_refill_stackPtr = RAMSTART + (64 * PAGESIZE) + (cpu_id * PAGESIZE);
      cpu_passup->exception_stackPtr = 0x20020000 + (cpu_id * PAGESIZE);
  }

  // 2. Semplifichiamo l'inizializzazione dell'IRT
  for (int i = 0; i < IRT_NUM_ENTRY; i++) {
      int cpu_id = i / 6;  // 6 entry per CPU
      unsigned int dest_mask = 1 << cpu_id;
      volatile unsigned int* irt_entry = (unsigned int*)(IRT_START + i * 4);
      *irt_entry = IRT_RP_BIT_ON | dest_mask;
  }

  // 3. Inizializzazione strutture dati Level 2
  initPcbs();
  initASL();

  // 4. Inizializzazione variabili globali
  process_count = 0;
  waiting_count = 0;
  global_lock = 0;
  mkEmptyProcQ(&ready_queue);
  mkEmptyProcQ(&pseudoclock_blocked_list); // Aggiungiamo questa

  // 5. Setup stato corrente e registri
  currentState = GET_EXCEPTION_STATE_PTR(0); // Usiamo la macro invece dell'indirizzo diretto
  stateCauseReg = &currentState->cause;

  // 6. Inizializzazione array processi e semafori
  memset(current_process, 0, sizeof(pcb_PTR) * NCPU);
  memset(dev_semaph, 0, sizeof(int) * NRSEMAPHORES);
}