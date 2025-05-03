
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
void main() {
    // 1. Inizializzazione strutture dati
    initPcbs();
    initASL();

    // 2. Inizializzazione variabili globali
    process_count = 0;
    waiting_count = 0;
    global_lock = 0;
    mkEmptyProcQ(&ready_queue);
    currentState = (state_t *)BIOSDATAPAGE;
    stateCauseReg = &currentState->cause;
    // 3. Inizializzazione Pass-Up Vector per tutte le CPU
    for(int cpu_id = 0; cpu_id < NCPU; cpu_id++) {
        passupvector_t *cpu_passup = (passupvector_t *)(PASSUPVECTOR + cpu_id * sizeof(passupvector_t));
        
        cpu_passup->tlb_refill_handler = (memaddr)uTLB_RefillHandler;
        cpu_passup->exception_handler = (memaddr)exceptionHandler;
        
        cpu_passup->tlb_refill_stackPtr = (cpu_id == 0) ? 
            KERNELSTACK : 
            RAMSTART + (64 * PAGESIZE) + (cpu_id * PAGESIZE);
            
        cpu_passup->exception_stackPtr = (cpu_id == 0) ? 
            KERNELSTACK : 
            0x20020000 + (cpu_id * PAGESIZE);
    }

    // 4. Configurazione IRT (Interrupt Routing Table)
    for (int i = 0; i < IRT_NUM_ENTRY; i++) {
        int cpu_id = i / (IRT_NUM_ENTRY / NCPU); // 6 entry per CPU
        unsigned int dest = 1 << cpu_id;
        *((unsigned int*)(IRT_START + i*4)) = IRT_RP_BIT_ON | dest;
    }

    // 5. Inizializzazione timer di sistema
    LDIT(PSECOND * (*(cpu_t *)TIMESCALEADDR));

    // 6. Creazione processo iniziale (test)
    pcb_PTR initial_pcb = allocPcb();
    if (!initial_pcb) PANIC(); // Fallimento allocazione
    
    // Inizializzazione stato del processo
    initial_pcb->p_s.mie = MIE_ALL;
    initial_pcb->p_s.status = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;
    RAMTOP(initial_pcb->p_s.reg_sp);
    initial_pcb->p_s.pc_epc = (memaddr)test;
    
    // Inizializzazione altri campi PCB
    initial_pcb->p_time = 0;
    initial_pcb->p_semAdd = NULL;
    initial_pcb->p_supportStruct = NULL;
    
    // Inserimento in ready queue
    insertProcQ(&ready_queue, initial_pcb);
    process_count++;

    // 7. Inizializzazione altri processori
    for (int cpu_id = 1; cpu_id < NCPU; cpu_id++) {
        state_t cpu_state;
        memset(&cpu_state, 0, sizeof(state_t));
        
        cpu_state.status = MSTATUS_MPP_M;
        cpu_state.pc_epc = (memaddr)schedule;
        cpu_state.reg_sp = 0x20020000 + (cpu_id * PAGESIZE);
        
        INITCPU(cpu_id, &cpu_state);
    }

    // 8. Avvio scheduler
    schedule();
}
