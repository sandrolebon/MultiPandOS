#include "./headers/pcb.h"
//Consigliato iniziare da questo file!

static struct list_head pcbFree_h = LIST_HEAD_INIT(pcbFree_h);
static pcb_t pcbFree_table[MAXPROC];
static int next_pid = 1;

void *memcpy (void *dest, const void *src, size_t len)
{
  char *d = dest;
  const char *s = src;
  while (len--)
    *d++ = *s++;
  return dest;
}

/*
    FUNZIONE CHIAMATA SOLO AD INIZIO PROGRAMMA

    Inizializza la lista pcbFree per fargli contenere [MAXPROC] processi vuoti
*/
void initPcbs() {
    for (int i = 0; i < MAXPROC; i++) {
        list_add(&pcbFree_table[i].p_list, &pcbFree_h);
    }
}

/*
    Inserisce il processo p in coda alla lista dei processi liberi (pcbFree)

    p = puntatore al processo interessato

    return = VOID
*/
void freePcb(pcb_t* p) {
    list_add_tail(&p->p_list, &pcbFree_h);
}

/*
    Rimuove dalla lista dei processi liberi un processo e lo inizializza con tutti i valori a NULL o a 0

    input = VOID

    return = puntatore al processo inizializzato, NULL se non esistono processi liberi
*/
pcb_t* allocPcb() {
    if(list_empty(&pcbFree_h)){
        return NULL;
    }
    else{
        pcb_t* tmp = container_of(pcbFree_h.next, pcb_t, p_list);
        list_del(pcbFree_h.next);
        /* 2. AZZERA TUTTI I CAMPI ----------------------------- */
        for (size_t i = 0; i < sizeof(pcb_t); i++){
            ((unsigned char *)tmp)[i] = 0;
        }
        INIT_LIST_HEAD(&tmp->p_list);
        INIT_LIST_HEAD(&tmp->p_child);
        INIT_LIST_HEAD(&tmp->p_sib);
        tmp->p_parent = NULL;
        tmp->p_time = 0;
        tmp->p_supportStruct = NULL;
        tmp->p_pid = next_pid;
        next_pid++;
        tmp->p_s.status = ALLOFF;
        return tmp;
    }
}

/*
    Funzione usata per inizializzare un processo perché faccia da testa di una lista

    head = puntatore ad un elemento list_head da inizializzare come lista

    return = VOID
*/
void mkEmptyProcQ(struct list_head* head) {
    INIT_LIST_HEAD(head);
}

/*
    Controlla se la coda passata è vuota

    head = testa della coda da controllare

    return = 1 se la lista è vuota, 0 se la lista non è vuota
*/
int emptyProcQ(struct list_head* head) {
    return list_empty(head);
}

/*
    Inserisce il processo p nella coda di head

    head = testa della lista in cui inserire il nuovo processo
    p = processo da inserire in coda

    return = VOID
*/
void insertProcQ(struct list_head* head, pcb_t* p) {
    list_add_tail(&p->p_list, head);
}

/*
    Ritorna un puntatore al primo processo nella coda della lista head

    head = testa della coda da cui leggere il primo elemento

    return = puntatore al primo processo in coda, NULL se non possiede processi in coda
*/
pcb_t* headProcQ(struct list_head* head) {
    if(list_empty(head)){
        return NULL;
    } else {
        return container_of(head->next, pcb_t, p_list);
    }
}

/*
    Rimuove e ritorna il primo elemento della coda dei processi nella lista di head

    head = testa della coda da cui rimuovere il primo processo

    return = puntatore al primo processo della lista, NULL se la lista non possedeva processi in coda
*/
pcb_t* removeProcQ(struct list_head* head) {
    if (list_empty(head)) {
        return NULL;
    } else {
        pcb_t* tmp = headProcQ(head);
        list_del(&tmp->p_list);
        return tmp;
    }
}

/*
    Rimuove il processo p dalla lista head, in qualunque posizione della coda esso si trovi

    head = testa della coda dei processi
    p = processo che si intende rimuovere dalla lista

    return = puntatore al processo che è stato rimosso, oppure NULL se il processo non era presente nella coda indicata
*/
pcb_t* outProcQ(struct list_head* head, pcb_t* p) {
    pcb_t* tmp;
    list_for_each_entry(tmp, head, p_list) {
        if (tmp == p) {
            list_del(&p->p_list);
            return p;
        }
    }
    return NULL;
}

/*
    Controlla se il processo p abbia o meno dei child

    p = processo da controllare

    return = 1 se non ha child, 0 se ne possiede almeno uno
*/
int emptyChild(pcb_t* p) {
    return list_empty(&p->p_child);
}

/*
    Inserisce il processo p nella lista dei child del processo prnt

    prnt = il processo da usare come padre del processo p
    p = il processo da inserire nei child di prnt

    return = VOID
*/
void insertChild(pcb_t* prnt, pcb_t* p) {
    p->p_parent = prnt;
    list_add_tail(&p->p_sib, &prnt->p_child);
}

/*
    Rimuove e ritorna il primo child del processo p

    p = processo da cui rimuovere il primo child

    return = puntatore al primo processo nella lista dei child di p, NULL se non erano presenti child in p
*/
pcb_t* removeChild(pcb_t* p) {
    if (!list_empty(&p->p_child)) {
        pcb_t* tmp = container_of(p->p_child.next, pcb_t, p_sib);

        tmp->p_parent = NULL;
        list_del(&tmp->p_sib);
        return tmp;
    } else {
        return NULL;
    }
}

/*
    Rimuove il processo p dalla lista child del processo padre di cui è un child

    p = processo (probabilmente child di un altro processo) interessato

    return = puntatore al processo appena rimosso (p), NULL se p non era child di nessun altro processo
*/
pcb_t* outChild(pcb_t* p) {
    if (p->p_parent != NULL){
        list_del(&p->p_sib);
        p->p_parent = NULL;
        return p;
    }
    else
        return NULL;
}
