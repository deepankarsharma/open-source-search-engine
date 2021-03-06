// Matt Wells, Copyright May 2001

#ifndef GB_RDBLIST_H
#define GB_RDBLIST_H

#define NUMTERMIDBITS 48

/**
 *
 * Core of the storage, this implements a list of <key><dataSize><data>.
 *
 * Additional documentation by Sam, May 15th 2015
 * Compared to a standard vector, this class offers a few low level optimizations
 * it seems, like compression of the keys when successive keys start with the same
 * bits.
 * The size of the key seems to be defined during creation (with maximum of 28 bytes,
 * defined in type.h
 * Sometimes, this type of list is used without any <data> (I guess in this case dataSize is 0)
 * This is the case for the term-lists used in Msg2 for instance.
 *
 *
 * Original documentation by Matt (2001?)
 * RdbList is the heart of Rdb, Record DataBase
 * an RdbList is a list of rdb records sorted by their keys.
 * An rdb record is just a key with an optional dataSize and/or data
 * All records in the RdbList must have keys in [m_startKey, m_endKey].
 * TODO: speed up by using templates are by having 2-3 different RdbLists:
 *         1 for dataLess Rdb's, 1 for fixedDataSize Rdb's, 1 for var dataSize

 *  m_useHalfKeys is only for IndexLists
 * it is a compression method for key-only lists (data-less)
 * it allows use of 6-byte keys if the last 12-byte key before has the same
 * most significant 6 bytes
 * this saves space and time (35% of indexdb can be cut)
 * we cannot just override skipCurrentRecord(), etc. in IndexList because
 * it would have to be a virtual function thing (ptr to a function) when
 * called in RdbMap, Msg1, merge_r(), ... OR the callers would have
 * to have a separate routine just for IndexLists
 * for speed I opted to just add the m_useHalfKeys option to the RdbList
 * class rather than have a virtual function or having to write lots of
 * additional support routines for IndexLists
 */
class RdbList {

 public:

	// IndexList sees keys as termId/score/docId tuples
	friend class RdbScan;   // for hacking to make first key read 12 bytes
	friend class RdbDump;   // for hacking m_listPtrHi/m_listPtr
	friend class RdbMap;    // for call to RdbList::setListPtr()
	friend class Msg1;

	RdbList () ;
	~RdbList () ;
	void constructor();
	void destructor ();

	// sets m_listSize to 0, keeps any allocated buffer (m_alloc), however
	void reset ( );

	// like reset, but frees m_alloc/m_allocSize and resets all to 0
	void freeList ( );

	// . set it to this list
	// . "list" is a serialized sequence of rdb records sorted by key
	// . startKey/endKey specifies the list's range
	// . there may, however, be some keys in the list outside of the range
	// . if "ownData" is true we free "list" on our reset/destruction
	void set (char *list          , 
		  int32_t  listSize      , 
		  char *alloc         ,
		  int32_t  allocSize     ,
		  const char *startKey      ,
		  const char *endKey        ,
		  int32_t  fixedDataSize , 
		  bool  ownData       ,
		  bool  useHalfKeys   ,
		  char  keySize       ); // 12 is default

	// like above but uses 0/maxKey for startKey/endKey
	void set (char *list          , 
		  int32_t  listSize      , 
		  char *alloc         ,
		  int32_t  allocSize     ,
		  int32_t  fixedDataSize , 
		  bool  ownData       ,
		  bool  useHalfKeys   ,
		  char  keySize       = sizeof(key_t) );

	void setFromPtr ( char *p , int32_t psize , char rdbId ) ;

	// just set the start and end keys
	void set ( const char *startKey, const char *endKey );

	void setStartKey ( const char *startKey ){KEYSET(m_startKey,startKey,m_ks);}
	void setEndKey   ( const char *endKey   ){KEYSET(m_endKey  ,endKey  ,m_ks);}

	void setUseHalfKeys ( bool use ) { m_useHalfKeys = use; }

	// if you don't want data to be freed on destruction then don't own it
	void setOwnData ( bool ownData ) { m_ownData = ownData; }

	void setFixedDataSize ( int32_t fixedDataSize ) { 
		m_fixedDataSize = fixedDataSize; }

	char *getStartKey        () { return m_startKey; }
	char *getEndKey          () { return m_endKey;   }
	int32_t  getFixedDataSize   () { return m_fixedDataSize; }
	bool  getOwnData         () { return m_ownData; }

	void  getStartKey        ( char *k ) { KEYSET(k,m_startKey,m_ks);}
	void  getEndKey          ( char *k ) { KEYSET(k,m_endKey  ,m_ks);}

	void  getLastKey         ( char *k ) { 
		if ( ! m_lastKeyIsValid ) { char *xx=NULL;*xx=0; }
		KEYSET(k,getLastKey(),m_ks);}

	// will scan through each record if record size is variable
	int32_t  getNumRecs         () ;

	// these operate on the whole list
	char *getList            () { return m_list; }
	int32_t  getListSize        () const { return m_listSize; }
	char *getListEnd         () { return m_list + m_listSize; }


	// often these equal m_list/m_listSize, but they may encompass
	char *getAlloc           () { return m_alloc; }
	int32_t  getAllocSize       () const { return m_allocSize; }

	// . skip over the current record and point to the next one
	// . returns false if we skipped into a black hole (end of list)
	bool skipCurrentRecord ( ) { 
		return skipCurrentRec ( getRecSize ( m_listPtr ) ); }

	bool skipCurrentRec ( ) { 
		return skipCurrentRec ( getRecSize ( m_listPtr ) ); }

	// this is specially-made for RdbMap's processing of IndexLists
	bool skipCurrentRec ( int32_t recSize ) {
		m_listPtr += recSize;
		if ( m_listPtr >= m_listEnd ) return false;
		if ( m_ks == 18 ) {
			// a 6 byte key? do not change listPtrHi nor Lo
			if ( m_listPtr[0] & 0x04 ) return true;
			// a 12 byte key?
			if ( m_listPtr[0] & 0x02 ) {
				m_listPtrLo = m_listPtr + 6;
				return true;
			}
			// if it's a full 18 byte key, change both ptrs
			m_listPtrHi = m_listPtr + 12;
			m_listPtrLo = m_listPtr + 6;
			return true;
		}
		if ( m_useHalfKeys && ! isHalfBitOn ( m_listPtr ) ) 
			m_listPtrHi = m_listPtr + (m_ks-6);
		return true;
	}

	bool  isExhausted        () const { return (m_listPtr >= m_listEnd); }
	key_t getCurrentKey      () const { 
		key_t key ; getKey ( m_listPtr,(char *)&key ); return key; }
	void  getCurrentKey      (void *key) const { getKey(m_listPtr,(char *)key);}
	int32_t  getCurrentDataSize () const { return getDataSize ( m_listPtr );}
	char *getCurrentData     () { return getData     ( m_listPtr );}
	int32_t  getCurrentRecSize  () const { return getRecSize  ( m_listPtr );}
	int32_t  getCurrentSize     () const { return m_listEnd - m_listPtr; }
	char *getCurrentRec      () { return m_listPtr; }
	char *getListPtr         () { return m_listPtr; }
	void  resetListPtr       () ;

	// are there any records in the list?
	bool  isEmpty     ( ) const { return (m_listSize == 0); }

	// . add this record to the end of the list,  @ m_list+m_listSize
	// . returns false and sets errno on error
	// . grows list (m_allocSize) if we need more space
	bool addRecord ( const char *key, int32_t dataSize, const char *data,
			 bool bitch = true );

	// . constrain a list to [startKey,endKey]
	// . returns false and sets g_errno on error
	// . only called by Msg3.cpp for 1 list reads to avoid memmov()'ing
	//   and malloc()'ing
	// . may change m_list and/or m_listSize
	bool constrain ( const char   *startKey    ,
			 char   *endKey      ,
			 int32_t    minRecSizes ,
			 int32_t    hintOffset  ,
			 const char   *hintKey     ,
			 const char   *filename    ,
			 int32_t    niceness    ) ;

	// . this MUST be called before calling merge_r() 
	// . will alloc enough space for m_listSize + sizes of "lists"
	bool prepareForMerge ( RdbList **lists            , 
			       int32_t      numLists         , 
			       int32_t      minRecSizes = -1 );

	// . merge the lists into this list
	// . set our startKey/endKey to "startKey"/"endKey"
	// . exclude any records from lists not in that range
	void merge_r ( RdbList **lists         , 
		       int32_t      numLists      , 
		       const  char    *startKey      ,
		       const char     *endKey        ,
		       int32_t      minRecSizes   ,
		       bool      removeNegRecs ,
		       char      rdbId         ,
		       int32_t      niceness      );

	bool posdbMerge_r ( RdbList **lists         ,  
			    int32_t      numLists      ,
			    const char     *startKey      ,
			    const char     *endKey        ,
			    int32_t      minRecSizes   ,
			    bool      removeNegKeys ,
			    int32_t      niceness       ) ;


	// returns false if we skipped into a black hole (end of list)
	int32_t getRecSize ( const char *rec ) const {
		// posdb?
		if ( m_ks == 18 ) {
			if ( rec[0]&0x04 ) return 6;
			if ( rec[0]&0x02 ) return 12;
			return 18;
		}
		if ( m_useHalfKeys ) {
			//if ( isHalfBitOn(rec) ) return 6;
			if ( isHalfBitOn(rec) ) return m_ks-6;
			//return sizeof(key_t);
			return m_ks;
		}
		//if (m_fixedDataSize == 0) return sizeof(key_t);
		//if (m_fixedDataSize >0) return sizeof(key_t)+m_fixedDataSize;
		//return *(int32_t *)(rec + sizeof(key_t)) + sizeof(key_t) + 4 ;
		if (m_fixedDataSize == 0) return m_ks;
		// negative keys always have no datasize entry
		if ( (rec[0] & 0x01) == 0 ) return m_ks;
		if (m_fixedDataSize >  0) return m_ks+m_fixedDataSize;
		return *(int32_t *)(rec + m_ks) + m_ks + 4 ;
	}

	// . is the format bit set? that means it's a 12-byte key
	// . used exclusively for index lists (indexdb)
	// . see Indexdb.h for format of the 12-byte and 6-byte indexdb keys
	bool isHalfBitOn ( const char *rec ) const { return ( *rec & 0x02 ); }

	bool useHalfKeys () const { return m_useHalfKeys; }

	char *getData     ( char *rec ) ;
	int32_t  getDataSize ( const char *rec ) const;
	void  getKey      ( const char *rec , char *key ) const;

	// . merge_r() sets m_lastKey for the list it merges the others into
	// . otherwise, this may be invalid
	char *getLastKey  ( ) ;
	void  setLastKey  ( const char *k );
	// sometimes we don't have a valid m_lastKey because it is only
	// set in calls to constrain(), merge_r() and indexMerge_r()
	bool  isLastKeyValid () const { return m_lastKeyIsValid; }

	char *getFirstKey ( ) { return m_list; }


	bool growList ( int32_t newSize ) ;

	// . check to see if keys in order
	// . logs any problems
	// . set "removedNegRecs" to true if neg recs should have been removed
	// . sleeps if any problems encountered
	bool checkList_r ( bool removedNegRecs , bool sleepOnProblem = true ,
			   char rdbId = 0 ); // RDB_NONE );

	// . removes records whose keys aren't in proper range (corruption)
	// . returns false and sets errno on error/problem
	bool removeBadData_r ( ) ;

	// . print out the list (uses log())
	int printList ( int32_t logtype=LOG_INFO);
	int printPosdbList ( int32_t logtype );

	void  setListPtrs ( char *p , char *hi ) {m_listPtr=p;m_listPtrHi=hi;}

	void setListSize ( int32_t size ) { m_listSize = size; }

	// private:

	// the unalterd raw list. keys may be outside of [m_startKey,m_endKey]
	char  *m_list;
	int32_t   m_listSize;     // how many bytes we're using for a list
	char   m_startKey [ MAX_KEY_BYTES ];
	// the list contains all the keys in [m_startKey,m_endKey] so make
	// sure if the list is truncated by minrecsizes that you decrease
	// m_endKey so this is still true. seems like zak did not do that
	// for rdbbuckets code.
	char   m_endKey   [ MAX_KEY_BYTES ];
	char  *m_listEnd;      // = m_list + m_listSize
	char  *m_listPtr;      // points to current record in list

	int32_t   m_allocSize;  // how many bytes we've allocated at m_alloc
	char  *m_alloc    ;  // start of chunk that was allocated

	// m_fixedDataSize is -1 if records are variable length,
	// 0 for data-less records (keys only) and N for records of dataSize N
	int32_t  m_fixedDataSize;

	// this is set to the last key in this list if we were made by merge()
	char  m_lastKey [ MAX_KEY_BYTES ];
	bool  m_lastKeyIsValid;

	// max list rec sizes to merge as set by prepareForMerge()
	int32_t   m_mergeMinListSize;

	// . this points to the most significant 6 bytes of a key
	// . only valid if m_useHalfKeys is true
	const char  *m_listPtrHi;

	// for the secondary compression bit for posdb
	const char  *m_listPtrLo;

	// do we own the list data (m_list)? if so free it on destruction
	bool   m_ownData;       

	// are keys compressed? only used for index lists right now
	bool   m_useHalfKeys;

	// keysize, usually 12, for 12 bytes. can be 16 for date index (datedb)
	char   m_ks;
};

#endif // GB_RDBLIST_H
