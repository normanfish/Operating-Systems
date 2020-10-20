//
// Created by noam on 4/1/19.
//

#include "thread.h"
#include <deque>
#include <cstdlib>

address_t translate_address(address_t addr) {
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

thread::~thread() {
    //free(env);
    delete (threadStack);
}

thread::thread(int id, void(*f)()) : id(id) {
    address_t sp, pc;
    quantum_used = 0;
    state[READY] = true;
    state[BLOCKED] = false;
    state[SLEEPING] = false;
    runningThread = f;
    threadStack = new char[STACK_SIZE];
    sp = (address_t) threadStack + STACK_SIZE - sizeof(address_t);
    pc = (address_t) f;
    sigsetjmp(env, 1);
    (env->__jmpbuf)[JB_SP] = translate_address(sp);
    (env->__jmpbuf)[JB_PC] = translate_address(pc);
    sigemptyset(&env->__saved_mask);
}

void thread::block() {
    state[READY] = false;
    state[BLOCKED] = true;
}

void thread::unblock() {
    state[BLOCKED] = false;
    state[READY] = !state[SLEEPING];
}

void thread::sleep() {
    state[READY] = false;
    state[SLEEPING] = true;
}

void thread::wakeUp() {
    state[SLEEPING] = false;
    state[READY] = !state[BLOCKED];
}
