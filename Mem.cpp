#include "gb-include.h"

#include "Mem.h"
#include <sys/time.h>      // setrlimit()

#include "SafeBuf.h"
#include "Pages.h"
#include "ScopedLock.h"

// only Mem.cpp should call ::malloc, everyone else must call mmalloc() so
// we can keep tabs on memory usage.

bool g_inMemFunction = false;

#define sysmalloc ::malloc
#define syscalloc ::calloc
#define sysrealloc ::realloc
#define sysfree ::free

// allocate an extra space before and after the allocated memory to try
// to catch sequential buffer underruns and overruns. if the write is way
// beyond this padding radius, chances are it will seg fault right then and
// there because it will hit a different PAGE, to be more sure we could
// make UNDERPAD and OVERPAD PAGE bytes, although the overrun could still write
// to another allocated area of memory and we can never catch it.
#define UNDERPAD 4
#define OVERPAD  4

static const char MAGICCHAR = (char)0xda;

class Mem g_mem;

static bool freeCacheMem();



static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

// a table used in debug to find mem leaks
static void **s_mptrs ;
static int32_t  *s_sizes ;
static char  *s_labels;
static char  *s_isnew;
static int32_t   s_n = 0;
static bool   s_initialized = 0;

// our own memory manager
void operator delete (void *ptr) throw () {
	logTrace( g_conf.m_logTraceMem, "ptr=%p", ptr );

	// now just call this
	g_mem.gbfree ( (char *)ptr , -1 , NULL );
}

void operator delete [] ( void *ptr ) throw () {
	logTrace( g_conf.m_logTraceMem, "ptr=%p", ptr );

	// now just call this
	g_mem.gbfree ( (char *)ptr , -1 , NULL );
}

#define MINMEM 6000000


void Mem::addnew ( void *ptr , int32_t size , const char *note ) {
	logTrace( g_conf.m_logTraceMem, "ptr=%p size=%" PRId32" note=%s", ptr, size, note );
	// 1 --> isnew
	addMem ( ptr , size , note , 1 );
}

void Mem::delnew ( void *ptr , int32_t size , const char *note ) {
	logTrace( g_conf.m_logTraceMem, "ptr=%p size=%" PRId32" note=%s", ptr, size, note );

	// we don't need to use mdelete() if checking for leaks is enabled
	// because the size of the allocated mem is in the hash table under
	// s_sizes[]. and the delete() operator is overriden below to catch this.
	return;
}

// . global override of new and delete operators
// . seems like constructor and destructor are still called
// . just use to check if enough memory
// . before this just called mmalloc which sometimes returned NULL which 
//   would cause us to throw an unhandled signal. So for now I don't
//   call mmalloc since it is limited in the mem it can use and would often
//   return NULL and set g_errno to ENOMEM
void * operator new (size_t size) throw (std::bad_alloc) {
	logTrace( g_conf.m_logTraceMem, "size=%zu", size );

	// don't let electric fence zap us
	if ( size == 0 ) return (void *)0x7fffffff;

	// . fail randomly
	// . good for testing if we can handle out of memory gracefully
	//static int32_t s_mcount = 0;
	//s_mcount++;
	//if ( s_mcount > 57 && (rand() % 1000) < 2 ) { 
	if ( g_conf.m_testMem && (rand() % 100) < 2 ) { 
		g_errno = ENOMEM; 
		log(LOG_ERROR, "mem: new-fake(%" PRIu32"): %s",(uint32_t)size, mstrerror(g_errno));
		throw std::bad_alloc(); 
		// return NULL; }
	} 

	// hack so hostid #0 can use more mem
	int64_t max = g_conf.m_maxMem;
	//if ( g_hostdb.m_hostId == 0 )  max += 2000000000;

	// don't go over max
	if ( g_mem.m_used + (int32_t)size >= max &&
	     g_conf.m_maxMem > 1000000 ) {
		log("mem: new(%" PRIu32"): Out of memory.", (uint32_t)size );
		throw std::bad_alloc();
		//throw 1;
	}

	g_inMemFunction = true;

	void *mem = sysmalloc ( size );

	g_inMemFunction = false;

	int32_t  memLoop = 0;
newmemloop:
	if ( ! mem && size > 0 ) {
		g_mem.m_outOfMems++;
		g_errno = errno;
		log( LOG_WARN, "mem: new(%" PRId32"): %s",(int32_t)size,mstrerror(g_errno));
		throw std::bad_alloc();
		//return NULL;
	}
	if ( (PTRTYPE)mem < 0x00010000 ) {
		void *remem = sysmalloc(size);
		log ( LOG_WARN, "mem: Caught low memory allocation "
		      "at %08" PTRFMT", "
		      "reallocated to %08" PTRFMT, 
		      (PTRTYPE)mem,
		      (PTRTYPE)remem );
		sysfree(mem);
		mem = remem;
		if ( memLoop > 100 ) {
			log ( LOG_WARN, "mem: Attempted to reallocate low "
					"memory allocation 100 times, "
					"aborting and returning ENOMEM." );
			g_errno = ENOMEM;
			throw std::bad_alloc();
		}
		goto newmemloop;
	}

	g_mem.addMem ( mem , size , "TMPMEM" , 1 );

	return mem;
}

void * operator new [] (size_t size) throw (std::bad_alloc) {
	logTrace( g_conf.m_logTraceMem, "size=%zu", size );

	// don't let electric fence zap us
	if ( size == 0 ) return (void *)0x7fffffff;
	// . fail randomly
	// . good for testing if we can handle out of memory gracefully

	//static int32_t s_count = 0;
	//s_count++;
	//if ( s_count > 3000 && (rand() % 100) < 2 ) { 
	//	g_errno = ENOMEM; 
	//	log("mem: new-fake(%i): %s",size, mstrerror(g_errno));
	//	throw bad_alloc(); 
	//	// return NULL; }
	//} 
	
	int64_t max = g_conf.m_maxMem;

	// don't go over max
	if ( g_mem.m_used + (int32_t)size >= max &&
	     g_conf.m_maxMem > 1000000 ) {
		log("mem: new(%" PRIu32"): Out of memory.", (uint32_t)size );
		throw std::bad_alloc();
		//throw 1;
	}

	g_inMemFunction = true;

	void *mem = sysmalloc ( size );

	g_inMemFunction = false;


	int32_t  memLoop = 0;
newmemloop:
	if ( ! mem && size > 0 ) {
		g_errno = errno;
		g_mem.m_outOfMems++;
		log( LOG_WARN, "mem: new(%" PRIu32"): %s",
		    (uint32_t)size, mstrerror(g_errno));
		throw std::bad_alloc();
		//return NULL;
	}

	if ( (PTRTYPE)mem < 0x00010000 ) {
		void *remem = sysmalloc(size);
		log ( LOG_WARN, "mem: Caught low memory allocation at "
		      "%08" PTRFMT", "
				"reallocated to %08" PTRFMT"", 
		      (PTRTYPE)mem, (PTRTYPE)remem );
		sysfree(mem);
		mem = remem;
		if ( memLoop > 100 ) {
			log ( LOG_WARN, "mem: Attempted to reallocate low "
					"memory allocation 100 times, "
					"aborting and returning ENOMEM." );
			g_errno = ENOMEM;
			throw std::bad_alloc();
		}
		goto newmemloop;
	}

	g_mem.addMem ( (char*)mem , size, "TMPMEM" , 1 );

	return mem;
}


Mem::Mem() {
	m_used = 0;
	// assume large max until this gets set for real
	//m_maxMem  = 50000000;
	m_numAllocated = 0;
	m_numTotalAllocated = 0;
	m_maxAlloc = 0;
	m_maxAllocBy = "";
	m_maxAlloced = 0;
	m_memtablesize = 0;
	// count how many allocs/news failed
	m_outOfMems = 0;
}

Mem::~Mem() {
	if ( getUsedMem() == 0 ) return;
}

int64_t Mem::getMaxMem     () { return g_conf.m_maxMem; }


bool Mem::init  ( ) {
	if ( g_conf.m_detectMemLeaks )
		log(LOG_INIT,"mem: Memory leak checking is enabled.");

	// reset this, our max mem used over time ever because we don't
	// want the mem test we did above to count towards it
	m_maxAlloced = 0;

	// init or own malloc stuff in malloc.c (from doug leay)
	//if ( mdw_init_sbrk ( maxMem ) ) return true;
	// bitch
	//return log("Mem::init: failed to malloc %" PRId32" bytes", maxMem);
	return true;
}


// this is called after a memory block has been allocated and needs to be registered
void Mem::addMem ( void *mem , int32_t size , const char *note , char isnew ) {
	ScopedLock sl(s_lock);

	logTrace( g_conf.m_logTraceMem, "mem=%p size=%" PRId32 " note='%s' is_new=%d", mem, size, note, isnew );

	//validate();

	  // 4G/x = 600*1024 -> x = 4000000000.0/(600*1024) = 6510
	// crap, g_hostdb.init() is called inmain.cpp before
	// g_conf.init() which is needed to set g_conf.m_maxMem...
	if ( ! s_initialized ) {
		//m_memtablesize = m_maxMem / 6510;
		// support 1.2M ptrs for now. good for about 8GB
		// raise from 3000 to 8194 to fix host #1
		m_memtablesize = 8194*1024;//m_maxMem / 6510;
		//if ( m_maxMem < 8000000000 ) { char *xx=NULL;*xx=0; }
	}

	if ( (int32_t)m_numAllocated + 100 >= (int32_t)m_memtablesize ) { 
		static bool s_printed = false;
		if ( ! s_printed ) {
			log("mem: using too many slots");
			printMem();
			s_printed = true;
		}
	}

	logDebug( g_conf.m_logDebugMem, "mem: add %08" PTRFMT" %" PRId32" bytes (%" PRId64") (%s)", (PTRTYPE)mem, size, m_used, note );

	// check for breech after every call to alloc or free in order to
	// more easily isolate breeching code.. this slows things down a lot
	// though.
	if ( g_conf.m_logDebugMem ) printBreeches(1);

	// copy the magic character, iff not a new() call
	if ( size == 0 ) { char *xx = NULL; *xx = 0; }
	// sanity check
	if ( size < 0 ) {
		log("mem: addMem: Negative size.");
		return;
	}

	// sanity check -- for machines with > 4GB ram?
	if ( (PTRTYPE)mem + (PTRTYPE)size < (PTRTYPE)mem ) {
		log(LOG_LOGIC,"mem: Kernel returned mem at "
		    "%08" PTRFMT" of size %" PRId32" "
		    "which would wrap. Bad kernel.",
		    (PTRTYPE)mem,(int32_t)size);
		char *xx = NULL; 
		*xx = 0;
	}

	// umsg00
	bool useElectricFence = false;
	if ( ! isnew && ! useElectricFence ) {
		for ( int32_t i = 0 ; i < UNDERPAD ; i++ )
			((char *)mem)[0-i-1] = MAGICCHAR;
		for ( int32_t i = 0 ; i < OVERPAD ; i++ )
			((char *)mem)[0+size+i] = MAGICCHAR;
	}

	// if no label!
	if ( ! note[0] ) log(LOG_LOGIC,"mem: addmem: NO note.");

	// lock for threads
	//pthread_mutex_lock ( &s_lock );
	// return NULL if we'd go over our limit
	//if ( getUsedMem() + size > s_maxMem ) {
	//	log("Mem::addMem: max mem limit breeched");
	//	sleep(50000); 
	//	return;
	//}
	// clear mem ptrs if this is our first call
	if ( ! s_initialized ) {

		s_mptrs  = (void **)sysmalloc ( m_memtablesize*sizeof(void *));
		s_sizes  = (int32_t  *)sysmalloc ( m_memtablesize*sizeof(int32_t  ));
		s_labels = (char  *)sysmalloc ( m_memtablesize*16            );
		s_isnew  = (char  *)sysmalloc ( m_memtablesize               );
		if ( ! s_mptrs || ! s_sizes || ! s_labels || ! s_isnew ) {
			if ( s_mptrs  ) sysfree ( s_mptrs  );
			if ( s_sizes  ) sysfree ( s_sizes  );
			if ( s_labels ) sysfree ( s_labels );
			if ( s_isnew  ) sysfree ( s_isnew );
			log("mem: addMem: Init failed. Disabling checks.");
			g_conf.m_detectMemLeaks = false;
			return;
		}
		s_initialized = true;
		memset ( s_mptrs , 0 , sizeof(char *) * m_memtablesize );
	}
	// try to add ptr/size/note to leak-detecting table
	if ( (int32_t)s_n > (int32_t)m_memtablesize ) {
		log( LOG_WARN, "mem: addMem: No room in table for %s size=%" PRId32".",
		    note,size);
		// unlock for threads
		//pthread_mutex_unlock ( &s_lock );
		return;
	}
	// hash into table
	uint32_t u = (PTRTYPE)mem * (PTRTYPE)0x4bf60ade;
	uint32_t h = u % (uint32_t)m_memtablesize;
	// chain to an empty bucket
	int32_t count = (int32_t)m_memtablesize;
	while ( s_mptrs[h] ) {
		// if an occupied bucket as our same ptr then chances are
		// we freed without calling rmMem() and a new addMem() got it
		if ( s_mptrs[h] == mem ) {
			// if we are being called from addnew(), the 
			// overloaded "operator new" function above should
			// have stored a temp ptr in here... allow that, it
			// is used in case an engineer forgets to call 
			// mnew() after calling new() so gigablast would never
			// realize that the memory was allocated.
			if ( s_sizes[h] == size &&
			     s_labels[h*16+0] == 'T' &&
			     s_labels[h*16+1] == 'M' &&
			     s_labels[h*16+2] == 'P' &&
			     s_labels[h*16+3] == 'M' &&
			     s_labels[h*16+4] == 'E' &&
			     s_labels[h*16+5] == 'M'  ) {
				goto skipMe;
			}
			log( LOG_ERROR, "mem: addMem: Mem already added. rmMem not called? label=%c%c%c%c%c%c",
			     s_labels[h*16+0],
			     s_labels[h*16+1],
			     s_labels[h*16+2],
			     s_labels[h*16+3],
			     s_labels[h*16+4],
			     s_labels[h*16+5] );
			char *xx = NULL; *xx = 0;
		}
		h++;
		if ( h == m_memtablesize ) h = 0;
		if ( --count == 0 ) {
			log( LOG_ERROR, "mem: addMem: Mem table is full.");
			printMem();
			char *xx = NULL; *xx = 0;
		}
	}
	// add to debug table
	s_mptrs  [ h ] = mem;
	s_sizes  [ h ] = size;
	s_isnew  [ h ] = isnew;
	//log("adding %" PRId32" size=%" PRId32" to [%" PRId32"] #%" PRId32" (%s)",
	//(int32_t)mem,size,h,s_n,note);
	s_n++;
	// debug
	if ( (size > MINMEM && g_conf.m_logDebugMemUsage) || size>=100000000 )
		log(LOG_INFO,"mem: addMem(%" PRId32"): %s. ptr=0x%" PTRFMT" "
		    "used=%" PRId64,
		    size,note,(PTRTYPE)mem,m_used);
	// now update used mem
	// we do this here now since we always call addMem() now
	m_used += size;
	m_numAllocated++;
	m_numTotalAllocated++;
	if ( size > m_maxAlloc ) { m_maxAlloc = size; m_maxAllocBy = note; }
	if ( m_used > m_maxAlloced ) m_maxAlloced = m_used;


 skipMe:
	int32_t len = gbstrlen(note);
	if ( len > 15 ) len = 15;
	char *here = &s_labels [ h * 16 ];
	memcpy ( here , note , len );
	// make sure NULL terminated
	here[len] = '\0';
	// unlock for threads
	//pthread_mutex_unlock ( &s_lock );
	//validate();
}


#define PRINT_TOP 40

class MemEntry {
public:
	int32_t  m_hash;
	char *m_label;
	int32_t  m_allocated;
	int32_t  m_numAllocs;
};

// print out the mem table
// but combine allocs with the same label
// sort by mem allocated
bool Mem::printMemBreakdownTable ( SafeBuf* sb, 
				   char *lightblue, 
				   char *darkblue) {
	char *ss = "";

	sb->safePrintf (
		       "<table>"

		       "<table %s>"
		       "<tr>"
		       "<td colspan=3 bgcolor=#%s>"
		       "<center><b>Mem Breakdown%s</b></td></tr>\n"

		       "<tr bgcolor=#%s>"
		       "<td><b>allocator</b></td>"
		       "<td><b>num allocs</b></td>"
		       "<td><b>allocated</b></td>"
		       "</tr>" ,
		       TABLE_STYLE, darkblue , ss , darkblue );

	int32_t n = m_numAllocated * 2;
	MemEntry *e = (MemEntry *)mcalloc ( sizeof(MemEntry) * n , "Mem" );
	if ( ! e ) {
		log("admin: Could not alloc %" PRId32" bytes for mem table.",
		    (int32_t)sizeof(MemEntry)*n);
		return false;
	}

	// hash em up, combine allocs of like label together for this hash
	for ( int32_t i = 0 ; i < (int32_t)m_memtablesize ; i++ ) {
		// skip empty buckets
		if ( ! s_mptrs[i] ) continue;
		// get label ptr, use as a hash
		char *label = &s_labels[i*16];
		int32_t  h     = hash32n ( label );
		if ( h == 0 ) h = 1;
		// accumulate the size
		int32_t b = (uint32_t)h % n;
		// . chain till we find it or hit empty
		// . use the label as an indicator if bucket is full or empty
		while ( e[b].m_hash && e[b].m_hash != h )
			if ( ++b >= n ) b = 0;
		// add it in
		e[b].m_hash       = h;
		e[b].m_label      = label;
		e[b].m_allocated += s_sizes[i];
		e[b].m_numAllocs++;
	}

	// get the top 20 users of mem
	MemEntry *winners [ PRINT_TOP ];

	int32_t i = 0;
	int32_t count = 0;
	for ( ; i < n && count < PRINT_TOP ; i++ )
		// if non-empty, add to winners array
		if ( e[i].m_hash ) winners [ count++ ] = &e[i];

	// compute new min
	int32_t min  = 0x7fffffff;
	int32_t mini = -1000;
	for ( int32_t j = 0 ; j < count ; j++ ) {
		if ( winners[j]->m_allocated > min ) continue;
		min  = winners[j]->m_allocated;
		mini = j;
	}

	// now the rest must compete
	for ( ; i < n ; i++ ) {
		// if empty skip
		if ( ! e[i].m_hash ) continue;
		//if ( e[i].m_allocated > 120 && e[i].m_allocated < 2760 )
		//	log("hey %" PRId32, e[i].m_allocated);
		// skip if not a winner
		if ( e[i].m_allocated <= min ) continue;
		// replace the lowest winner
		winners[mini] = &e[i];
		// compute new min
		min = 0x7fffffff;
		for ( int32_t j = 0 ; j < count ; j++ ) {
			if ( winners[j]->m_allocated > min ) continue;
			min  = winners[j]->m_allocated;
			mini = j;
		}
	}

	// now sort them
	bool flag = true;
	while ( flag ) {
		flag = false;
		for ( int32_t i = 1 ; i < count ; i++ ) {
			// no need to swap?
			if ( winners[i-1]->m_allocated >= 
			     winners[i]->m_allocated ) continue;
			// swap
			flag = true;
			MemEntry *tmp = winners[i-1];
			winners[i-1]  = winners[i];
			winners[i  ]  = tmp;
		}
	}
			
	// now print into buffer
	for ( int32_t i = 0 ; i < count ; i++ ) 
		sb->safePrintf (
			       "<tr bgcolor=%s>"
			       "<td>%s</td>"
			       "<td>%" PRId32"</td>"
			       "<td>%" PRId32"</td>"
			       "</tr>\n",
			       LIGHT_BLUE,
			       winners[i]->m_label,
			       winners[i]->m_numAllocs,
			       winners[i]->m_allocated);

	sb->safePrintf ( "</table>\n");

	// don't forget to release this mem
	mfree ( e , (int32_t)sizeof(MemEntry) * n , "Mem" );

	return true;
}


// Relabels memory in table.  Returns true on success, false on failure.
// Purpose is for times when UdpSlot's buffer is not owned and freed by someone
// else.  Now we can verify that passed memory is freed.
bool Mem::lblMem( void *mem, int32_t size, const char *note ) {
	logTrace( g_conf.m_logTraceMem, "mem=%p size=%" PRId32" note=%s", mem, size, note );

	// seems to be a bad bug in this...
	return true;

#if 0
	bool val = false;

	// Make sure we're not relabeling a NULL or dummy memory address,
	// if so, error then exit
	if ( !mem ) {
		//log( "mem: lblMem: Mem addr (0x%08X) invalid/NULL, not "
		//     "relabeling.", mem );
		return val;
	}

	uint32_t u = ( PTRTYPE ) mem * ( PTRTYPE ) 0x4bf60ade;
	uint32_t h = u % ( uint32_t ) m_memtablesize;
	// chain to bucket
	while ( s_mptrs[ h ] ) {
		if ( s_mptrs[ h ] == mem ) {
			if ( s_sizes[ h ] != size ) {
				val = false;
				log( LOG_WARN, "mem: lblMem: Mem addr (0x%08" PTRFMT") exists, size is %" PRId32" off.",
				     ( PTRTYPE ) mem,
				     s_sizes[ h ] - size );
				break;
			}
			int32_t len = gbstrlen( note );
			if ( len > 15 ) len = 15;
			char *here = &s_labels[ h * 16 ];
			gbmemcpy ( here, note, len );
			// make sure NULL terminated
			here[ len ] = '\0';
			val = true;
			break;
		}
		h++;
		if ( h == m_memtablesize ) h = 0;
	}

	if ( !val ) {
		log( "mem: lblMem: Mem addr (0x%08" PTRFMT") not found.", ( PTRTYPE ) mem );
	}

	return val;
#endif
}

// this is called just before a memory block is freed and needs to be deregistered
bool Mem::rmMem  ( void *mem , int32_t size , const char *note ) {
	ScopedLock sl(s_lock);
	logTrace( g_conf.m_logTraceMem, "mem=%p size=%" PRId32 "note='%s'", mem, size, note );

	//validate();

	logDebug( g_conf.m_logDebugMem, "mem: free %08" PTRFMT" %" PRId32"bytes (%s)", (PTRTYPE)mem,size,note);

	// check for breech after every call to alloc or free in order to
	// more easily isolate breeching code.. this slows things down a lot
	// though.
	if ( g_conf.m_logDebugMem ) printBreeches(1);

	// don't free 0 bytes
	if ( size == 0 ) return true;
	// lock for threads
	//pthread_mutex_lock ( &s_lock );
	// . hash by first hashing "mem" to mix it up some
	// . balance the mallocs/frees
	// . hash into table
	uint32_t u = (PTRTYPE)mem * (PTRTYPE)0x4bf60ade;
	uint32_t h = u % (uint32_t)m_memtablesize;
	// . chain to an empty bucket
	// . CAUTION: loops forever if no empty bucket
	while ( s_mptrs[h] && s_mptrs[h] != mem ) {
		h++;
		if ( h == m_memtablesize ) h = 0;
	}
	// if not found, bitch
	if ( ! s_mptrs[h] ) {
		log( LOG_ERROR, "mem: rmMem: Unbalanced free. note=%s size=%" PRId32".",note,size);
		// . return false for now to prevent coring
		// . NOTE: but if entry was not added to table because there 
		//   was no room, we really need to be decrementing m_used
		//   and m_numAllocated here
		// . no, we should core otherwise it can result in some
		//   pretty hard to track down bugs later.
		//return false;
		char *xx = NULL; *xx = 0;
	}

	// are we from the "new" operator
	bool isnew = s_isnew[h];
	// set our size
	if ( size == -1 ) size = s_sizes[h];
	// must be legit now
	if ( size <= 0 ) { char *xx=NULL;*xx=0; }
	// . bitch is sizes don't match
	// . delete operator does not provide a size now (it's -1)
	if ( s_sizes[h] != size ) {
		log( LOG_ERROR, "mem: rmMem: Freeing %" PRId32" should be %" PRId32". (%s)", size,s_sizes[h],note);
		char *xx = NULL; *xx = 0;
	}

	// debug
	if ( (size > MINMEM && g_conf.m_logDebugMemUsage) || size>=100000000 )
		log(LOG_INFO,"mem: rmMem (%" PRId32"): ptr=0x%" PTRFMT" %s.",size,(PTRTYPE)mem,note);

	//
	// we do this here now since we always call rmMem() now
	//
	// decrement freed mem
	m_used -= size;
	// new/delete does not have padding because the "new"
	// function can't support it right now
	//if ( ! isnew ) m_used -= (UNDERPAD + OVERPAD);
	m_numAllocated--;

	// check for breeches, if we don't do it here, we won't be able
	// to check this guy for breeches later, cuz he's getting 
	// removed
	if ( ! isnew ) printBreech ( h , 1 );
	// empty our bucket, and point to next bucket after us
	s_mptrs[h++] = NULL;
	// dec the count
	s_n--;
	// wrap if we need to
	if ( h >= m_memtablesize ) h = 0;
	// var decl.
	uint32_t k;
	// shit after us may has to be rehashed in case it chained over us
	while ( s_mptrs[h] ) {
		// get mem ptr in bucket #h
		char *mem = (char *)s_mptrs[h];
		// find the most wanted bucket for this mem ptr
		u = (PTRTYPE)mem * (PTRTYPE)0x4bf60ade;
		k= u % (uint32_t)m_memtablesize;
		// if it's in it, continue
		if ( k == h ) { h++; continue; }
		// otherwise, move it back to fill the gap
		s_mptrs[h] = NULL;
		// dec count
		//s_n--;
		// if slot #k is full, chain
		for ( ; s_mptrs[k] ; )
			if ( ++k >= m_memtablesize ) k = 0;
		// re-add it to table
		s_mptrs[k] = (void *)mem;
		s_sizes[k] = s_sizes[h];
		s_isnew[k] = s_isnew[h];
		memcpy(&s_labels[k*16],&s_labels[h*16],16);
		// try next bucket now
		h++;
		// wrap if we need to
		if ( h >= m_memtablesize ) h = 0;
	}

	//validate();

	// unlock for threads
	//pthread_mutex_unlock ( &s_lock );
	return true;
}

int32_t Mem::validate ( ) {
	if ( ! s_mptrs ) return 1;
	// stock up "p" and compute total bytes alloced
	int64_t total = 0;
	int32_t count = 0;
	for ( int32_t i = 0 ; i < (int32_t)m_memtablesize ; i++ ) {
		// skip empty buckets
		if ( ! s_mptrs[i] ) continue;
		total += s_sizes[i];
		count++;
	}
	// see if it matches
	if ( total != m_used ) { char *xx=NULL;*xx=0; }
	if ( count != m_numAllocated ) { char *xx=NULL;*xx=0; }
	return 1;
}


int32_t Mem::getMemSlot ( void *mem ) {
	// hash into table
	uint32_t u = (PTRTYPE)mem * (PTRTYPE)0x4bf60ade;
	uint32_t h = u % (uint32_t)m_memtablesize;
	// . chain to an empty bucket
	// . CAUTION: loops forever if no empty bucket
	while ( s_mptrs[h] && s_mptrs[h] != mem ) {
		h++;
		if ( h == m_memtablesize ) h = 0;
	}
	// if not found, return -1
	if ( ! s_mptrs[h] ) return -1;
	return h;
}


int Mem::printBreech ( int32_t i , char core ) {
	// skip if empty
	if ( ! s_mptrs    ) return 0;
	if ( ! s_mptrs[i] ) return 0;
	// skip if isnew is true, no padding there
	if ( s_isnew[i] ) return 0;

	// if no label!
	if ( ! s_labels[i*16] ) 
		log(LOG_LOGIC,"mem: NO label found.");
	// do not test "Stack" allocated in Threads.cpp because it
	// uses mprotect() which messes up the magic chars
	if ( s_labels[i*16+0] == 'T' &&
	     s_labels[i*16+1] == 'h' &&
	     !strcmp(&s_labels[i*16  ],"ThreadStack" ) ) return 0;
	char flag = 0;
	// check for underruns
	char *mem = (char *)s_mptrs[i];
	char *bp = NULL;
	for ( int32_t j = 0 ; j < UNDERPAD ; j++ ) {
		if ( mem[0-j-1] == MAGICCHAR ) continue;
		log(LOG_LOGIC,"mem: underrun at %" PTRFMT" loff=%" PRId32" "
		    "size=%" PRId32" "
		    "i=%" PRId32" note=%s",
		    (PTRTYPE)mem,0-j-1,(int32_t)s_sizes[i],i,&s_labels[i*16]);

		// mark it for freed mem re-use check below
		if ( ! bp ) bp = &mem[0-j-1];

		// now scan the whole hash table and find the mem buffer
		// just before that! but only do this once
		if ( flag == 1 ) continue;
		PTRTYPE min = 0;
		int32_t mink = -1;
		for ( int32_t k = 0 ; k < (int32_t)m_memtablesize ; k++ ) {
			// skip empties
			if ( ! s_mptrs[k] ) continue;
			// do not look at mem after us
			if ( (PTRTYPE)s_mptrs[k] >= (PTRTYPE)mem ) 
				continue;
			// get min diff
			if ( mink != -1 && (PTRTYPE)s_mptrs[k] < min ) 
				continue;
			// new winner
			min = (PTRTYPE)s_mptrs[k];
			mink = k;
		}
		// now report it
		if ( mink == -1 ) continue;
		log( LOG_WARN, "mem: possible breeching buffer=%s dist=%" PRIu32,
		    &s_labels[mink*16],
		    (uint32_t)(
		    (PTRTYPE)mem-
		    ((PTRTYPE)s_mptrs[mink]+(uint32_t)s_sizes[mink])));
		flag = 1;
	}		    

	// check for overruns
	int32_t size = s_sizes[i];
	for ( int32_t j = 0 ; j < OVERPAD ; j++ ) {
		if ( mem[size+j] == MAGICCHAR ) continue;
		log(LOG_LOGIC,"mem: overrun  at 0x%" PTRFMT" (size=%" PRId32")"
		    "roff=%" PRId32" note=%s",
		    (PTRTYPE)mem,size,j,&s_labels[i*16]);

		// mark it for freed mem re-use check below
		if ( ! bp ) bp = &mem[size+j];

		// now scan the whole hash table and find the mem buffer
		// just before that! but only do this once
		if ( flag == 1 ) continue;
		PTRTYPE min = 0;
		int32_t mink = -1;
		for ( int32_t k = 0 ; k < (int32_t)m_memtablesize ; k++ ) {
			// skip empties
			if ( ! s_mptrs[k] ) continue;
			// do not look at mem before us
			if ( (PTRTYPE)s_mptrs[k] <= (PTRTYPE)mem ) 
				continue;
			// get min diff
			if ( mink != -1 && (PTRTYPE)s_mptrs[k] > min ) 
				continue;
			// new winner
			min = (PTRTYPE)s_mptrs[k];
			mink = k;
		}
		// now report it
		if ( mink == -1 ) continue;
		log(LOG_WARN, "mem: possible breeching buffer=%s at 0x%" PTRFMT" "
		    "breaching at offset of %" PTRFMT" bytes",
		    &s_labels[mink*16],
		    (PTRTYPE)s_mptrs[mink],
		    (PTRTYPE)s_mptrs[mink]-((PTRTYPE)mem+s_sizes[i]));
		flag = 1;
	}
	
	// return now if no breach
	if ( flag == 0 ) return 1;

	// need this
	if ( ! bp ) { char *xx=NULL;*xx=0; }

	if ( flag && core ) { char *xx = NULL; *xx = 0; }
	return 1;
}

// check all allocated memory for buffer under/overruns
int Mem::printBreeches ( char core ) {
	if ( ! s_mptrs ) return 0;
	// do not bother if no padding at all
	if ( (int32_t)UNDERPAD == 0 && (int32_t)OVERPAD == 0 ) return 0;
	
	log("mem: checking mem for breeches");

	// loop through the whole mem table
	for ( int32_t i = 0 ; i < (int32_t)m_memtablesize ; i++ )
		// only check if non-empty
		if ( s_mptrs[i] ) printBreech ( i , core );
	return 0;
}


int Mem::printMem ( ) {
	// has anyone breeched their buffer?
	printBreeches ( 0 ) ;

	// print table entries sorted by most mem first
	int32_t *p = (int32_t *)sysmalloc ( m_memtablesize * 4 );
	if ( ! p ) return 0;
	// stock up "p" and compute total bytes alloced
	int64_t total = 0;
	int32_t np    = 0;
	for ( int32_t i = 0 ; i < (int32_t)m_memtablesize ; i++ ) {
		// skip empty buckets
		if ( ! s_mptrs[i] ) continue;
		total += s_sizes[i];
		p[np++] = i;
	}

	// print out table sorted by sizes
	for ( int32_t i = 0 ; i < np ; i++ ) {
		int32_t a = p[i];

		log(LOG_INFO,"mem: %05" PRId32") %" PRId32" 0x%" PTRFMT" %s", 
		    i,s_sizes[a] , (PTRTYPE)s_mptrs[a] , &s_labels[a*16] );
	}
	sysfree ( p );
	log(LOG_INFO,"mem: # current objects allocated now = %" PRId32, np );
	log(LOG_INFO,"mem: totalMem alloced now = %" PRId64, total );
	//log("mem: max alloced at one time = %" PRId32, (int32_t)(m_maxAlloced));
	log(LOG_INFO,"mem: Memory allocated now: %" PRId64".\n", getUsedMem() );
	log(LOG_INFO,"mem: Num allocs %" PRId32".\n", m_numAllocated );
	return 1;
}

void *Mem::gbmalloc ( int size , const char *note ) {
	logTrace( g_conf.m_logTraceMem, "size=%d note='%s'", size, note );

	// don't let electric fence zap us
	if ( size == 0 ) return (void *)0x7fffffff;
	
	// random oom testing
	//static int32_t s_mcount = 0;
	//s_mcount++;
	if ( g_conf.m_testMem && (rand() % 100) < 2 ) { 
		//if ( s_mcount > 1055 && (rand() % 1000) < 2 ) { 
		g_errno = ENOMEM; 
		log( LOG_WARN, "mem: malloc-fake(%i,%s): %s",size,note, mstrerror(g_errno));
		return NULL;
	} 

retry:
	int64_t max = g_conf.m_maxMem;

	// don't go over max
	if ( m_used + size + UNDERPAD + OVERPAD >= max ) {
		// try to free temp mem. returns true if it freed some.
		if ( freeCacheMem() ) goto retry;
		g_errno = ENOMEM;
		log( LOG_WARN, "mem: malloc(%i): Out of memory", size );
		return NULL;
	}

	if ( size < 0 ) {
		g_errno = EBADENGINEER;
		log( LOG_ERROR, "mem: malloc(%i): Bad value.", size );
		char *xx = NULL; *xx = 0;
		return NULL;
	}

	void *mem;

	g_inMemFunction = true;

	mem = (void *)sysmalloc ( size + UNDERPAD + OVERPAD );

	g_inMemFunction = false;

	int32_t memLoop = 0;
mallocmemloop:
	if ( ! mem && size > 0 ) {
		g_mem.m_outOfMems++;
		// try to free temp mem. returns true if it freed some.
		if ( freeCacheMem() ) goto retry;
		g_errno = errno;
		static int64_t s_lastTime;
		static int32_t s_missed = 0;
		int64_t now = gettimeofdayInMillisecondsLocal();
		int64_t avail = (int64_t)g_conf.m_maxMem - 
			(int64_t)m_used;
		if ( now - s_lastTime >= 1000LL ) {
			log(LOG_WARN, "mem: system malloc(%i,%s) availShouldBe=%" PRId64": "
			    "%s (%s) (ooms suppressed since last log msg = %" PRId32")",
			    size+UNDERPAD+OVERPAD,
			    note,
			    avail,
			    mstrerror(g_errno),
			    note,
			    s_missed);
			s_lastTime = now;
			s_missed = 0;
		} else {
			s_missed++;
		}
		// to debug oom issues:
		//char *xx=NULL;*xx=0;

		// send an email alert if this happens! it is a sign of "memory fragmentation"
		//static bool s_sentEmail = false;
		// stop sending these now... seems to be problematic. says
		// 160MB is avail and can't alloc 20MB...
		static bool s_sentEmail = true;
		// assume only 90% is really available because of 
		// inefficient mallocing
		avail = (int64_t)((float)avail * 0.80);
		// but if it is within about 15MB of what is theoretically
		// available, don't send an email, because there is always some
		// minor fragmentation
		if ( ! s_sentEmail && avail > size ) {
			s_sentEmail = true;
			char msgbuf[1024];
			Host *h = g_hostdb.m_myHost;
			snprintf(msgbuf, 1024,
				 "Possible memory fragmentation "
				 "on host #%" PRId32" %s",
				 h->m_hostId,h->m_note);
			log(LOG_WARN, "query: %s",msgbuf);
			g_pingServer.sendEmail(NULL, msgbuf,true,true);
		}
		return NULL;
	}
	if ( (PTRTYPE)mem < 0x00010000 ) {
		void *remem = sysmalloc(size);
		log ( LOG_WARN, "mem: Caught low memory allocation "
		      "at %08" PTRFMT", "
		      "reallocated to %08" PTRFMT"",
		      (PTRTYPE)mem, (PTRTYPE)remem );
		sysfree(mem);
		mem = remem;
		memLoop++;
		if ( memLoop > 100 ) {
			log ( LOG_WARN, "mem: Attempted to reallocate low "
					"memory allocation 100 times, "
					"aborting and returning NOMEM." );
			g_errno = ENOMEM;
			return NULL;
		}
		goto mallocmemloop;
	}

	logTrace( g_conf.m_logTraceMem, "mem=%p size=%d note='%s'", mem, size, note );

	addMem ( (char *)mem + UNDERPAD , size , note , 0 );
	return (char *)mem + UNDERPAD;
}

void *Mem::gbcalloc ( int size , const char *note ) {
	logTrace( g_conf.m_logTraceMem, "size=%d note='%s'", size, note );

	void *mem = gbmalloc ( size , note );
	logTrace( g_conf.m_logTraceMem, "mem=%p size=%d note='%s'", mem, size, note );

	// init it
	if ( mem ) memset ( mem , 0, size );
	return mem;
}

void *Mem::gbrealloc ( void *ptr , int oldSize , int newSize , const char *note ) {
	logTrace( g_conf.m_logTraceMem, "ptr=%p oldSize=%d newSize=%d note='%s'", ptr, oldSize, newSize, note );

	// return dummy values since realloc() returns NULL if failed
	if ( oldSize == 0 && newSize == 0 ) return (void *)0x7fffffff;
	// do nothing if size is same
	if ( oldSize == newSize ) return ptr;
	// crazy?
	if ( newSize < 0 ) { char *xx=NULL;*xx=0; }
	// if newSize is 0...
	if ( newSize == 0 ) { 
		//mfree ( ptr , oldSize , note );
		gbfree ( ptr , oldSize , note );
		return (void *)0x7fffffff;
	}

retry:

	// hack so hostid #0 can use more mem
	int64_t max = g_conf.m_maxMem;
	//if ( g_hostdb.m_hostId == 0 )  max += 2000000000;

	// don't go over max
	if ( m_used + newSize - oldSize >= max ) {
		// try to free temp mem. returns true if it freed some.
		if ( freeCacheMem() ) goto retry;
		g_errno = ENOMEM;
		log( LOG_WARN, "mem: realloc(%i,%i): Out of memory.",oldSize,newSize);
		return NULL;
	}
	// if oldSize is 0, use our malloc() instead
	if ( oldSize == 0 ) {
		return gbmalloc ( newSize , note );
	}

	char *mem;
	// assume it will be successful. we can't call rmMem() after
	// calling sysrealloc() because it will mess up our MAGICCHAR buf
	rmMem  ( ptr , oldSize , note );

	// . do the actual realloc
	// . CAUTION: don't pass in 0x7fffffff in as "ptr" 
	// . this was causing problems
	mem = (char *)sysrealloc ( (char *)ptr - UNDERPAD , newSize + UNDERPAD + OVERPAD );

	// remove old guy on sucess
	if ( mem ) {
		addMem ( (char *)mem + UNDERPAD , newSize , note , 0 );
		char *returnMem = mem + UNDERPAD;
		// set magic char bytes for mem
		for ( int32_t i = 0 ; i < UNDERPAD ; i++ )
			returnMem[0-i-1] = MAGICCHAR;
		for ( int32_t i = 0 ; i < OVERPAD ; i++ )
			returnMem[0+newSize+i] = MAGICCHAR;
		return returnMem;
	}

	// ok, just try using malloc then!
	mem = (char *)mmalloc ( newSize , note );
	// bail on error
	if ( ! mem ) {
		g_mem.m_outOfMems++;
		// restore the original buf we tried to grow
		addMem ( ptr , oldSize , note , 0 );
		errno = g_errno = ENOMEM;
		return NULL;
	}
	// log a note
	log(LOG_INFO,"mem: had to use malloc/gbmemcpy instead of realloc.");

	// copy over to it
	memmove ( mem , ptr , oldSize );
	// we already called rmMem() so don't double call
	sysfree ( (char *)ptr - UNDERPAD );	

	return mem;
}

char *Mem::dup ( const void *data , int32_t dataSize , const char *note ) {
	logTrace( g_conf.m_logTraceMem, "data=%p dataSize=%" PRId32" note='%s'", data, dataSize, note );

	// keep it simple
	char *mem = (char *)mmalloc ( dataSize , note );
	logTrace( g_conf.m_logTraceMem, "mem=%p data=%p dataSize=%" PRId32" note='%s'", (void*)mem, data, dataSize, note );

	if ( mem ) memcpy ( mem , data , dataSize );
	return mem;
}

void Mem::gbfree ( void *ptr , int size , const char *note ) {
	logTrace( g_conf.m_logTraceMem, "ptr=%p size=%d note='%s'", ptr, size, note );

	// don't let electric fence zap us
	//if ( size == 0 && ptr==(void *)0x7fffffff) return;
	if ( size == 0 ) return;
	// huh?
	if ( ! ptr ) return;

	// . get how much it was from the mem table
	// . this is used for alloc/free wrappers for zlib because it does
	//   not give us a size to free when it calls our mfree(), so we use -1
	int32_t slot = g_mem.getMemSlot ( ptr );
	if ( slot < 0 ) {
		log(LOG_LOGIC,"mem: could not find slot (note=%s)",note);
//Do NOT abort here... Let it run, otherwise it dies during merges.  abort();
		//log(LOG_LOGIC,"mem: FIXME!!!");
		// return for now so procog does not core all the time!
		return;
		//char *xx = NULL; *xx = 0;
	}

	bool isnew = s_isnew[slot];

	// if this returns false it was an unbalanced free
	if ( ! rmMem ( ptr , size , note ) ) return;

	g_inMemFunction = true;
	if ( isnew ) sysfree ( (char *)ptr );
	else         sysfree ( (char *)ptr - UNDERPAD );
	g_inMemFunction = false;
}


//#include "Msg20.h"

static bool freeCacheMem() {
	// returns true if it did free some stuff
	//if ( resetMsg20Cache() ) {
	//	log("mem: freed cache mem.");
	//	return true;
	//}
	return false;
}
