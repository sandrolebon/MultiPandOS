
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
extern void klog_print(char *str);
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
    /* --- 2. Inizializzazione Variabili Globali --- */
    currentState = (state_t *)BIOSDATAPAGE; // Puntatore allo stato salvato dal BIOS
    // stateCauseReg = &currentState->cause; // Inizializzazione non strettamente necessaria qui
    
    /* --- 3. Inizializzazione Pass-Up Vector per TUTTE le CPU --- */
    // Imposta gli Stack Pointer DEDICATI per ciascuna CPU
    // CPU 0 usa lo stack kernel principale
    passupvector_t *cpu_puv = (passupvector_t *)(PASSUPVECTOR);
    cpu_puv->tlb_refill_stackPtr = (memaddr)KERNELSTACK;
    cpu_puv->exception_stackPtr = (memaddr)KERNELSTACK; 
    // Imposta gli handler (puntano alle stesse funzioni per tutte le CPU in Phase 2)
    cpu_puv->tlb_refill_handler = (memaddr)uTLB_RefillHandler; // Handler TLB (placeholder)
    cpu_puv->exception_handler = (memaddr)exceptionHandler;  // Handler eccezioni generale
    
    for(int cpu_id = 1; cpu_id < NCPU; cpu_id++) {
        cpu_puv = (passupvector_t *)(PASSUPVECTOR + cpu_id * sizeof(passupvector_t));
        // Stack per TLB Refill Handler (CPU >= 1)
        // Es: RAMSTART + 64 pagine + offset per CPU
        cpu_puv->tlb_refill_stackPtr = (memaddr)(RAMSTART + (64 * PAGESIZE) + (cpu_id * PAGESIZE));
        
        // Stack per Exception Handler (CPU >= 1)
        // Es: Indirizzo base 0x20020000 + offset per CPU
        cpu_puv->exception_stackPtr = (memaddr)(0x20020000 + (cpu_id * PAGESIZE));
    }
    
    /* --- 1. Inizializzazione Strutture Dati Base --- */
    initPcbs(); // Prepara la lista pcbFree_h
    initASL();  // Prepara la lista semdFree_h
    
    //inizializzo variabili globali
    process_count = 0;
    waiting_count = 0;
    global_lock = 0;
    mkEmptyProcQ(&ready_queue);
    for(int i = 0; i < NCPU; i++) {
        current_process[i] = NULL;
    }
    /*Dispositivi Standard (Disk, Flash, Network, Printer): 4 linee * 8 dispositivi/linea = 32 semafori.
    Terminali (con 2 sub-device, TX/RX): 1 linea * 8 dispositivi/linea * 2 sub-device/dispositivo = 16 semafori.
    Pseudo-clock: 1 semaforo aggiuntivo.*/
    for(int i = 0; i < NRSEMAPHORES; i++) {
        dev_semaph[i] = 0;
    }
    
    /* --- 5. Inizializzazione Interval Timer, carico con 100ms --- */
    LDIT(PSECOND);
    
    /* --- 6. Creazione Processo Iniziale (test) --- */
    pcb_PTR initial_pcb = allocPcb();
    ACQUIRE_LOCK((volatile unsigned int*)&global_lock); // Proteggi accesso alla coda
    (initial_pcb->p_s).status = MIE_ALL | MSTATUS_MPIE_MASK | MSTATUS_MPP_M;   /* processor Local Timer abilitato, Kernel-mode on, Interrupts Abilitati */
    RAMTOP((initial_pcb->p_s).reg_sp);     /* Inizializzazione stack pointer */
    (initial_pcb->p_s).pc_epc = (memaddr) test; 
    (initial_pcb->p_s).gpr[24] = (memaddr) test; 
    /* Nuovo processo "iniziato" */
    insertProcQ(&(ready_queue), initial_pcb);
    process_count++;
    RELEASE_LOCK((volatile unsigned int*)&global_lock);
    
    /* --- 4. Configurazione IRT (Interrupt Routing Table) --- */
    // Assicurati che IRT_START sia un indirizzo valido e scrivibile //FIXME - dio stramerda doppio for? Vedi sium
    for (int i = 0; i < IRT_NUM_ENTRY; i++) {
        int cpu_id_target = i / (IRT_NUM_ENTRY / NCPU); // Assegna 6 entry per CPU
        unsigned int dest_mask = 1 << cpu_id_target;
        // Scrittura nel registro IRT
        *((volatile unsigned int*)(IRT_START + i*4)) = IRT_RP_BIT_ON | dest_mask;
    }
    
    //FIXME - INITCPU e cazzi cpu state
    /**--- 7. Inizializzazione Altri Processori (se NCPU > 1) --- */
    for (int cpu_id = 1; cpu_id < NCPU; cpu_id++) {
            state_t cpu_state;
            memset(&cpu_state, 0, sizeof(state_t)); // Azzera stato CPU
            cpu_state.status = MSTATUS_MPP_M; // Kernel mode
            cpu_state.pc_epc = (memaddr)schedule; // Punto di ingresso: lo scheduler
            // ATTENZIONE: Verifica che questo indirizzo stack sia valido e non si sovrapponga
            cpu_state.reg_sp = 0x20020000 + (cpu_id * PAGESIZE);
            INITCPU(cpu_id, &cpu_state);    
        }
        /* --- 8. Avvio Scheduler sulla CPU 0 --- */
        schedule(); // Non dovrebbe mai ritornare

    // Se arriva qui, qualcosa è andato storto nello scheduler
    PANIC();
}