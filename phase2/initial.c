
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
int debug = 0;
void *memset (void *dest, register int val, register size_t len)
{
    register unsigned char *ptr = (unsigned char*)dest;
    while (len-- > 0)
    *ptr++ = val;
    return dest;
}
//entry point del sistema operativo
void main() {
    /* --- 1. Inizializzazione Strutture Dati Base --- */
    initPcbs(); // Prepara la lista pcbFree_h
    initASL();  // Prepara la lista semdFree_h

    /* --- 2. Inizializzazione Variabili Globali --- */
    process_count = 0;
    waiting_count = 0;
    global_lock = 0; // Assicurati che il tipo sia corretto (unsigned)
    mkEmptyProcQ(&ready_queue);
    currentState = (state_t *)BIOSDATAPAGE; // Puntatore allo stato salvato dal BIOS
    // stateCauseReg = &currentState->cause; // Inizializzazione non strettamente necessaria qui

    /* --- 3. Inizializzazione Pass-Up Vector (MANTENUTO COME RICHIESTO) --- */
    // NOTA: Questo loop sovrascrive lo stesso PassUpVector base (PASSUPVECTOR)
    // per ogni CPU. Se NCPU > 1, questo potrebbe essere un problema.
    // La versione corretta userebbe:
    // passupvector_t *cpu_passup = (passupvector_t *)(PASSUPVECTOR + cpu_id * sizeof(passupvector_t));
    passupvector_t* passupvector = (passupvector_t *) PASSUPVECTOR;
    // Pass Up Vector for CPU 0
    passupvector->tlb_refill_handler = (memaddr) uTLB_RefillHandler;
    passupvector->exception_handler = (memaddr) exceptionHandler;
    passupvector->tlb_refill_stackPtr = (memaddr) KERNELSTACK;
    passupvector->exception_stackPtr = (memaddr) KERNELSTACK;
    // Pass Up Vector for CPU >=1 (Logica attuale)
    for(int cpu_id = 1; cpu_id < NCPU; cpu_id++){ // Parte da 1 per non sovrascrivere CPU0
      // NOTA: Sta ancora scrivendo su passupvector base, non sull'offset corretto
      passupvector->tlb_refill_handler = (memaddr) uTLB_RefillHandler;
      passupvector->exception_handler = (memaddr) exceptionHandler;
      // Gli stack pointer sembrano corretti per le CPU >= 1
      passupvector->tlb_refill_stackPtr = (memaddr) RAMSTART + (64 * PAGESIZE) + (cpu_id * PAGESIZE);
      passupvector->exception_stackPtr = (memaddr) 0x20020000 + (cpu_id * PAGESIZE);
    }

    
    
    /* --- 4. Configurazione IRT (Interrupt Routing Table) --- */
    // Assicurati che IRT_START sia un indirizzo valido e scrivibile
    for (int i = 0; i < IRT_NUM_ENTRY; i++) {
        int cpu_id_target = i / (IRT_NUM_ENTRY / NCPU); // Assegna 6 entry per CPU
        unsigned int dest_mask = 1 << cpu_id_target;
        // Scrittura nel registro IRT
        *((volatile unsigned int*)(IRT_START + i*4)) = IRT_RP_BIT_ON | dest_mask;
    }
    
    /* --- 5. Inizializzazione Interval Timer --- */
    // ATTENZIONE: Verifica che TIMESCALEADDR sia valido prima di dereferenziare
    // Se l'hardware non è pronto, questo può causare un crash.
    // Considera un check preliminare se possibile.
    cpu_t timescale = (*(volatile cpu_t *)TIMESCALEADDR);
    if (timescale == 0) PANIC(); // Evita divisione per zero o valore non valido
    LDIT(PSECOND * timescale);
    
    /* --- 6. Creazione Processo Iniziale (test) --- */
    pcb_PTR initial_pcb = allocPcb();
    if (!initial_pcb) {
        PANIC(); // Fallimento critico: non ci sono PCB disponibili
    }
    ACQUIRE_LOCK((volatile unsigned int*)&global_lock); // Proteggi accesso alla coda
    /* Dichiarazione del processo da iniziare e inizializzazione */
    insertProcQ(&(ready_queue), initial_pcb);
    
    /* processor Local Timer abilitato, Kernel-mode on, Interrupts Abilitati */
    (initial_pcb->p_s).status = TEBITON | IEPON | IMON;
    
    /* Inizializzazione sp */
    RAMTOP((initial_pcb->p_s).reg_sp);

    (initial_pcb->p_s).pc_epc = (memaddr) test; 
    (initial_pcb->p_s).gpr[24] = (memaddr) test; 

    /* Nuovo processo "iniziato" */
    process_count++;

    RELEASE_LOCK((volatile unsigned int*)&global_lock);
    
    /**/ //NOTE - Riabilitare per test CPU>1
    /**--- 7. Inizializ/ zazione Altri Processori (se NCPU > 1) --- */
    for (int cpu_id = 1; cpu_id < NCPU; cpu_id++) {
            state_t cpu_state;
            memset(&cpu_state, 0, sizeof(state_t)); // Azzera stato CPU
        
            cpu_state.status = MSTATUS_MPP_M; // Kernel mode
            cpu_state.pc_epc = (memaddr)schedule; // Punto di ingresso: lo scheduler
            // ATTENZIONE: Verifica che questo indirizzo stack sia valido e non si sovrapponga
            cpu_state.reg_sp = 0x20020000 + (cpu_id * PAGESIZE);
        
            // Avvia la CPU aggiuntiva
            // ATTENZIONE: INITCPU potrebbe fallire se lo stato o l'indirizzo sono invalidi
            INITCPU(cpu_id, &cpu_state); 
        }
        
        
        /* --- 8. Avvio Scheduler sulla CPU 0 --- */
        schedule(); // Non dovrebbe mai ritornare

    // Se arriva qui, qualcosa è andato storto nello scheduler
    PANIC();
}