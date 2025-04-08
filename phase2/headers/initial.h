/* This module implements main() and exports the Nucleus’s global variables */
//NOTE - vale in ogni file xd -i commenti sono adattati da ivan e notebookLM quindi andranno riscritti (sogno il momento in cui sara' l'ultima cosa da fare)
#ifndef INITIAL_H
#define INITIAL_H

#include <uriscv/liburiscv.h>
#include "../headers/const.h"
#include "../headers/types.h"
#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "../phase2/headers/scheduler.h"

// number of started processes not yet terminated
int process_count;
// number of soft-blocked processes 
int waiting_count;
// Un intero che può assumere solo i valori 0 e 1, utilizzato per la sincronizzazione tra diverse istanze del Nucleo in esecuzione su CPU differenti
int global_lock;
int dev_semaph[NRSEMAPHORES]; //con questo array gestisco i Device Semaphores
// running process
pcb_PTR current_process[NCPU]; //array volto a tenere traccia dei processi su ogni CPU
pcb_PTR process_zero_pcb; //Il PRIMO processo istanziato; quite sure che sia dichiarazione analoga a pcb_t* ma non sono (ancora) il capo della sintassi C 
pcb_PTR next_process; //che vada al posto di process_zero_pcb?
// queue of PCBs in ready state
struct list_head ready_queue;
passupvector_t *first_processor;
passupvector_t *second_processor;
passupvector_t *third_processor;
passupvector_t *fourth_processor;
passupvector_t *fifth_processor;
passupvector_t *sixth_processor;
passupvector_t *seventh_processor;
passupvector_t *eighth_processor;

static void initialize();

#endif