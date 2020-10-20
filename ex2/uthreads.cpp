//
// Created by noam on 3/28/19.
//
#include <iostream>
#include <vector>
#include <deque>
#include <queue>
#include <memory>

#include "uthreads.h"
#include "thread.h"
#include "sleeping_threads_list.h"

using namespace std;


#define SYSTEM_ERROR_MESSAGE "system error: "
#define  LIB_ERROR "thread library error: "
#define NO_SUCH_THREAD "no such thread exists"
#define SIGNAL_ERROR "signal error"
#define SECOND 1000000
#define ERROR_VAL -1
typedef shared_ptr<thread> pthread;

/* API and function documentation at implementation*/

void switchThreads(int sig);

void wakeUp(int sig);

pthread getThreadFromAnyList(int tid);

void mainThreadFunc();

int initSignals();

void startTheVirtualClock();

void remove(deque<pthread> &threadQueue, int tid);

void startTheRealClock();

bool threadDoesntExist(int tid);

int runningTimeInusecs; // the running time allowed for each thread
unsigned int totalQuantums; // the total number of quantums that each of the threads ran together
priority_queue<int, vector<int>, greater<int>> idMinHeap; // a priority queue that holds the fre ID numbers
deque<pthread> readyThreads; //a 2-way queue holding the list of threads that are in the READY state
deque<pthread> blockedThreads; //a 2-way queue holding the list of threads that are in the BLOCKED state
vector<pthread> deletedThreads; //a list holding all of the threads that were deleted
SleepingThreadsList sleepyHeads; // a list of timevals that hold all of the wake up times of currently sleeping threads
pthread running; // the currently running thread
struct itimerval vtimer; //the virtual timer
struct itimerval rtimer;//  the real time timer
struct sigaction timerAction; // the virtual time signal handler
struct sigaction rtimerAction;// the real time signal handler
sigset_t set; // a set of signals that can be blocked or masked (in our case, holds the SIGVALARM and SIGALARM)


timeval calc_wake_up_timeval(int usecs_to_sleep) {

    timeval now, time_to_sleep, wake_up_timeval;
    gettimeofday(&now, nullptr);
    time_to_sleep.tv_sec = usecs_to_sleep / 1000000;
    time_to_sleep.tv_usec = usecs_to_sleep % 1000000;
    timeradd(&now, &time_to_sleep, &wake_up_timeval);
    return wake_up_timeval;
}

/**
 * prints an error message according to the type of error (library or system) and either exits the program in case
 * of a system error, or returns an error value in case of a library error
 * @param errType 1 if it's a library error, and 0 if it's a system error
 * @param errMessage the message to be displayed to the user
 * @return -1 after printing the library error, and the function exit with exit(1) otherwise
 */
int criticalError(int errType, const string &errMessage) {
    if (errType == 0) {
        cerr << SYSTEM_ERROR_MESSAGE << errMessage << endl;
    } else {
        cerr << LIB_ERROR << errMessage << endl;
        return ERROR_VAL;
    }
    exit(1);
}

/**
 * initializes the virtaul and real signal handlers, and initializes the virtual timer interval to be the allocated
 * running time for each thread
 * @return 0 if the initialization of the clocks was successful, and the program exits otherwise
 */
int initTimer() {
    //set the timer signal handler to be the switch threads function
    timerAction.sa_handler = &switchThreads;
    if (sigaction(SIGVTALRM, &timerAction, NULL) < 0) {
        criticalError(0, "could not pair virtual alarm signal to function");
    }
    rtimerAction.sa_handler = &wakeUp;
    if (sigaction(SIGALRM, &rtimerAction, NULL) < 0) {
        criticalError(0, "could not pair real alarm signal to function");
    }

    // Configure the virtual timer to expire after the allotted time for each thread */
    vtimer.it_value.tv_sec = 0;        // first time interval, seconds part
    vtimer.it_value.tv_usec = runningTimeInusecs;        // first time interval, microseconds part
    vtimer.it_interval.tv_sec = runningTimeInusecs / SECOND;        //  time interval, seconds part
    vtimer.it_interval.tv_usec = runningTimeInusecs % SECOND;        // time interval, microseconds part
    return 0;
}

/**
 * Starts the virtual timer. This function is called every time we switch to a new thread.
 */
void startTheVirtualClock() {
    // Start a virtual timer. It counts down whenever this process is executing.
    if (setitimer(ITIMER_VIRTUAL, &vtimer, NULL)) {
        criticalError(0, "error setting the timer for the thread");
    }
}

/**
 * Tells the system to ignore the clock signals from the real and virtual timer.
 */
void ignoreClock() {
    if (sigprocmask(SIG_BLOCK, &set, NULL) == ERROR_VAL) {
        criticalError(0, "signal masking error");
    }
}

/**
 * Tells the system to accept the clock signals from the real and virtual timer.
 */
void acceptClockSignal() {
    if (sigprocmask(SIG_UNBLOCK, &set, NULL) == ERROR_VAL) {
        criticalError(0, "signal masking error");
    }
}

/**
 * The function handles switching threads. A thread is switched in any one of the following stated:
 * 1. The running time of a thread is up, and we wish to move on to the next thread (in this case, the function will be
 * automatically called from the handler getting the virtual time signal).
 * 2. The running function put itself to sleep, and we need to call the next thread to run
 * 3. The running function blocked itself, in which case, similar to the second case, we need to move on to the next
 * thread.
 * The function moves the running thread to the proper list (according to the cases above), loads up the next thread,
 * and jumps to it
 * @param sig the signal parameter sent to the function
 */
void switchThreads(int sig) {
    //first thing's first - stop the virtual clock and increase the number of quantums
    ignoreClock();
    totalQuantums++;
    //first, we need to check if we got here because of the "sleep" function,  because the thread's time is up, or because
    //the running thread was suddenly blocked
    if (running->getState(SLEEPING)) {
        //we got here because the running thread went to sleep, and now we need to reschedule
        struct timeval newAlarm = calc_wake_up_timeval(sig); //calculate the wakeup time for the thread
        sleepyHeads.add(running->getId(), newAlarm); // add the alarm clocks to the sleeping threads list
        //now, we'll set the clock for the time the has process to sleep
        startTheRealClock();
        /*we have now set the alarm clock for the running process that went to sleep. now, we'll move it to the
         * "blocked queue*/
        blockedThreads.push_back(running);

    } else if (running->getState(READY)) {
        /*if we are here, it means that we entered the function because the virtual running time of the thread is up,
         * or we terminated the running thread from itself
         *we'll move the thread that just finished to the back of the "ready" line */
        if (sig != -3) {
            readyThreads.push_back(running);
        } else {
            //we terminated the process from itself
            deletedThreads.push_back(running);
        }

    } else if (running->getState(BLOCKED)) {
        /*if we are here, it means that we entered the function because the blocked thread blocked itself */
        blockedThreads.push_back(running);
    }
    //next - we'll check if this is a direct call to the thread we are going back to
    int ret = sigsetjmp(running->getEnv(), 1);
    //don't forget to set the timer and accept signals again!
    acceptClockSignal();
    startTheVirtualClock();
    if (ret == 0) {
        /* at his point, we have (hopefully) moved the running thread to the back of the ready line or to the blocked line.
         * we can now move up the next thread from ready to running*/
        running = readyThreads.front();
        readyThreads.pop_front();
        running->incQuantumUsed();
        siglongjmp(running->getEnv(), 1);
    }
    //else - we can simply return to the thread we stopped at before.
    return;
}

/**
 * sets the real timer to go off at the time the closest sleeping thread has to wake upץ
 */
void startTheRealClock() {
    timeval now, timetillwakeup;
    gettimeofday(&now, nullptr);
    timersub(&sleepyHeads.peek()->awaken_tv, &now, &timetillwakeup);
    rtimer.it_value = timetillwakeup;//set the next time to wake up to be the nearest alarm
    //"turn on" the alarm clock
    if (setitimer(ITIMER_REAL, &rtimer, NULL) == ERROR_VAL) {
        criticalError(0, "Could not set the timer");
    }
}

/**
 * This function is the handler function for when a thread wakes up (i.e. when the system receives a SIGALARM signal).
 * The function changes the state of the thread (disables sleep), and then, if the thread isn't blocked, moves it
 * to the end of the READY queue.
 * @param sig signal parameter
 */
void wakeUp(int sig) {
    /*
     * this function is the handler for the case when a process has finished sleeping
     */
    if(threadDoesntExist(sleepyHeads.peek()->id))
        //we killed this thread before it had the chance to wake up
        return;

    ignoreClock();
    //wake up the thread that is currently in blocked state
    int idToWake = sleepyHeads.peek()->id;
    sleepyHeads.pop();
    pthread temp = getThreadFromAnyList(idToWake);
    temp->wakeUp();
    //if the thread isn't blocked, then we can move it back to the ready queue
    if (!temp->getState(BLOCKED)) {
        remove(blockedThreads, idToWake);
        readyThreads.push_back(temp);
    }
    //now, we'll check if we have any other threads that are sleeping.
    if (!sleepyHeads.empty()) {
        //means we still have some threads that are sleeping. we'll set the next timer to go off at the nearest
        // wake-up time
        startTheRealClock();
    }
    acceptClockSignal();
    return;
}

/**
 * The main thread's function. Just busy waits until the virtual time is up.
 */
void mainThreadFunc() {
    printf("main thread");
    for (;;) {}
}

/**
 * return the minimum ID number that is available. If no numbers are avialble (meaning, we used up all of the ID numbers
 * on currently active threads) then we return -1.
 * @return
 */
int extractMin() {
    if (idMinHeap.empty())
        return 0;
    int min = idMinHeap.top();
    idMinHeap.pop();
    return min;
}

/**
 * returns the index from the start of a queue that a thread with ID is at.
 * @param threadQueue the queue we wish to search for the thread with the specified ID
 * @param id the ID number of the thread we wish to look for
 * @return -1 if the thread doesn't exist in the list, and it's offset (starting from 0 if it's the first thread in the
 * queue) from the base otherwise.
 */
int idOffset(deque<pthread> &threadQueue, int id) {
    int index = 0;
    for (auto &it :threadQueue) {
        if (it->getId() == id) {
            return index;
        }
        index++;
    }
    return ERROR_VAL;
}

/**
 * checks whether an thread with a the inputted ID exists in the thread queue or not
 * @param threadQueue the thread queue we wish to check in
 * @param id the ID of the thread we wish to know exists in the queue
 * @return true if the thread exists in the queue, and false otherwise
 */
bool isInQueue(deque<pthread> &threadQueue, int id) {
    return idOffset(threadQueue, id) != ERROR_VAL;
}

/**
 * returns the thread that is at the offest position in the queue
 * @param threadQueue the queue from which we wish to get the thread
 * @param offset the placement of the thread inside the queue
 * @return essentially - threadQueue[offset]
 */
pthread getThread(deque<pthread> &threadQueue, int offset) {
    return threadQueue[offset];
}

/**
 * returns a thread (that was initialized) by it's ID.
 * @param tid the ID of the thread we wish to get
 * @return the thread with the matching ID
 */
pthread getThreadFromAnyList(int tid) {
    if (isInQueue(readyThreads, tid)) {
        return getThread(readyThreads, idOffset(readyThreads, tid));
    } else {
        return getThread(blockedThreads, idOffset(blockedThreads, tid));
    }
}

/**
 * removes a thread with tid from the queue threadQueue
 * @param threadQueue the queue from which we wish to remove the thread
 * @param tid the thread ID wewish to remove
 */
void remove(deque<pthread> &threadQueue, int tid) {
    int offset = idOffset(threadQueue, tid);
    threadQueue.erase(threadQueue.begin() + offset);
}

/**
 * initializes the signal set which we will block and accept signals from. In this program, we will handle
 * the SIGVTALRM and SIGALRM signals.
 * @return
 */
int initSignals() {
    if (sigemptyset(&set) == ERROR_VAL) {
        return criticalError(0, SIGNAL_ERROR);
    }
    if (sigaddset(&set, SIGVTALRM) == ERROR_VAL) {
        return criticalError(0, SIGNAL_ERROR);
    }
    if (sigaddset(&set, SIGALRM) == ERROR_VAL) {
        return criticalError(0, SIGNAL_ERROR);
    }
    return 0;
}

/**
 * checks if a thread exists in any one of the thread queues (READY, BLOCKED or deleted).
 * @param tid the thread that we wish to check if was initialized
 * @return true if the thread was never initialized, and false if it exists in any of the queues
 */
bool threadDoesntExist(int tid) {
    return (!isInQueue(blockedThreads, tid)) && (!isInQueue(readyThreads, tid)) && (running->getId() != tid);
}

int uthread_init(int quantum_usecs) {
    //check that we got a positive number for the number of microseconds
    if (quantum_usecs < 0) {
        return criticalError(1, "time must be positive");
    }
    runningTimeInusecs = quantum_usecs;
    //sets the total number of quantums passed to be 1
    totalQuantums++;
    for (int i = 1; i < MAX_THREAD_NUM; i++) {
        idMinHeap.push(i);
    }
    // initialize the virtual timer of the scheduler to the proper time interval, and the set
    // of signals that can be masked to be the virtual timer
    if (initTimer() != 0 || initSignals() != 0)
        return ERROR_VAL;
    //add the main thread to the READY queue
    pthread newThread(new thread(0, &mainThreadFunc));
    running = newThread;
    running->incQuantumUsed();
    //start up the clock, which will wait for a second, and then switch to the main thread
    startTheVirtualClock();
    return 0;
}


int uthread_spawn(void (*f)(void)) {
    ignoreClock();
    unsigned int newID = extractMin();
    if (newID == 0) {
        return criticalError(1, "reached the maximum allowed number of threads");
    }
    pthread newThread(new thread(newID, f));
    readyThreads.push_back(newThread);
    acceptClockSignal();
    return newID;
}


int uthread_terminate(int tid) {
    ignoreClock();
    if (tid == 0) {
        //we just tried to terminate the main thread. according to the assignment instructions,
        // this means we exit the program
        exit(0);
    }
    if (threadDoesntExist(tid)) {
        return criticalError(1, NO_SUCH_THREAD);
    }
    if (isInQueue(readyThreads, tid)) {
        pthread it = getThreadFromAnyList(tid);
        deletedThreads.push_back(it);
        remove(readyThreads, it->getId());
    } else if (isInQueue(blockedThreads, tid)) {
        pthread it = getThreadFromAnyList(tid);
        deletedThreads.push_back(it);
        remove(blockedThreads, it->getId());
    } else {
        //running thread is the one we are terminating
        {
            switchThreads(-3);
        }
    }
    idMinHeap.push(tid);
    acceptClockSignal();
    return 0;
}


int uthread_block(int tid) {
    ignoreClock();
    if (tid == 0) {
        return criticalError(1, "cannot block main thread");
    }
    if (threadDoesntExist(tid)) {
        //if we reached here, the thread never existed - ERROR
        return criticalError(1, NO_SUCH_THREAD);
    }
    //let's look for the thread we wish to block
    if (isInQueue(readyThreads, tid)) {
        //it's in the ready threads
        auto it = getThreadFromAnyList(tid);
        it->block();
        blockedThreads.push_back(it);
        remove(readyThreads, it->getId());
        //acceptClockSignal();
        //return 0;
    } else if (running->getId() == tid) {
        running->block();
        //blockedThreads.push_back(running);
        switchThreads(1);
    }
    else if(isInQueue(blockedThreads, tid)){
        auto it = getThreadFromAnyList(tid);
        it->block();
    }

    //if we reached this point, it was already blocked - we can safely do nothing
    acceptClockSignal();
    return 0;
}


int uthread_resume(int tid) {
    ignoreClock();
    if (threadDoesntExist(tid)) {
        return criticalError(1, NO_SUCH_THREAD);
    }
    if (isInQueue(blockedThreads, tid)) {
        auto it = getThreadFromAnyList(tid);
        it->unblock(); //unblock the thread
        //now, we'll check if the thread is sleeping. if it isn't, then we can put it back in the ready queue
        if (!it->getState(SLEEPING)) {
            readyThreads.push_back(it);
            remove(blockedThreads, it->getId());
        }
    }
    acceptClockSignal();
    return 0;

}


int uthread_sleep(unsigned int usec) {
    //first, we have to check that the process we are trying to make sleep isn't the main thread
    if (running->getId() == 0) {
        return criticalError(1, "cannot put the main thread to sleep!");
    }
    //make the process sleep
    running->sleep();
    //make the scheduling decision
    switchThreads(usec);
    return 0;
}


int uthread_get_tid() {
    return running->getId();
}


int uthread_get_total_quantums() {
    return totalQuantums;
}


int uthread_get_quantums(int tid) {
    ignoreClock();
    if ((running->getId() != tid) && (!isInQueue(readyThreads, tid)) && (!isInQueue(blockedThreads, tid))) {
        acceptClockSignal();
        return criticalError(1, NO_SUCH_THREAD);
    }
    acceptClockSignal();
    if ((running->getId() == tid)) {
        return running->getQuantumUsed();
    }
    return getThreadFromAnyList(tid)->getQuantumUsed();
}


