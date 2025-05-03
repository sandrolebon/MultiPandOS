#include <uriscv/liburiscv.h>
#include <string.h>
#include "../phase1/headers/pcb.h"
#include "../headers/const.h"

extern int process_count;
extern int waiting_count;
extern int global_lock;
extern pcb_PTR current_process[NCPU];
extern struct list_head ready_queue;

/**
 * Scheduler - Seleziona il prossimo processo da eseguire
 * 
 * Comportamento:
 * 1. Prende un processo dalla ready_queue (se disponibile)
 * 2. Imposta timer e priorità
 * 3. Se la ready_queue è vuota, attende interrupt o gestisce deadlock
 * 
 * Note:
 * - Usa gpr[5] (t0) per memorizzare l'istante di inizio timeslice
 * - Gestisce correttamente TPR (Task Priority Register)
 */
void schedule() {
    /* === 1. Acquisizione lock e selezione processo === */
    ACQUIRE_LOCK((volatile unsigned int*)&global_lock); //NOTE -  simpatico casting indecifrabile che però mi toglie warning
    const int cpu_id = getPRID();
    pcb_PTR next_process = removeProcQ(&ready_queue);
    RELEASE_LOCK((volatile unsigned int*)&global_lock);  // Rilascio immediato (ottimizzazione)

    /* === 2. Caso: processo disponibile === */
    if (next_process != NULL) {
        current_process[cpu_id] = next_process;

        /* --- Configurazione timer e priorità --- */
        // Patch: Moltiplica per TIMESCALEADDR per scaling corretto
        setTIMER(TIMESLICE * (*(cpu_t *)TIMESCALEADDR));
        
        // Patch: Imposta priorità alta (0) per processi non-idle
        *((volatile unsigned int *)TPR) = 0;

        /* --- Inizializzazione tempi --- */
        cpu_t now;
        STCK(now);
        next_process->p_s.gpr[5] = now;  // t0 = inizio timeslice

        /* --- Context switch --- */
        LDST(&next_process->p_s);
        __builtin_unreachable();  //NOTE - secondo me è una tamarrata inutile, chiederemo a colleghi /* __builtin_unreachable() è un'intrinsic function fornita da GCC/Clang (supportata anche in toolchain RISC-V) che:

        //Comunica al compilatore che un certo punto del codice non sarà mai eseguito
        //È definita in <uriscv/liburiscv.h> (usata implicitamente nel progetto)*/

    /* === 3. Caso: nessun processo (sistema idle) === */
    } else if (process_count == 0) {
        HALT();  // Spegne il sistema

    /* === 4. Caso: deadlock o processi bloccati === */
    } else {
        if (waiting_count > 0) {
            /* --- Modalità idle (attesa interrupt) --- */
            // Patch: Disabilita solo timer interrupt
            setMIE(MIE_ALL & ~MIE_MTIE_MASK);
            
            // Patch: Priorità bassa (1) per CPU idle
            *((volatile unsigned int *)TPR) = 1;
            
            // Abilita interrupt globali
            unsigned int status = getSTATUS();
            status |= MSTATUS_MIE_MASK;
            setSTATUS(status);
            
            WAIT();  // Aspetta interrupt
        } else {
            PANIC();  // Deadlock irreversibile
        }
    }
}

