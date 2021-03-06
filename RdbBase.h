// Matt Wells, copyright Sep 2000

// . the core database class, we have one of these for each collection and
//   pointer to them are stored in the new "Rdb" class
// . records stored on disk ordered by lowest key first
// . allows simple key-based record retrieval
// . uses non-blocking i/o with callbacks
// . thread UNsafe for maximum speed
// . has a "groupMask" that allows us to split db between multiple Rdb's
// . uses BigFile class to support files bigger than 2gb
// . can instantly delete records in memory 
// . deletes records on disk by re-writing them to disk with key low bit CLEAR
// . we merge files (non-blocking operation) into 1 file to save on disk seeks
// . adding a record with the same key as an existing one we will replace it
//   unless you set dedup to false which is yet to be supported
// . when mem is low dumps records from tree to disk, frees as it dumps
// . has a key-to-diskOffset/dataSize map in memory (good for small records)
//   for mapping a file of records on disk
// . this key-to-offset map takes up sizeof(key_t)+  bytes per disk page 
// . we can map .8 gigs of disk with 1 meg of mem (using page size of 8k)
// . memory is only freed by the Mem.h class when it finds it's running out
// . addRecord will only return false if there's some lack of memory problems
// . we can only dump the RdbTree to disk if it's using at least "minMem" or 
//   we are shutting down and Rdb::close() was called 

#ifndef GB_RDBBASE_H
#define GB_RDBBASE_H

#include "Conf.h"
#include "Mem.h"
#include "RdbScan.h"
#include "RdbDump.h"
#include "RdbTree.h"
#include "RdbBuckets.h"
#include "RdbCache.h"
#include "RdbMerge.h"
#include "Msg3.h"               // MAX_RDB_FILES definition
#include "Dir.h"
#include "RdbMem.h"

// how many rdbs are in "urgent merge" mode?
extern int32_t g_numUrgentMerges;

extern RdbMerge g_merge;
extern RdbMerge g_merge2;

class RdbBase {

 public:

	 RdbBase ( );
	~RdbBase ( );

	// . the more memory the tree has the less file merging required
	// . when a slot's key is ANDed with "groupMask" the result must equal
	//   "groupId" in order to be in this database
	// . "minMem" is how much mem must be used before considering dumping
	//   the RdbTree (our unbalanced btree) to disk
	// . you can fix the dataSize of all records in this rdb by setting
	//   "fixedDataSize"
	// . if "maskKeyLowLong" we mask the lower int32_t of the key and then
	//   compare that to the groupId to see if the record belongs
	// . this is currently just used by Spiderdb
	// . otherwise, we mask the high int32_t in the key
	bool init ( char  *dir             , // working directory
		    char  *dbname          , // "indexdb","tagdb",...
		    bool   dedup           , //= true ,
		    int32_t   fixedDataSize   , //= -1   ,
		    int32_t   minToMerge      , //, //=  2   ,
		    bool   useHalfKeys     ,
		    char   keySize         ,
		    int32_t   pageSize        ,
		    char                *coll    ,
		    collnum_t            collnum ,
		    RdbTree             *tree    ,
		    RdbBuckets          *buckets ,
		    RdbDump             *dump    ,
		    class Rdb           *rdb    ,
		    void *pc = NULL,
		    bool                 isTitledb = false , // use fileIds2[]?
		    bool                 preloadDiskPageCache = false ,
		    bool                 biasDiskPageCache    = false );

	void closeMaps ( bool urgent );
	void saveMaps  ();

	// get the directory name where this rdb stores it's files
	const char *getDir ( ) { return m_dir.getDirname(); }

	int32_t getFixedDataSize ( ) { return m_fixedDataSize; }

	bool useHalfKeys ( ) { return m_useHalfKeys; }

	RdbMap   **getMaps  ( ) { return m_maps; }
	BigFile  **getFiles ( ) { return m_files; }

	BigFile   *getFile   ( int32_t n ) { return m_files   [n]; }
	int32_t       getFileId ( int32_t n ) { return m_fileIds [n]; }
	int32_t       getFileId2( int32_t n ) { return m_fileIds2[n]; }
	RdbMap    *getMap    ( int32_t n ) { return m_maps    [n]; }

	float getPercentNegativeRecsOnDisk ( int64_t *totalArg ) ;

	// how much mem is alloced for our maps?
	int64_t getMapMemAlloced ();

	int32_t       getNumFiles ( ) { return m_numFiles; }

	// sum of all parts of all big files
	int32_t      getNumSmallFiles ( ) ;
	int64_t getDiskSpaceUsed ( );

	// returns -1 if variable (variable dataSize)
	int32_t getRecSize ( ) {
		if ( m_fixedDataSize == -1 ) return -1;
		//return sizeof(key_t) + m_fixedDataSize; }
		return m_ks + m_fixedDataSize; }

	// use the maps and tree to estimate the size of this list
	//int32_t getListSize ( key_t startKey ,key_t endKey , key_t *maxKey ,
	int64_t getListSize ( char *startKey ,char *endKey , char *maxKey ,
			        int64_t oldTruncationLimit ) ;

	// positive minus negative
	int64_t getNumTotalRecs ( ) ;

	int64_t getNumGlobalRecs ( );
	
	// private:

	// returns true if merge was started, false if no merge could
	// be launched right now for some reason.
	bool attemptMerge ( int32_t niceness , bool forceMergeAll , 
			    bool doLog = true ,
			    // -1 means to not override it
			    int32_t minToMergeOverride = -1 );

	// called after merge completed
	bool incorporateMerge ( );

	// . you'll lose your data in this class if you call this
	void reset();

	// . set the m_files, m_fileMaps, m_fileIds arrays and m_numFiles
	bool setFiles ( ) ;

	void verifyDiskPageCache ( );

	bool verifyFileSharding ( );

	// . add a (new) file to the m_files/m_maps/m_fileIds arrays
	// . both return array position we added it to
	// . both return -1 and set errno on error
	int32_t addFile     ( int32_t fileId, bool isNew, int32_t mergeNum, int32_t id2 ) ;
	int32_t addNewFile  ( int32_t id2 ) ;

	// used by the high priority udp server to suspend merging for ALL
	// rdb's since we share a common merge class, s_merge
	//void suspendAllMerges ( ) ;
	// resume ANY merges
	//void resumeAllMerges ( ) ;

	//bool needsDump ( );

	// these are used for computing load on a machine
	bool isMerging ( ) { return m_isMerging; }
	bool isDumping ( ) { return m_dump->isDumping(); }

	bool hasMergeFile ( ) { return m_hasMergeFile; }

	// used for translating titledb file # 255 (as read from new tfndb)
	// into the real file number
	int32_t getNewestFileNum ( ) { return m_numFiles - 1; }

	// Msg22 needs the merge info so if the title file # of a read we are
	// doing is being merged, we have to include the start merge file num
	int32_t      getMergeStartFileNum ( ) { return m_mergeStartFileNum; }
	int32_t      getMergeNumFiles     ( ) { return m_numFilesToMerge; }

	// bury m_files[] in [a,b)
	void buryFiles ( int32_t a , int32_t b );

	void doneWrapper2 ( ) ;
	void doneWrapper4 ( ) ;
	int32_t m_x;
	int32_t m_a;

	// PageRepair indirectly calls this to move the map and data of this
	// rdb into the trash subdir after renaming them, because they will
	// be replaced by the rebuilt files.
	bool moveToDir   ( char *dstDir ) { return moveToTrash ( dstDir ); }
	bool moveToTrash ( char *dstDir ) ;
	// PageRepair indirectly calls this to rename the map and data files
	// of a secondary/rebuilt rdb to the filenames of the primary rdb.
	// after that, RdbBase::setFiles() is called to reload them into
	// the primary rdb. this is called after moveToTrash() is called for
	// the primary rdb.
	bool removeRebuildFromFilenames ( ) ;
	bool removeRebuildFromFilename  ( BigFile *f ) ;

	// . we try to minimize the number of files to minimize disk seeks
	// . records that end up as not found will hit all these files
	// . when we get "m_minToMerge" or more files a merge kicks in
	// . TODO: merge should combine just the smaller files... kinda
	// . files are sorted by fileId
	// . older files are listed first (lower fileIds)
	// . filenames should include the directory (full filenames)
	// . TODO: RdbMgr should control what rdb gets merged?
	BigFile  *m_files     [ MAX_RDB_FILES+1 ];
	int32_t      m_fileIds   [ MAX_RDB_FILES+1 ];
	int32_t      m_fileIds2  [ MAX_RDB_FILES+1 ]; // for titledb/tfndb linking
	RdbMap   *m_maps      [ MAX_RDB_FILES+1 ];
	int32_t      m_numFiles;

	// this class contains a ptr to us
	class Rdb           *m_rdb;

	bool      m_dedup;
	int32_t      m_fixedDataSize;

	Dir       m_dir;
	char      m_dbname [32];
	int32_t      m_dbnameLen;

	char      *m_coll;
	collnum_t  m_collnum;

	bool m_didRepair;

	//RdbCache  m_cache;
	// for storing records in memory
	RdbTree    *m_tree;  
	RdbBuckets *m_buckets;  
	// for dumping a table to an rdb file
	RdbDump    *m_dump;  

	int32_t      m_maxTreeMem ; // max mem tree can use, dump at 90% of this

	int32_t      m_minToMergeArg;
	int32_t      m_minToMerge;  // need at least this many files b4 merging
	int32_t      m_absMaxFiles;
	int32_t      m_numFilesToMerge   ;
	int32_t      m_mergeStartFileNum ;

	// should our next merge in waiting force itself?
	bool      m_nextMergeForced;

	// do we need to dump to disk?
	//bool      m_needsSave;

	// . when we dump list to an rdb file, can we use short keys?
	// . currently exclusively used by indexdb
	bool      m_useHalfKeys;

	// key size
	char      m_ks;

	bool m_checkedForMerge;

	int32_t      m_pageSize;

	// . is our merge urgent? (if so, it will starve spider disk reads)
	// . also see Threads.cpp for the starvation
	bool      m_mergeUrgent;

	bool      m_niceness;

	bool      m_waitingForTokenForMerge;

	// we now determine when in merge mode
	bool      m_isMerging;

	// have we create the merge file?
	bool      m_hasMergeFile;

	// rec counts for files being merged
	int64_t m_numPos ;
	int64_t m_numNeg ;

	bool m_isTitledb;

	int32_t m_numThreads;

	bool m_isUnlinking;

	char m_doLog;
};

extern int32_t g_numThreads;

extern char g_dumpMode;

#endif // GB_RDBBASE_H
