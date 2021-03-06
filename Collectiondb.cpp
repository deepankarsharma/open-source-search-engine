#include "gb-include.h"

#include "Collectiondb.h"
#include "Xml.h"
#include "Url.h"
#include "Loop.h"
#include "Spider.h"  // for calling SpiderLoop::collectionsUpdated()
#include "SpiderLoop.h"
#include "SpiderColl.h"
#include "Doledb.h"
#include "Posdb.h"
#include "Titledb.h"
#include "Tagdb.h"
#include "Spider.h"
#include "Clusterdb.h"
#include "Spider.h"
#include "Repair.h"
#include "Parms.h"

static HashTableX g_collTable;

// a global class extern'd in .h file
Collectiondb g_collectiondb;

Collectiondb::Collectiondb ( ) {
	m_wrapped = 0;
	m_numRecs = 0;
	m_numRecsUsed = 0;
	m_numCollsSwappedOut = 0;
	m_initializing = false;
	//m_lastUpdateTime = 0LL;
	m_needsSave = false;
	// sanity
	if ( RDB_END2 >= RDB_END ) return;
	log("db: increase RDB_END2 to at least %" PRId32" in "
	    "Collectiondb.h",(int32_t)RDB_END);
	char *xx=NULL;*xx=0;
}

// reset rdb
void Collectiondb::reset() {
	log(LOG_INFO,"db: resetting collectiondb.");
	for ( int32_t i = 0 ; i < m_numRecs ; i++ ) {
		if ( ! m_recs[i] ) continue;
		mdelete ( m_recs[i], sizeof(CollectionRec), "CollectionRec" );
		delete ( m_recs[i] );
		m_recs[i] = NULL;
	}
	m_numRecs     = 0;
	m_numRecsUsed = 0;
	g_collTable.reset();
}

extern bool g_inAutoSave;

// . save to disk
// . returns false if blocked, true otherwise
bool Collectiondb::save ( ) {
	if ( g_conf.m_readOnlyMode ) {
		return true;
	}

	if ( g_inAutoSave && m_numRecsUsed > 20 && g_hostdb.m_hostId != 0 ) {
		return true;
	}

	// which collection rec needs a save
	for ( int32_t i = 0 ; i < m_numRecs ; i++ ) {
		if ( ! m_recs[i]              ) continue;
		// temp debug message
		//logf(LOG_DEBUG,"admin: SAVING collection #%" PRId32" ANYWAY",i);
		if ( ! m_recs[i]->m_needsSave ) {
			continue;
		}

		// if we core in malloc we won't be able to save the
		// coll.conf files
		if ( m_recs[i]->m_isCustomCrawl &&
		     g_inMemFunction &&
		     g_hostdb.m_hostId != 0 ) {
			continue;
		}

		//log(LOG_INFO,"admin: Saving collection #%" PRId32".",i);
		m_recs[i]->save ( );
	}
	// oh well
	return true;
}



///////////
//
// fill up our m_recs[] array based on the coll.*.*/coll.conf files
//
///////////
bool Collectiondb::loadAllCollRecs ( ) {

	m_initializing = true;

	char dname[1024];
	// MDW: sprintf ( dname , "%s/collections/" , g_hostdb.m_dir );
	sprintf ( dname , "%s" , g_hostdb.m_dir );
	Dir d;
	d.set ( dname );
	if ( ! d.open ()) {
		log( LOG_WARN, "admin: Could not load collection config files." );
		return false;
	}

	int32_t count = 0;
	const char *f;
	while ( ( f = d.getNextFilename ( "*" ) ) ) {
		// skip if first char not "coll."
		if ( strncmp ( f , "coll." , 5 ) != 0 ) continue;
		// must end on a digit (i.e. coll.main.0)
		if ( ! is_digit (f[gbstrlen(f)-1]) ) continue;
		// count them
		count++;
	}

	// reset directory for another scan
	d.set ( dname );
	if ( ! d.open ()) {
		log( LOG_WARN, "admin: Could not load collection config files." );
		return false;
	}

	// note it
	//log(LOG_INFO,"db: loading collection config files.");
	// . scan through all subdirs in the collections dir
	// . they should be like, "coll.main/" and "coll.mycollection/"
	while ( ( f = d.getNextFilename ( "*" ) ) ) {
		// skip if first char not "coll."
		if ( strncmp ( f , "coll." , 5 ) != 0 ) continue;
		// must end on a digit (i.e. coll.main.0)
		if ( ! is_digit (f[gbstrlen(f)-1]) ) continue;
		// point to collection
		const char *coll = f + 5;
		// NULL terminate at .
		const char *pp = strchr ( coll , '.' );
		if ( ! pp ) continue;
		char collname[256];
		memcpy(collname,coll,pp-coll);
		collname[pp-coll] = '\0';
		// get collnum
		collnum_t collnum = atol ( pp + 1 );
		// add it
		if ( ! addExistingColl ( collname, collnum ) )
			return false;
		// swap it out if we got 100+ collections
		// if ( count < 100 ) continue;
		// CollectionRec *cr = getRec ( collnum );
		// if ( cr ) cr->swapOut();
	}
	// if no existing recs added... add coll.main.0 always at startup
	if ( m_numRecs == 0 ) {
		log("admin: adding main collection.");
		addNewColl ( "main", 0, true, 0 );
	}

	m_initializing = false;

	return true;
}

// after we've initialized all rdbs in main.cpp call this to clean out
// our rdb trees
bool Collectiondb::cleanTrees ( ) {

	// remove any nodes with illegal collnums
	Rdb *r;
	r = g_posdb.getRdb();
	r->m_buckets.cleanBuckets();

	r = g_titledb.getRdb();
	r->m_tree.cleanTree    ();//(char **)r->m_bases);
	r = g_spiderdb.getRdb();
	r->m_tree.cleanTree    ();//(char **)r->m_bases);
	r = g_doledb.getRdb();
	r->m_tree.cleanTree    ();//(char **)r->m_bases);
	// success
	return true;
}

#include "Statsdb.h"

// same as addOldColl()
bool Collectiondb::addExistingColl ( const char *coll, collnum_t collnum ) {

	int32_t i = collnum;

	// ensure does not already exist in memory
	collnum_t oldCollnum = getCollnum(coll);
	if ( oldCollnum >= 0 ) {
		g_errno = EEXIST;
		log("admin: Trying to create collection \"%s\" but "
		    "already exists in memory. Do an ls on "
		    "the working dir to see if there are two "
		    "collection dirs with the same coll name",coll);
		char *xx=NULL;*xx=0;
	}

	// also try by #, i've seen this happen too
	CollectionRec *ocr = getRec ( i );
	if ( ocr ) {
		g_errno = EEXIST;
		log("admin: Collection id %i is in use already by "
		    "%s, so we can not add %s. moving %s to trash."
		    ,(int)i,ocr->m_coll,coll,coll);
		SafeBuf cmd;
		int64_t now = gettimeofdayInMilliseconds();
		cmd.safePrintf ( "mv coll.%s.%i trash/coll.%s.%i.%" PRIu64
				 , coll
				 ,(int)i
				 , coll
				 ,(int)i
				 , now );
		//log("admin: %s",cmd.getBufStart());
		gbsystem ( cmd.getBufStart() );
		return true;
	}

	// create the record in memory
	CollectionRec *cr = new (CollectionRec);
	if ( ! cr )
		return log("admin: Failed to allocated %" PRId32" bytes for new "
			   "collection record for \"%s\".",
			   (int32_t)sizeof(CollectionRec),coll);
	mnew ( cr , sizeof(CollectionRec) , "CollectionRec" );

	// set collnum right for g_parms.setToDefault() call just in case
	// because before it was calling CollectionRec::reset() which
	// was resetting the RdbBases for the m_collnum which was garbage
	// and ended up resetting random collections' rdb. but now
	// CollectionRec::CollectionRec() sets m_collnum to -1 so we should
	// not need this!
	//cr->m_collnum = oldCollnum;

	// get the default.conf from working dir if there
	g_parms.setToDefault( (char *)cr , OBJ_COLL , cr );

	strcpy ( cr->m_coll , coll );
	cr->m_collLen = gbstrlen ( coll );
	cr->m_collnum = i;

	// point to this, so Rdb and RdbBase can reference it
	coll = cr->m_coll;

	cr->m_needsSave = false;
	//log("admin: loaded old coll \"%s\"",coll);

	// load coll.conf file
	if ( ! cr->load ( coll , i ) ) {
		mdelete ( cr, sizeof(CollectionRec), "CollectionRec" );
		log("admin: Failed to load coll.%s.%" PRId32"/coll.conf",coll,i);
		delete ( cr );
		if ( m_recs ) m_recs[i] = NULL;
		return false;
	}

	if ( ! registerCollRec ( cr , false ) ) return false;

	// always index spider status docs now for custom crawls
	if ( cr->m_isCustomCrawl )
		cr->m_indexSpiderReplies = true;

	// and don't do link voting, will help speed up
	if ( cr->m_isCustomCrawl ) {
		cr->m_getLinkInfo = false;
		cr->m_computeSiteNumInlinks = false;
		// limit each shard to 5 spiders per collection to prevent
		// ppl from spidering the web and hogging up resources
		cr->m_maxNumSpiders = 5;

 		// diffbot download docs up to 50MB so we don't truncate
		// things like sitemap.xml. but keep regular html pages
		// 1MB
		cr->m_maxTextDocLen  = 1024*1024;
		// xml, pdf, etc can be this. 50MB
		cr->m_maxOtherDocLen = 50000000;
	}

	// we need to compile the regular expressions or update the url
	// filters with new logic that maps crawlbot parms to url filters
	return cr->rebuildUrlFilters ( );
}

// . add a new rec
// . returns false and sets g_errno on error
// . was addRec()
// . "isDump" is true if we don't need to initialize all the rdbs etc
//   because we are doing a './gb dump ...' cmd to dump out data from
//   one Rdb which we will custom initialize in main.cpp where the dump
//   code is. like for instance, posdb.
// . "customCrawl" is 0 for a regular collection, 1 for a simple crawl
//   2 for a bulk job. diffbot terminology.
bool Collectiondb::addNewColl ( const char *coll, char customCrawl, bool saveIt,
				// Parms.cpp reserves this so it can be sure
				// to add the same collnum to every shard
				collnum_t newCollnum ) {
	//do not send add/del coll request until we are in sync with shard!!
	// just return ETRYAGAIN for the parmlist...

	// ensure coll name is legit
	const char *p = coll;
	for ( ; *p ; p++ ) {
		if ( is_alnum_a(*p) ) continue;
		if ( *p == '-' ) continue;
		if ( *p == '_' ) continue; // underscore now allowed
		break;
	}
	if ( *p ) {
		g_errno = EBADENGINEER;
		log( LOG_WARN, "admin: '%s' is a malformed collection name because it contains the '%c' character.",coll,*p);
		return false;
	}
	if ( newCollnum < 0 ) { char *xx=NULL;*xx=0; }

	// if empty... bail, no longer accepted, use "main"
	if ( ! coll || !coll[0] ) {
		g_errno = EBADENGINEER;
		log( LOG_WARN, "admin: Trying to create a new collection but no collection name provided. "
		     "Use the 'c' cgi parameter to specify it.");
		return false;
	}
	// or if too big
	if ( gbstrlen(coll) > MAX_COLL_LEN ) {
		g_errno = ENOBUFS;
		log( LOG_WARN, "admin: Trying to create a new collection whose name '%s' of %i chars is longer than the "
		     "max of %" PRId32" chars.", coll, gbstrlen(coll), (int32_t)MAX_COLL_LEN );
		return false;
	}

	// ensure does not already exist in memory
	if ( getCollnum ( coll ) >= 0 ) {
		g_errno = EEXIST;
		log( LOG_WARN, "admin: Trying to create collection '%s' but already exists in memory.",coll);
		// just let it pass...
		g_errno = 0 ;
		return true;
	}

	// MDW: ensure not created on disk since time of last load
	char dname[512];
	sprintf(dname, "%scoll.%s.%" PRId32"/",g_hostdb.m_dir,coll,(int32_t)newCollnum);
	DIR *dir = opendir ( dname );
	if ( dir ) closedir ( dir );
	if ( dir ) {
		g_errno = EEXIST;
		log(LOG_WARN, "admin: Trying to create collection %s but directory %s already exists on disk.",coll,dname);
		return false;
	}

	// create the record in memory
	CollectionRec *cr = new (CollectionRec);
	if ( ! cr ) {
		log( LOG_WARN, "admin: Failed to allocated %" PRId32" bytes for new collection record for '%s'.",
		     ( int32_t ) sizeof( CollectionRec ), coll );
		return false;
	}

	// register the mem
	mnew ( cr , sizeof(CollectionRec) , "CollectionRec" );

	// get the default.conf from working dir if there
	g_parms.setToDefault( (char *)cr , OBJ_COLL , cr );

	// put search results back so it doesn't mess up results in qatest123
	if ( strcmp(coll,"qatest123") == 0 )
		cr->m_sameLangWeight = 20.0;

	// set coll id and coll name for coll id #i
	strcpy ( cr->m_coll , coll );
	cr->m_collLen = gbstrlen ( coll );
	cr->m_collnum = newCollnum;

	// point to this, so Rdb and RdbBase can reference it
	coll = cr->m_coll;

	//
	// BEGIN NEW CODE
	//

	//
	// get token and crawlname if customCrawl is 1 or 2
	//
	char *token = NULL;
	char *crawl = NULL;
	SafeBuf tmp;
	// . return true with g_errno set on error
	// . if we fail to set a parm right we should force ourselves
	//   out sync
	if ( customCrawl ) {
		if ( ! tmp.safeStrcpy ( coll ) ) return true;
		token = tmp.getBufStart();
		// diffbot coll name format is <token>-<crawlname>
		char *h = strchr ( tmp.getBufStart() , '-' );
		if ( ! h ) {
			log( LOG_WARN, "crawlbot: bad custom collname");
			g_errno = EBADENGINEER;
			mdelete ( cr, sizeof(CollectionRec), "CollectionRec" );
			delete ( cr );
			return true;
		}
		*h = '\0';
		crawl = h + 1;
		if ( ! crawl[0] ) {
			log( LOG_WARN, "crawlbot: bad custom crawl name");
			mdelete ( cr, sizeof(CollectionRec), "CollectionRec" );
			delete ( cr );
			g_errno = EBADENGINEER;
			return true;
		}
		// or if too big!
		if ( gbstrlen(crawl) > 30 ) {
			log( LOG_WARN, "crawlbot: crawlbot crawl NAME is over 30 chars");
			mdelete ( cr, sizeof(CollectionRec), "CollectionRec" );
			delete ( cr );
			g_errno = EBADENGINEER;
			return true;
		}
	}

	//log("parms: added new collection \"%s\"", collName );

	cr->m_maxToCrawl = -1;
	cr->m_maxToProcess = -1;


	if ( customCrawl ) {
		// always index spider status docs now
		cr->m_indexSpiderReplies = true;
		// remember the token
		cr->m_diffbotToken.set ( token );
		cr->m_diffbotCrawlName.set ( crawl );
		// bring this back
		cr->m_diffbotApiUrl.set ( "" );
		cr->m_diffbotUrlCrawlPattern.set ( "" );
		cr->m_diffbotUrlProcessPattern.set ( "" );
		cr->m_diffbotPageProcessPattern.set ( "" );
		cr->m_diffbotUrlCrawlRegEx.set ( "" );
		cr->m_diffbotUrlProcessRegEx.set ( "" );
		cr->m_diffbotMaxHops = -1;

		cr->m_spiderStatus = SP_INITIALIZING;
		// do not spider more than this many urls total.
		// -1 means no max.
		cr->m_maxToCrawl = 100000;
		// do not process more than this. -1 means no max.
		cr->m_maxToProcess = 100000;
		// -1 means no max
		cr->m_maxCrawlRounds = -1;
		// diffbot download docs up to 10MB so we don't truncate
		// things like sitemap.xml
		cr->m_maxTextDocLen  = 1024*1024 * 5;
		cr->m_maxOtherDocLen = 1024*1024 * 10;
		// john want's deduping on by default to avoid
		// processing similar pgs
		cr->m_dedupingEnabled = true;
		// show the ban links in the search results. the
		// collection name is cryptographic enough to show that
		cr->m_isCustomCrawl = customCrawl;
		cr->m_diffbotOnlyProcessIfNewUrl = true;
		// default respider to off
		cr->m_collectiveRespiderFrequency = 0.0;
		//cr->m_restrictDomain = true;
		// reset the crawl stats
		// turn off link voting, etc. to speed up
		cr->m_getLinkInfo = false;
		cr->m_computeSiteNumInlinks = false;
	}

	// . this will core if a host was dead and then when it came
	//   back up host #0's parms.cpp told it to add a new coll
	cr->m_diffbotCrawlStartTime = getTimeGlobalNoCore();
	cr->m_diffbotCrawlEndTime   = 0;

	// . just the basics on these for now
	// . if certain parms are changed then the url filters
	//   must be rebuilt, as well as possibly the waiting tree!!!
	// . need to set m_urlFiltersHavePageCounts etc.
	cr->rebuildUrlFilters ( );

	cr->m_useRobotsTxt = true;

	// reset crawler stats.they should be loaded from crawlinfo.txt
	memset ( &cr->m_localCrawlInfo , 0 , sizeof(CrawlInfo) );
	memset ( &cr->m_globalCrawlInfo , 0 , sizeof(CrawlInfo) );

	// note that
	log("colldb: initial revival for %s",cr->m_coll);

	// . assume we got some urls ready to spider
	// . Spider.cpp will wait SPIDER_DONE_TIME seconds and if it has no
	//   urls it spidered in that time these will get set to 0 and it
	//   will send out an email alert if m_sentCrawlDoneAlert is not true.
	cr->m_localCrawlInfo.m_hasUrlsReadyToSpider = 1;
	cr->m_globalCrawlInfo.m_hasUrlsReadyToSpider = 1;

	// set some defaults. max spiders for all priorities in this
	// collection. NO, default is in Parms.cpp.
	//cr->m_maxNumSpiders = 10;

	//cr->m_needsSave = 1;

	// start the spiders!
	cr->m_spideringEnabled = true;

	// override this?
	saveIt = true;

	//
	// END NEW CODE
	//

	//log("admin: adding coll \"%s\" (new=%" PRId32")",coll,(int32_t)isNew);

	// MDW: create the new directory
 retry22:
	if ( ::mkdir ( dname, getDirCreationFlags() ) ) {
		// valgrind?
		if ( errno == EINTR ) goto retry22;
		g_errno = errno;
		mdelete ( cr , sizeof(CollectionRec) , "CollectionRec" );
		delete ( cr );
		return log( LOG_WARN, "admin: Creating directory %s had error: %s.", dname,mstrerror(g_errno));
	}

	// save it into this dir... might fail!
	if ( saveIt && ! cr->save() ) {
		mdelete ( cr , sizeof(CollectionRec) , "CollectionRec" );
		delete ( cr );
		return log( LOG_WARN, "admin: Failed to save file %s: %s", dname,mstrerror(g_errno));
	}


	if ( ! registerCollRec ( cr , true ) ) {
		return false;
	}

	// add the rdbbases for this coll, CollectionRec::m_bases[]
	return addRdbBasesForCollRec ( cr );
}

void CollectionRec::setBasePtr ( char rdbId , class RdbBase *base ) {
	if ( rdbId < 0 || rdbId >= RDB_END ) { char *xx=NULL;*xx=0; }
	// Rdb::deleteColl() will call this even though we are swapped in
	// but it calls it with "base" set to NULL after it nukes the RdbBase
	// so check if base is null here.
	if ( base && m_bases[ (unsigned char)rdbId ]){ char *xx=NULL;*xx=0; }
	m_bases [ (unsigned char)rdbId ] = base;
}

RdbBase *CollectionRec::getBasePtr ( char rdbId ) {
	if ( rdbId < 0 || rdbId >= RDB_END ) { char *xx=NULL;*xx=0; }
	return m_bases [ (unsigned char)rdbId ];
}

static bool s_inside = false;

// . returns NULL w/ g_errno set on error.
// . TODO: ensure not called from in thread, not thread safe
RdbBase *CollectionRec::getBase ( char rdbId ) {

	if ( s_inside ) { char *xx=NULL;*xx=0; }

	return m_bases[(unsigned char)rdbId];
}


// . called only by addNewColl() and by addExistingColl()
bool Collectiondb::registerCollRec ( CollectionRec *cr ,  bool isNew ) {
	// add m_recs[] and to hashtable
	return setRecPtr ( cr->m_collnum , cr );
}

// swap it in
bool Collectiondb::addRdbBaseToAllRdbsForEachCollRec ( ) {
	for ( int32_t i = 0 ; i < m_numRecs ; i++ ) {
		CollectionRec *cr = m_recs[i];
		if ( ! cr ) continue;
		// add rdb base files etc. for it
		addRdbBasesForCollRec ( cr );
	}

	// now clean the trees. moved this into here from
	// addRdbBasesForCollRec() since we call addRdbBasesForCollRec()
	// now from getBase() to load on-demand for saving memory
	cleanTrees();

	return true;
}

bool Collectiondb::addRdbBasesForCollRec ( CollectionRec *cr ) {

	char *coll = cr->m_coll;

	//////
	//
	// if we are doing a dump from the command line, skip this stuff
	//
	//////
	if ( g_dumpMode ) return true;

	// tell rdbs to add one, too
	if ( ! g_posdb.getRdb()->addRdbBase1        ( coll ) ) goto hadError;
	if ( ! g_titledb.getRdb()->addRdbBase1      ( coll ) ) goto hadError;
	if ( ! g_tagdb.getRdb()->addRdbBase1        ( coll ) ) goto hadError;
	if ( ! g_clusterdb.getRdb()->addRdbBase1    ( coll ) ) goto hadError;
	if ( ! g_linkdb.getRdb()->addRdbBase1       ( coll ) ) goto hadError;
	if ( ! g_spiderdb.getRdb()->addRdbBase1     ( coll ) ) goto hadError;
	if ( ! g_doledb.getRdb()->addRdbBase1       ( coll ) ) goto hadError;

	// now clean the trees
	//cleanTrees();

	// debug message
	//log ( LOG_INFO, "db: verified collection \"%s\" (%" PRId32").",
	//      coll,(int32_t)cr->m_collnum);

	// tell SpiderCache about this collection, it will create a
	// SpiderCollection class for it.
	//g_spiderCache.reset1();

	// success
	return true;

 hadError:
	log(LOG_WARN, "db: error registering coll: %s",mstrerror(g_errno));
	return false;
}

/// this deletes the collection, not just part of a reset.
bool Collectiondb::deleteRec2 ( collnum_t collnum ) { //, WaitEntry *we ) {
	// do not allow this if in repair mode
	if ( g_repair.isRepairActive() && g_repair.m_collnum == collnum ) {
		log(LOG_WARN, "admin: Can not delete collection while in repair mode.");
		g_errno = EBADENGINEER;
		return true;
	}
	// bitch if not found
	if ( collnum < 0 ) {
		g_errno = ENOTFOUND;
		log(LOG_LOGIC,"admin: Collection #%" PRId32" is bad, "
		    "delete failed.",(int32_t)collnum);
		return true;
	}
	CollectionRec *cr = m_recs [ collnum ];
	if ( ! cr ) {
		log(LOG_WARN, "admin: Collection id problem. Delete failed.");
		g_errno = ENOTFOUND;
		return true;
	}

	if ( g_process.isAnyTreeSaving() ) {
		// note it
		log("admin: tree is saving. waiting2.");
		// all done
		return false;
	}

	char *coll = cr->m_coll;

	// note it
	log(LOG_INFO,"db: deleting coll \"%s\" (%" PRId32")",coll,
	    (int32_t)cr->m_collnum);

	// we need a save
	m_needsSave = true;

	// CAUTION: tree might be in the middle of saving
	// we deal with this in Process.cpp now

	// . TODO: remove from g_sync
	// . remove from all rdbs
	g_posdb.getRdb()->delColl    ( coll );

	g_titledb.getRdb()->delColl    ( coll );
	g_tagdb.getRdb()->delColl ( coll );
	g_spiderdb.getRdb()->delColl   ( coll );
	g_doledb.getRdb()->delColl     ( coll );
	g_clusterdb.getRdb()->delColl  ( coll );
	g_linkdb.getRdb()->delColl     ( coll );

	// reset spider info
	SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull(collnum);
	if ( sc ) {
		// remove locks from lock table:
		sc->clearLocks();

		// you have to set this for tryToDeleteSpiderColl to
		// actually have a shot at deleting it
		sc->m_deleteMyself = true;

		sc->setCollectionRec ( NULL );
		// this will put it on "death row" so it will be deleted
		// once Msg5::m_waitingForList/Merge is NULL
		tryToDeleteSpiderColl ( sc , "10" );

		// don't let cr reference us anymore, sc is on deathrow
		// and "cr" is delete below!
		cr->m_spiderColl = NULL;
	}


	// the bulk urls file too i guess
	if ( cr->m_isCustomCrawl == 2 && g_hostdb.m_hostId == 0 ) {
		SafeBuf bu;
		bu.safePrintf("%sbulkurls-%s.txt",
			      g_hostdb.m_dir , cr->m_coll );
		File bf;
		bf.set ( bu.getBufStart() );
		if ( bf.doesExist() ) bf.unlink();
	}

	// now remove from list of collections that might need a disk merge
	removeFromMergeLinkedList ( cr );

	//////
	//
	// remove from m_recs[]
	//
	//////
	setRecPtr ( cr->m_collnum , NULL );

	// free it
	mdelete ( cr, sizeof(CollectionRec),  "CollectionRec" );
	delete ( cr );

	// do not do this here in case spiders were outstanding
	// and they added a new coll right away and it ended up getting
	// recs from the deleted coll!!
	//while ( ! m_recs[m_numRecs-1] ) m_numRecs--;

	// update the time
	//updateTime();
	// done
	return true;
}

// ensure m_recs[] is big enough for m_recs[collnum] to be a ptr
bool Collectiondb::growRecPtrBuf ( collnum_t collnum ) {

	// an add, make sure big enough
	int32_t need = ((int32_t)collnum+1)*sizeof(CollectionRec *);
	int32_t have = m_recPtrBuf.getLength();
	int32_t need2 = need - have;

	// if already big enough
	if ( need2 <= 0 ) {
		m_recs [ collnum ] = NULL;
		return true;
	}

	m_recPtrBuf.setLabel ("crecptrb");

	// . true here means to clear the new space to zeroes
	// . this shit works based on m_length not m_capacity
	if ( ! m_recPtrBuf.reserve ( need2 ,NULL, true ) ) {
		log( LOG_WARN, "admin: error growing rec ptr buf2.");
		return false;
	}

	// sanity
	if ( m_recPtrBuf.getCapacity() < need ) { char *xx=NULL;*xx=0; }

	// set it
	m_recs = (CollectionRec **)m_recPtrBuf.getBufStart();

	// update length of used bytes in case we re-alloc
	m_recPtrBuf.setLength ( need );

	// re-max
	int32_t max = m_recPtrBuf.getCapacity() / sizeof(CollectionRec *);
	// sanity
	if ( collnum >= max ) { char *xx=NULL;*xx=0; }

	// initialize slot
	m_recs [ collnum ] = NULL;

	return true;
}


bool Collectiondb::setRecPtr ( collnum_t collnum , CollectionRec *cr ) {

	// first time init hashtable that maps coll to collnum
	if ( g_collTable.m_numSlots == 0 &&
	     ! g_collTable.set(8,sizeof(collnum_t), 256,NULL,0, false,0,"nhshtbl")) {
		return false;
	}

	// sanity
	if ( collnum < 0 ) { char *xx=NULL;*xx=0; }

	// sanity
	int32_t max = m_recPtrBuf.getCapacity() / sizeof(CollectionRec *);

	// set it
	m_recs = (CollectionRec **)m_recPtrBuf.getBufStart();

	// tell spiders to re-upadted the active list
	g_spiderLoop.m_activeListValid = false;
	g_spiderLoop.m_activeListModified = true;

	// a delete?
	if ( ! cr ) {
		// sanity
		if ( collnum >= max ) { char *xx=NULL;*xx=0; }
		// get what's there
		CollectionRec *oc = m_recs[collnum];
		// let it go
		m_recs[collnum] = NULL;
		// if nothing already, done
		if ( ! oc ) return true;
		// tally it up
		m_numRecsUsed--;
		// delete key
		int64_t h64 = hash64n(oc->m_coll);
		// if in the hashtable UNDER OUR COLLNUM then nuke it
		// otherwise, we might be called from resetColl2()
		void *vp = g_collTable.getValue ( &h64 );
		if ( ! vp ) return true;
		collnum_t ct = *(collnum_t *)vp;
		if ( ct != collnum ) return true;
		g_collTable.removeKey ( &h64 );
		return true;
	}

	// ensure m_recs[] is big enough for m_recs[collnum] to be a ptr
	if ( ! growRecPtrBuf ( collnum ) ) {
		return false;
	}

	// sanity
	if ( cr->m_collnum != collnum ) { char *xx=NULL;*xx=0; }

	// add to hash table to map name to collnum_t
	int64_t h64 = hash64n(cr->m_coll);
	// debug
	//log("coll: adding key %" PRId64" for %s",h64,cr->m_coll);
	if ( ! g_collTable.addKey ( &h64 , &collnum ) )
		return false;

	// ensure last is NULL
	m_recs[collnum] = cr;

	// count it
	m_numRecsUsed++;

	//log("coll: adding key4 %" PRIu64" for coll \"%s\" (%" PRId32")",h64,cr->m_coll,
	//    (int32_t)i);

	// reserve it
	if ( collnum >= m_numRecs ) m_numRecs = collnum + 1;

	// sanity to make sure collectionrec ptrs are legit
	for ( int32_t j = 0 ; j < m_numRecs ; j++ ) {
		if ( ! m_recs[j] ) continue;
		if ( m_recs[j]->m_collnum == 1 ) continue;
	}

	return true;
}

// . returns false if we need a re-call, true if we completed
// . returns true with g_errno set on error
bool Collectiondb::resetColl2( collnum_t oldCollnum, collnum_t newCollnum, bool purgeSeeds ) {
	// do not allow this if in repair mode
	if ( g_repair.isRepairActive() && g_repair.m_collnum == oldCollnum ) {
		log(LOG_WARN, "admin: Can not delete collection while in repair mode.");
		g_errno = EBADENGINEER;
		return true;
	}

	//log("admin: resetting collnum %" PRId32,(int32_t)oldCollnum);

	// CAUTION: tree might be in the middle of saving
	// we deal with this in Process.cpp now
	if ( g_process.isAnyTreeSaving() ) {
		// we could not complete...
		return false;
	}

	CollectionRec *cr = m_recs [ oldCollnum ];

	// let's reset crawlinfo crap
	cr->m_globalCrawlInfo.reset();
	cr->m_localCrawlInfo.reset();

	// reset spider info
	SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull(oldCollnum);
	if ( sc ) {
		// remove locks from lock table:
		sc->clearLocks();

		// this will put it on "death row" so it will be deleted
		// once Msg5::m_waitingForList/Merge is NULL
		tryToDeleteSpiderColl ( sc, "11" );

		cr->m_spiderColl = NULL;
	}

	// reset spider round
	cr->m_spiderRoundNum = 0;
	cr->m_spiderRoundStartTime = 0;

	cr->m_spiderStatus = SP_INITIALIZING; // this is 0
	//cr->m_spiderStatusMsg = NULL;

	// reset seed buf
	if ( purgeSeeds ) {
		// free the buffer of seed urls
		cr->m_diffbotSeeds.purge();
		// reset seed dedup table
		HashTableX *ht = &cr->m_seedHashTable;
		ht->reset();
	}

	// so XmlDoc.cpp can detect if the collection was reset since it
	// launched its spider:
	cr->m_lastResetCount++;


	if ( newCollnum >= m_numRecs ) m_numRecs = (int32_t)newCollnum + 1;

	// advance sanity check. did we wrap around?
	// right now we #define collnum_t int16_t
	if ( m_numRecs > 0x7fff ) { char *xx=NULL;*xx=0; }

	// make a new collnum so records in transit will not be added
	// to any rdb...
	cr->m_collnum = newCollnum;

	// update the timestamps since we are restarting/resetting
	cr->m_diffbotCrawlStartTime = getTimeGlobalNoCore();
	cr->m_diffbotCrawlEndTime   = 0;


	////////
	//
	// ALTER m_recs[] array
	//
	////////

	// Rdb::resetColl() needs to know the new cr so it can move
	// the RdbBase into cr->m_bases[rdbId] array. recycling.
	setRecPtr ( newCollnum , cr );

	// a new directory then since we changed the collnum
	char dname[512];
	sprintf(dname, "%scoll.%s.%" PRId32"/",
		g_hostdb.m_dir,
		cr->m_coll,
		(int32_t)newCollnum);
	DIR *dir = opendir ( dname );
	if ( dir )
	     closedir ( dir );
	if ( dir ) {
		//g_errno = EEXIST;
		log("admin: Trying to create collection %s but "
		    "directory %s already exists on disk.",cr->m_coll,dname);
	}
	if ( ::mkdir ( dname ,
		       getDirCreationFlags() ) ) {
		       // S_IRUSR | S_IWUSR | S_IXUSR |
		       // S_IRGRP | S_IWGRP | S_IXGRP |
		       // S_IROTH | S_IXOTH ) ) {
		// valgrind?
		//if ( errno == EINTR ) goto retry22;
		//g_errno = errno;
		log("admin: Creating directory %s had error: "
		    "%s.", dname,mstrerror(g_errno));
	}

	// be sure to copy back the bulk urls for bulk jobs
	// MDW: now i just store that file in the root working dir
	//if (cr->m_isCustomCrawl == 2)
	//	mv( tmpbulkurlsname, newbulkurlsname );

	// . unlink all the *.dat and *.map files for this coll in its subdir
	// . remove all recs from this collnum from m_tree/m_buckets
	// . updates RdbBase::m_collnum
	// . so for the tree it just needs to mark the old collnum recs
	//   with a collnum -1 in case it is saving...
	g_posdb.getRdb()->deleteColl     ( oldCollnum , newCollnum );
	g_titledb.getRdb()->deleteColl   ( oldCollnum , newCollnum );
	g_tagdb.getRdb()->deleteColl     ( oldCollnum , newCollnum );
	g_spiderdb.getRdb()->deleteColl  ( oldCollnum , newCollnum );
	g_doledb.getRdb()->deleteColl    ( oldCollnum , newCollnum );
	g_clusterdb.getRdb()->deleteColl ( oldCollnum , newCollnum );
	g_linkdb.getRdb()->deleteColl    ( oldCollnum , newCollnum );

	// reset crawl status too!
	cr->m_spiderStatus = SP_INITIALIZING;

	// . set m_recs[oldCollnum] to NULL and remove from hash table
	// . do after calls to deleteColl() above so it wont crash
	setRecPtr ( oldCollnum , NULL );


	// save coll.conf to new directory
	cr->save();

	// and clear the robots.txt cache in case we recently spidered a
	// robots.txt, we don't want to use it, we want to use the one we
	// have in the test-parser subdir so we are consistent
	//RdbCache *robots = Msg13::getHttpCacheRobots();
	//RdbCache *others = Msg13::getHttpCacheOthers();
	// clear() was removed do to possible corruption
	//robots->clear ( oldCollnum );
	//others->clear ( oldCollnum );

	//g_templateTable.reset();
	//g_templateTable.save( g_hostdb.m_dir , "turkedtemplates.dat" );

	// repopulate CollectionRec::m_sortByDateTable. should be empty
	// since we are resetting here.
	//initSortByDateTable ( coll );

	// done
	return true;
}

// a hack function
bool addCollToTable ( char *coll , collnum_t collnum ) {
	// readd it to the hashtable that maps name to collnum too
	int64_t h64 = hash64n(coll);
	g_collTable.set(8,sizeof(collnum_t), 256,NULL,0,
			false,0,"nhshtbl");
	return g_collTable.addKey ( &h64 , &collnum );
}

// get coll rec specified in the HTTP request
CollectionRec *Collectiondb::getRec ( HttpRequest *r , bool useDefaultRec ) {
	const char *coll = r->getString ( "c" );
	if ( coll && ! coll[0] ) coll = NULL;
	// maybe it is crawlbot?
	const char *name = NULL;
	const char *token = NULL;
	if ( ! coll ) {
		name = r->getString("name");
		token = r->getString("token");
	}
	char tmp[MAX_COLL_LEN+1];
	if ( ! coll && token && name ) {
		snprintf(tmp,MAX_COLL_LEN,"%s-%s",token,name);
		coll = tmp;
	}

	// default to main first
	if ( ! coll && useDefaultRec ) {
		CollectionRec *cr = g_collectiondb.getRec("main");
		if ( cr ) return cr;
	}

	// try next in line
	if ( ! coll && useDefaultRec ) {
		return getFirstRec ();
	}

	// give up?
	if ( ! coll ) return NULL;
	//if ( ! coll || ! coll[0] ) coll = g_conf.m_defaultColl;
	return g_collectiondb.getRec ( coll );
}

const char *Collectiondb::getDefaultColl ( HttpRequest *r ) {
	const char *coll = r->getString ( "c" );
	if ( coll && ! coll[0] ) coll = NULL;
	if ( coll ) return coll;
	CollectionRec *cr = NULL;
	// default to main first
	if ( ! coll ) {
		cr = g_collectiondb.getRec("main");
		// CAUTION: cr could be deleted so don't trust this ptr
		// if you give up control of the cpu
		if ( cr ) return cr->m_coll;
	}
	// try next in line
	if ( ! coll ) {
		cr = getFirstRec ();
		if ( cr ) return cr->m_coll;
	}
	// give up?
	return NULL;
}


// . get collectionRec from name
// . returns NULL if not available
CollectionRec *Collectiondb::getRec ( const char *coll ) {
	if ( ! coll ) coll = "";
	return getRec ( coll , gbstrlen(coll) );
}

CollectionRec *Collectiondb::getRec ( const char *coll , int32_t collLen ) {
	if ( ! coll ) coll = "";
	collnum_t collnum = getCollnum ( coll , collLen );
	if ( collnum < 0 ) return NULL;
	return m_recs [ (int32_t)collnum ];
}

CollectionRec *Collectiondb::getRec ( collnum_t collnum) {
	if ( collnum >= m_numRecs || collnum < 0 ) {
		// Rdb::resetBase() gets here, so don't always log.
		// it is called from CollectionRec::reset() which is called
		// from the CollectionRec constructor and ::load() so
		// it won't have anything in rdb at that time
		//log("colldb: collnum %" PRId32" > numrecs = %" PRId32,
		//    (int32_t)collnum,(int32_t)m_numRecs);
		return NULL;
	}
	return m_recs[collnum];
}


CollectionRec *Collectiondb::getFirstRec ( ) {
	for ( int32_t i = 0 ; i < m_numRecs ; i++ )
		if ( m_recs[i] ) return m_recs[i];
	return NULL;
}

collnum_t Collectiondb::getFirstCollnum ( ) {
	for ( int32_t i = 0 ; i < m_numRecs ; i++ )
		if ( m_recs[i] ) return i;
	return (collnum_t)-1;
}

char *Collectiondb::getFirstCollName ( ) {
	for ( int32_t i = 0 ; i < m_numRecs ; i++ )
		if ( m_recs[i] ) return m_recs[i]->m_coll;
	return NULL;
}

char *Collectiondb::getCollName ( collnum_t collnum ) {
	if ( collnum < 0 || collnum > m_numRecs ) return NULL;
	if ( ! m_recs[(int32_t)collnum] ) return NULL;
	return m_recs[collnum]->m_coll;
}

collnum_t Collectiondb::getCollnum ( const char *coll ) {

	int32_t clen = 0;
	if ( coll ) clen = gbstrlen(coll );
	return getCollnum ( coll , clen );
}

collnum_t Collectiondb::getCollnum ( const char *coll , int32_t clen ) {

	// default empty collection names
	if ( coll && ! coll[0] ) coll = NULL;
	if ( ! coll ) {
		coll = g_conf.m_defaultColl;
		if ( coll ) clen = gbstrlen(coll);
		else clen = 0;
	}
	if ( ! coll || ! coll[0] ) {
		coll = "main";
		clen = gbstrlen(coll);
	}

	// not associated with any collection. Is this
	// necessary for Catdb?
	if ( coll[0]=='s' && coll[1] =='t' &&
	     strcmp ( "statsdb\0", coll ) == 0)
		return 0;

	// because diffbot may have thousands of crawls/collections
	// let's improve the speed here. try hashing it...
	int64_t h64 = hash64(coll,clen);
	void *vp = g_collTable.getValue ( &h64 );
	if ( ! vp ) return -1; // not found
	return *(collnum_t *)vp;
}


// what collnum will be used the next time a coll is added?
collnum_t Collectiondb::reserveCollNum ( ) {

	if ( m_numRecs < 0x7fff ) {
		collnum_t next = m_numRecs;
		// make the ptr NULL at least to accomodate the
		// loop that scan up to m_numRecs lest we core
		growRecPtrBuf ( next );
		m_numRecs++;
		return next;
	}

	// collnum_t is signed right now because we use -1 to indicate a
	// bad collnum.
	int32_t scanned = 0;
	// search for an empty slot
	for ( int32_t i = m_wrapped ; ; i++ ) {
		// because collnum_t is 2 bytes, signed, limit this here
		if ( i > 0x7fff ) i = 0;
		// how can this happen?
		if ( i < 0      ) i = 0;
		// if we scanned the max # of recs we could have, we are done
		if ( ++scanned >= m_numRecs ) break;
		// skip if this is in use
		if ( m_recs[i] ) continue;
		// start after this one next time
		m_wrapped = i+1;
		// note it
		log("colldb: returning wrapped collnum "
		    "of %" PRId32,(int32_t)i);
		return (collnum_t)i;
	}

	log("colldb: no new collnum available. consider upping collnum_t");
	// none available!!
	return -1;
}


///////////////
//
// COLLECTIONREC
//
///////////////

#include "gb-include.h"

//#include "CollectionRec.h"
//#include "Collectiondb.h"
#include "HttpServer.h"     // printColors2()
#include "Msg5.h"
#include "Spider.h"
#include "Process.h"


CollectionRec::CollectionRec() {
	m_nextLink = NULL;
	m_prevLink = NULL;
	m_spiderCorruptCount = 0;
	m_collnum = -1;
	m_coll[0] = '\0';
	m_updateRoundNum = 0;
	memset ( m_bases , 0 , sizeof(RdbBase *)*RDB_END );
	// how many keys in the tree of each rdb? we now store this stuff
	// here and not in RdbTree.cpp because we no longer have a maximum
	// # of collection recs... MAX_COLLS. each is a 32-bit "int32_t" so
	// it is 4 * RDB_END...
	memset ( m_numNegKeysInTree , 0 , 4*RDB_END );
	memset ( m_numPosKeysInTree , 0 , 4*RDB_END );
	m_spiderColl = NULL;
	m_overflow  = 0x12345678;
	m_overflow2 = 0x12345678;
	// the spiders are currently uninhibited i guess
	m_spiderStatus = SP_INITIALIZING; // this is 0
	// inits for sortbydatetable
	m_msg5       = NULL;
	m_importState = NULL;
	// JAB - track which regex parsers have been initialized
	//log(LOG_DEBUG,"regex: %p initalizing empty parsers", m_pRegExParser);

	// clear these out so Parms::calcChecksum can work:
	memset( m_spiderFreqs, 0, MAX_FILTERS*sizeof(*m_spiderFreqs) );
	//for ( int i = 0; i < MAX_FILTERS ; i++ )
	//	m_spiderQuotas[i] = -1;
	memset( m_spiderPriorities, 0,
		MAX_FILTERS*sizeof(*m_spiderPriorities) );
	memset ( m_harvestLinks,0,MAX_FILTERS);
	memset ( m_forceDelete,0,MAX_FILTERS);

	m_numRegExs = 0;

	//m_requests = 0;
	//m_replies = 0;
	//m_doingCallbacks = false;

	m_lastResetCount = 0;

	// regex_t types
	m_hasucr = false;
	m_hasupr = false;

	// for diffbot caching the global spider stats
	reset();

	// add default reg ex if we do not have one
	//setUrlFiltersToDefaults();
	//rebuildUrlFilters();
}

CollectionRec::~CollectionRec() {
	//invalidateRegEx ();
        reset();
}

void CollectionRec::reset() {

	//log("coll: resetting collnum=%" PRId32,(int32_t)m_collnum);

	// . grows dynamically
	// . setting to 0 buckets should never have error
	//m_pageCountTable.set ( 4,4,0,NULL,0,false,MAX_NICENESS,"pctbl" );

	// regex_t types
	if ( m_hasucr ) regfree ( &m_ucr );
	if ( m_hasupr ) regfree ( &m_upr );

	m_hasucr = false;
	m_hasupr = false;

	m_sendingAlertInProgress = false;

	// make sure we do not leave spiders "hanging" waiting for their
	// callback to be called... and it never gets called
	//if ( m_callbackQueue.length() > 0 ) { char *xx=NULL;*xx=0; }
	//if ( m_doingCallbacks ) { char *xx=NULL;*xx=0; }
	//if ( m_replies != m_requests  ) { char *xx=NULL;*xx=0; }
	m_localCrawlInfo.reset();
	m_globalCrawlInfo.reset();
	//m_requests = 0;
	//m_replies = 0;
	// free all RdbBases in each rdb
	for ( int32_t i = 0 ; i < g_process.m_numRdbs ; i++ ) {
	     Rdb *rdb = g_process.m_rdbs[i];
	     rdb->resetBase ( m_collnum );
	}

	for ( int32_t i = 0 ; i < g_process.m_numRdbs ; i++ ) {
		RdbBase *base = m_bases[i];
		if ( ! base ) continue;
		mdelete (base, sizeof(RdbBase), "Rdb Coll");
		delete  (base);
	}

	SpiderColl *sc = m_spiderColl;
	// debug hack thing
	//if ( sc == (SpiderColl *)0x8888 ) return;
	// if never made one, we are done
	if ( ! sc ) return;

	// spider coll also!
	sc->m_deleteMyself = true;

	// if not currently being accessed nuke it now
	tryToDeleteSpiderColl ( sc , "12" );
}

// . load this data from a conf file
// . values we do not explicitly have will be taken from "default",
//   collection config file. if it does not have them then we use
//   the value we received from call to setToDefaults()
// . returns false and sets g_errno on load error
bool CollectionRec::load ( const char *coll , int32_t i ) {
	// also reset some counts not included in parms list
	reset();
	// before we load, set to defaults in case some are not in xml file
	g_parms.setToDefault ( (char *)this , OBJ_COLL , this );
	// get the filename with that id
	File f;
	char tmp2[1024];
	sprintf ( tmp2 , "%scoll.%s.%" PRId32"/coll.conf", g_hostdb.m_dir , coll,i);
	f.set ( tmp2 );
	if ( ! f.doesExist () ) return log("admin: %s does not exist.",tmp2);
	// set our collection number
	m_collnum = i;
	// set our collection name
	m_collLen = gbstrlen ( coll );
	if ( coll != m_coll)
		strcpy ( m_coll , coll );

	if ( ! g_conf.m_doingCommandLine )
		log(LOG_INFO,"db: Loading conf for collection %s (%" PRId32")",coll,
		    (int32_t)m_collnum);

	// the default conf file
	char tmp1[1024];
	snprintf ( tmp1 , 1023, "%sdefault.conf" , g_hostdb.m_dir );

	// . set our parms from the file.
	// . accepts OBJ_COLLECTIONREC or OBJ_CONF
	g_parms.setFromFile ( this , tmp2 , tmp1 , OBJ_COLL );

	// this only rebuild them if necessary
	rebuildUrlFilters();//setUrlFiltersToDefaults();

	//
	// LOAD the crawlinfo class in the collectionrec for diffbot
	//
	// LOAD LOCAL
	snprintf ( tmp1 , 1023, "%scoll.%s.%" PRId32"/localcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (int32_t)m_collnum );
	log(LOG_DEBUG,"db: Loading %s",tmp1);
	m_localCrawlInfo.reset();
	SafeBuf sb;
	// fillfromfile returns 0 if does not exist, -1 on read error
	if ( sb.fillFromFile ( tmp1 ) > 0 )
		//m_localCrawlInfo.setFromSafeBuf(&sb);
		// it is binary now
		gbmemcpy ( &m_localCrawlInfo , sb.getBufStart(),sb.length() );


	if ( ! g_conf.m_doingCommandLine && ! g_collectiondb.m_initializing )
		log("coll: Loaded %s (%" PRId32") local hasurlsready=%" PRId32,
		    m_coll,
		    (int32_t)m_collnum,
		    (int32_t)m_localCrawlInfo.m_hasUrlsReadyToSpider);


	// we introduced the this round counts, so don't start them at 0!!
	if ( m_spiderRoundNum == 0 &&
	     m_localCrawlInfo.m_pageDownloadSuccessesThisRound <
	     m_localCrawlInfo.m_pageDownloadSuccesses ) {
		log("coll: fixing process count this round for %s",m_coll);
		m_localCrawlInfo.m_pageDownloadSuccessesThisRound =
			m_localCrawlInfo.m_pageDownloadSuccesses;
	}

	// we introduced the this round counts, so don't start them at 0!!
	if ( m_spiderRoundNum == 0 &&
	     m_localCrawlInfo.m_pageProcessSuccessesThisRound <
	     m_localCrawlInfo.m_pageProcessSuccesses ) {
		log("coll: fixing process count this round for %s",m_coll);
		m_localCrawlInfo.m_pageProcessSuccessesThisRound =
			m_localCrawlInfo.m_pageProcessSuccesses;
	}

	// LOAD GLOBAL
	snprintf ( tmp1 , 1023, "%scoll.%s.%" PRId32"/globalcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (int32_t)m_collnum );
	log(LOG_DEBUG,"db: Loading %s",tmp1);
	m_globalCrawlInfo.reset();
	sb.reset();
	if ( sb.fillFromFile ( tmp1 ) > 0 )
		//m_globalCrawlInfo.setFromSafeBuf(&sb);
		// it is binary now
		gbmemcpy ( &m_globalCrawlInfo , sb.getBufStart(),sb.length() );

	if ( ! g_conf.m_doingCommandLine && ! g_collectiondb.m_initializing )
		log("coll: Loaded %s (%" PRId32") global hasurlsready=%" PRId32,
		    m_coll,
		    (int32_t)m_collnum,
		    (int32_t)m_globalCrawlInfo.m_hasUrlsReadyToSpider);

	// the list of ip addresses that we have detected as being throttled
	// and therefore backoff and use proxies for
	if ( ! g_conf.m_doingCommandLine ) {
		sb.reset();
		sb.safePrintf("%scoll.%s.%" PRId32"/",
			      g_hostdb.m_dir , m_coll , (int32_t)m_collnum );
		m_twitchyTable.m_allocName = "twittbl";
		m_twitchyTable.load ( sb.getBufStart() , "ipstouseproxiesfor.dat" );
	}

	// ignore errors i guess
	g_errno = 0;


	// fix for diffbot, spider time deduping
	if ( m_isCustomCrawl ) m_dedupingEnabled = true;

	// make min to merge smaller than normal since most collections are
	// small and we want to reduce the # of vfds (files) we have
	if ( m_isCustomCrawl ) {
		m_posdbMinFilesToMerge   = 6;
		m_titledbMinFilesToMerge = 4;
		m_linkdbMinFilesToMerge  = 3;
		m_tagdbMinFilesToMerge   = 2;
		m_spiderdbMinFilesToMerge = 4;
	}

	return true;
}

bool CollectionRec::rebuildUrlFilters2 ( ) {

	// tell spider loop to update active list
	g_spiderLoop.m_activeListValid = false;

	bool rebuild = true;
	if ( m_numRegExs == 0 )
		rebuild = true;
	// don't touch it if not supposed to as int32_t as we have some already
	//if ( m_urlFiltersProfile != UFP_NONE )
	//	rebuild = true;
	// never for custom crawls however
	if ( m_isCustomCrawl )
		rebuild = false;

	const char *s = m_urlFiltersProfile.getBufStart();

	// support the old UFP_CUSTOM, etc. numeric values
	if ( !strcmp(s,"0" ) )
		s = "custom";
	// UFP_WEB SUPPORT
	if ( !strcmp(s,"1" ) )
		s = "web";
	// UFP_NEWS
	if ( !strcmp(s,"2" ) )
		s = "shallow";



	// leave custom profiles alone
	if ( !strcmp(s,"custom" ) )
		rebuild = false;



	//if ( m_numRegExs > 0 && strcmp(m_regExs[m_numRegExs-1],"default") )
	//	addDefault = true;
	if ( ! rebuild ) return true;


	if ( !strcmp(s,"privacore" ) )
		return rebuildPrivacoreRules();


	if ( !strcmp(s,"shallow" ) )
		return rebuildShallowRules();

	//if ( strcmp(s,"web") )
	// just fall through for that


	if ( !strcmp(s,"english") )
		return rebuildLangRules( "en","com,us,gov");

	if ( !strcmp(s,"german") )
		return rebuildLangRules( "de","de");

	if ( !strcmp(s,"french") )
		return rebuildLangRules( "fr","fr");

	if ( !strcmp(s,"norwegian") )
		return rebuildLangRules( "nl","nl");

	if ( !strcmp(s,"spanish") )
		return rebuildLangRules( "es","es");

	//if ( m_urlFiltersProfile == UFP_EURO )
	//	return rebuildLangRules( "de,fr,nl,es,sv,no,it",
	//				 "com,gov,org,de,fr,nl,es,sv,no,it");


	if ( !strcmp(s,"romantic") )
		return rebuildLangRules("en,de,fr,nl,es,sv,no,it,fi,pt",
					"de,fr,nl,es,sv,no,it,fi,pt,"
					"com,gov,org"
					);

	if ( !strcmp(s,"chinese") )
		return rebuildLangRules( "zh_cn,zh_tw","cn");


	int32_t n = 0;

	/*
	m_regExs[n].set("default");
	m_regExs[n].nullTerm();
	m_spiderFreqs     [n] = 30; // 30 days default
	m_spiderPriorities[n] = 0;
	m_maxSpidersPerRule[n] = 99;
	m_spiderIpWaits[n] = 1000;
	m_spiderIpMaxSpiders[n] = 7;
	m_harvestLinks[n] = 1;
	*/

	// max spiders per ip
	int32_t ipms = 7;

	m_regExs[n].set("isreindex");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 0; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 80;
	n++;

	m_regExs[n].set("ismedia");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 0; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 100; // delete!
	m_forceDelete        [n] = 1;
	n++;

	// if not in the site list then nuke it
	m_regExs[n].set("!ismanualadd && !insitelist");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 0; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 100;
	m_forceDelete        [n] = 1;
	n++;

	m_regExs[n].set("errorcount>=3 && hastmperror");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 1; // 30 days default
	m_maxSpidersPerRule  [n] = 1; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 100;
	m_forceDelete        [n] = 1;
	n++;

	m_regExs[n].set("errorcount>=1 && hastmperror");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 1; // 30 days default
	m_maxSpidersPerRule  [n] = 1; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 45;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;

	// a non temporary error, like a 404? retry once per 5 days
	m_regExs[n].set("errorcount>=1");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 5; // 5 day retry
	m_maxSpidersPerRule  [n] = 1; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 2;
	m_forceDelete        [n] = 1;
	n++;

	m_regExs[n].set("isaddurl");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 85;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;

	// 20+ unique c block parent request urls means it is important!
	m_regExs[n].set("numinlinks>7 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 52;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;

	// 20+ unique c block parent request urls means it is important!
	m_regExs[n].set("numinlinks>7");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 51;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;



	m_regExs[n].set("hopcount==0 && iswww && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 50;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;

	m_regExs[n].set("hopcount==0 && iswww");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7.0; // days b4 respider
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 48;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;

	m_regExs[n].set("hopcount==0 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 49;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;

	m_regExs[n].set("hopcount==0");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 10.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 47;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;


	m_regExs[n].set("isparentrss && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 45;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;

	m_regExs[n].set("isparentsitemap && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 44;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;


	m_regExs[n].set("isparentrss");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 20.0; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 43;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;

	m_regExs[n].set("isparentsitemap");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 20.0; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 42;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .00347; // 5 mins
	n++;




	m_regExs[n].set("hopcount==1 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 40;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .04166; // 60 minutes
	n++;

	m_regExs[n].set("hopcount==1");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 39;
	if ( ! strcmp(s,"news") )
		m_spiderFreqs [n] = .04166; // 60 minutes
	n++;

	m_regExs[n].set("hopcount==2 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 30;
	// do not harvest links if we are spiderings NEWS
	if ( ! strcmp(s,"news") ) {
		m_spiderFreqs  [n] = 5.0;
		m_harvestLinks [n] = 0;
	}
	n++;

	m_regExs[n].set("hopcount==2");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 29;
	// do not harvest links if we are spiderings NEWS
	if ( ! strcmp(s,"news") ) {
		m_spiderFreqs  [n] = 5.0;
		m_harvestLinks [n] = 0;
	}
	n++;

	m_regExs[n].set("hopcount>=3 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 20;
	// turn off spidering if hopcount is too big and we are spiderings NEWS
	if ( ! strcmp(s,"news") ) {
		m_maxSpidersPerRule [n] = 0;
		m_harvestLinks      [n] = 0;
	}
	else {
		n++;
	}

	m_regExs[n].set("hopcount>=3");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 19;
	// turn off spidering if hopcount is too big and we are spiderings NEWS
	if ( ! strcmp(s,"news") ) {
		m_maxSpidersPerRule [n] = 0;
		m_harvestLinks      [n] = 0;
	}
	else {
		n++;
	}

	m_regExs[n].set("default");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 1;
	if ( ! strcmp(s,"news") ) {
		m_maxSpidersPerRule [n] = 0;
		m_harvestLinks      [n] = 0;
	}
	n++;


	m_numRegExs				= n;
	m_numSpiderFreqs		= n;
	m_numSpiderPriorities	= n;
	m_numMaxSpidersPerRule	= n;
	m_numSpiderIpWaits		= n;
	m_numSpiderIpMaxSpiders	= n;
	m_numHarvestLinks		= n;
	m_numForceDelete		= n;

	return true;
}



bool CollectionRec::rebuildPrivacoreRules () {
	const char *langWhitelistStr = "xx,en,bg,sr,ca,cs,da,et,fi,fr,de,el,hu,is,ga,it,lv,lt,lb,nl,pl,pt,ro,es,sv,no,vv";
	const char *tldBlacklistStr = "cn,vn,kr,my,in,pk,ru,ua,jp,th";

	// max spiders per ip
	int32_t ipms = 7;

	int32_t n = 0;

	m_regExs[n].set("isreindex");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 0; 		// 0 days default
	m_maxSpidersPerRule  [n] = 99; 		// max spiders
	m_spiderIpMaxSpiders [n] = 1; 		// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 80;
	n++;

	m_regExs[n].set("ismedia");
	m_harvestLinks       [n] = 0;
	m_spiderFreqs        [n] = 0; 		// 0 days default
	m_maxSpidersPerRule  [n] = 99; 		// max spiders
	m_spiderIpMaxSpiders [n] = 1; 		// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 100;
	m_forceDelete        [n] = 1;		// delete!
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("lang!=%s", langWhitelistStr);
	m_harvestLinks       [n] = 0;
	m_spiderFreqs        [n] = 0; 		// 0 days default
	m_maxSpidersPerRule  [n] = 99; 		// max spiders
	m_spiderIpMaxSpiders [n] = 1; 		// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 100;
	m_forceDelete        [n] = 1;		// delete!
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("tld==%s", tldBlacklistStr);
	m_harvestLinks       [n] = 0;
	m_spiderFreqs        [n] = 0; 		// 0 days default
	m_maxSpidersPerRule  [n] = 99; 		// max spiders
	m_spiderIpMaxSpiders [n] = 1; 		// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 100;
	m_forceDelete        [n] = 1;		// delete!
	n++;




	// 3 or more non-temporary errors - delete it
	m_regExs[n].set("errorcount>=3 && !hastmperror");
	m_harvestLinks       [n] = 0;
	m_spiderFreqs        [n] = 0; 		// 1 days default
	m_maxSpidersPerRule  [n] = 99; 		// max spiders
	m_spiderIpMaxSpiders [n] = 1; 		// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 100;
	m_forceDelete        [n] = 1;		// delete!
	n++;

	// 3 or more temporary errors - slow down retries a bit
	m_regExs[n].set("errorcount>=3 && hastmperror");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 3; 		// 1 days default
	m_maxSpidersPerRule  [n] = 1; 		// max spiders
	m_spiderIpMaxSpiders [n] = 1; 		// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 45;
	n++;

	// 1 or more temporary errors - retry in a day
	m_regExs[n].set("errorcount>=1 && hastmperror");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 1; 		// 1 days default
	m_maxSpidersPerRule  [n] = 1; 		// max spiders
	m_spiderIpMaxSpiders [n] = 1; 		// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 45;
	n++;


	m_regExs[n].set("isaddurl");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7; 		// 7 days default
	m_maxSpidersPerRule  [n] = 99; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 85;
	n++;

	m_regExs[n].set("hopcount==0 && iswww && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7; 		// 7 days default
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 50;
	n++;

	m_regExs[n].set("hopcount==0 && iswww");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7.0; 	// 7 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 48;
	n++;

	m_regExs[n].set("hopcount==0 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7.0;		// 7 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 18;
	n++;

	m_regExs[n].set("hopcount==0");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 10.0;	// 10 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 17;
	n++;


	m_regExs[n].set("hopcount==1 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 20.0;	// 20 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 16;
	n++;

	m_regExs[n].set("hopcount==1");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 20.0;	// 20 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 15;
	n++;


	m_regExs[n].set("hopcount==2 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 40;		// 40 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 14;
	n++;

	m_regExs[n].set("hopcount==2");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 40;		// 40 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 13;
	n++;



	m_regExs[n].set("hopcount>=3 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;		// 60 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 12;
	n++;

	m_regExs[n].set("hopcount>=3");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;		// 60 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 11;
	n++;


	m_regExs[n].set("default");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;		// 60 days before respider
	m_maxSpidersPerRule  [n] = 9; 		// max spiders
	m_spiderIpMaxSpiders [n] = ipms; 	// max spiders per ip
	m_spiderIpWaits      [n] = 1000; 	// same ip wait
	m_spiderPriorities   [n] = 1;
	n++;


	m_numRegExs				= n;
	m_numSpiderFreqs		= n;
	m_numSpiderPriorities	= n;
	m_numMaxSpidersPerRule	= n;
	m_numSpiderIpWaits		= n;
	m_numSpiderIpMaxSpiders	= n;
	m_numHarvestLinks		= n;
	m_numForceDelete		= n;

	return true;
}




bool CollectionRec::rebuildLangRules ( const char *langStr , const char *tldStr ) {

	// max spiders per ip
	int32_t ipms = 7;

	int32_t n = 0;

	m_regExs[n].set("isreindex");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 0; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 80;
	n++;

	m_regExs[n].set("ismedia");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 0; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 100; // delete!
	m_forceDelete        [n] = 1;
	n++;

	// if not in the site list then nuke it
	m_regExs[n].set("!ismanualadd && !insitelist");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 0; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 100; // delete!
	m_forceDelete        [n] = 1;
	n++;

	m_regExs[n].set("errorcount>=3 && hastmperror");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 1; // 30 days default
	m_maxSpidersPerRule  [n] = 1; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 100;
	m_forceDelete        [n] = 1;
	n++;

	m_regExs[n].set("errorcount>=1 && hastmperror");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 1; // 30 days default
	m_maxSpidersPerRule  [n] = 1; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 45;
	n++;

	m_regExs[n].set("isaddurl");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 85;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && iswww && isnew && tld==%s",
			       tldStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 50;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && iswww && isnew && "
			       "parentlang==%s,xx"
			       ,langStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 50;
	n++;

	// m_regExs[n].set("hopcount==0 && iswww && isnew");
	// m_harvestLinks       [n] = 1;
	// m_spiderFreqs        [n] = 7; // 30 days default
	// m_maxSpidersPerRule  [n] = 9; // max spiders
	// m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	// m_spiderIpWaits      [n] = 1000; // same ip wait
	// m_spiderPriorities   [n] = 20;
	// n++;



	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && iswww && tld==%s",tldStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7.0; // days b4 respider
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 48;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && iswww && parentlang==%s,xx",
			       langStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7.0; // days b4 respider
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 48;
	n++;

	m_regExs[n].set("hopcount==0 && iswww");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7.0; // days b4 respider
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 19;
	n++;





	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && isnew && tld==%s",tldStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 49;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && isnew && parentlang==%s,xx",
			       langStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 49;
	n++;

	m_regExs[n].set("hopcount==0 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 18;
	n++;



	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && tld==%s",tldStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 10.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 47;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==0 && parentlang==%s,xx",langStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 10.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 47;
	n++;

	m_regExs[n].set("hopcount==0");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 10.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 17;
	n++;




	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==1 && isnew && tld==%s",tldStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 40;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==1 && isnew && parentlang==%s,xx",
			       tldStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 40;
	n++;

	m_regExs[n].set("hopcount==1 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 16;
	n++;



	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==1 && tld==%s",tldStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 39;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==1 && parentlang==%s,xx",langStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 39;
	n++;

	m_regExs[n].set("hopcount==1");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 15;
	n++;



	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==2 && isnew && tld==%s",tldStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 30;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==2 && isnew && parentlang==%s,xx",
			       langStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 30;
	n++;

	m_regExs[n].set("hopcount==2 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 14;
	n++;




	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==2 && tld==%s",tldStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 29;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount==2 && parentlang==%s,xx",langStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 29;
	n++;

	m_regExs[n].set("hopcount==2");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 13;
	n++;




	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount>=3 && isnew && tld==%s",tldStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 22;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount>=3 && isnew && parentlang==%s,xx",
			       langStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 22;
	n++;

	m_regExs[n].set("hopcount>=3 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 12;
	n++;




	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount>=3 && tld==%s",tldStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 21;
	n++;

	m_regExs[n].reset();
	m_regExs[n].safePrintf("hopcount>=3 && parentlang==%s,xx",langStr);
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 21;
	n++;

	m_regExs[n].set("hopcount>=3");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 11;
	n++;



	m_regExs[n].set("default");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 1;
	n++;

	m_numRegExs				= n;
	m_numSpiderFreqs		= n;
	m_numSpiderPriorities	= n;
	m_numMaxSpidersPerRule	= n;
	m_numSpiderIpWaits		= n;
	m_numSpiderIpMaxSpiders	= n;
	m_numHarvestLinks		= n;
	m_numForceDelete		= n;

	// done rebuilding CHINESE rules
	return true;
}

bool CollectionRec::rebuildShallowRules ( ) {

	// max spiders per ip
	int32_t ipms = 7;

	int32_t n = 0;

	m_regExs[n].set("isreindex");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 0; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 80;
	n++;

	m_regExs[n].set("ismedia");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 0; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 100; // delete!
	m_forceDelete        [n] = 1;
	n++;

	// if not in the site list then nuke it
	m_regExs[n].set("!ismanualadd && !insitelist");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 0; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 100; // delete!
	m_forceDelete        [n] = 1;
	n++;

	m_regExs[n].set("errorcount>=3 && hastmperror");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 1; // 30 days default
	m_maxSpidersPerRule  [n] = 1; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 100;
	m_forceDelete        [n] = 1;
	n++;

	m_regExs[n].set("errorcount>=1 && hastmperror");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 1; // 30 days default
	m_maxSpidersPerRule  [n] = 1; // max spiders
	m_spiderIpMaxSpiders [n] = 1; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 45;
	n++;

	m_regExs[n].set("isaddurl");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 99; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 85;
	n++;




	//
	// stop if hopcount>=2 for things tagged shallow in sitelist
	//
	m_regExs[n].set("tag:shallow && hopcount>=2");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 0; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 30;
	n++;


	// if # of pages in this site indexed is >= 10 then stop as well...
	m_regExs[n].set("tag:shallow && sitepages>=10");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 0; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 30;
	n++;




	m_regExs[n].set("hopcount==0 && iswww && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7; // 30 days default
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 50;
	n++;

	m_regExs[n].set("hopcount==0 && iswww");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7.0; // days b4 respider
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 48;
	n++;




	m_regExs[n].set("hopcount==0 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 7.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 49;
	n++;




	m_regExs[n].set("hopcount==0");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 10.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 47;
	n++;





	m_regExs[n].set("hopcount==1 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 40;
	n++;


	m_regExs[n].set("hopcount==1");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 20.0;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 39;
	n++;




	m_regExs[n].set("hopcount==2 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 30;
	n++;

	m_regExs[n].set("hopcount==2");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 40;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 29;
	n++;




	m_regExs[n].set("hopcount>=3 && isnew");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 22;
	n++;

	m_regExs[n].set("hopcount>=3");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 21;
	n++;



	m_regExs[n].set("default");
	m_harvestLinks       [n] = 1;
	m_spiderFreqs        [n] = 60;
	m_maxSpidersPerRule  [n] = 9; // max spiders
	m_spiderIpMaxSpiders [n] = ipms; // max spiders per ip
	m_spiderIpWaits      [n] = 1000; // same ip wait
	m_spiderPriorities   [n] = 1;
	n++;

	m_numRegExs				= n;
	m_numSpiderFreqs		= n;
	m_numSpiderPriorities	= n;
	m_numMaxSpidersPerRule	= n;
	m_numSpiderIpWaits		= n;
	m_numSpiderIpMaxSpiders	= n;
	m_numHarvestLinks		= n;
	m_numForceDelete		= n;

	// done rebuilding SHALLOW rules
	return true;
}

// returns false on failure and sets g_errno, true otherwise
bool CollectionRec::save ( ) {
	if ( g_conf.m_readOnlyMode ) {
		return true;
	}

	//File f;
	char tmp[1024];
	//sprintf ( tmp , "%scollections/%" PRId32".%s/c.conf",
	//	  g_hostdb.m_dir,m_id,m_coll);
	// collection name HACK for backwards compatibility
	//if ( m_collLen == 0 )
	//	sprintf ( tmp , "%scoll.main/coll.conf", g_hostdb.m_dir);
	//else
	snprintf ( tmp , 1023, "%scoll.%s.%" PRId32"/coll.conf",
		  g_hostdb.m_dir , m_coll , (int32_t)m_collnum );
	if ( ! g_parms.saveToXml ( (char *)this , tmp ,OBJ_COLL)) {
		return false;
	}
	// log msg
	//log (LOG_INFO,"db: Saved %s.",tmp);//f.getFilename());

	//
	// save the crawlinfo class in the collectionrec for diffbot
	//
	// SAVE LOCAL
	snprintf ( tmp , 1023, "%scoll.%s.%" PRId32"/localcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (int32_t)m_collnum );
	//log("coll: saving %s",tmp);
	// in case emergency save from malloc core, do not alloc
	char stack[1024];
	SafeBuf sb(stack,1024);
	//m_localCrawlInfo.print ( &sb );
	// binary now
	sb.safeMemcpy ( &m_localCrawlInfo , sizeof(CrawlInfo) );
	if ( sb.safeSave ( tmp ) == -1 ) {
		log("db: failed to save file %s : %s",
		    tmp,mstrerror(g_errno));
		g_errno = 0;
	}
	// SAVE GLOBAL
	snprintf ( tmp , 1023, "%scoll.%s.%" PRId32"/globalcrawlinfo.dat",
		  g_hostdb.m_dir , m_coll , (int32_t)m_collnum );
	//log("coll: saving %s",tmp);
	sb.reset();
	//m_globalCrawlInfo.print ( &sb );
	// binary now
	sb.safeMemcpy ( &m_globalCrawlInfo , sizeof(CrawlInfo) );
	if ( sb.safeSave ( tmp ) == -1 ) {
		log("db: failed to save file %s : %s",
		    tmp,mstrerror(g_errno));
		g_errno = 0;
	}

	// the list of ip addresses that we have detected as being throttled
	// and therefore backoff and use proxies for
	sb.reset();
	sb.safePrintf("%scoll.%s.%" PRId32"/",
		      g_hostdb.m_dir , m_coll , (int32_t)m_collnum );
	m_twitchyTable.save ( sb.getBufStart() , "ipstouseproxiesfor.dat" );

	// do not need a save now
	m_needsSave = false;

	return true;
}

static bool expandRegExShortcuts ( SafeBuf *sb ) ;
void nukeDoledb ( collnum_t collnum );

// rebuild the regexes related to diffbot, such as the one for the URL pattern
bool CollectionRec::rebuildDiffbotRegexes() {
		//logf(LOG_DEBUG,"db: rebuilding url filters");
		char *ucp = m_diffbotUrlCrawlPattern.getBufStart();
		if ( ucp && ! ucp[0] ) ucp = NULL;

		// get the regexes
		if ( ! ucp ) ucp = m_diffbotUrlCrawlRegEx.getBufStart();
		if ( ucp && ! ucp[0] ) ucp = NULL;
		char *upp = m_diffbotUrlProcessPattern.getBufStart();
		if ( upp && ! upp[0] ) upp = NULL;

		if ( ! upp ) upp = m_diffbotUrlProcessRegEx.getBufStart();
		if ( upp && ! upp[0] ) upp = NULL;
		char *ppp = m_diffbotPageProcessPattern.getBufStart();
		if ( ppp && ! ppp[0] ) ppp = NULL;

		// recompiling regexes starts now
		if ( m_hasucr ) {
			regfree ( &m_ucr );
			m_hasucr = false;
		}
		if ( m_hasupr ) {
			regfree ( &m_upr );
			m_hasupr = false;
		}

		// copy into tmpbuf
		SafeBuf tmp;
		char *rx = m_diffbotUrlCrawlRegEx.getBufStart();
		if ( rx && ! rx[0] ) rx = NULL;
		if ( rx ) {
			tmp.reset();
			tmp.safeStrcpy ( rx );
			expandRegExShortcuts ( &tmp );
			m_hasucr = true;
		}
		if ( rx && regcomp ( &m_ucr , tmp.getBufStart() ,
				     REG_EXTENDED| //REG_ICASE|
				     REG_NEWLINE ) ) { // |REG_NOSUB) ) {
			// error!
			log("coll: regcomp %s failed: %s. "
				   "Ignoring.",
				   rx,mstrerror(errno));
			regfree ( &m_ucr );
			m_hasucr = false;
		}


		rx = m_diffbotUrlProcessRegEx.getBufStart();
		if ( rx && ! rx[0] ) rx = NULL;
		if ( rx ) m_hasupr = true;
		if ( rx ) {
			tmp.reset();
			tmp.safeStrcpy ( rx );
			expandRegExShortcuts ( &tmp );
			m_hasupr = true;
		}
		if ( rx && regcomp ( &m_upr , tmp.getBufStart() ,
				     REG_EXTENDED| // REG_ICASE|
				     REG_NEWLINE ) ) { // |REG_NOSUB) ) {
			// error!
			log("coll: regcomp %s failed: %s. "
			    "Ignoring.",
			    rx,mstrerror(errno));
			regfree ( &m_upr );
			m_hasupr = false;
		}
		return true;

}

bool CollectionRec::rebuildUrlFiltersDiffbot() {

	//logf(LOG_DEBUG,"db: rebuilding url filters");
	char *ucp = m_diffbotUrlCrawlPattern.getBufStart();
	if ( ucp && ! ucp[0] ) ucp = NULL;

	// if we had a regex, that works for this purpose as well
	if ( ! ucp ) ucp = m_diffbotUrlCrawlRegEx.getBufStart();
	if ( ucp && ! ucp[0] ) ucp = NULL;
	char *upp = m_diffbotUrlProcessPattern.getBufStart();
	if ( upp && ! upp[0] ) upp = NULL;

	// if we had a regex, that works for this purpose as well
	if ( ! upp ) upp = m_diffbotUrlProcessRegEx.getBufStart();
	if ( upp && ! upp[0] ) upp = NULL;
	char *ppp = m_diffbotPageProcessPattern.getBufStart();
	if ( ppp && ! ppp[0] ) ppp = NULL;

	///////
	//
	// recompile regular expressions
	//
	///////


	if ( m_hasucr ) {
		regfree ( &m_ucr );
		m_hasucr = false;
	}

	if ( m_hasupr ) {
		regfree ( &m_upr );
		m_hasupr = false;
	}

	// copy into tmpbuf
	SafeBuf tmp;

	char *rx = m_diffbotUrlCrawlRegEx.getBufStart();
	if ( rx && ! rx[0] ) rx = NULL;
	if ( rx ) {
		tmp.reset();
		tmp.safeStrcpy ( rx );
		expandRegExShortcuts ( &tmp );
		m_hasucr = true;
	}
	int32_t err;
	if ( rx && ( err = regcomp ( &m_ucr , tmp.getBufStart() ,
				     REG_EXTENDED| //REG_ICASE|
				     REG_NEWLINE ) ) ) { // |REG_NOSUB) ) {
		// error!
		char errbuf[1024];
		regerror(err,&m_ucr,errbuf,1000);
		log("coll: regcomp %s failed: %s. "
		    "Ignoring.",
		    rx,errbuf);
		regfree ( &m_ucr );
		m_hasucr = false;
	}


	rx = m_diffbotUrlProcessRegEx.getBufStart();
	if ( rx && ! rx[0] ) rx = NULL;
	if ( rx ) m_hasupr = true;
	if ( rx ) {
		tmp.reset();
		tmp.safeStrcpy ( rx );
		expandRegExShortcuts ( &tmp );
		m_hasupr = true;
	}
	if ( rx && ( err = regcomp ( &m_upr , tmp.getBufStart() ,
				     REG_EXTENDED| // REG_ICASE|
				     REG_NEWLINE ) ) ) { // |REG_NOSUB) ) {
		char errbuf[1024];
		regerror(err,&m_upr,errbuf,1000);
		// error!
		log("coll: regcomp %s failed: %s. "
		    "Ignoring.",
		    rx,errbuf);
		regfree ( &m_upr );
		m_hasupr = false;
	}

	// what diffbot url to use for processing
	char *api = m_diffbotApiUrl.getBufStart();
	if ( api && ! api[0] ) api = NULL;

	// convert from seconds to milliseconds. default is 250ms?
	int32_t wait = (int32_t)(m_collectiveCrawlDelay * 1000.0);
	// default to 250ms i guess. -1 means unset i think.
	if ( m_collectiveCrawlDelay < 0.0 ) wait = 250;

	// it looks like we are assuming all crawls are repeating so that
	// &rountStart=<currenttime> or &roundStart=0 which is the same
	// thing, will trigger a re-crawl. so if collectiveRespiderFreq
	// is 0 assume it is like 999999.0 days. so that stuff works.
	// also i had to make the "default" rule below always have a respider
	// freq of 0.0 so it will respider right away if we make it past the
	// "lastspidertime>={roundstart}" rule which we will if they
	// set the roundstart time to the current time using &roundstart=0
	float respiderFreq = m_collectiveRespiderFrequency;
	if ( respiderFreq <= 0.0 ) respiderFreq = 3652.5;

	// lower from 7 to 1 since we have so many collections now
	// ok, now we have much less colls so raise back to 7
	int32_t diffbotipms = 7;//1; // 7

	// make the gigablast regex table just "default" so it does not
	// filtering, but accepts all urls. we will add code to pass the urls
	// through m_diffbotUrlCrawlPattern alternatively. if that itself
	// is empty, we will just restrict to the seed urls subdomain.
	for ( int32_t i = 0 ; i < MAX_FILTERS ; i++ ) {
		m_regExs[i].purge();
		m_spiderPriorities[i] = 0;
		m_maxSpidersPerRule [i] = 100;

		// when someone has a bulk job of thousands of different
		// domains it slows diffbot back-end down, so change this
		// from 100 to 7 if doing a bulk job
		if ( m_isCustomCrawl == 2 )
			m_maxSpidersPerRule[i] = 2;// try 2 not 1 to be faster

		m_spiderIpWaits     [i] = wait;
		m_spiderIpMaxSpiders[i] = diffbotipms; // keep it respectful
		//m_spidersEnabled    [i] = 1;
		m_spiderFreqs       [i] = respiderFreq;
		//m_spiderDiffbotApiUrl[i].purge();
		m_harvestLinks[i] = true;
		m_forceDelete [i] = false;
	}

	int32_t i = 0;

	// 1st one! for query reindex/ query delete
	m_regExs[i].set("isreindex");
	m_spiderIpMaxSpiders [i] = 10;
	m_spiderPriorities   [i] = 70;
	i++;

	// 2nd default url
	m_regExs[i].set("ismedia && !ismanualadd");
	m_maxSpidersPerRule  [i] = 0;
	m_spiderPriorities   [i] = 100; // delete!
	m_forceDelete        [i] = 1;
	i++;

	// de-prioritize fakefirstip urls so we don't give the impression our
	// spiders are slow. like if someone adds a bulk job with 100,000 urls
	// then we sit there and process to lookup their ips and add a real
	// spider request (if it falls onto the same shard) before we actually
	// do any real spidering. so keep the priority here low.
	m_regExs[i].set("isfakeip");
	m_maxSpidersPerRule  [i] = 7;
	m_spiderIpMaxSpiders [i] = 7;
	m_spiderPriorities   [i] = 20;
	m_spiderIpWaits      [i] = 0;
	i++;

	// hopcount filter if asked for
	if( m_diffbotMaxHops >= 0 ) {

		// transform long to string
		char numstr[21]; // enough to hold all numbers up to 64-bits
		sprintf(numstr, "%" PRId32, (int32_t)m_diffbotMaxHops);

		// form regEx like: hopcount>3
		char hopcountStr[30];
		strcpy(hopcountStr, "hopcount>");
		strcat(hopcountStr, numstr);

		m_regExs[i].set(hopcountStr);

		// means DELETE :
		m_spiderPriorities   [i] = 0;//SPIDER_PRIORITY_FILTERED;

		//  just don't spider
		m_maxSpidersPerRule[i] = 0;

		// compatibility with m_spiderRoundStartTime:
		m_spiderFreqs[i] = 0.0;
		i++;
	}

	// 2nd default filter
	// always turn this on for now. they need to add domains they want
	// to crawl as seeds so they do not spider the web.
	// no because FTB seeds with link pages that link to another
	// domain. they just need to be sure to supply a crawl pattern
	// to avoid spidering the whole web.
	//
	// if they did not EXPLICITLY provide a url crawl pattern or
	// url crawl regex then restrict to seeds to prevent from spidering
	// the entire internet.
	//if ( ! ucp && ! m_hasucr ) { // m_restrictDomain ) {
	// MDW: even if they supplied a crawl pattern let's restrict to seed
	// domains 12/15/14
	m_regExs[i].set("!isonsamedomain && !ismanualadd");
	m_maxSpidersPerRule  [i] = 0;
	m_spiderPriorities   [i] = 100; // delete!
	m_forceDelete        [i] = 1;
	i++;
	//}

	bool ucpHasPositive = false;
	// . scan them to see if all patterns start with '!' or not
	// . if pattern starts with ! it is negative, otherwise positve
	if ( ucp ) ucpHasPositive = hasPositivePattern ( ucp );

	// if no crawl regex, and it has a crawl pattern consisting of
	// only negative patterns then restrict to domains of seeds
	if ( ucp && ! ucpHasPositive && ! m_hasucr ) {
		m_regExs[i].set("!isonsamedomain && !ismanualadd");
		m_maxSpidersPerRule  [i] = 0;
		m_spiderPriorities   [i] = 100; // delete!
		m_forceDelete        [i] = 1;
		i++;
	}


	// 3rd rule for respidering
	if ( respiderFreq > 0.0 ) {
		m_regExs[i].set("lastspidertime>={roundstart}");
		// do not "remove" from index
		m_spiderPriorities   [i] = 10;
		// just turn off spidering. if we were to set priority to
		// filtered it would be removed from index!
		//m_spidersEnabled     [i] = 0;
		m_maxSpidersPerRule[i] = 0;
		// temp hack so it processes in xmldoc.cpp::getUrlFilterNum()
		// which has been obsoleted, but we are running old code now!
		//m_spiderDiffbotApiUrl[i].set ( api );
		i++;
	}
	// if doing a one-shot crawl limit error retries to 3 times or
	// if no urls currently available to spider, whichever comes first.
	else {
		m_regExs[i].set("errorcount>=3");
		m_spiderPriorities   [i] = 11;
		m_spiderFreqs        [i] = 0.0416;
		m_maxSpidersPerRule  [i] = 0; // turn off spiders
		i++;
	}

	m_regExs[i].set("errorcount>=1 && !hastmperror");
	m_spiderPriorities   [i] = 14;
	m_spiderFreqs        [i] = 0.0416; // every hour
	//m_maxSpidersPerRule  [i] = 0; // turn off spiders if not tmp error
	i++;

	// and for docs that have errors respider once every 5 hours
	m_regExs[i].set("errorcount==1 && hastmperror");
	m_spiderPriorities   [i] = 40;
	m_spiderFreqs        [i] = 0.001; // 86 seconds
	i++;

	// and for docs that have errors respider once every 5 hours
	m_regExs[i].set("errorcount==2 && hastmperror");
	m_spiderPriorities   [i] = 40;
	m_spiderFreqs        [i] = 0.003; // 3*86 seconds (was 24 hrs)
	i++;

	// excessive errors? (tcp/dns timed out, etc.) retry once per month?
	m_regExs[i].set("errorcount>=3 && hastmperror");
	m_spiderPriorities   [i] = 39;
	m_spiderFreqs        [i] = .25; // 1/4 day
	// if bulk job, do not download a url more than 3 times
	if ( m_isCustomCrawl == 2 ) m_maxSpidersPerRule [i] = 0;
	i++;

	// if collectiverespiderfreq is 0 or less then do not RE-spider
	// documents already indexed.
	if ( respiderFreq <= 0.0 ) { // else {
		// this does NOT work! error docs continuosly respider
		// because they are never indexed!!! like EDOCSIMPLIFIEDREDIR
		//m_regExs[i].set("isindexed");
		m_regExs[i].set("hasreply");
		m_spiderPriorities   [i] = 10;
		// just turn off spidering. if we were to set priority to
		// filtered it would be removed from index!
		//m_spidersEnabled     [i] = 0;
		m_maxSpidersPerRule[i] = 0;
		// temp hack so it processes in xmldoc.cpp::getUrlFilterNum()
		// which has been obsoleted, but we are running old code now!
		//m_spiderDiffbotApiUrl[i].set ( api );
		i++;
	}

	// url crawl and PAGE process pattern
	if ( ucp && ! upp && ppp ) {
		// if just matches ucp, just crawl it, do not process
		m_regExs[i].set("matchesucp");
		m_spiderPriorities   [i] = 53;
		if ( m_collectiveRespiderFrequency<=0.0) m_spiderFreqs [i] = 0;
		// let's always make this without delay because if we
		// restart the round we want these to process right away
		if ( respiderFreq > 0.0 ) m_spiderFreqs[i] = 0.0;
		i++;

		// crawl everything else, but don't harvest links,
		// we have to see if the page content matches the "ppp"
		// to determine whether the page should be processed or not.
		m_regExs[i].set("default");
		m_spiderPriorities   [i] = 52;
		if ( m_collectiveRespiderFrequency<=0.0) m_spiderFreqs [i] = 0;
		// let's always make this without delay because if we
		// restart the round we want these to process right away
		if ( respiderFreq > 0.0 ) m_spiderFreqs[i] = 0.0;
		m_harvestLinks       [i] = false;
		i++;
		goto done;
	}

	// url crawl and process pattern
	if ( ucp && upp ) {
		m_regExs[i].set("matchesucp && matchesupp");
		m_spiderPriorities   [i] = 55;
		if ( m_collectiveRespiderFrequency<=0.0) m_spiderFreqs [i] = 0;
		// let's always make this without delay because if we
		// restart the round we want these to process right away
		if ( respiderFreq > 0.0 ) m_spiderFreqs[i] = 0.0;

		//m_spiderDiffbotApiUrl[i].set ( api );
		i++;
		// if just matches ucp, just crawl it, do not process
		m_regExs[i].set("matchesucp");
		m_spiderPriorities   [i] = 53;
		if ( m_collectiveRespiderFrequency<=0.0) m_spiderFreqs [i] = 0;
		// let's always make this without delay because if we
		// restart the round we want these to process right away
		if ( respiderFreq > 0.0 ) m_spiderFreqs[i] = 0.0;
		i++;
		// just process, do not spider links if does not match ucp
		m_regExs[i].set("matchesupp");
		m_spiderPriorities   [i] = 54;
		m_harvestLinks       [i] = false;
		if ( m_collectiveRespiderFrequency<=0.0) m_spiderFreqs [i] = 0;
		// let's always make this without delay because if we
		// restart the round we want these to process right away
		if ( respiderFreq > 0.0 ) m_spiderFreqs[i] = 0.0;
		//m_spiderDiffbotApiUrl[i].set ( api );
		i++;
		// do not crawl anything else
		m_regExs[i].set("default");
		m_spiderPriorities   [i] = 0;//SPIDER_PRIORITY_FILTERED;
		// don't spider
		m_maxSpidersPerRule[i] = 0;
		// this needs to be zero so &spiderRoundStart=0
		// functionality which sets m_spiderRoundStartTime
		// to the current time works
		// otherwise Spider.cpp's getSpiderTimeMS() returns a time
		// in the future and we can't force the round
		m_spiderFreqs[i] = 0.0;
		i++;
	}

	// harvest links if we should crawl it
	if ( ucp && ! upp ) {
		m_regExs[i].set("matchesucp");
		m_spiderPriorities   [i] = 53;
		if ( m_collectiveRespiderFrequency<=0.0) m_spiderFreqs [i] = 0;
		// let's always make this without delay because if we
		// restart the round we want these to process right away
		if ( respiderFreq > 0.0 ) m_spiderFreqs[i] = 0.0;
		// process everything since upp is empty
		//m_spiderDiffbotApiUrl[i].set ( api );
		i++;
		// do not crawl anything else
		m_regExs[i].set("default");
		m_spiderPriorities   [i] = 0;//SPIDER_PRIORITY_FILTERED;
		// don't delete, just don't spider
		m_maxSpidersPerRule[i] = 0;
		// this needs to be zero so &spiderRoundStart=0
		// functionality which sets m_spiderRoundStartTime
		// to the current time works
		// otherwise Spider.cpp's getSpiderTimeMS() returns a time
		// in the future and we can't force the rounce
		m_spiderFreqs[i] = 0.0;
		i++;
	}

	// just process
	if ( upp && ! ucp ) {
		m_regExs[i].set("matchesupp");
		m_spiderPriorities   [i] = 54;
		if ( m_collectiveRespiderFrequency<=0.0) m_spiderFreqs [i] = 0;
		// let's always make this without delay because if we
		// restart the round we want these to process right away
		if ( respiderFreq > 0.0 ) m_spiderFreqs[i] = 0.0;
		//m_harvestLinks       [i] = false;
		//m_spiderDiffbotApiUrl[i].set ( api );
		i++;
		// crawl everything by default, no processing
		m_regExs[i].set("default");
		m_spiderPriorities   [i] = 50;
		// this needs to be zero so &spiderRoundStart=0
		// functionality which sets m_spiderRoundStartTime
		// to the current time works
		// otherwise Spider.cpp's getSpiderTimeMS() returns a time
		// in the future and we can't force the rounce
		m_spiderFreqs[i] = 0.0;
		i++;
	}

	// no restraints
	if ( ! upp && ! ucp ) {
		// crawl everything by default, no processing
		m_regExs[i].set("default");
		m_spiderPriorities   [i] = 50;
		// this needs to be zero so &spiderRoundStart=0
		// functionality which sets m_spiderRoundStartTime
		// to the current time works
		// otherwise Spider.cpp's getSpiderTimeMS() returns a time
		// in the future and we can't force the rounce
		m_spiderFreqs[i] = 0.0;
		//m_spiderDiffbotApiUrl[i].set ( api );
		i++;
	}

 done:
	m_numRegExs				= i;
	m_numSpiderFreqs		= i;
	m_numSpiderPriorities	= i;
	m_numMaxSpidersPerRule	= i;
	m_numSpiderIpWaits		= i;
	m_numSpiderIpMaxSpiders	= i;
	m_numHarvestLinks		= i;
	m_numForceDelete		= i;


	//char *x = "http://staticpages.diffbot.com/testCrawl/article1.html";
	//if(m_hasupr && regexec(&m_upr,x,0,NULL,0) ) { char *xx=NULL;*xx=0; }

	return true;


}


// . anytime the url filters are updated, this function is called
// . it is also called on load of the collection at startup
bool CollectionRec::rebuildUrlFilters ( ) {

	if ( ! g_conf.m_doingCommandLine && ! g_collectiondb.m_initializing )
		log("coll: Rebuilding url filters for %s ufp=%s",m_coll,
		    m_urlFiltersProfile.getBufStart());

	// if not a custom crawl, and no expressions, add a default one
	//if ( m_numRegExs == 0 && ! m_isCustomCrawl ) {
	//	setUrlFiltersToDefaults();
	//}

	// if not a custom crawl then set the url filters based on
	// the url filter profile, if any
	if ( ! m_isCustomCrawl )
		rebuildUrlFilters2();

	// set this so we know whether we have to keep track of page counts
	// per subdomain/site and per domain. if the url filters have
	// 'sitepages' 'domainpages' 'domainadds' or 'siteadds' we have to keep
	// the count table SpiderColl::m_pageCountTable.
	m_urlFiltersHavePageCounts = false;
	for ( int32_t i = 0 ; i < m_numRegExs ; i++ ) {
		// get the ith rule
		SafeBuf *sb = &m_regExs[i];
		char *p = sb->getBufStart();
		if ( strstr(p,"sitepages") ||
		     strstr(p,"domainpages") ||
		     strstr(p,"siteadds") ||
		     strstr(p,"domainadds") ) {
			m_urlFiltersHavePageCounts = true;
			break;
		}
	}

	// if collection is brand new being called from addNewColl()
	// then sc will be NULL
	SpiderColl *sc = g_spiderCache.getSpiderCollIffNonNull(m_collnum);

	// . do not do this at startup
	// . this essentially resets doledb
	if ( g_doledb.m_rdb.m_initialized &&
	     // somehow this is initialized before we set m_recs[m_collnum]
	     // so we gotta do the two checks below...
	     sc &&
	     // must be a valid coll
	     m_collnum < g_collectiondb.m_numRecs &&
	     g_collectiondb.m_recs[m_collnum] ) {


		log("coll: resetting doledb for %s (%li)",m_coll,
		    (long)m_collnum);

		// clear doledb recs from tree
		//g_doledb.getRdb()->deleteAllRecs ( m_collnum );
		nukeDoledb ( m_collnum );

		// add it back
		//if ( ! g_doledb.getRdb()->addRdbBase2 ( m_collnum ) )
		//	log("coll: error re-adding doledb for %s",m_coll);

		// just start this over...
		// . MDW left off here
		//tryToDelete ( sc );
		// maybe this is good enough
		//if ( sc ) sc->m_waitingTreeNeedsRebuild = true;

		//CollectionRec *cr = sc->m_cr;

		// . rebuild sitetable? in PageBasic.cpp.
		// . re-adds seed spdierrequests using msg4
		// . true = addSeeds
		// . no, don't do this now because we call updateSiteList()
		//   when we have &sitelist=xxxx in the request which will
		//   handle updating those tables
		//updateSiteListTables ( m_collnum ,
		//		       true ,
		//		       cr->m_siteListBuf.getBufStart() );
	}


	// If the crawl is not generated by crawlbot, then we will just update
	// the regexes concerning the urls to process
	rebuildDiffbotRegexes();
	if ( ! m_isCustomCrawl ){
		return true;
	}

	// on the other hand, if it is a crawlbot job, then by convention the url filters are all set
	// to some default ones.
	return rebuildUrlFiltersDiffbot();
}

// for some reason the libc we use doesn't support these shortcuts,
// so expand them to something it does support
static bool expandRegExShortcuts ( SafeBuf *sb ) {
	if ( ! sb->safeReplace3 ( "\\d" , "[0-9]" ) ) return false;
	if ( ! sb->safeReplace3 ( "\\D" , "[^0-9]" ) ) return false;
	if ( ! sb->safeReplace3 ( "\\l" , "[a-z]" ) ) return false;
	if ( ! sb->safeReplace3 ( "\\a" , "[A-Za-z]" ) ) return false;
	if ( ! sb->safeReplace3 ( "\\u" , "[A-Z]" ) ) return false;
	if ( ! sb->safeReplace3 ( "\\w" , "[A-Za-z0-9_]" ) ) return false;
	if ( ! sb->safeReplace3 ( "\\W" , "[^A-Za-z0-9_]" ) ) return false;
	return true;
}


int64_t CollectionRec::getNumDocsIndexed() {
	RdbBase *base = getBase(RDB_TITLEDB);//m_bases[RDB_TITLEDB];
	if ( ! base ) return 0LL;
	return base->getNumGlobalRecs();
}

// messes with m_spiderColl->m_sendLocalCrawlInfoToHost[MAX_HOSTS]
// so we do not have to keep sending this huge msg!
bool CollectionRec::shouldSendLocalCrawlInfoToHost ( int32_t hostId ) {
	if ( ! m_spiderColl ) return false;
	if ( hostId < 0 ) { char *xx=NULL;*xx=0; }
	if ( hostId >= g_hostdb.m_numHosts ) { char *xx=NULL;*xx=0; }
	// sanity
	return m_spiderColl->m_sendLocalCrawlInfoToHost[hostId];
}

void CollectionRec::localCrawlInfoUpdate() {
	if ( ! m_spiderColl ) return;
	// turn on all the flags
	memset(m_spiderColl->m_sendLocalCrawlInfoToHost,1,g_hostdb.m_numHosts);
}

// right after we send copy it for sending we set this so we do not send
// again unless localCrawlInfoUpdate() is called
void CollectionRec::sentLocalCrawlInfoToHost ( int32_t hostId ) {
	if ( ! m_spiderColl ) return;
	m_spiderColl->m_sendLocalCrawlInfoToHost[hostId] = 0;
}
