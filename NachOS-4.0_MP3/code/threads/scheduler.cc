// scheduler.cc 
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would 
//	end up calling FindNextToRun(), and that would put us in an 
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "debug.h"
#include "scheduler.h"
#include "main.h"

//MP3
int L1cmp(Thread *a, Thread *b) //short time first
{
    double T1 = a->getBurstTime();
    double T2 = b->getBurstTime();
    //short time first -> return -1 means don't move(short already first)
    if(T1 < T2) return -1;
    else if(T1 == T2)   return 0;
    else return 1;
}
int L2cmp(Thread *a, Thread *b) //priority scheduling
{
    int T1 = a->getPriority();
    int T2 = b->getPriority();
    //larger priority first
    return T2 - T1;
}
//MP3: deal with aging issue
bool Scheduler::CheckAging(Thread *thread)
{
    int nowOSTime = kernel->stats->totalTicks;
    //check period: per 1500 ticks
    if(thread->getStatus()==READY && nowOSTime - thread->getWaitTime() >= 1500 && thread->getID()>=2)
    {
        int oldPriority = thread->getPriority();
        int newPriority = (oldPriority+10>=149)? 149 : oldPriority+10;
        thread->setPriority(newPriority);
        //reset the wait time beginning to now
        thread->setWaitTime(nowOSTime);

        if(oldPriority!=newPriority){
            cout<<"Tick "<< nowOSTime <<": Thread "<< thread->getID()<<" changes its priority from "
                << oldPriority <<" to "<< newPriority <<endl;
        }
        //new to L1
        if(oldPriority < 100 && newPriority >= 100){
            if(QueueL2->IsInList(thread)){
                QueueL2->Remove(thread);
            }
            QueueL1->Insert(thread);

            cout<<"Tick "<< nowOSTime <<": Thread "<< thread->getID()<<" is removed from queue L2"<<endl;
            cout<<"Tick "<< nowOSTime <<": Thread "<< thread->getID()<<" is inserted into queue L1"<<endl;
 
            
            if(kernel->currentThread->getPriority() >= 100){
                if(kernel->currentThread->getID()!=thread->getID()){
                    cout<<"Aging! Two process are in L1: compare the burst time"<<endl;
                    //double actualBT = kernel->stats->userTicks - kernel->currentThread->getStartTime();
                    //double estiBT = 0.5 * actualBT + 0.5 * kernel->currentThread->getBurstTime();
                    /*
                        12/16 modified: since updating burst time allow only in "Sleep" function
                        which means, only update if process do I/O (release its right to use)
                        So here, just use previous time's burst time to do comparison!
                    */
                    if(thread->getBurstTime() < kernel->currentThread->getBurstTime()){
                        cout<<"\nThread: "<<thread->getID()<<" burst time = "<<thread->getBurstTime()<<endl;
                        cout<<"Current Thread: "<<kernel->currentThread->getID()<<" burst time = "<<kernel->currentThread->getBurstTime()<<endl;
                        cout<<"Since new Thread "<<thread->getID()<<" has smaller burst time, ";
                        cout<<"preempt the currentThread\n\n";
                        kernel->currentThread->Yield();
                    }
                }
            }
            
            //For preemtive, if P1 & P2 originally in L2, P1 with higher Priority
            //but P2 move to L1 after aging
            //In this case, P2 should "preempt" P1
            else if(kernel->currentThread->getPriority()<100 && kernel->currentThread->getID()!=thread->getID()){
                kernel->currentThread->Yield();
            }
            return true;
        }
        //new to L2
        else if(oldPriority < 50 && newPriority >=50){
            if(QueueL3->IsInList(thread)){
                QueueL3->Remove(thread);
            }
            QueueL2->Insert(thread);
            cout<<"Tick "<< nowOSTime <<": Thread "<< thread->getID()<<" is removed from queue L3"<<endl;
            cout<<"Tick "<< nowOSTime <<": Thread "<< thread->getID()<<" is inserted into queue L2"<<endl;
            
            if(kernel->currentThread->getPriority() < 50 && kernel->currentThread->getID()!=thread->getID()){
                kernel->currentThread->Yield();
            }
            return true;
        }
    }
    //still in original queue -> maybe need to refresh the queue
    return false;
}

//----------------------------------------------------------------------
// Scheduler::Scheduler
// 	Initialize the list of ready but not running threads.
//	Initially, no ready threads.
//----------------------------------------------------------------------

Scheduler::Scheduler()
{ 
    //MP3
    QueueL3 = new List<Thread *>; 
    toBeDestroyed = NULL;
    //MP3
    QueueL1 = new SortedList<Thread *>(L1cmp);
    QueueL2 = new SortedList<Thread *>(L2cmp);
} 

//----------------------------------------------------------------------
// Scheduler::~Scheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

Scheduler::~Scheduler()
{ 
    delete QueueL3;
    delete QueueL2;
    delete QueueL1; 
} 

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//----------------------------------------------------------------------

void
Scheduler::ReadyToRun (Thread *thread)
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    DEBUG(dbgThread, "Putting thread on ready list: " << thread->getName());
	//cout << "Putting thread on ready list: " << thread->getName() << endl ;
    thread->setStatus(READY);
    //readyList->Append(thread);

    //MP3
    int threadPriority = thread->getPriority();
    int nowOSTime = kernel->stats->totalTicks;

    //from now to wait -> record the startwaittime
    thread->setWaitTime(nowOSTime);
    //Insert the comming thread to its queue -> by priority
    if(threadPriority >= 100 && threadPriority <= 149){
        QueueL1->Insert(thread);
        cout<<"Tick "<< nowOSTime <<": Thread "<<thread->getID()<<" is inserted into queue L1\n";
    }else if(threadPriority >= 50 && threadPriority <= 99){
        QueueL2->Insert(thread);
        cout<<"Tick "<< nowOSTime <<": Thread "<<thread->getID()<<" is inserted into queue L2\n";
    }else if(threadPriority >= 0 && threadPriority <= 49){
        QueueL3->Append(thread);
        cout<<"Tick "<< nowOSTime <<": Thread "<<thread->getID()<<" is inserted into queue L3\n";
    }else{  //wrong priority
        cout<<"Priority out of legal range.\n";
    }

    
    //move to Onetick() check => avoid interrupt issue!
    if(kernel->currentThread->getID() != thread->getID() && kernel->currentThread->getID()>=2){
        if(threadPriority >= 100){
            if(kernel->currentThread->getPriority()>=100){
                cout<<"ReadyToRun! Two process are in L1: compare the burst time"<<endl;
                //double actualBT = kernel->stats->userTicks - kernel->currentThread->getStartTime();
                //double estiBT = 0.5 * actualBT + 0.5 * kernel->currentThread->getBurstTime();
                /*
                    12/16 modified: since updating burst time allow only in "Sleep" function
                    which means, only update if process do I/O (release its right to use)
                    So here, just use previous time's burst time to do comparison!
                */
                if(thread->getBurstTime() < kernel->currentThread->getBurstTime()){
                    cout<<"\nThread: "<<thread->getID()<<" burst time = "<<thread->getBurstTime()<<endl;
                    cout<<"Current Thread: "<<kernel->currentThread->getID()<<" burst time = "<<kernel->currentThread->getBurstTime()<<endl;
                    cout<<"Since new Thread "<<thread->getID()<<" has smaller burst time, ";
                    cout<<"preempt the currentThread\n\n";
                    //kernel->currentThread->Yield();
                    thread->setJump(true);
                }
            }else{
                //kernel->currentThread->Yield();
                thread->setJump(true);
            }
        }else if(threadPriority >= 50 && kernel->currentThread->getPriority() <50 ){
            //kernel->currentThread->Yield();
            thread->setJump(true);
        }
    }
    
}

//----------------------------------------------------------------------
// Scheduler::FindNextToRun
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

Thread *
Scheduler::FindNextToRun ()
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    /*
    if (readyList->IsEmpty()) {
		return NULL;
    } else {
    	return readyList->RemoveFront();
    }*/

    //MP3
    int nowOSTime = kernel->stats->totalTicks;

    if(!QueueL1->IsEmpty()){
        cout<<"Tick "<< nowOSTime <<": Thread "<< QueueL1->Front()->getID() <<" is removed from queue L1\n";
        return QueueL1->RemoveFront();
    }else if(!QueueL2->IsEmpty()){
        cout<<"Tick "<< nowOSTime <<": Thread "<< QueueL2->Front()->getID() <<" is removed from queue L2\n";
        return QueueL2->RemoveFront();
    }else if(!QueueL3->IsEmpty()){
        cout<<"Tick "<< nowOSTime <<": Thread "<< QueueL3->Front()->getID() <<" is removed from queue L3\n";
        return QueueL3->RemoveFront();
    }else{
        return NULL;
    }

}

//----------------------------------------------------------------------
// Scheduler::Run
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable kernel->currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//	"finishing" is set if the current thread is to be deleted
//		once we're no longer running on its stack
//		(when the next thread starts running)
//----------------------------------------------------------------------

void
Scheduler::Run (Thread *nextThread, bool finishing)
{
    Thread *oldThread = kernel->currentThread;
    //MP3
    int nowOSTime = kernel->stats->totalTicks;
    int nowUserTime = kernel->stats->userTicks;
    int oldThreadTime = nowUserTime - oldThread->getStartTime();
    nextThread->setStartTime(nowUserTime);
    
    //MP3: context switch msg
    cout<<"Tick "<< nowOSTime <<": Thread "<<nextThread->getID()<<" is now selected for execution\n";
    cout<<"Tick "<< nowOSTime <<": Thread "<<oldThread->getID()<<" is replaced, and it has executed "
        <<oldThreadTime<<" ticks\n";
    
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (finishing) {	// mark that we need to delete current thread
         ASSERT(toBeDestroyed == NULL);
	 toBeDestroyed = oldThread;
    }
    
    if (oldThread->space != NULL) {	// if this thread is a user program,
        oldThread->SaveUserState(); 	// save the user's CPU registers
	oldThread->space->SaveState();
    }
    
    oldThread->CheckOverflow();		    // check if the old thread
					    // had an undetected stack overflow

    kernel->currentThread = nextThread;  // switch to the next thread
    nextThread->setStatus(RUNNING);      // nextThread is now running
    
    DEBUG(dbgThread, "Switching from: " << oldThread->getName() << " to: " << nextThread->getName());
    
    // This is a machine-dependent assembly language routine defined 
    // in switch.s.  You may have to think
    // a bit to figure out what happens after this, both from the point
    // of view of the thread and from the perspective of the "outside world".

    SWITCH(oldThread, nextThread);

    // we're back, running oldThread
      
    // interrupts are off when we return from switch!
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    DEBUG(dbgThread, "Now in thread: " << oldThread->getName());

    CheckToBeDestroyed();		// check if thread we were running
					// before this one has finished
					// and needs to be cleaned up
    
    if (oldThread->space != NULL) {	    // if there is an address space
        oldThread->RestoreUserState();     // to restore, do it.
	oldThread->space->RestoreState();
    }
}

//----------------------------------------------------------------------
// Scheduler::CheckToBeDestroyed
// 	If the old thread gave up the processor because it was finishing,
// 	we need to delete its carcass.  Note we cannot delete the thread
// 	before now (for example, in Thread::Finish()), because up to this
// 	point, we were still running on the old thread's stack!
//----------------------------------------------------------------------

void
Scheduler::CheckToBeDestroyed()
{
    if (toBeDestroyed != NULL) {
        delete toBeDestroyed;
	toBeDestroyed = NULL;
    }
}
 
//----------------------------------------------------------------------
// Scheduler::Print
// 	Print the scheduler state -- in other words, the contents of
//	the ready list.  For debugging.
//----------------------------------------------------------------------
void
Scheduler::Print()
{
    cout << "Ready list contents:\n";
    QueueL3->Apply(ThreadPrint);
}
