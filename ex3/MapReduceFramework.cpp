//
// Created by noam on 4/18/19.
//



#include <atomic>
#include <cstdio>
#include <iostream>
#include <deque>
#include <algorithm>
#include <semaphore.h>
#include <set>
#include "MapReduceFramework.h"
#include "MapReduceClient.h"
#include "pthread.h"
#include "Barrier.h"
#include "Barrier.cpp"


#define HANDLETOCONTEXT (JobContext *)job //a macro that is used as a shorthand for casting a (void *) parameter named
// "job" to a "JobContext" type
#define VOIDTOTHREAD(x) (ThreadContext *)x//a macro that is used as a shorthand for casting a (void *) parameter
// x to a "ThreadContext" type
#define DECIMALTOPERCENT 100
#define BAD_SEMAPHORE -101
#define BAD_MUTEX -100
#define INIT_THREAD_ERROR -102
#define JOIN_THREAD_ERROR -103

using namespace std;


class ThreadContext;

/** The JobContext class holds all of the data structures relevant to this job (i.e. the input and output vectors,
 * client and mutexes) as long as the individual threads that are running the map reduce job (in the form of 2
 * deques - one of type pthread (unsigned long) which holds the threads reference number after initilization,
 * and the other is a deque of the each thread's "context" - which is a class that wwil be passed to "pthread_create" as
 * the argument, and in it contains each threads working enviornment).
 */
class JobContext {
public:
    int numThreads; // the number of threads this job is running
    bool joined; // a boolean value that indicated whether we have already joined the threads. This is to prevent
    // multiple joining, as that behavior is undefined.
    //pthread_t *jobNum;
    Barrier *bar; // the barrier used to ensure all threads finish mapping and sorting at the same time
    JobState jobState; // a jobstate strcut instant used to indicate where we are right now
    float inc; // the increment to that we will increase our current percent status after completing a task
    deque<pthread_t> threadPool; // a pool of threads that will run this job
    deque<ThreadContext> threadContexts; // a deque of thread contexts - as described below
    pthread_mutex_t mutex1; // the first mutex - used for accessing the input vector
    pthread_mutex_t mutex2; // the second mutex - used for accessing the output vector
    pthread_mutex_t mutex3; // the third mutex - used for accessing the vector of int' vectors
    pthread_mutex_t percent_mutex; // a mutex used to update the percent value of the JobState struct
    sem_t sortedVecsToReduce; // a semaphore that is used to indicate how many vectors with same keys need to be reduced
    InputVec inVec; // the input vector
    OutputVec *outVec; // the output vector
    vector<IntermediateVec> vecOfSameKeyVecs; // a vector that holds vectors of intermediate pairs that share the
    // same key value
    vector<IntermediateVec> vecOfFirstVecs; // a vector that holds vectors intermediate vectors obtained from mapping
    //the input vector
    const MapReduceClient *jobClient; // the jobclient
    atomic<int> JobInputVectorCount; // an atomic counter that holds the index of the input pair we are about to map
    atomic<bool> shuffled; // an atomic boolean value that indicates whether we have finished shuffling
    atomic<bool> startedShuffling; // an atomic boolean value that indicates wehther we have started shuffling

    /**
     * class c'tor
     * @param numThread the maximal number of threads allowed for this job
     * @param inputVec the input vector
     * @param outputVec the output vector
     * @param client the map-reduce client
     */
    JobContext(int numThread, const InputVec &inputVec, OutputVec &outputVec, const MapReduceClient &client)
            : joined(false), threadPool(), threadContexts(), vecOfSameKeyVecs(), vecOfFirstVecs(),
              jobClient(&client), shuffled(false), startedShuffling(false) {
        init_mutex(&mutex1);
        init_mutex(&mutex2);
        init_mutex(&mutex3);
        init_mutex(&percent_mutex);
        init_semaphore(sortedVecsToReduce, 0);
        //jobNum = new pthread_t();
        inVec = inputVec;
        outVec = &outputVec;
        numThreads = min(numThread, (int) inVec.size());
        jobState.percentage = 0.0;
        jobState.stage = MAP_STAGE;
        inc = (float) ((float) DECIMALTOPERCENT / inputVec.size());
        bar = new Barrier(numThreads);
        JobInputVectorCount = 0;
        /*
         * initialize the theadPool and the each thread's context (read - enviornment)
         */
        for (int i = 0; i < numThreads; i++) {
            threadPool.emplace_back();
            threadContexts.emplace_back(*this, i);
        }
    }

    /**
     * a function that safely initializes a semaphore do a initial value
     * @param semaphore the semaphorew we wish to initialize
     * @param initVal the value we wish to set the semaphore to
     */
    static void init_semaphore(sem_t &semaphore, int initVal) {
        if (sem_init(&semaphore, 0, initVal) != 0) {
            cerr << "bad semaphore initialization" << endl;
            exit(BAD_SEMAPHORE);
        }
    }

    /**
     * a function that safely initializes a mutex
     * @param mutex the mutex we wish to initialize
     */
    static void init_mutex(pthread_mutex_t *mutex) {
        if (pthread_mutex_init(mutex, nullptr)) {
            cerr << "bad mutex initialization" << endl;
            exit(BAD_MUTEX);
        }
    }
};

/**
 * The class ThreadContext contains all of the objects and data structures a single thread will manipulate in its
 * lifetime. An instance of ThreadContext is initialized for each thread when we start the job (in JobContext's
 * constructor, and is passed as an argument to each thread's function when we "initialize" the functions with
 * "p_thread_create".
 * It is worth noting that the vast majority of this class contains pointers to data structures that are held
 * in the main "JobContext" of this job.
 */
class ThreadContext {
public:
    int threadID; // the thread's id number
    Barrier *barrier; // a pointer to the barrier of this job
    atomic<int> *localInputVectorCount; // a pointer to the atomic counter that is the index of which input pair we are
    // reading
    /*
     * pointers to the mutexes and semaphore of this job
     */
    pthread_mutex_t *mutex2;
    pthread_mutex_t *mutex1;
    pthread_mutex_t *mutex3;
    pthread_mutex_t *percentMutex;
    sem_t *vecsToReduce;
    float *p_inc; // pointer to the increment value we will use to update the percentage of the job
    InputVec *p_inVec; // pointer to the input vector
    OutputVec *p_outVec; // pointer to the output vector
    vector<IntermediateVec> *p_vecOfSameKeyVecs;
    vector<IntermediateVec> *p_vecOfFirstVecs;
    IntermediateVec threadVector;      //this vector's internal vector. This vector will be loaded with
    // Intermediate pair after calling "map" on an input pair.
    const MapReduceClient *jobClient; // pointer to the job's client
    /*
     * pointers to the atomic boolean variables that show the state of shuffling
     */
    atomic<bool> *p_shuffled;
    atomic<bool> *p_startedShuffling;
    JobState *p_jobState; // a pointer to the job's current state


    /**
     * class c'tor. gets the jobContext object we creates, and the thread's id, and initializes all of the thread's
     * internal data structures. The main job of this contructor though, is to point all of the pointers to the internal
     * parameters of the JobContext class
     * @param newContext the jobcontext instance that holds these threads
     * @param i the thread's id number
     */
    ThreadContext(JobContext &newContext, int i) : threadID(i), threadVector() {
        barrier = newContext.bar;
        localInputVectorCount = &newContext.JobInputVectorCount;
        mutex2 = &newContext.mutex2;
        mutex1 = &newContext.mutex1;
        mutex3 = &newContext.mutex3;
        percentMutex = &newContext.percent_mutex;
        vecsToReduce = &newContext.sortedVecsToReduce;
        p_inc = &newContext.inc;
        p_inVec = &newContext.inVec;
        p_outVec = newContext.outVec;
        p_vecOfSameKeyVecs = &newContext.vecOfSameKeyVecs;
        p_vecOfFirstVecs = &newContext.vecOfFirstVecs;
        jobClient = newContext.jobClient;
        p_shuffled = &newContext.shuffled;
        p_startedShuffling = &newContext.startedShuffling;
        p_jobState = &newContext.jobState;
    }
};

/**
 *  frees all of the dynamically allocated memory in the current jobContext
 * @param job the jobContext object
 */
void freeJobContext(void *job) {
    auto *toFree = HANDLETOCONTEXT;
    sem_destroy(&toFree->sortedVecsToReduce);
    pthread_mutex_destroy(&toFree->mutex1);
    pthread_mutex_destroy(&toFree->mutex2);
    pthread_mutex_destroy(&toFree->mutex3);
    pthread_mutex_destroy(&toFree->percent_mutex);
    delete (toFree->bar);
}

/**
 * raises the semaphore
 * @param semaphore the semaphore to raise
 */
void sem_up(sem_t *semaphore) {
    if (sem_post(semaphore)) {
        cerr << "could not get into the semaphore";
        exit(BAD_SEMAPHORE);
    }
}

/**
 * lowers the semaphore
 * @param semaphore the semaphore to lower
 */
void sem_down(sem_t *semaphore) {
    if (sem_wait(semaphore)) {
        cerr << "could not get out of the semaphore";
        exit(BAD_SEMAPHORE);
    }
}

/**
 * locks a mutex
 * @param mutex the mutex to lock
 */
void lock_mutex(pthread_mutex_t *mutex) {
    if (pthread_mutex_lock(mutex)) {
        cerr << "could not lock mutex";
        exit(BAD_MUTEX);
    }
}

/**
 * unlocks a mutex
 * @param mutex the mutex to unlock
 */
void unlock_mutex(pthread_mutex_t *mutex) {
    if (pthread_mutex_unlock(mutex)) {
        cerr << "could not unlock mutex";
        exit(BAD_MUTEX);
    }
}

/**
 * a function that compares two Intermediate Pairs by their key. sed by the "sort" function to sort intermediate pairs
 * @param first the first pair
 * @param second the second pair
 * @return true if the first's pair K2 key is smaller than the second pair's K2 key, and false otherwise.
 */
bool sortByKeys(pair<K2 *, V2 *> first, pair<K2 *, V2 *> second) {
    return (*first.first < *second.first);
}

/**
 * a struct with the single overloaded () operator, meant to be used by a set of Intermediate values, so that we can
 *  count only the pairs  with separate keys.
 */
struct KeyCompare {
    bool operator()(K2 *first, K2 *second) {
        return (*first < *second);
    }
};

/**
 * The function corrects the value of a jobstate's percentage value once we have finished running the map reduce
 * algorithm. This can happen due to small rounding errors in floating point representations.
 * @param threadContext a threadcontext which contains a pointer to the jobstate we wish to correct
 */
void correctFinishedState(const ThreadContext *threadContext) {
    if ((threadContext->p_jobState->percentage > 99.9 && threadContext->p_jobState->percentage < 100) ||
        ((threadContext->p_jobState->percentage > 100) && (threadContext->p_jobState->percentage < 100.1))) {
        threadContext->p_jobState->percentage = 100.0;
    }
}

/**
 * returns the size of longest vector in the vector containing Intermediate vectors. If all of the vectors inside
 * the vector containing the vectors are empty, then it returns 0
 * @param context the thread's context, which contains a pointer to the vector of intermediate vectors
 * @return the size of the longest sub-vector
 */
int longestVecOrEmpty(ThreadContext *context) {
    int maxSize = 0;
    for (const auto &vec:*context->p_vecOfFirstVecs) {
        if ((int) vec.size() > maxSize) {
            maxSize = (int) vec.size();
        }
    }
    return maxSize;
}

/**
 * returns the index of the first vector in the vector of intermediate vectors that is not empty. This function
 * assumes that the vector contains at least one non-empty vector
 * @param context the thread's context, which contains a pointer to the vector of intermediate vectors
 * @return the index (starting at 0) of the first vector that is not empty.
 */
int firstNonEmptyIndex(ThreadContext *context) {
    auto it = context->p_vecOfFirstVecs->begin();
    for (int i = 0; i < (int) (context->p_vecOfFirstVecs->size()); i++) {
        if (!(it + i)->empty()) {
            return i;
        }
    }
    //should not ever reach here
    return -1;
}

/**
 * finds the maximal K2 key in a vector containing Intermediate Vectors. This is done in Linear time, as all of the
 * Intermediate vectors are assumed to be sorted
 * @param context the thread's context, which contains a pointer to the vector of intermediate vectors
 * @return the Key with the maximal value in the vector of vectors
 */
K2 *findMaxKey(ThreadContext *context) {
    K2 *maxKey = (context->p_vecOfFirstVecs->begin() + firstNonEmptyIndex(context)).operator*().back().first;
    for (auto vec:*context->p_vecOfFirstVecs) {
        if (!vec.empty()) {
            if (*maxKey < *vec.back().first) {
                maxKey = vec.back().first;
            }
        }
    }
    return maxKey;
}

/**
 * checks if two K2 keys are equal
 * @param first the first key
 * @param second the second key
 * @return true if the two keys are equal, and false otherwise
 */
bool areEqual(K2 *first, K2 *second) {
    return ((!(*first < *second)) && (!(*second < *first)));
}

/**
 *
 * @param context the thread's context, which contains a pointer to the vector of intermediate vectors
 * @return the number of K2 keys in the vector of vectors
 */
int getNumKeys(ThreadContext *context) {
    set<K2 *, KeyCompare> keysEncountered = {};
    for (const IntermediateVec &vec:*context->p_vecOfFirstVecs) {
        for (IntermediatePair pair:vec) {
            keysEncountered.insert(pair.first);
        }
    }
    return (int) (keysEncountered.size());
}


/**
 *  shuffles the vectors from intermediate vectors containing any key-value pair,
 * to vectors that all their pairs have the same key. This will be done by going over all of the vectors that were
 *  created, and creating a new vector that will hold the maximal value of all of the generated intermediate vectors
 * @param context the thread's context, which contains a pointer to the vector of intermediate vectors and to the new
 * vectors of intermediate vectors that will store the shuffled vectors
 * @param num_vecs_to_reduce a pointer that will hold he amount of vectors that will be created in the shuffling process
 */
void shufflePhase(ThreadContext *context, atomic<int> *num_vecs_to_reduce) {
    /**#############################################################################
 *                          SHUFFLE PHASE
 * #############################################################################*/

    //get the number of keys in all of the intermediate vectors
    int numKeys = getNumKeys(context);
    *num_vecs_to_reduce = numKeys;
    // set the job's state to reduce and percentage to 0
    context->p_jobState->stage = REDUCE_STAGE;
    context->p_jobState->percentage = 0.0;
    *context->p_inc = 100.0 / numKeys;
    int isNotEmpty = numKeys;
    /*
     * while the vector of intermediate vectors that we got from the map phase is still not empty, we will
     * continue shuffling
     */
    while (isNotEmpty) {
        auto newVec = new IntermediateVec; // the new vector which will store all of the pairs that have the same key
        K2 *maxKey = findMaxKey(context);
        int i = 0;
        for (auto vec:*context->p_vecOfFirstVecs) { //go over all of the Intermediate vectors
            if (!context->p_vecOfFirstVecs->at(i).empty()) {
                //if the vector is not empty, then we can check if the last pair (i.e. the value with the largest key)
                // equals the largest key found.
                bool eq = areEqual(maxKey, context->p_vecOfFirstVecs->at(i).back().first);
                while (eq) {
                    newVec->push_back(
                            context->p_vecOfFirstVecs->at(i).back());//add the pair to the new vector of the same keys
                    context->p_vecOfFirstVecs->at(i).pop_back(); // remove the pair from the Intermediate vector
                    /*
                     * keep checking if there are more keys in the Intermediate vector with the same key, so we can
                     * insert them to the new vector as well
                     */
                    if (!context->p_vecOfFirstVecs->at(i).empty()) {
                        eq = areEqual(maxKey, context->p_vecOfFirstVecs->at(i).back().first);
                    } else {
                        eq = false;
                    }
                }
            }
            i++;
        }
        /*now, we have a vector containing the same key for each pair. we'll insert it into the vector of int' vectors
        * with the same key*/
        lock_mutex(context->mutex3);
        //push the newly formed vector to the vector of same key vectors
        context->p_vecOfSameKeyVecs->push_back(*newVec);
        unlock_mutex(context->mutex3);
        //notify the other threads that we entered a new vector for them to reduce.
        sem_up(context->vecsToReduce);
        //check if we still have vectors that were not shuffled
        isNotEmpty = longestVecOrEmpty(context);
        delete newVec; //release the memory
    }

}

/**
 * the main function each thread will run - handles map, sort, shuffle and reduce
 * @param context a void * that references to the memory address of the thread's enviornment
 */
void *mapReduceSingleThread(void *context) {
    //first, recreate the threadContext
    auto *threadContext = VOIDTOTHREAD (context);
    /**#############################################################################
     *                          MAP PHASE
     * #############################################################################*/
    int old = -1; //the  counter for the current pair in inVec to be read
    while ((int) (threadContext->p_inVec->size()) > (old=(*threadContext->localInputVectorCount)++)){
        //increase the atomic counter and call the "map" function for the thread's key-value pair
        //old = (*(threadContext->localInputVectorCount))++;
        threadContext->jobClient->map(threadContext->p_inVec->at(old).first, threadContext->p_inVec->at(old).second,
                                      threadContext);
        /*
         * this block of code puts the newly created vector into a vector containing Intermediate vectors
         */
        lock_mutex(threadContext->mutex1); // lock the mutex so we can write to the vector containing the vectors
        // correctly
        threadContext->p_jobState->percentage += *threadContext->p_inc; // increase the percent of the job done
        //threadContext->threadVector.clear();
        unlock_mutex(threadContext->mutex1); // unlock the mutex
    }
    //sort the vector back from "map" that was put in the thread's internal vector
    sort(threadContext->threadVector.begin(), threadContext->threadVector.end(), sortByKeys);
    lock_mutex(threadContext->mutex1); // lock the mutex so we can write to the vector containing the vectors
    // correctly
    threadContext->p_vecOfFirstVecs->push_back(threadContext->threadVector); // add the Intermediate vector to the
    // vector of vectors
    unlock_mutex(threadContext->mutex1); // unlock the mutex
    //enter the barrier, so we can wait for other threads to finish
    threadContext->barrier->barrier();
    /**#############################################################################
 *                          REDUCE PHASE
 * #############################################################################*/
    /*
     * *the first vector to reach this phase will shuffle the intermediate vectors
    */
    if (!*threadContext->p_startedShuffling) {
        *threadContext->p_startedShuffling = true;
        shufflePhase(threadContext, threadContext->localInputVectorCount);
        *threadContext->p_shuffled = true;
    }
    /*
     * to start the reduce phase, any thread that ends up here is essentially waiting for a vector to be inserted.
     * so, we'll wait for the semaphore
     */
    while (!*threadContext->p_shuffled || !threadContext->p_vecOfSameKeyVecs->empty()) {
        /*if a thread entered the loop, then either shuffling still hasn't been finished (so the thread will wait for
         * the semaphore) or, there are more vectors to be reduced*/
        sem_down(threadContext->vecsToReduce); //wait for the semaphore
        /*
         * we are now immediately after the semaphore - this can be due to one of two reasons:
         * 1) the shuffling thread has created a new vector, and we can reduce it
         * 2) we have finished reducing, and the last thread to actively reduce a vector has exited the while loop
         * we'll deal with the second case first - if any thread has finished reducing the last vector and exited the
         * while loop, it sends a signal to all the other threads waiting in the semaphore that they can exit the loop
         * as well. We expect to see this if we finished shuffling AND there are no more threads to reduce
         */
        if (*threadContext->p_shuffled && threadContext->p_vecOfSameKeyVecs->empty()) {
            //this means that a thread has exited the loop and raised the semaphore - this thread can exit the loop
            // as well
            break;
        }
        //if we have reached here, that means we are in the first case, and there's at least one vector in the vector
        // of vectors that we should reduce. we'll lock the mutex so we can read it from the vector of Int' vectors
        lock_mutex(threadContext->mutex3);
        const IntermediateVec toReduce = threadContext->p_vecOfSameKeyVecs->cbegin().operator*();
        threadContext->p_vecOfSameKeyVecs->erase(threadContext->p_vecOfSameKeyVecs->begin());
        unlock_mutex(threadContext->mutex3);
        //now that we succesfully have a vector with the same key and different values, we can call reduce on its values
        threadContext->jobClient->reduce(&toReduce, threadContext);
        /*
         * the next 3 lines update the percentage of the job we just reduced, through a mutex
         */
        lock_mutex(threadContext->percentMutex);
        threadContext->p_jobState->percentage += *threadContext->p_inc;
        unlock_mutex(threadContext->percentMutex);
    }
    correctFinishedState(threadContext); // correct any innaccuracies in percent calculation
    sem_up(threadContext->vecsToReduce); // notify other threads that there are no more threads to reduce
    return nullptr;
}


void emit2(K2 *key, V2 *value, void *context) {
    IntermediatePair newPair(key, value);
    auto threadContext = (ThreadContext *) (context);
    threadContext->threadVector.push_back(newPair);
}

void emit3(K3 *key, V3 *value, void *context) {
    auto temp = VOIDTOTHREAD(context);
    lock_mutex(temp->mutex2);
    temp->p_outVec->emplace_back(key, value);
    unlock_mutex(temp->mutex2);
}

JobHandle startMapReduceJob(const MapReduceClient &client,
                            const InputVec &inputVec, OutputVec &outputVec,
                            int multiThreadLevel) {
    /*first, we'll create the new job and initialize what needs to be set (including the  barrier,
     * the threads and their contexts   */
    JobContext *newJob = new JobContext(multiThreadLevel, inputVec, outputVec, client);
    for (int i = 0; i < newJob->numThreads; ++i) {
        if (pthread_create(&newJob->threadPool.at(i), nullptr, mapReduceSingleThread, &newJob->threadContexts[i])) {
            cerr << "could not create the thread";
            exit(INIT_THREAD_ERROR);
        }
    }
    return newJob;
}


void waitForJob(JobHandle job) {
    auto *waitFor = HANDLETOCONTEXT;
    if (waitFor->joined) {
        return;
    }
    for (int j = 0; j < waitFor->numThreads; ++j) {
        if (pthread_join(waitFor->threadPool.at(j), nullptr)) {
            cerr << "could not join threads";
            exit(JOIN_THREAD_ERROR);
        }
    }
    waitFor->joined = true;
}

void getJobState(JobHandle job, JobState *state) {
    auto *idToGetState = HANDLETOCONTEXT;
    *state = idToGetState->jobState;
}

void closeJobHandle(JobHandle job) {
    waitForJob(job);
    freeJobContext(job);
    auto toDel = (JobContext *) job;
    delete (toDel);
}
