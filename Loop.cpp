#include "gb-include.h"

#include "Loop.h"
#include "JobScheduler.h"
#include "UdpServer.h"  // g_udpServer2.makeCallbacks()
#include "HttpServer.h" // g_httpServer.m_tcp.m_numQueued
#include "Profiler.h"
#include "Process.h"
#include "PageParser.h"

#include "Stats.h"

#include <execinfo.h>
#include <sys/auxv.h>

// raised from 5000 to 10000 because we have more UdpSlots now and Multicast
// will call g_loop.registerSleepCallback() if it fails to get a UdpSlot to
// send on.
#define MAX_SLOTS 10000


// TODO: . if signal queue overflows another signal is sent
//       . capture that signal and use poll or something???

// Tricky Gotchas:
// TODO: if an event happens on a TCP fd/socket before we fully accept it
//       we should just register it then call the read callback in case
//       we just missed a ready for reading signal!!!!!
// TODO: signals can be gotten off the queue after we've closed an fd
//       in which case the handler should be removed from Loop's registry
//       BEFORE being closed... so the handler will be NULL... ???
// NOTE: keep in mind that the signals might be delayed or be really fast!

// TODO: don't mask signals, catch them as they arrive? (like in phhttpd)

static int32_t g_missedQuickPolls = 0;
int32_t g_numSigChlds = 0;
int32_t g_numSigPipes = 0;
int32_t g_numSigIOs = 0;
int32_t g_numSigQueues = 0;
int32_t g_numSigOthers = 0;

// a global class extern'd in .h file
Loop g_loop;

// the global niceness
char g_niceness = 0;

// we make sure the same callback/handler is not hogging the cpu when it is
// niceness 0 and we do not interrupt it, so this is a critical check
class UdpSlot *g_callSlot = NULL;

// use this in case we unregister the "next" callback
static Slot *s_callbacksNext;

// free up all our mem
void Loop::reset() {
	if ( m_slots ) {
		log(LOG_DEBUG,"db: resetting loop");
		mfree ( m_slots , MAX_SLOTS * sizeof(Slot) , "Loop" );
	}
	m_slots = NULL;
}

static void sigbadHandler ( int x , siginfo_t *info , void *y ) ;
static void sigpwrHandler ( int x , siginfo_t *info , void *y ) ;
static void sighupHandler ( int x , siginfo_t *info , void *y ) ;
static void sigprofHandler(int signo, siginfo_t *info, void *context);

void Loop::unregisterReadCallback ( int fd, void *state ,
				    void (* callback)(int fd,void *state),
				    bool silent ){
	if ( fd < 0 ) return;
	// from reading
	unregisterCallback ( m_readSlots,fd, state , callback, silent,true );
}

void Loop::unregisterWriteCallback ( int fd, void *state ,
				    void (* callback)(int fd,void *state)){
	// from writing
	unregisterCallback ( m_writeSlots , fd  , state,callback,false,false);
}

void Loop::unregisterSleepCallback ( void *state ,
				     void (* callback)(int fd,void *state)){
	unregisterCallback (m_readSlots,MAX_NUM_FDS,state,callback,false,true);
}

static fd_set s_selectMaskRead;
static fd_set s_selectMaskWrite;

static int s_readFds[MAX_NUM_FDS];
static int32_t s_numReadFds = 0;
static int s_writeFds[MAX_NUM_FDS];
static int32_t s_numWriteFds = 0;

void Loop::unregisterCallback ( Slot **slots , int fd , void *state ,
				void (* callback)(int fd,void *state) ,
				bool silent , bool forReading ) {
	// bad fd
	if ( fd < 0 ) {log(LOG_LOGIC,
			   "loop: fd to unregister is negative.");return;}
	// set a flag if we found it
	bool found = false;
	// slots is m_readSlots OR m_writeSlots
	Slot *s        = slots [ fd ];
	Slot *lastSlot = NULL;
	// . keep track of new min tick for sleep callbacks
	// . sleep a min of 40ms so g_now is somewhat up to date
	int32_t min     = 40; // 0x7fffffff;
	int32_t lastMin = min;

	// chain through all callbacks registerd with this fd
	while ( s ) {
		// get the next slot (NULL if no more)
		Slot *next = s->m_next;
		// if we're unregistering a sleep callback
		// we might have to recalculate m_minTick
		if ( s->m_tick < min ) { lastMin = min; min = s->m_tick; }
		// skip this slot if callbacks don't match
		if ( s->m_callback != callback ) { lastSlot = s; goto skip; }
		// skip this slot if states    don't match
		if ( s->m_state    != state    ) { lastSlot = s; goto skip; }
		// free this slot since it callback matches "callback"
		//mfree ( s , sizeof(Slot) , "Loop" );
		returnSlot ( s );
		found = true;
		// if the last one, then remove the FD from s_fdList
		// so and clear a bit so doPoll() function is fast
		if ( slots[fd] == s && s->m_next == NULL ) {
			for (int32_t i = 0; i < s_numReadFds ; i++ ) {
				if ( ! forReading ) break;
				if ( s_readFds[i] != fd ) continue;
				s_readFds[i] = s_readFds[s_numReadFds-1];
				s_numReadFds--;
				// remove from select mask too
				FD_CLR(fd,&s_selectMaskRead );
				if ( g_conf.m_logDebugLoop ||
				     g_conf.m_logDebugTcp )
					log("loop: unregistering read "
					    "callback for fd=%i",fd);
				break;
			}
			for (int32_t i = 0; i < s_numWriteFds ; i++ ) {
				if ( forReading ) break;
			 	if ( s_writeFds[i] != fd ) continue;
			 	s_writeFds[i] = s_writeFds[s_numWriteFds-1];
			 	s_numWriteFds--;
			 	// remove from select mask too
			 	FD_CLR(fd,&s_selectMaskWrite);
				if ( g_conf.m_logDebugLoop ||
				     g_conf.m_logDebugTcp )
					log("loop: unregistering write "
					    "callback for fd=%" PRId32" from "
					    "write #wrts=%" PRId32,
					    (int32_t)fd,
					    (int32_t)s_numWriteFds);
			 	break;
			}
		}
		// debug msg
		//log("Loop::unregistered fd=%" PRId32" state=%" PRIu32, fd, (int32_t)state );
		// revert back to old min if this is the Slot we're removing
		min = lastMin;
		// excise the previous slot from linked list
		if   ( lastSlot ) lastSlot->m_next = next;
		else              slots[fd]        = next;
		// watch out if we're in the previous callback, we need to
		// fix the linked list in callCallbacks_ass
		if ( s_callbacksNext == s ) s_callbacksNext = next;
	skip:
		// advance to the next slot
		s = next;
	}
	// set our new minTick if we were unregistering a sleep callback
	if ( fd == MAX_NUM_FDS ) {
		m_minTick = min;
	}

	return;
}

bool Loop::registerReadCallback  ( int fd,
				   void *state,
				   void (* callback)(int fd,void *state ) ,
				   int32_t  niceness ) {
	// the "true" answers the question "for reading?"
	if ( addSlot ( true, fd, state, callback, niceness ) ) return true;
	return log("loop: Unable to register read callback.");
}


bool Loop::registerWriteCallback ( int fd,
				   void *state,
				   void (* callback)(int fd, void *state ) ,
				   int32_t  niceness ) {
	// the "false" answers the question "for reading?"
	if ( addSlot ( false, fd, state, callback, niceness ) )return true;
	return log("loop: Unable to register write callback.");
}

// tick is in milliseconds
bool Loop::registerSleepCallback ( int32_t tick ,
				   void *state,
				   void (* callback)(int fd,void *state ) ,
				   int32_t niceness ) {
	if ( ! addSlot ( true, MAX_NUM_FDS, state, callback , niceness ,tick) )
		return log("loop: Unable to register sleep callback");
	if ( tick < m_minTick ) m_minTick = tick;
	return true;
}

// . returns false and sets g_errno on error
bool Loop::addSlot ( bool forReading , int fd, void *state,
		     void (* callback)(int fd, void *state), int32_t niceness ,
		     int32_t tick ) {

	// ensure fd is >= 0
	if ( fd < 0 ) {
		g_errno = EBADENGINEER;
		return log(LOG_LOGIC,"loop: fd to register is negative.");
	}
	// sanity
	if ( fd > MAX_NUM_FDS ) {
		log("loop: bad fd of %" PRId32,(int32_t)fd);
		char *xx=NULL;*xx=0;
	}
	// debug note
	if (  forReading && (g_conf.m_logDebugLoop || g_conf.m_logDebugTcp) )
		log("loop: registering read callback sd=%i",fd);
	else if ( g_conf.m_logDebugLoop || g_conf.m_logDebugTcp )
		log("loop: registering write callback sd=%i",fd);

	// . ensure fd not already registered with this callback/state
	// . prevent dups so you can keep calling register w/o fear
	Slot *s;
	if ( forReading ) s = m_readSlots  [ fd ];
	else              s = m_writeSlots [ fd ];
	while ( s ) {
		if ( s->m_callback == callback &&
		     s->m_state    == state      ) {
			// don't set g_errno for this anymore, just bitch
			//g_errno = EBADENGINEER;
			log(LOG_LOGIC,"loop: fd=%i is already registered.",fd);
			return true;
		}
		s = s->m_next;
	}
	// . make a new slot
	// . TODO: implement mprimealloc() to pre-alloc slots for us for speed
	//s = (Slot *) mmalloc ( sizeof(Slot ) ,"Loop");
	s = getEmptySlot ( );
	if ( ! s ) return false;
	// for pointing to slot already in position for fd
	Slot *next ;
	// store ourselves in the slot for this fd
	if ( forReading ) {
		next = m_readSlots [ fd ];
		m_readSlots  [ fd ] = s;
		// if not already registered, add to list
		if ( fd<MAX_NUM_FDS && ! FD_ISSET ( fd,&s_selectMaskRead ) ) {
			s_readFds[s_numReadFds++] = fd;
			FD_SET ( fd,&s_selectMaskRead  );
			// sanity
			if ( s_numReadFds>MAX_NUM_FDS){char *xx=NULL;*xx=0;}
		}
		// fd == MAX_NUM_FDS if it's a sleep callback
		//if ( fd < MAX_NUM_FDS ) {
		//FD_SET ( fd , &m_readfds   );
		//FD_SET ( fd , &m_exceptfds );
		//}
	}
	else {
	 	next = m_writeSlots [ fd ];
	 	m_writeSlots [ fd ] = s;
	 	//FD_SET ( fd , &m_writefds );
	 	// if not already registered, add to list
	 	if ( fd<MAX_NUM_FDS && ! FD_ISSET ( fd,&s_selectMaskWrite ) ) {
	 		s_writeFds[s_numWriteFds++] = fd;
	 		FD_SET ( fd,&s_selectMaskWrite  );
	 		// sanity
	 		if ( s_numWriteFds>MAX_NUM_FDS){char *xx=NULL;*xx=0;}
	 	}
	}
	// set our callback and state
	s->m_callback  = callback;
	s->m_state     = state;
	// point to the guy that was registered for fd before us
	s->m_next      = next;
	// save our niceness for doPoll()
	s->m_niceness  = niceness;
	// store the tick for sleep wrappers (should be max for others)
	s->m_tick      = tick;
	// and the last called time for sleep wrappers only really
	if ( fd == MAX_NUM_FDS ) s->m_lastCall = gettimeofdayInMilliseconds();
	//isj: above comment is bogus. Proved with valgrind
	s->m_lastCall = gettimeofdayInMilliseconds();
	// debug msg
	//log("Loop::registered fd=%i state=%" PRIu32,fd,state);
	// if fd == MAX_NUM_FDS if it's a sleep callback
	if ( fd == MAX_NUM_FDS ) return true;
	// watch out for big bogus fds used for thread exit callbacks
	if ( fd >  MAX_NUM_FDS ) return true;
	// set fd non-blocking
	return setNonBlocking ( fd , niceness ) ;
}

// . now make sure we're listening for an interrupt on this fd
// . set it non-blocing and enable signal catching for it
// . listen for an interrupt for this fd
bool Loop::setNonBlocking ( int fd , int32_t niceness ) {
 retry:
	int flags = fcntl ( fd , F_GETFL ) ;
	if ( flags < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry;
		g_errno = errno;
		return log("loop: fcntl(F_GETFL): %s.",strerror(errno));
	}
 retry9:
	if ( fcntl ( fd, F_SETFL, flags|O_NONBLOCK|O_ASYNC) < 0 ) {
		// valgrind
		if ( errno == EINTR ) goto retry9;
		g_errno = errno;
		return log("loop: fcntl(NONBLOCK): %s.",strerror(errno));
	}

	// we use select()/poll now so skip stuff below
	return true;
}

// . if "forReading" is true  call callbacks registered for reading on "fd"
// . if "forReading" is false call callbacks registered for writing on "fd"
// . if fd is MAX_NUM_FDS and "forReading" is true call all sleepy callbacks
void Loop::callCallbacks_ass ( bool forReading , int fd , int64_t now ,
			       int32_t niceness ) {
	// save the g_errno to send to all callbacks
	int saved_errno = g_errno;
	// get the first Slot in the chain that is waiting on this fd
	Slot *s ;
	if ( forReading ) s = m_readSlots  [ fd ];
	else              s = m_writeSlots [ fd ];
	//s = m_readSlots [ fd ];
	// ensure we called something
	int32_t numCalled = 0;

	// a hack fix
	if ( niceness == -1 && m_inQuickPoll ) niceness = 0;

	// . now call all the callbacks
	// . most will re-register themselves (i.e. call registerCallback...()
	while ( s ) {
		// skip this slot if he has no callback
		if ( ! s->m_callback ) continue;
		// NOTE: callback can unregister fd for Slot s, so get next
		//Slot *next = s->m_next;
		s_callbacksNext = s->m_next;
		// watch out if clock was set back
		if ( s->m_lastCall > now ) s->m_lastCall = now;
		// if we're a sleep callback, check to make sure not premature
		if ( fd == MAX_NUM_FDS && s->m_lastCall + s->m_tick > now ) {
			s = s_callbacksNext; continue; }
		// skip if not a niceness match
		if ( niceness == 0 && s->m_niceness != 0 ) {
			s = s_callbacksNext; continue; }
		// update the lastCall timestamp for this slot
		if ( fd == MAX_NUM_FDS ) s->m_lastCall = now;
		// . debug msg
		// . this is called a lot cuz we process all dgrams/whatever
		//   in one clump so there's a lot of redundant signals
		//if ( g_conf.m_logDebugUdp && fd != 1024 )
		//	log("Loop::callCallbacks_ass: for fd=%" PRId32" state=%" PRIu32,
		//	    fd,(int32_t)s->m_state);
		// do the callback

		// log it now
		if (  g_conf.m_logDebugLoop )
			log(LOG_DEBUG,"loop: enter fd callback fd=%" PRId32" "
			    "nice=%" PRId32,(int32_t)fd,(int32_t)s->m_niceness);

		// sanity check. -1 no longer supported
		if ( s->m_niceness < 0 ) { char *xx=NULL;*xx=0; }

		// Temporarily (for the duration of the callback call) switch
		// niceness to the niceness of the slot
		int32_t saved_niceness = g_niceness;
		g_niceness = s->m_niceness;
		// make sure not 2
		if ( g_niceness >= 2 ) g_niceness = 1;

		s->m_callback ( fd , s->m_state );

		// restore niceness
		g_niceness = saved_niceness;

		// log it now
		if ( g_conf.m_logDebugLoop )
			log(LOG_DEBUG,"loop: exit fd callback fd=%" PRId32" "
			    "nice=%" PRId32, (int32_t)fd,(int32_t)s->m_niceness);

		// . debug msg
		// . this is called a lot cuz we process all dgrams/whatever
		//   in one clump so there's a lot of redundant signals
		//if ( g_conf.m_logDebugUdp && fd != 1024 )
		//	log("Loop::callCallbacks_ass: back");
		// inc the flag
		numCalled++;
		// reset g_errno so all callbacks for this fd get same g_errno
		g_errno = saved_errno;
		// get the next n (will be -1 if no slot after it)
		s = s_callbacksNext;
	}
	s_callbacksNext = NULL;
}

Loop::Loop ( ) {
	m_inQuickPoll      = false;
	m_needsToQuickPoll = false;
	m_canQuickPoll     = false;
	m_isDoingLoop      = false;

	// set all callbacks to NULL so we know they're empty
	for ( int32_t i = 0 ; i < MAX_NUM_FDS+2 ; i++ ) {
		m_readSlots [i] = NULL;
		m_writeSlots[i] = NULL;
	}
	// the extra sleep slots
	//m_readSlots [ MAX_NUM_FDS ] = NULL;
	m_slots = NULL;
	m_pipeFd[0] = -1;
	m_pipeFd[1] = -1;
}

// free all slots from addSlots
Loop::~Loop ( ) {
	reset();
	if(m_pipeFd[0]>=0) {
		close(m_pipeFd[0]);
		m_pipeFd[0] = -1;
	}
	if(m_pipeFd[1]>=0) {
		close(m_pipeFd[1]);
		m_pipeFd[1] = -1;
	}
}

// returns NULL and sets g_errno if none are left
Slot *Loop::getEmptySlot ( ) {
	Slot *s = m_head;
	if ( ! s ) {
		g_errno = EBUFTOOSMALL;
		log("loop: No empty slots available. "
		    "Increase #define MAX_SLOTS.");
		return NULL;
	}
	m_head = s->m_nextAvail;
	return s;
}

void Loop::returnSlot ( Slot *s ) {
	s->m_nextAvail = m_head;
	m_head = s;
}


// . come here when we get a GB_SIGRTMIN+X signal etc.
// . do not call anything from here because the purpose of this is to just
//   queue the signals up and DO DEDUPING which linux does not do causing
//   the sigqueue to overflow.
// . we should break out of the sleep loop after the signal is handled
//   so we can handle/process the queued signals properly. 'man sleep'
//   states "sleep()  makes  the  calling  process  sleep until seconds
//   seconds have elapsed or a signal arrives which is not ignored."
void sigHandlerQueue_r ( int x , siginfo_t *info , void *v ) {

	// if we just needed to cleanup a thread
	if ( info->si_signo == SIGCHLD ) {
		g_numSigChlds++;
		return;
	}

	if ( info->si_signo == SIGPIPE ) {
		g_numSigPipes++;
		return;
	}

	if ( info->si_signo == SIGIO ) {
		g_numSigIOs++;
		return;
	}

	if ( info->si_code == SI_QUEUE ) {
		g_numSigQueues++;
		//log("admin: got sigqueue");
		return;
	}

	// wtf is this?
	g_numSigOthers++;

	// the stuff below should no longer be used since we
	// do not use F_SETSIG now
	return;
}



bool Loop::init ( ) {

	// clear this up here before using in doPoll()
	FD_ZERO(&s_selectMaskRead);
	FD_ZERO(&s_selectMaskWrite);

	// set-up wakeup pipe
	if(pipe(m_pipeFd)!=0) {
		log(LOG_ERROR,"pipe() failed with errno=%d",errno);
		return false;
	}
	setNonBlocking(m_pipeFd[0],0);
	setNonBlocking(m_pipeFd[1],0);
	FD_SET(m_pipeFd[0],&s_selectMaskRead);

	// sighupHandler() will set this to true so we know when to shutdown
	m_shutdown  = 0;
	// . reset this cuz we have no sleep callbacks right now
	// . sleep a min of 40ms so g_now is somewhat up to date
	m_minTick = 40; //0x7fffffff;
	// reset the need to poll flag
	m_needToPoll = false;
	// make slots
	m_slots = (Slot *) mmalloc ( MAX_SLOTS * (int32_t)sizeof(Slot) , "Loop" );
	if ( ! m_slots ) return false;
	// log it
	log(LOG_DEBUG,"loop: Allocated %" PRId32" bytes for %" PRId32" callbacks.",
	     MAX_SLOTS * (int32_t)sizeof(Slot),(int32_t)MAX_SLOTS);
	// init link list ptr
	for ( int32_t i = 0 ; i < MAX_SLOTS - 1 ; i++ ) {
		m_slots[i].m_nextAvail = &m_slots[i+1];
	}
	m_slots[MAX_SLOTS - 1].m_nextAvail = NULL;
	m_head = &m_slots[0];
	m_tail = &m_slots[MAX_SLOTS - 1];
	// an innocent log msg
	//log ( 0 , "Loop: starting the i/o loop");
	// . when using threads GB_SIGRTMIN becomes 35, not 32 anymore
	//   since threads use these signals to reactivate suspended threads
	// . debug msg
	//log("admin: GB_SIGRTMIN=%" PRId32, (int32_t)GB_SIGRTMIN );
	// . block the GB_SIGRTMIN signal
	// . anytime this is raised it goes onto the signal queue
	// . we use sigtimedwait() to get signals off the queue
	// . sigtimedwait() selects the lowest signo first for handling
	// . therefore, GB_SIGRTMIN is higher priority than (GB_SIGRTMIN + 1)
	//sigfillset ( &sigs );
	// set of signals to block
	sigset_t sigs;
	sigemptyset ( &sigs                );
	sigaddset   ( &sigs , SIGPIPE      ); //if we write to a close socket
	sigaddset   ( &sigs , SIGCHLD      );

	// now since we took out SIGIO... (see below)
	// we should ignore this signal so it doesn't suddenly stop the gb
	// process since we took out the SIGIO handler because newer kernels
	// were throwing SIGIO signals ALL the time, on every datagram
	// send/receive it seemed and bogged us down.
	sigaddset   ( &sigs , SIGIO );
	// . block on any signals in this set (in addition to current sigs)
	// . use SIG_UNBLOCK to remove signals from block list
	// . this returns -1 and sets g_errno on error
	// . we block a signal so it does not interrupt us, then we can
	//   take it off using our call to sigtimedwait()
	// . allow it to interrupt us now and we will queue it ourselves
	//   to prevent the linux queue from overflowing
	// . see 'cat /proc/<pid>/status | grep SigQ' output to see if
	//   overflow occurs. linux does not dedup the signals so when a
	//   host cpu usage hits 100% it seems to miss a ton of signals.
	//   i suspect the culprit is pthread_create() so we need to get
	//   thread pools out soon.
	// . now we are handling the signals and queueing them ourselves
	//   so comment out this sigprocmask() call
	// if ( sigprocmask ( SIG_BLOCK, &sigs, 0 ) < 0 ) {
	// 	g_errno = errno;
	// 	return log("loop: sigprocmask: %s.",strerror(g_errno));
	// }
	struct sigaction sa2;
	// . sa_mask is the set of signals that should be blocked when
	//   we're handling the GB_SIGRTMIN, make this empty
	// . GB_SIGRTMIN signals will be automatically blocked while we're
	//   handling a SIGIO signal, so don't worry about that
	// . what sigs should be blocked when in our handler? the same
	//   sigs we are handling i guess
	sa2.sa_mask = sigs;
	sa2.sa_flags = SA_SIGINFO ; //| SA_ONESHOT;
	// call this function
	sa2.sa_sigaction = sigHandlerQueue_r;
	g_errno = 0;
	if ( sigaction ( SIGPIPE, &sa2, 0 ) < 0 ) g_errno = errno;
	if ( sigaction ( SIGCHLD, &sa2, 0 ) < 0 ) g_errno = errno;
	if ( sigaction ( SIGIO, &sa2, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction(): %s.", mstrerror(g_errno) );

	struct sigaction sa;
	// . sa_mask is the set of signals that should be blocked when
	//   we're handling the signal, make this empty
	// . GB_SIGRTMIN signals will be automatically blocked while we're
	//   handling a SIGIO signal, so don't worry about that
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO ; // | SA_ONESHOT;

	// handle HUP signals gracefully by saving and shutting down
	sa.sa_sigaction = sighupHandler;
	if ( sigaction ( SIGHUP , &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGHUP: %s.", mstrerror(errno));
	if ( sigaction ( SIGTERM, &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGTERM: %s.", mstrerror(errno));
	// if ( sigaction ( SIGABRT, &sa, 0 ) < 0 ) g_errno = errno;
	// if ( g_errno ) log("loop: sigaction SIGTERM: %s.",mstrerror(errno));

	// we should save our data on segv, sigill, sigfpe, sigbus
	sa.sa_sigaction = sigbadHandler;
	if ( sigaction ( SIGSEGV, &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGSEGV: %s.", mstrerror(errno));
	if ( sigaction ( SIGILL , &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGILL: %s.", mstrerror(errno));
	if ( sigaction ( SIGFPE , &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGFPE: %s.", mstrerror(errno));
	if ( sigaction ( SIGBUS , &sa, 0 ) < 0 ) g_errno = errno;
	if ( g_errno ) log("loop: sigaction SIGBUS: %s.", mstrerror(errno));
	// if ( sigaction ( SIGQUIT , &sa, 0 ) < 0 ) g_errno = errno;
	// if ( g_errno ) log("loop: sigaction SIGBUS: %s.", mstrerror(errno));
	// if ( sigaction ( SIGSYS , &sa, 0 ) < 0 ) g_errno = errno;
	// if ( g_errno ) log("loop: sigaction SIGBUS: %s.", mstrerror(errno));


	// if the UPS is about to go off it sends a SIGPWR
	sa.sa_sigaction = sigpwrHandler;
	if ( sigaction ( SIGPWR, &sa, 0 ) < 0 ) g_errno = errno;


	//SIGPROF is used by the profiler
	sa.sa_sigaction = sigprofHandler;
	if ( sigaction ( SIGPROF, &sa, NULL ) < 0 ) g_errno = errno;
	// setitimer(ITIMER_PROF...) is called when profiling is enabled/disabled
	// it has noticeable overhead so it must not be enabled by default.

	// success
	return true;
}

// TODO: if we get a segfault while saving, what then?
void sigpwrHandler ( int x , siginfo_t *info , void *y ) {
	// let main process know to shutdown
	g_loop.m_shutdown = 3;
}

void printStackTrace (bool print_location) {
	logf(LOG_ERROR, "gb: Printing stack trace");

	if ( g_inMemFunction ) {
		logf(LOG_ERROR, "gb: in mem function not doing backtrace");
		return;
	}

	static void *s_bt[200];
	size_t sz = backtrace(s_bt, 200);

	// find ourself
	const char* process = (const char*)getauxval(AT_EXECFN);

	for( size_t i = 0; i < sz; ++i ) {
		char cmd[256];
		sprintf(cmd,"addr2line -e %s 0x%" PRIx64, process, (uint64_t)s_bt[i]);
		logf(LOG_ERROR, "%s", cmd);

		if (print_location) {
			FILE *output = gbpopen(cmd);
			if ( output ) {
				while ( ! feof(output) ) {
					char buf[256];
					if ( fgets(buf, 256, output) ) {
						buf[strlen(buf) - 1] = '\0';
						logf(LOG_ERROR, "%s", buf);
					}
				}
			}
		}
	}
}


// TODO: if we get a segfault while saving, what then?
void sigbadHandler ( int x , siginfo_t *info , void *y ) {

	// turn off sigalarms
	g_loop.disableQuickpollTimer();

	log("loop: sigbadhandler. disabling handler from recall.");
	// . don't allow this handler to be called again
	// . does this work if we're in a thread?
	struct sigaction sa;
	sigemptyset (&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO ; //| SA_ONESHOT;
	sa.sa_sigaction = NULL;
	sigaction ( SIGSEGV, &sa, 0 ) ;
	sigaction ( SIGILL , &sa, 0 ) ;
	sigaction ( SIGFPE , &sa, 0 ) ;
	sigaction ( SIGBUS , &sa, 0 ) ;
	sigaction ( SIGQUIT, &sa, 0 ) ;
	sigaction ( SIGSYS , &sa, 0 ) ;
	// if we've already been here, or don't need to be, then bail
	if ( g_loop.m_shutdown ) {
		log("loop: sigbadhandler. shutdown already called.");
		return;
	}

	// unwind
	printStackTrace();

	// if we're a thread, let main process know to shutdown
	g_loop.m_shutdown = 2;
	log("loop: sigbadhandler. trying to save now. mode=%" PRId32, (int32_t)g_process.m_mode);

	// . this will save all Rdb's
	// . if "urgent" is true it will dump core
	// . if "urgent" is true it won't broadcast its shutdown to all hosts
	g_process.shutdown ( true );
}

static void sigprofHandler(int signo, siginfo_t *info, void *context)
{
	//This is called on SIGPROF meaning that profiling is enabled
	g_profiler.getStackFrame();
}

// shit, we can't make this realtime!! RdbClose() cannot be called by a
// real time sig handler
void sighupHandler ( int x , siginfo_t *info , void *y ) {
	// let main process know to shutdown
	g_loop.m_shutdown = 1;
}

// . keep a timestamp for the last time we called the sleep callbacks
// . we have to call those every 1 second
static int64_t s_lastTime = 0;

void Loop::runLoop ( ) {

	// set of signals to watch for
	sigset_t sigs0;

	// clear all signals from the set
	sigemptyset ( &sigs0 );

	// . set sigs on which sigtimedwait() listens for
	// . add this signal to our set of signals to watch (currently NONE)
	sigaddset ( &sigs0, SIGPIPE      );
	sigaddset ( &sigs0, SIGCHLD      );
	// . TODO: do we need to mask SIGIO too? (sig queue overflow?)
	// . i would think so, because what if we tried to queue an important
	//   handler to be called in the high priority UdpServer but the queue
	//   was full? Then we would finish processing the signals on the queue
	//   before we would address the excluded high priority signals by
	//   calling doPoll()
	sigaddset ( &sigs0, SIGIO );

	s_lastTime = 0;

	m_isDoingLoop = true;

	// . now loop forever waiting for signals
	// . but every second check for timer-based events
	for (;;) {
		m_lastPollTime = gettimeofdayInMilliseconds();
		m_needsToQuickPoll = false;

		g_errno = 0;

		if ( m_shutdown ) {
			// a msg
			if (m_shutdown == 1) {
				log(LOG_INIT,"loop: got SIGHUP or SIGTERM.");
			} else if (m_shutdown == 2) {
				log(LOG_INIT,"loop: got SIGBAD in thread.");
			} else {
				log(LOG_INIT,"loop: got SIGPWR.");
			}

			// . turn off interrupts here because it doesn't help to do
			//   it in the thread
			// . TODO: turn off signals for sigbadhandler()
			// if thread got the signal, just wait for him to save all
			// Rdbs and then dump core
			if ( m_shutdown == 2 ) {
				//log(0,"Thread is saving & shutting down urgently.");
				//log("loop: Resuming despite thread crash.");
				//m_shutdown = 0;
				//goto BIGLOOP;
			}

			// otherwise, thread did not save, so we must do it
			log ( LOG_INIT ,"loop: Saving and shutting down urgently.");

			g_process.shutdown ( true );
		}

		//
		//
		// THE HEART OF GB. process events/signals on FDs.
		//
		//
		doPoll();
	}
}

//--- TODO: flush the signal queue after polling until done
//--- are we getting stale signals resolved by flush so we get
//--- read event on a socket that isnt in read mode???
// TODO: set signal handler to SIG_DFL to prevent signals from queuing up now
// . this handles high priority fds first (lowest niceness)
void Loop::doPoll ( ) {
	// set time
	//g_now = gettimeofdayInMilliseconds();

	// . turn it off here so it can be turned on again after we've
	//   called select() so we don't lose any fd events through the cracks
	// . some callbacks we call make trigger another SIGIO, but if they
	//   fail they should set Loop::g_needToPoll to true
	m_needToPoll = false;

	// debug msg
	//if ( g_conf.m_logDebugLoop ) log(LOG_DEBUG,"loop: Entered doPoll.");
	if ( g_conf.m_logDebugLoop) log(LOG_DEBUG,"loop: Entered doPoll.");

	if(g_udpServer.needBottom()) g_udpServer.makeCallbacks_ass ( 1 );

	int32_t n;

	timeval v;
	v.tv_sec  = 0;
	if ( m_inQuickPoll ) v.tv_usec = 0;
	// 10ms for sleepcallbacks so they can be called...
	// and we need this to be the same as sigalrmhandler() since we
	// keep track of cpu usage here too, since sigalrmhandler is "VT"
	// based it only goes off when that much "cpu time" has elapsed.
	else                 v.tv_usec = QUICKPOLL_INTERVAL * 1000;

 again:

	// gotta copy to our own since bits get cleared by select() function
	fd_set readfds = s_selectMaskRead;
	fd_set writefds = s_selectMaskWrite;

	if ( g_conf.m_logDebugLoop )
		log("loop: in select");

	// . poll the fd's searching for socket closes
	// . the sigalrms and sigvtalrms and SIGCHLDs knock us out of this
	//   select() with n < 0 and errno equal to EINTR.
	// . crap the sigalarms kick us out here every 1ms. i noticed
	//   then when running disableTimer() above and we don't get
	//   any EINTRs... can we mask those out here? it only seems to be
	//   the SIGALRMs not the SIGVTALRMs that interrupt us.
	n = select (MAX_NUM_FDS,
		    &readfds,
		    &writefds,
		    NULL,//&exceptfds,
		    &v );

	if ( n >= 0 ) errno = 0;

	if ( g_conf.m_logDebugLoop )
		log("loop: out select n=%" PRId32" errno=%" PRId32" errnomsg=%s "
		    "ms_wait=%i",
		    (int32_t)n,(int32_t)errno,mstrerror(errno),
		    (int)v.tv_sec*1000);

	if ( n < 0 ) {
		// valgrind
		if ( errno == EINTR ) {
			// got it. if we get a sig alarm or vt alarm or
			// SIGCHLD (from Threads.cpp) we end up here.
			//log("loop: got errno=%" PRId32,(int32_t)errno);
			// if not linux we have to decrease this by 1ms
			//count -= 1000;
			// and re-assign to wait less time. we are
			// assuming SIGALRM goes off once per ms and if
			// that is not what interrupted us we may end
			// up exiting early
			//if ( count <= 0 && m_shutdown ) return;
			// wait less this time around
			//v.tv_usec = count;
			// if shutting down was it a sigterm ?
			if ( m_shutdown ) goto again;
			// handle returned threads for niceness 0
			g_jobScheduler.cleanup_finished_jobs();
			if ( m_inQuickPoll ) goto again;
			// high niceness threads
			g_jobScheduler.cleanup_finished_jobs();

			goto again;
		}
		g_errno = errno;
		log("loop: select: %s.",strerror(g_errno));
		return;
	}

	// if we wait for 10ms with nothing happening, fix cpu usage here too
	// if ( n == 0 ) {
	// 	Host *h = g_hostdb.m_myHost;
	// 	h->m_cpuUsage = .99 * h->m_cpuUsage + .01 * 000;
	// }

	// debug msg
	if ( g_conf.m_logDebugLoop)
		logf(LOG_DEBUG,"loop: Got %" PRId32" fds waiting.",n);

	for ( int32_t i = 0 ;
	      (g_conf.m_logDebugLoop || g_conf.m_logDebugTcp) && i<MAX_NUM_FDS;
	      i++){
	  	// continue if not set for reading
		 if ( FD_ISSET ( i , &readfds ) )
			 log("loop: fd=%" PRId32" is on for read qp=%i",i,
			     (int)m_inQuickPoll);
	 	if ( FD_ISSET ( i , &writefds ) )
			log("loop: fd=%" PRId32" is on for write qp=%i",i,
			    (int)m_inQuickPoll);

	 	// if niceness is not -1, handle it below
	}

	// a Slot ptr
	Slot *s;

	// handle returned threads for niceness 0
	g_jobScheduler.cleanup_finished_jobs();


	bool calledOne = false;
	const int64_t now = gettimeofdayInMilliseconds();

	if(n>0 && FD_ISSET(m_pipeFd[0],&readfds)) {
		//drain the wakeup pipe
		char buf[32];
		(void)read(m_pipeFd[0],buf,sizeof(buf));
		n--;
		FD_CLR(m_pipeFd[0],&readfds);
	}
	
	// now keep this fast, too. just check fds we need to.
	for ( int32_t i = 0 ; i < s_numReadFds ; i++ ) {
		if ( n == 0 ) break;
		int fd = s_readFds[i];
	 	s = m_readSlots  [ fd ];
	 	// if niceness is not 0, handle it below
		if ( s && s->m_niceness > 0 ) continue;
		// must be set
		if ( ! FD_ISSET ( fd , &readfds ) ) continue;
		if ( g_conf.m_logDebugLoop || g_conf.m_logDebugTcp )
			log("loop: calling cback0 niceness=%" PRId32" "
			    "fd=%i", s->m_niceness , fd );
		calledOne = true;
		callCallbacks_ass (true,fd, now,0);//read?
	}
	for ( int32_t i = 0 ; i < s_numWriteFds ; i++ ) {
		if ( n == 0 ) break;
		int fd = s_writeFds[i];
	 	s = m_writeSlots  [ fd ];
	 	// if niceness is not 0, handle it below
		if ( s && s->m_niceness > 0 ) continue;
		// fds are always ready for writing so take this out.
		if ( ! FD_ISSET ( fd , &writefds ) ) continue;
		if ( g_conf.m_logDebugLoop || g_conf.m_logDebugTcp )
			log("loop: calling wcback0 niceness=%" PRId32" fd=%i"
			    , s->m_niceness , fd );
		calledOne = true;
		callCallbacks_ass (false,fd, now,0);//false=forRead?
	}

	// handle returned threads for niceness 0
	g_jobScheduler.cleanup_finished_jobs();

	//
	// the stuff below is not super urgent, do not do if in quickpoll
	//
	if ( m_inQuickPoll ) return;

	// now for lower priority fds
	for ( int32_t i = 0 ; i < s_numReadFds ; i++ ) {
		if ( n == 0 ) break;
		int fd = s_readFds[i];
	 	s = m_readSlots  [ fd ];
	  	// if niceness is <= 0 we did it above
		if ( s && s->m_niceness <= 0 ) continue;
		// must be set
		if ( ! FD_ISSET ( fd , &readfds ) ) continue;
		if ( g_conf.m_logDebugLoop || g_conf.m_logDebugTcp )
			log("loop: calling cback1 niceness=%" PRId32" "
			    "fd=%i", s->m_niceness , fd );
		calledOne = true;
		callCallbacks_ass (true,fd, now,1);//read?
	}

	for ( int32_t i = 0 ; i < s_numWriteFds ; i++ ) {
		if ( n == 0 ) break;
	 	int fd = s_writeFds[i];
	  	s = m_writeSlots  [ fd ];
	  	// if niceness is <= 0 we did it above
	 	if ( s && s->m_niceness <= 0 ) continue;
	 	// must be set
	 	if ( ! FD_ISSET ( fd , &writefds ) ) continue;
		if ( g_conf.m_logDebugLoop || g_conf.m_logDebugTcp )
			log("loop: calling wcback1 niceness=%" PRId32" "
			    "fd=%i", s->m_niceness , fd );
		calledOne = true;
		callCallbacks_ass (false,fd, now,1);//forread?
	}

	// handle returned threads for all other nicenesses
	g_jobScheduler.cleanup_finished_jobs();

	// call sleepers if they need it
	// call this every (about) 1 second
	int32_t elapsed = gettimeofdayInMilliseconds() - s_lastTime;
	// if someone changed the system clock on us, this could be negative
	// so fix it! otherwise, times may NEVER get called in our lifetime
	if ( elapsed < 0 ) elapsed = m_minTick;
	if ( elapsed >= m_minTick ) {
		// MAX_NUM_FDS is the fd for sleep callbacks
		callCallbacks_ass ( true , MAX_NUM_FDS , gettimeofdayInMilliseconds() );
		// note the last time we called them
		s_lastTime = gettimeofdayInMilliseconds();
		// handle returned threads for all other nicenesses
		g_jobScheduler.cleanup_finished_jobs();
	}
	// debug msg
	if ( g_conf.m_logDebugLoop ) log(LOG_DEBUG,"loop: Exited doPoll.");
}

// for FileState class
#include "BigFile.h"


void Loop::quickPoll(int32_t niceness, const char* caller, int32_t lineno) {
	if ( ! g_conf.m_useQuickpoll ) return;

	// convert
	//if ( niceness > 1 ) niceness = 1;

	// sometimes we init HashTableX's with a niceness of 0 even though
	// g_niceness is 1. so prevent a core below.
	if ( niceness == 0 ) return;

	// sanity check
	if ( g_niceness > niceness ) {
		log(LOG_WARN,"loop: niceness mismatch!");
		//char *xx=NULL;*xx=0; }
	}

	// sanity -- temporary -- no quickpoll in a thread!!!
	//if ( g_threads.amThread() ) { char *xx=NULL;*xx=0; }

	// if we are niceness 1 and not in a handler, make it niceness 2
	// so the handlers can be answered and we don't slow other
	// spiders down and we don't slow turks' injections down as much
	if ( ! g_inHandler && niceness == 1 ) niceness = 2;

	// reset this
	g_missedQuickPolls = 0;

	if(m_inQuickPoll) {
		log(LOG_WARN,
		    "admin: tried to quickpoll from inside quickpoll");
		// this happens when handleRequest3f is called from
		// a quickpoll and it deletes a collection and BigFile::close
		// calls ThreadQueue::removeThreads and Msg3::doneScanning()
		// has niceness 2 and calls quickpoll again!
		return;
	}
	int64_t now = gettimeofdayInMilliseconds();
	//int64_t took = now - m_lastPollTime;
	int64_t now2 = gettimeofdayInMilliseconds();
	int32_t gerrno = g_errno;

	m_inQuickPoll = true;

	// doPoll() will since we are in quickpoll and only call niceness 0
	// callbacks for all the fds. and it will set the timer to 0.
	doPoll ();

	// reset this again
	g_missedQuickPolls = 0;

	// . avoid quickpolls within a quickpoll
	// . was causing seg fault in diskHeartbeatWrapper()
	//   which call Threads::bailOnReads()
	m_canQuickPoll = false;

	// . call sleepcallbacks, like the heartbeat in Process.cpp
	// . MAX_NUM_FDS is the fd for sleep callbacks
	// . specify a niceness of 0 so only niceness 0 sleep callbacks
	//   will be called
	callCallbacks_ass ( true , MAX_NUM_FDS , now , 0 );
	// sanity check
	if ( g_niceness > niceness ) {
		log("loop: niceness mismatch");
	}

	//log(LOG_WARN, "xx quickpolled took %" PRId64", waited %" PRId64" from %s",
	//    now2 - now, now - m_lastPollTime, caller);
	m_lastPollTime = now2;
	m_inQuickPoll = false;
	m_needsToQuickPoll = false;
	m_canQuickPoll = true;
	g_errno = gerrno;

	// reset this again
	g_missedQuickPolls = 0;
}


void Loop::canQuickPoll(int32_t niceness) {
	if(niceness && !m_shutdown) m_canQuickPoll = true;
	else         m_canQuickPoll = false;
}

void Loop::enableQuickpollTimer() {
	m_canQuickPoll = true;
}

void Loop::disableQuickpollTimer() {
	m_canQuickPoll = false;
}


void Loop::wakeupPollLoop() {
	char dummy='d';
	(void)write(m_pipeFd[1],&dummy,1);
}


int gbsystem(char *cmd ) {
	g_loop.disableQuickpollTimer();
	log("gb: running system(\"%s\")",cmd);
	int ret = system(cmd);
	g_loop.enableQuickpollTimer();
	return ret;
}


FILE* gbpopen(char* cmd) {
    // Block everything from interrupting this system call because
    // if there is an alarm or a child thread crashes (pdftohtml)
    // then this will hang forever.
    // We should actually write our own popen so that we do
    // fork, close all fds in the child, then exec.
    // These child processes can hold open the http server and
    // prevent a new gb from running even after it has died.
	g_loop.disableQuickpollTimer();

	sigset_t oldSigs;
	sigset_t sigs;
	sigfillset ( &sigs );

	if ( sigprocmask ( SIG_BLOCK  , &sigs, &oldSigs ) < 0 ) {
		log("build: had error blocking signals for popen");
	}
	FILE* fh = popen(cmd, "r");

	if ( sigprocmask ( SIG_SETMASK  , &oldSigs, NULL ) < 0 ) {
		log("build: had error unblocking signals for popen");
	}

	g_loop.enableQuickpollTimer();
	return fh;
}


