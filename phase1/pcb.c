#include "./headers/pcb.h"

static struct list_head pcbFree_h;
static pcb_t pcbFree_table[MAXPROC];
static int next_pid = 1;

void initPcbs() {
    /*Initialize the pcbFree list to contain all the elements of the static array of MAXPROC PCBs.
This method will be called only once during data structure initialization.*/
}

void freePcb(pcb_t* p) {
    //Insert the element pointed to by p onto the pcbFree list.
}

pcb_t* allocPcb() {
    /*Return NULL if the pcbFree list is empty. Otherwise, remove an element from the pcbFree
list, provide initial values for ALL of the PCBs fields (i.e. NULL and/or 0) and then return
a pointer to the removed element. PCBs get reused, so it is important that no previous
value persist in a PCB when it gets reallocated.*/
}

void mkEmptyProcQ(struct list_head* head) {
    //This method is used to initialize a variable to be head pointer to a process queue.
}

int emptyProcQ(struct list_head* head) {
    //Return TRUE if the queue whose head is pointed to by head is empty. Return FALSE otherwise.
}

void insertProcQ(struct list_head* head, pcb_t* p) {
    //Insert the PCB pointed by p into the process queue whose head pointer is pointed to by head.
}

pcb_t* headProcQ(struct list_head* head) {
    /*Return a pointer to the first PCB from the process queue whose head is pointed to by head. Do
not remove this PCB from the process queue. Return NULL if the process queue is empty.*/
}

pcb_t* removeProcQ(struct list_head* head) {
    /*Remove the first (i.e. head) element from the process queue whose head pointer is pointed to
by head. Return NULL if the process queue was initially empty; otherwise return the pointer
to the removed element.*/
}

pcb_t* outProcQ(struct list_head* head, pcb_t* p) {
  /*Remove the PCB pointed to by p from the process queue whose head pointer is pointed to by
head. If the desired entry is not in the indicated queue (an error condition), return NULL;
otherwise, return p. Note that p can point to any element of the process queue.*/  
}

int emptyChild(pcb_t* p) {
    /*Return TRUE if the PCB pointed to by p has no children. Return FALSE otherwise.*/
}

void insertChild(pcb_t* prnt, pcb_t* p) {
    /*Make the PCB pointed to by p a child of the PCB pointed to by prnt.*/
}

pcb_t* removeChild(pcb_t* p) {
    /*Make the first child of the PCB pointed to by p no longer a child of p. Return NULL if initially
there were no children of p. Otherwise, return a pointer to this removed first child PCB.*/
}

pcb_t* outChild(pcb_t* p) {
    /*Make the PCB pointed to by p no longer the child of its parent. If the PCB pointed to by p has
no parent, return NULL; otherwise, return p. Note that the element pointed to by p could be
in an arbitrary position (i.e. not be the first child of its parent).*/
}
