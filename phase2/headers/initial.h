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
// running process
pcb_PTR current_process;
// queue of PCBs in ready state
struct list_head ready_queue;
pcb_PTR first_process_pcb;

#endif