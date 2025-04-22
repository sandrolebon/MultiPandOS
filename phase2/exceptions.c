//con vergogna debbo puntualizzare che di mio c'è poco e niente qui
#include <uriscv/liburiscv.h>
#include "../headers/const.h"
#include "../headers/types.h"


/**
 * Gestore Trap
 * 
 * @param indexValue indica se si tratta di un PGFAULTEXCEPT o di un GENERALEXCEPT
 */
void passUpOrDie(int indexValue) {
    
    // Pass up
    if(current_process[cpu_id]->p_supportStruct != NULL) {
        
        unsigned int stackPtr, status, progCounter;
        stackPtr = current_process->p_supportStruct->sup_exceptContext[indexValue].stackPtr;
        status = current_process->p_supportStruct->sup_exceptContext[indexValue].status;
        progCounter = current_process->p_supportStruct->sup_exceptContext[indexValue].pc;

        LDCXT(stackPtr, status, progCounter);
    }
    // Or die
    else {
        terminateProcess(current_process);
        current_process = NULL;
        schedule();
    }
}

void exceptionHandler() {
    switch((getCAUSE() & GETEXECCODE) >> CAUSESHIFT) {
        case IOINTERRUPTS:
            // External Device Interrupt
            interruptHandler();
            break;
        case 1 ... 3:
            // TLB Exception
            // uTLB_RefillHandler();
            passUpOrDie(PGFAULTEXCEPT);
            break;
        case 4 ... 7:
            // Program Traps p1: Address and Bus Error Exception
            passUpOrDie(GENERALEXCEPT);
            break;
        case SYSEXCEPTION: 
            // Syscalls
            syscallHandler();
		    break;
        case 9 ... 12:
            // Breakpoint Calls, Program Traps p2
            passUpOrDie(GENERALEXCEPT);
            break;
        default: 
            // Wrong ExcCode
            passUpOrDie(GENERALEXCEPT);
            break;
	}
}

