#include "./headers/pcb.h"
//Consigliato iniziare da questo file!

static struct list_head pcbFree_h = LIST_HEAD_INIT(pcbFree_h);
static pcb_t pcbFree_table[MAXPROC];
static int next_pid = 1;

/*Initialize the pcbFree list to contain all the elements of the static array of MAXPROC PCBs.
This method will be called only once during data structure initialization.*/
void initPcbs() {
    for (int i = 0; i < MAXPROC; i++) {
        list_add(&pcbFree_table[i].p_list, &pcbFree_h);
    }
}
    
//Insert the element pointed to by p onto the pcbFree list.
void freePcb(pcb_t* p) {
    list_add_tail(&p->p_list, &pcbFree_h);
}

/*Return NULL if the pcbFree list is empty. Otherwise, remove an element from the pcbFree
list, provide initial values for ALL of the PCBs fields (i.e. NULL and/or 0) and then return
a pointer to the removed element. PCBs get reused, so it is important that no previous
value persist in a PCB when it gets reallocated.*/
pcb_t* allocPcb() {
    if(list_empty(&pcbFree_h)){
        return NULL;
    }
    else{
        pcb_t* tmp = container_of(pcbFree_h.next, pcb_t, p_list);
        list_del(pcbFree_h.next);
        
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

//This method is used to initialize a variable to be head pointer to a process queue.
void mkEmptyProcQ(struct list_head* head) {
    INIT_LIST_HEAD(head);
}

//Return TRUE if the queue whose head is pointed to by head is empty. Return FALSE otherwise.
int emptyProcQ(struct list_head* head) {
    return list_empty(head);
}

//Insert the PCB pointed by p into the process queue whose head pointer is pointed to by head.
void insertProcQ(struct list_head* head, pcb_t* p) {
    list_add_tail(&p->p_list, head);
}

/*Return a pointer to the first PCB from the process queue whose head is pointed to by head. Do
not remove this PCB from the process queue. Return NULL if the process queue is empty.*/
pcb_t* headProcQ(struct list_head* head) {
    if(list_empty(head)){
        return NULL;
    } else {
        return container_of(head->next, pcb_t, p_list);
    }
}

/*Remove the first (i.e. head) element from the process queue whose head pointer is pointed to
by head. Return NULL if the process queue was initially empty; otherwise return the pointer
to the removed element.*/
pcb_t* removeProcQ(struct list_head* head) {
    if (list_empty(head)) {
        return NULL;
    } else {
        pcb_t* tmp = headProcQ(head);
        list_del(&tmp->p_list);
        return tmp;
    }
}

/*Remove the PCB pointed to by p from the process queue whose head pointer is pointed to by
head. If the desired entry is not in the indicated queue (an error condition), return NULL;
otherwise, return p. Note that p can point to any element of the process queue.*/
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

/*Return TRUE if the PCB pointed to by p has no children. Return FALSE otherwise.*/
int emptyChild(pcb_t* p) {
    return list_empty(&p->p_child);
}

/*Make the PCB pointed to by p a child of the PCB pointed to by prnt.*/
void insertChild(pcb_t* prnt, pcb_t* p) {
    p->p_parent = prnt;
    list_add_tail(&p->p_sib, &prnt->p_child);
}

/*Make the first child of the PCB pointed to by p no longer a child of p. Return NULL if initially
there were no children of p. Otherwise, return a pointer to this removed first child PCB.*/
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

/*Make the PCB pointed to by p no longer the child of its parent. If the PCB pointed to by p has
no parent, return NULL; otherwise, return p. Note that the element pointed to by p could be
in an arbitrary position (i.e. not be the first child of its parent).*/
pcb_t* outChild(pcb_t* p) {
    if (p->p_parent != NULL){
        list_del(&p->p_sib);
        p->p_parent = NULL;
        return p;
    }
    else
        return NULL;
}
