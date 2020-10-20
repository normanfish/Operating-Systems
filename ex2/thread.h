//
// Created by noam on 4/1/19.
//

#ifndef EX2_THREAD_H
#define EX2_THREAD_H

#include <stdio.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>


#define STACK_SIZE 4096


#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
        "rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#endif

enum STATE {
    READY, BLOCKED, SLEEPING
};

/**
 * a class of thread objects. Each thread has a function that it runs, a state, a stack and buffer enviornment, and ID
 *  number, and a counter that counts the amount of quantums it was in play
 */
class thread {
    int id; //the thread's ID number
    unsigned int quantum_used; //the amount of quantums the thread was active
    void (*runningThread)(void); //the function that the thread runs
    bool state[3]; // a booleanarray of size 3, where represents what state the thread is in (ready, blocked, or sleeping)
    /*
     * the thread's stack and buffer
     */
    sigjmp_buf env;
    char *threadStack;


public:
    /**
     * class c'tor
     * @param id the thread's ID
     * @param f the function that the thread runs
     */
    thread(int id, void(*f)(void));

    /**
     * class d'tor
     */
    virtual ~thread();

    /**
     * blocks the thread (READY->BLOCKED)
     */
    void block();

    /**
     * unblocks the thread (BLOCKED->READY if the thread isn't sleeping)
     */
    void unblock();

    /**
     * puts the running thread to sleep (RUNNING->SLEEP)
     */
    void sleep();

    /**
     * wakes up the thread
     */
    void wakeUp();

    /**
     *
     * @return the thread's ID
     */
    int getId() const {
        return id;
    }


    /**
     *
     * @return the number of quantums the thread was active
     */
    unsigned int getQuantumUsed() const {
        return quantum_used;
    }

    /**
     *  increase the number of quantums the thread was active by 1.
     */
    void incQuantumUsed() {
        quantum_used++;
    }

    /**
     *
     * @return an array of size 3 with boolean values, where the value in each place is the state of the thread
     */
    bool getState(STATE stateToSample) const {
        return state[stateToSample];
    }


    /**
     *
     * @return the threads buffer
     */
    __jmp_buf_tag *getEnv() {
        return env;
    }

    /**
     *
     * @return the thread's stack
     */
    char *getThreadStack() const {
        return threadStack;
    }

};


#endif //EX2_THREAD_H
