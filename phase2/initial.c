
#include <uriscv/liburiscv.h>
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
extern void schedule(); //TODO - uguale a sotto xd
static void initialize(); //TODO not sure sia dichiarata così
int dev_semaph[NRSEMAPHORES]; //con questo array gestisco i Device Semaphores
int process_count;  // number of processes started but not yet terminated
int waiting_count; // number of soft-blocked processes 
int global_lock; // Un intero che può assumere solo i valori 0 e 1, utilizzato per la sincronizzazione tra diverse istanze del Nucleo in esecuzione su CPU differenti
pcb_PTR current_process[NCPU]; //array volto a tenere traccia dei processi su ogni CPU
struct list_head ready_queue; // queue of PCBs in ready state
passupvector_t* passupvector;

//entry point del sistema operativo
void main(){
    //nucleus initialization
    initialize();

    //Load interval timer 100ms
    LDIT(PSECOND);

    //istantiate a first process
    pcb_PTR next_process = allocPcb();
    next_process->p_s.mie = MIE_ALL; //abilito il bit "Machine Interrupt Enable"; MIE_ALL abilita tutti gli interrupt
    next_process->p_s.status |= MSTATUS_MIE_MASK | MSTATUS_MPP_M; //abilito tutti gli interrupt assegnando un valore che abbia il bit corrispondente a MIE impostato a 1, MSTATUS_MIE_MASK, incluso tramite un'operazione OR bit a bit; MPP - Machine Previous Privilege: indica la modalità in cui il processore stava operando prima di entrare in un'eccezione; all'inizio lo impostiamo in modalità machine (kernel), quindi primo processo avrà privilegi massimi
    RAMTOP(next_process->p_s.reg_sp); //imposto lo stack pointer ad indirizzo RAMPTOP (quindi la cima della memoria RAM disponibile per lo stack del kernel, che crescerà poi verso indirizzi inferiori)
    next_process->p_s.pc_epc = (memaddr)test; //il program counter pc_epc conterrà l'indirizzo della prima istruzione che il processo deve eseguire 
    
    //inserisco il primo processo nella coda processi disponibili, aumento il contatore dei processi;
    insertProcQ(&ready_queue, next_process);   
    process_count++;
    
    schedule(); //chiamo lo scheduler
    
}

static void initialize(){
  // Pass Up Vector: Tabella di riferimento per il BIOS. Quando il processore rileva un'eccezione 
  // (esclusi alcuni casi gestiti direttamente dal BIOS) o un evento di TLB-Refill, 
  // il BIOS consulta il Pass Up Vector della CPU corrente per determinare 
  // a quale funzione del Nucleo passare il controllo e quale stack utilizzare 
  // per l'esecuzione di quel gestore
  passupvector = (passupvector_t *) PASSUPVECTOR;

  // Pass Up Vector for CPU 0
  passupvector->tlb_refill_handler = (memaddr) uTLB_RefillHandler;
  passupvector->exception_handler = (memaddr) exceptionHandler;
  passupvector->tlb_refill_stackPtr = (memaddr) KERNELSTACK;
  passupvector->exception_stackPtr = (memaddr) KERNELSTACK;

  // Pass Up Vector for CPU >=1
  for(int cpu_id = 0; cpu_id < NCPU; cpu_id++){
    passupvector->tlb_refill_handler = (memaddr) uTLB_RefillHandler;
    passupvector->exception_handler = (memaddr) exceptionHandler;
    passupvector->tlb_refill_stackPtr = (memaddr) RAMSTART + (64 * PAGESIZE) + (cpu_id * PAGESIZE);
    passupvector->exception_stackPtr = (memaddr) 0x20020000 + (cpu_id * PAGESIZE);
  }
  
  // level 2 structures
  initPcbs();
  initASL();

  // initialize variables
  process_count = 0; 
  waiting_count = 0; 
  global_lock = 0; 
  mkEmptyProcQ(&ready_queue); //inizializzo la ready_queue

  for(int i=0; i<NCPU; i++){
    current_process[i]=NULL; //setto tutti i processi delle CPU a NULL
  }

  for(int i=0; i<NRSEMAPHORES; i++){
    dev_semaph[i]=0; //setto tutti i semafori dei devices a 0
  }
}