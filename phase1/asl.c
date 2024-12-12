#include "./headers/asl.h"

static semd_t semd_table[MAXPROC];
static struct list_head semdFree_h;
static struct list_head semd_h;

void initASL() {
    /*Initialize the semdFree list to contain all the elements of the array static semd_t semdTable[MAXPROC].
This method will be only called once during data structure initialization.*/
    for (int i = 0; i < MAXPROC; i++) {
        list_add(&semd_table[i], &semdFree_h);
    }
} 

int insertBlocked(int* semAdd, pcb_t* p) {
    /*Insert the PCB pointed to by p at the tail of the process queue associated with the semaphore
whose key is semAdd and set the semaphore address of p to semaphore with semAdd. If the
semaphore is currently not active (i.e. there is no descriptor for it in the ASL), allocate a new
descriptor from the semdFree list, insert it in the ASL (at the appropriate position), initialize
all of the fields (i.e. set s_key to semAdd, and s_procq to mkEmptyProcQ()), and proceed as
above. If a new semaphore descriptor needs to be allocated and the semdFree list is empty,
return TRUE. In all other cases return FALSE.*/
    semd_t* tmp;
    list_for_each_entry(tmp, &semd_h, s_link) {
        if (tmp->s_key == semAdd) {
            list_add_tail(&p->p_list, &tmp->s_procq);
            p->p_semAdd = semAdd;
            return 0;
        }
    }
    if (list_empty(&semdFree_h)) {
        return 1;
    } else {
        tmp = semdFree_h.next;
        list_del(&tmp->s_link);
        list_add_tail(&tmp->s_link, &semd_h);
        tmp->s_key = semAdd;
        p->p_semAdd = semAdd;
        INIT_LIST_HEAD(&tmp->s_procq);
        list_add(&p->p_list, &tmp->s_procq);
        return 0;
    }
}

pcb_t* removeBlocked(int* semAdd) {
    /*Search the ASL for a descriptor of this semaphore. If none is found, return NULL; other-
wise, remove the first (i.e. head) PCB from the process queue of the found semaphore de-
scriptor and return a pointer to it. If the process queue for this semaphore becomes empty
(emptyProcQ(s_procq) is TRUE), remove the semaphore descriptor from the ASL and return
it to the semdFree list.*/

    semd_t* tmp;
    list_for_each_entry(tmp, &semd_h, s_link) {
        if (tmp->s_key == semAdd) {
            if (list_empty(&tmp->s_procq)) {
                list_del(&tmp->s_link);
                list_add(tmp, &semdFree_h);
            } else {
                pcb_t* p = &tmp->s_procq.next;
                list_del(&tmp->s_procq);
                return p;
            }
        }
    }
    return NULL;
}

pcb_t* outBlockedPid(int pid) {
    /* //NOTE - non richiesta per ph1, utile in ph2
    rimuove il PCB con quel PID dal semaforo su cui è bloccato (se non è bloccato su un semaforo restituisce NULL).*/
}

pcb_t* outBlocked(pcb_t* p) {
    /*Remove the PCB pointed to by p from the process queue associated with p’s semaphore (p->p_semAdd)
on the ASL. If PCB pointed to by p does not appear in the process queue associated with p’s
semaphore, which is an error condition, return NULL; otherwise, return p.*/
    if (list_empty(&semd_h)) {
        return NULL;
    } else {
        int key = p->p_semAdd;
        semd_t* sem_tmp;
        list_for_each_entry(sem_tmp, &semd_h, s_link) {
            if(sem_tmp->s_key == key){
                if (list_empty(&sem_tmp->s_procq)) {
                    return NULL;
                } else {
                    pcb_t* pcb_tmp;
                    
                    list_for_each_entry(pcb_tmp, &sem_tmp->s_procq, p_list) {
                        if(pcb_tmp == p){
                            list_del(&pcb_tmp->p_list);
                            return p;
                        }
                    }
                    return NULL;
                }
            }
        }
    }
    return NULL;
}

pcb_t* headBlocked(int* semAdd) {
    /*Return a pointer to the PCB that is at the head of the process queue associated with the
semaphore semAdd. Return NULL if semAdd is not found on the ASL or if the process queue
associated with semAdd is empty.*/
    if (list_empty(&semd_h)) {
        return NULL;
    } else {
        semd_t* sem_tmp;
        list_for_each_entry(sem_tmp, &semd_h, s_link) {
            if(sem_tmp->s_key == semAdd){
                return &semd_h.next;
            }
        }
        return NULL;
    }
}
