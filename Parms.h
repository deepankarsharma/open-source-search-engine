// Matt Wells, copyright Feb 2002

// Ideally, CollectionRec.h and SearchInput.h should be automatically generated
// from Parms.cpp. But Parms need to be marked if they contribute to
// SearchInput::makeKey() for caching the SERPS.

#ifndef GB_PARMS_H
#define GB_PARMS_H

#include "Rdb.h"

void handleRequest3e ( UdpSlot *slot , int32_t niceness ) ;
void handleRequest3f ( UdpSlot *slot , int32_t niceness ) ;

enum {
	OBJ_CONF    = 1 ,
	OBJ_COLL        ,
	OBJ_SI          , // SearchInput class
	OBJ_GBREQUEST   , // for GigablastRequest class of parms
	OBJ_IR          , // InjectionRequest class from PageInject.h
	OBJ_NONE
};

enum {
	TYPE_BOOL       = 1 ,
	TYPE_BOOL2          ,
	TYPE_CHECKBOX       ,
	TYPE_CHAR           ,
	TYPE_CHAR2          , //needed to display char as a number (maxNumHops)
	TYPE_CMD            ,
	TYPE_FLOAT          ,
	TYPE_IP             ,
	TYPE_LONG           ,
	TYPE_LONG_LONG      , // 10
	TYPE_NONE           ,
	TYPE_PRIORITY       ,
	TYPE_PRIORITY2      ,
	TYPE_PRIORITY_BOXES ,
	TYPE_RETRIES        ,
	TYPE_STRING         ,
	TYPE_STRINGBOX      ,
	TYPE_STRINGNONEMPTY ,
	TYPE_TIME           ,
	TYPE_DATE2          , // 20
	TYPE_DATE           ,
	TYPE_RULESET        ,
	TYPE_FILTER         ,
	TYPE_COMMENT        ,
	TYPE_CONSTANT       ,
	TYPE_MONOD2         ,
	TYPE_MONOM2         ,
	TYPE_LONG_CONST     ,
	TYPE_SITERULE       , // 29
	TYPE_SAFEBUF        ,
	TYPE_UFP            ,
	TYPE_FILEUPLOADBUTTON,
	TYPE_DOUBLE,
	TYPE_CHARPTR
};

//forward decls to make compiler happy:
class HttpRequest;
class TcpSocket;

#include "Msg4.h"

// generic gigablast request. for all apis offered.
class GigablastRequest {
 public:

	//
	// make a copy of the http request because the original is
	// on the stack. AND the "char *" types below will reference into
	// this because they are listed as TYPE_CHARPTR in Parms.cpp.
	// that saves us memory as opposed to making them all SafeBufs.
	//
	HttpRequest m_hr;

	// ptr to socket to send reply back on
	TcpSocket *m_socket;

	// TYPE_CHARPTR
	char *m_coll;

	////////////
	//
	// /admin/inject parms
	//
	////////////
	// these all reference into m_hr or into the Parm::m_def string!
	char *m_url; // also for /get

	///////////
	//
	// /get parms (for getting cached web pages)
	//
	///////////
	int64_t m_docId;
	int32_t m_strip;
	char m_includeHeader;

	///////////
	//
	// /admin/addurl parms
	//
	///////////
	char *m_urlsBuf;
	char m_stripBox;
	char  m_harvestLinks;
	SafeBuf m_listBuf;
	Msg4 m_msg4;

	/////////////
	//
	// /admin/reindex parms
	//
	////////////
	char *m_query;
	int32_t  m_srn;
	int32_t  m_ern;
	char *m_qlang;
	bool  m_forceDel;
	char  m_recycleContent;
};

// values for Parm::m_flags
#define PF_COOKIE  0x01  // store in cookie?
#define PF_REDBOX  0x02  // redbox constraint on search results
//#define PF_UNUSED  0x04
#define PF_WIDGET_PARM     0x08
#define PF_API             0x10
#define PF_REBUILDURLFILTERS 0x20
#define PF_NOSYNC            0x40
#define PF_DIFFBOT           0x80

#define PF_HIDDEN   0x0100
#define PF_NOSAVE   0x0200
#define PF_DUP      0x0400
#define PF_TEXTAREA 0x0800
#define PF_COLLDEFAULT 0x1000
#define PF_NOAPI       0x2000
#define PF_REQUIRED    0x4000
#define PF_REBUILDPROXYTABLE 0x8000

#define PF_NOHTML      0x10000

#define PF_CLONE       0x20000
#define PF_PRIVATE     0x40000 // for password to not show in api
#define PF_SMALLTEXTAREA 0x80000
#define PF_REBUILDACTIVELIST 0x100000

class Parm {
 public:
	const char *m_title; // displayed above m_desc on admin gui page
	const char *m_desc;  // description of variable displayed on admin gui page
	const char *m_cgi;   // cgi name, contains %i if an array

	const char *m_xml;   // default to rendition of m_title if NULL
	int32_t  m_off;   // this variable's offset into the CollectionRec class
	int32_t	m_arrayCountOffset;	// Arrays element count offset into the CollectionRec class


	char  m_colspan;
	char  m_type;  // TYPE_BOOL, TYPE_LONG, ...
	int32_t  m_page;  // PAGE_MASTER, PAGE_SPIDER, ... see Pages.h
	char  m_obj;   // OBJ_CONF or OBJ_COLL
	// the maximum number of elements supported in the array.
	// this is 1 if NOT an array (i.e. array of only one parm).
	// in such cases a "count" is NOT stored before the parm in
	// CollectionRec.h or Conf.h.
	bool isArray() { return (m_max>1); }

	int32_t  m_max;   // max elements in the array
	// if array is fixed size, how many elements in it?
	// this is 0 if not a FIXED size array.
	int32_t  m_fixed;
	int32_t  m_size;  // max string size
	const char *m_def;   // default value of this variable if not in either conf
	int32_t  m_defOff; // if default value points to a collectionrec parm!
	char  m_cast;  // true if we should broadcast to all hosts (default)
	const char *m_units;
	char  m_addin; // add "insert above" link to gui when displaying array
	char  m_rowid; // id of row controls are in, if any
	char  m_rdonly;// if in read-only mode, blank out this control?
	char  m_hdrs;  // print headers for row or print title/desc for single?
	int32_t  m_flags;
	int32_t  m_parmNum; // slot # in the m_parms[] array that we are
	bool (*m_func)(char *parmRec);
	// some functions can block, like when deleting a coll because
	// the tree might be saving, so they take a "we" ptr
	bool (*m_func2)(char *parmRec,class WaitEntry *we);
	int32_t  m_plen;  // offset of length for TYPE_STRINGS (m_htmlHeadLen...)
	bool  m_group; // start of a new group of controls?
	char  m_save;  // save to xml file? almost always true
	int32_t  m_min;
	// these are used for search parms in PageResults.cpp
	int32_t  m_sminc ;// offset of min in CollectionRec (-1 for none)
	int32_t  m_smaxc ;// offset of max in CollectionRec (-1 for none)
	int32_t  m_smin;  // absolute min
	int32_t  m_smax;  // absolute max
	char  m_sprpg; // propagate the cgi variable to other pages via GET?
	char  m_sprpp; // propagate the cgi variable to other pages via POST?
	bool  m_sync;  // this parm should be synced
	int32_t  m_hash;  // hash of "title"
	int32_t  m_cgiHash; // hash of m_cgi

	int32_t getNumInArray ( collnum_t collnum ) ;

	bool printVal ( class SafeBuf *sb , collnum_t collnum , int32_t occNum ) ;
};

#define MAX_PARMS 940

#define MAX_XML_CONF (200*1024)

#include "Xml.h"
#include "SafeBuf.h"

class Parms {

 public:

	Parms();

	void init();

	bool sendPageGeneric ( class TcpSocket *s, class HttpRequest *r );

	bool printParmTable ( SafeBuf *sb , TcpSocket *s , HttpRequest *r );

	bool printParms (SafeBuf* sb, TcpSocket *s , HttpRequest *r );

	bool printParms2 (SafeBuf* sb,
			  int32_t page,
			  CollectionRec *cr,
			  int32_t nc ,
			  int32_t pd ,
			  bool isCrawlbot ,
			  char format, //bool isJSON,
			  TcpSocket *sock,
			  bool isMasterAdmin,
			  bool isCollAdmin
			  );

	bool printParm ( SafeBuf* sb,
			  const char *username,
			  Parm *m    ,
			  int32_t  mm   , // m = &m_parms[mm]
			  int32_t  j    ,
			  int32_t  jend ,
			  char *THIS ,
			  const char *coll ,
			  const char *pwd  ,
			  const char *bg   ,
			  int32_t  nc   ,
			 int32_t  pd   ,
			 bool lastRow ,
			 bool isCrawlbot ,//= false,
			 char format , //= FORMAT_HTML,
			 bool isMasterAdmin ,
			 bool isCollAdmin ,
			 class TcpSocket *sock );

	class Parm *getParmFromParmHash ( int32_t parmHash );

	bool setFromRequest ( HttpRequest *r , //int32_t user,
			      TcpSocket* s,
			      class CollectionRec *newcr ,
			      char *THIS ,
			      int32_t objType );

	bool insertParm ( int32_t i , int32_t an , char *THIS ) ;
	bool removeParm ( int32_t i , int32_t an , char *THIS ) ;

	void setParm ( char *THIS, Parm *m, int32_t mm, int32_t j, const char *def,
		       bool isHtmlEncoded , bool fromRequest ) ;

	void setToDefault ( char *THIS , char objType ,
			    CollectionRec *argcr );//= NULL ) ;

	bool setFromFile ( void *THIS        ,
			   char *filename    ,
			   char *filenameDef ,
			   char  objType ) ;

	bool setXmlFromFile(Xml *xml, char *filename, class SafeBuf *sb );

	bool saveToXml ( char *THIS , char *f , char objType ) ;

	bool getParmHtmlEncoded ( SafeBuf *sb , Parm *m , char *s );

	bool setGigablastRequest ( class TcpSocket *s ,
				   class HttpRequest *hr ,
				   class GigablastRequest *gr );

	// . make it so a collectionrec can be copied in Collectiondb.cpp
	// . so the rec can be copied and the old one deleted without
	//   freeing the safebufs now used by the new one.
	void detachSafeBufs ( class CollectionRec *cr ) ;

	void overlapTest ( char step ) ;

	//
	// new functions
	//

	bool addNewParmToList1 ( SafeBuf *parmList ,
				 collnum_t collnum ,
				 const char *parmValString ,
				 int32_t  occNum ,
				 const char *parmName ) ;
	bool addNewParmToList2 ( SafeBuf *parmList ,
				 collnum_t collnum ,
				 const char *parmValString ,
				 int32_t occNum ,
				 Parm *m ) ;

	bool addCurrentParmToList2 ( SafeBuf *parmList ,
				     collnum_t collnum ,
				     int32_t occNum ,
				     Parm *m ) ;
	bool convertHttpRequestToParmList (HttpRequest *hr,SafeBuf *parmList,
					   int32_t page , TcpSocket *sock );
	Parm *getParmFast2 ( int32_t cgiHash32 ) ;
	Parm *getParmFast1 ( const char *cgi , int32_t *occNum ) ;
	bool broadcastParmList ( SafeBuf *parmList ,
				 void    *state ,
				 void   (* callback)(void *) ,
				 bool sendToGrunts  = true ,
				 bool sendToProxies = false ,
				 // send to this single hostid? -1 means all
				 int32_t hostId = -1 ,
				 int32_t hostId2 = -1 ); // hostid range?
	bool doParmSendingLoop ( ) ;
	bool syncParmsWithHost0 ( ) ;
	bool makeSyncHashList ( SafeBuf *hashList ) ;
	bool addAllParmsToList ( SafeBuf *parmList, collnum_t collnum ) ;
	bool updateParm ( char *rec , class WaitEntry *we ) ;

	bool cloneCollRec ( char *srcCR , char *dstCR ) ;

	//
	// end new functions
	//

	bool m_inSyncWithHost0;
	bool m_triedToSync;

	bool m_isDefaultLoaded;

	Parm m_parms [ MAX_PARMS ];
	int32_t m_numParms;

	// just those Parms that have a m_sparm of 1
	Parm *m_searchParms [ MAX_PARMS ];
	int32_t m_numSearchParms;

	// for parsing default.conf file for collection recs for OBJ_COLL
	Xml m_xml2;
};

extern Parms g_parms;

#endif // GB_PARMS_H
