#include "./headers/asl.h"

static semd_t semd_table[MAXPROC];
static struct list_head semdFree_h = LIST_HEAD_INIT(semdFree_h);
static struct list_head semd_h = LIST_HEAD_INIT(semd_h);

/*
    FUNZIONE CHIAMATA SOLO AD INIZIO PROGRAMMA

    Inizializza la lista semdFree per fargli contenere [MAXPROC] semafori vuoti
*/
void initASL() {
    for (int i = 0; i < MAXPROC; i++) {
        list_add(&semd_table[i].s_link, &semdFree_h);
    }
} 

/*
    Inserisce il processo p in fondo alla coda dei processi del semaforo con s_key = semAdd.
    Se il semaforo interessato non fosse attivo, inizializza il semaforo e aggiunge il processo in coda.

    semAdd = la key del semaforo interessato
    p = il puntatore al processo da aggiungere in coda al semaforo desiderato

    return: 0 se tutto è andato bene, 1 se il semaforo non era attivato e non ci sono semafori liberi da inizializzare
*/
int insertBlocked(int* semAdd, pcb_t* p) {
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
        tmp = container_of(semdFree_h.next, semd_t, s_link);
        list_del(&tmp->s_link);
        list_add_tail(&tmp->s_link, &semd_h);
        tmp->s_key = semAdd;
        p->p_semAdd = semAdd;
        INIT_LIST_HEAD(&tmp->s_procq);
        list_add(&p->p_list, &tmp->s_procq);
        return 0;
    }
}

/*
    Rimuove il primo processo in coda nel semaforo il cui s_key = semAdd.
    Se non dovessero esserci processi in coda, disattiva il semaforo e lo aggiunge ai semafori liberi.

    semAdd = la s_key del semaforo interessato

    return: puntatore al processo rimosso, oppure NULL se non esiste il semaforo / non erano presenti processi in coda al semaforo
*/
pcb_t* removeBlocked(int* semAdd) {
    semd_t* tmp;
    list_for_each_entry(tmp, &semd_h, s_link) {
        if (tmp->s_key == semAdd) {
            pcb_t* p = container_of(tmp->s_procq.next, pcb_t, p_list);
            list_del(&tmp->s_procq);
            if (list_empty(&tmp->s_procq)) {
                list_del(&tmp->s_link);
                list_add(tmp->s_link.prev->next, &semdFree_h);
            }
            return p;
        }
    }
    return NULL;
}

/*
    Rimuove e restituisce il processo con p_pid = pid dalla lista del semaforo nel quale è contenuto

    pid = int uguale al p_pid del processo interessato

    return = puntatore al processo rimosso dalla coda del semaforo, NULL se non era in coda a nessun semaforo
*/
pcb_t* outBlockedPid(int pid) {
    semd_t* sem_tmp;
    list_for_each_entry(sem_tmp, &semd_h, s_link){
        pcb_t* pcb_tmp;

        list_for_each_entry(pcb_tmp, &sem_tmp->s_procq, p_list) {
            if(pcb_tmp->p_pid == pid){
                list_del(&pcb_tmp->p_list);
                return pcb_tmp;
            }
        }
    }
    return NULL;
 }

/*
    Rimuove il processo p dalla coda dei processi del semaforo da lui puntato (p_semAdd)

    p = il puntatore al processo da "liberare"

    return = p, altrimenti NULL se non esistesse il semaforo o il processo non fosse in coda al semaforo
*/
pcb_t* outBlocked(pcb_t* p) {
    int* key = p->p_semAdd;
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
                        if (list_empty(&sem_tmp->s_procq)) {
                            /* rimuovi semd da ASL e inseriscilo su semdFree_h */
                            list_del(&sem_tmp->s_link);
                            list_add(&sem_tmp->s_link, &semdFree_h);
                        }
                        return p;
                    }
                }
                return NULL;
            }
        }
    }
    return NULL;
}

/*
    Funzione che torna il puntatore al primo processo nella coda del semaforo la cui s_key = semAdd

    semAdd = la s_key del semaforo interessato

    return = un puntatore al primo processo, oppure NULL se non esiste nessun semaforo con quella s_key o non ci siano processi in coda a quel semaforo
*/
pcb_t* headBlocked(int* semAdd) {
    semd_t* sem_tmp;
    list_for_each_entry(sem_tmp, &semd_h, s_link) {
        if(sem_tmp->s_key == semAdd){
            if (list_empty(&sem_tmp->s_procq)) {
                return NULL;
            } else {
                return container_of(sem_tmp->s_procq.next, pcb_t, p_list);
            }
        }
    }
    return NULL;
}
