// Copyright Matt Wells, Apr 2001

// . every host has a config record
// . like tagdb, record in 100% xml
// . allows remote configuration of hosts through Msg4 class
// . remote user sends some xml, we set our member vars using that xml
// . when we save to disk we convert our mem vars to xml
// . is global so everybody can see it
// . conf record can be changed by director OR with the host's priv key
// . use Conf remotely to get setup info about a specific host
// . get your local ip/port/groupMask/etc. from this class not HostMap

#ifndef GB_CONF_H
#define GB_CONF_H

#include "Xml.h"         // Xml class
#include "File.h"        // File class
#include "ip.h"          // atoip()
#include "Hostdb.h"      // g_hostdb.makeGroupId(),makeGroupMask()
#include "HttpRequest.h"
#include "TcpSocket.h"
#include "Url.h"  // MAX_COLL_LEN
#include "Collectiondb.h"

#define USERAGENTMAXSIZE      128

#define MAX_DNSIPS            16
#define MAX_RNSIPS            13
#define MAX_MX_LEN            128
#define MAX_EMAIL_LEN         64

//Publicly accessible and generallyy HA / reachable DNS servers. Use Google's servers - works reasonably well
#define PUBLICLY_AVAILABLE_DNS1 "8.8.8.8"
#define PUBLICLY_AVAILABLE_DNS2 "8.8.4.4"


mode_t getFileCreationFlags();
mode_t getDirCreationFlags ();

class Conf {

  public:

	Conf();

	bool isCollAdmin ( TcpSocket *socket , HttpRequest *hr ) ;
	bool isCollAdminForColl (TcpSocket *sock, HttpRequest *hr, const char *coll );
	bool isCollAdmin2 (TcpSocket *socket , HttpRequest *hr,
			   class CollectionRec *cr) ;


	bool isMasterAdmin ( TcpSocket *socket , HttpRequest *hr ) ;
	bool hasMasterPwd ( HttpRequest *hr ) ;
	bool isMasterIp      ( uint32_t ip );
	bool isConnectIp    ( uint32_t ip );

	// loads conf parms from this file "{dir}/gb.conf"
	bool init ( char *dir );

	void setRootIps();

	// saves any changes to the conf file
	bool save ( );

	// reset all values to their defaults
	void reset();

	// . get the default collection based on hostname
	//   will look for the hostname in each collection for a match
	//   no match defaults to default collection
	char *getDefaultColl ( char *hostname, int32_t hostnameLen );

	// max amount of memory we can use
	int64_t        m_maxMem;

	// if this is false, we do not save, used by dump routines
	// in main.cpp so they can change parms here and not worry about
	// a core dump saving them
	char m_save;

	bool m_runAsDaemon;

	bool m_logToFile;
	
	bool m_isLocal;

	// an additional strip directory on a different drive
	char m_stripeDir[256];

	char m_defaultColl [ MAX_COLL_LEN + 1 ];

	char m_clusterName[32];

	// . dns parameters
	// . dnsDir should hold our saved cached (TODO: save the dns cache)
	int32_t  m_numDns ;
	int32_t  m_dnsIps[MAX_DNSIPS];
	int16_t m_dnsPorts[MAX_DNSIPS];            

	int32_t  m_dnsMaxCacheMem;
	bool  m_dnsSaveCache;

	int32_t m_wikiProxyIp;
	int32_t m_wikiProxyPort;

	SafeBuf m_proxyIps;
	bool    m_useProxyIps;
	bool    m_automaticallyUseProxyIps;
	SafeBuf m_proxyAuth;

	// built-in dns parameters using name servers
	char  m_askRootNameservers;
	int32_t  m_numRns;
	int32_t  m_rnsIps[MAX_RNSIPS];

	// used to limit all rdb's to one merge per machine at a time
	int32_t  m_mergeBufSize;

	// rdb settings
	// posdb
	int64_t m_posdbFileCacheSize;
	int32_t  m_posdbMaxTreeMem;

	// tagdb
	int64_t m_tagdbFileCacheSize;
	int32_t  m_tagdbMaxTreeMem;

	// clusterdb for site clustering, each rec is 16 bytes
	int64_t m_clusterdbFileCacheSize;
	int32_t  m_clusterdbMaxTreeMem;
	int32_t  m_clusterdbMinFilesToMerge;
	bool  m_clusterdbSaveCache;

	// titledb
	int64_t m_titledbFileCacheSize;
	int32_t  m_titledbMaxTreeMem;

	// spiderdb
	int64_t m_spiderdbFileCacheSize;
	int32_t  m_spiderdbMaxTreeMem;

	// linkdb for storing linking relations
	int32_t  m_linkdbMaxTreeMem;
	int32_t  m_linkdbMinFilesToMerge;

	// statdb
	int32_t m_statsdbMaxTreeMem;
	int32_t m_statsdbMaxCacheMem;
	bool m_useStatsdb;

	// are we doing a command line thing like 'gb 0 dump s ....' in
	// which case we do not want to log certain things
	bool m_doingCommandLine;

	int32_t  m_maxCpuThreads;
	int32_t  m_maxIOThreads;
	int32_t  m_maxExternalThreads;

	int32_t  m_deadHostTimeout;
	int32_t  m_sendEmailTimeout;
	int32_t  m_pingSpacer;

	int32_t m_maxDocsWanted;        //maximum number of results in one go. Puts a limit on SearchInput::m_docsWanted
	int32_t m_maxFirstResultNum;    //maximum document offset / result-page. Puts a limit on SearchInput::m_firstResultNum

	int32_t  min_docid_splits; //minimum number of DocId splits using Msg40
	int32_t  max_docid_splits; //maximum number of DocId splits using Msg40
	int64_t  m_msg40_msg39_timeout; //timeout for entire get-docid-list phase, in milliseconds.
	int64_t  m_msg3a_msg39_network_overhead; //additional latency/overhead of sending reqeust+response over network.

	char	m_useHighFrequencyTermCache;

	bool  m_spideringEnabled     ;
	bool  m_injectionsEnabled     ;
	bool  m_queryingEnabled ;
	bool  m_returnResultsAnyway;
	// qa testing loop going on? uses "test" subdir
	bool  m_testParserEnabled     ;
	bool  m_testSpiderEnabled     ;
	bool  m_testSearchEnabled     ;
	bool  m_addUrlEnabled        ; // TODO: use at http interface level
	bool  m_doStripeBalancing    ;

	// . true if the server is on the production cluster
	// . we enforce the 'elvtune -w 32 /dev/sd?' cmd on all drives because
	//   that yields higher performance when dumping/merging on disk
	bool  m_isLive;

	int32_t  m_maxTotalSpiders;

	// indexdb has a max cached age for getting IndexLists (10 mins deflt)
	int32_t  m_indexdbMaxIndexListAge;

	// TODO: parse these out!!!!
	int32_t  m_httpMaxSockets     ;
	int32_t  m_httpsMaxSockets    ;
	int32_t  m_httpMaxSendBufSize ;

	// a search results cache (for Msg40)
	int32_t  m_searchResultsMaxCacheMem    ;
	int32_t  m_searchResultsMaxCacheAge    ; // in seconds
	int64_t m_docSummaryWithDescriptionMaxCacheAge; //cache timeout for document summaries for documents with a meta-tag with description, in milliseconds

	// for Weights.cpp
	int32_t   m_sliderParm;

	//ranking settings
	float m_termFreqWeightFreqMin;
	float m_termFreqWeightFreqMax;
	float m_termFreqWeightMin;
	float m_termFreqWeightMax;
	
	float m_densityWeightMin;
	float m_densityWeightMax;
	
	float m_diversityWeightMin;
	float m_diversityWeightMax;
	
	float m_hashGroupWeightBody;
	float m_hashGroupWeightTitle;
	float m_hashGroupWeightHeading;
	float m_hashGroupWeightInlist;
	float m_hashGroupWeightInMetaTag;
	float m_hashGroupWeightInLinkText;
	float m_hashGroupWeightInTag;
	float m_hashGroupWeightNeighborhood;
	float m_hashGroupWeightInternalLinkText;
	float m_hashGroupWeightInUrl;
	float m_hashGroupWeightInMenu;
	
	float m_synonymWeight;

	// use sendmail to forward emails we send out
	char   m_sendmailIp[MAX_MX_LEN];

	// send emails when a host goes down?
	bool   m_sendEmailAlerts;
	//should we delay when only 1 host goes down out of twins till 9 30 am?
	bool   m_delayNonCriticalEmailAlerts;
	bool   m_sendEmailAlertsToSysadmin;

	bool   m_sendEmailAlertsToEmail1;
	char   m_email1MX[MAX_MX_LEN]; 
	char   m_email1Addr[MAX_EMAIL_LEN];
	char   m_email1From[MAX_EMAIL_LEN];

	bool   m_sendEmailAlertsToEmail2;
	char   m_email2MX[MAX_MX_LEN];
	char   m_email2Addr[MAX_EMAIL_LEN];
	char   m_email2From[MAX_EMAIL_LEN];

	bool   m_sendEmailAlertsToEmail3;
	char   m_email3MX[MAX_MX_LEN];
	char   m_email3Addr[MAX_EMAIL_LEN];
	char   m_email3From[MAX_EMAIL_LEN];

	bool   m_sendEmailAlertsToEmail4;
	char   m_email4MX[MAX_MX_LEN];
	char   m_email4Addr[MAX_EMAIL_LEN];
	char   m_email4From[MAX_EMAIL_LEN];

	//bool   m_sendEmailAlertsToSabino;

	char   m_errstr1[MAX_URL_LEN];
	char   m_errstr2[MAX_URL_LEN];	
	char   m_errstr3[MAX_URL_LEN];

	char   m_sendParmChangeAlertsToEmail1;
	char   m_sendParmChangeAlertsToEmail2;
	char   m_sendParmChangeAlertsToEmail3;
	char   m_sendParmChangeAlertsToEmail4;

	float m_avgQueryTimeThreshold;
	//float m_maxQueryTime;
	float m_querySuccessThreshold;
	int32_t  m_numQueryTimes;
	int32_t m_maxCorruptLists;

	int32_t m_defaultQueryResultsValidityTime; //in seconds
	
	bool   m_useCollectionPasswords;

	bool   m_allowCloudUsers;

	// if in read-only mode we do no spidering and load no saved trees
	// so we can use all mem for caching index lists
	bool   m_readOnlyMode;

	// if this is true we use /etc/hosts for hostname lookup before dns
	bool   m_useEtcHosts;

	// . should we always read data from local machine if available?
	// . if your network is not gigabit, this may be a good idea
	bool   m_preferLocalReads;

	// should we bypass load balancing and always send titledb record
	// lookup requests to a host to maxmize tfndb page cache hits?
	//bool   m_useBiasedTfndb;

  
	// just ensure lists being written are valid rdb records (titlerecs)
	// trying to isolate titlerec corruption
	bool m_verifyDumpedLists;

	// calls fsync(fd) if true after each write
	bool   m_flushWrites ; 
	bool   m_verifyWrites;
	int32_t   m_corruptRetries;

	// log unfreed memory on exit
	bool   m_detectMemLeaks;

	// . if false we will not keep spelling information in memory
	bool   m_doSpellChecking;

	bool   m_forceIt;

	// if this is true we do not add indexdb keys that *should* already
	// be in indexdb. but if you recently upped the m_truncationLimit
	// then you can set this to false to add all indexdb keys.
	//bool   m_onlyAddUnchangedTermIds;
	bool   m_doIncrementalUpdating;

	// always true unless entire indexdb was deleted and we are rebuilding
	bool   m_indexDeletes;

	bool   m_splitTwins;

	bool   m_useQuickpoll;

	int64_t m_stableSummaryCacheSize;
	int64_t m_stableSummaryCacheMaxAge;
	int64_t m_unstableSummaryCacheSize;
	int64_t m_unstableSummaryCacheMaxAge;

	// tagrec cache (for Msg8a)
	int64_t m_tagRecCacheSize;
	int64_t m_tagRecCacheMaxAge;

	//bool   m_quickpollCoreOnError;
	bool   m_useShotgun;
	bool   m_testMem;
	bool   m_doConsistencyTesting;

	// defaults to "Gigabot/1.0"
	char m_spiderUserAgent [ USERAGENTMAXSIZE ];

	char m_spiderBotName [ USERAGENTMAXSIZE ];

	int32_t m_autoSaveFrequency;

	int32_t m_docCountAdjustment;

	bool m_profilingEnabled;
	bool m_dynamicPerfGraph;
	int32_t m_minProfThreshold;
	bool m_sequentialProfiling;
	int32_t m_realTimeProfilerMinQuickPollDelta;

	//
	// See Log.h for an explanation of the switches below
	//

	// GET and POST requests.
	bool  m_logHttpRequests;
	bool  m_logAutobannedQueries;
	//bool  m_logQueryTimes;
	// if query took this or more milliseconds, log its time
	int32_t  m_logQueryTimeThreshold;
	// if disk read took this or more milliseconds, log its time
	int32_t  m_logDiskReadTimeThreshold;
	
	bool  m_logQueryReply;
	// log what gets into the index
	bool  m_logSpideredUrls;
	// log informational messages, they are not indicative of any error.
	bool  m_logInfo;
	// when out of udp slots
	bool  m_logNetCongestion;
	// doc quota limits, url truncation limits
	bool  m_logLimits;
	// log debug switches
	bool  m_logDebugAddurl  ;
	bool  m_logDebugAdmin   ;
	bool  m_logDebugBuild   ;
	bool  m_logDebugBuildTime ;
	bool  m_logDebugDb      ;
	bool  m_logDebugDirty   ;
	bool  m_logDebugDisk    ;
	bool  m_logDebugDiskPageCache;
	bool  m_logDebugDns     ;
	bool  m_logDebugDownloads;
	bool  m_logDebugHttp    ;
	bool  m_logDebugImage   ;
	bool  m_logDebugLoop    ;
	bool  m_logDebugLang    ;
	bool  m_logDebugLinkInfo ;
	bool  m_logDebugMem     ;
	bool  m_logDebugMemUsage;
	bool  m_logDebugMerge   ;
	bool  m_logDebugNet     ;
	bool  m_logDebugProxies ;
	bool  m_logDebugQuery   ;
	bool  m_logDebugQuota   ;
	bool  m_logDebugRobots	;
	bool  m_logDebugSpcache ; // SpiderCache.cpp debug
	bool  m_logDebugSpeller ;
	bool  m_logDebugTagdb   ;
	bool  m_logDebugSections;
	bool  m_logDebugSEO;
	bool  m_logDebugStats   ;
	bool  m_logDebugSummary ;
	bool  m_logDebugSpider  ;
	bool  m_logDebugMsg13   ;
	bool  m_logDebugUrlAttempts ;
	bool  m_logDebugTcp     ;
	bool  m_logDebugTcpBuf  ;
	bool  m_logDebugThread  ;
	bool  m_logDebugTitle   ;
	bool  m_logDebugTopDocs ;
	bool  m_logDebugUdp     ;
	bool  m_logDebugUnicode ;
	bool  m_logDebugRepair  ;
	bool  m_logDebugDate    ;
	bool  m_logDebugDetailed;

	bool m_logTraceBigFile;
	bool m_logTraceDns;
	bool m_logTraceFile;
	bool m_logTraceMem;
	bool m_logTraceMsg0;
	bool m_logTraceMsg4;
	bool m_logTracePosdb;
	bool m_logTraceRdb;
	bool m_logTraceRdbBase;
	bool m_logTraceRdbBuckets;
	bool m_logTraceRdbDump;
	bool m_logTraceRdbMap;
	bool m_logTraceRepairs;
	bool m_logTraceRobots;
	bool m_logTraceSpider;
	bool m_logTraceXmlDoc;

	// expensive timing messages
	bool m_logTimingAddurl  ;
	bool m_logTimingAdmin   ;
	bool m_logTimingBuild;
	bool m_logTimingDb;
	bool m_logTimingNet;
	bool m_logTimingQuery;
	bool m_logTimingSpcache;
	bool m_logTimingRobots;

	// programmer reminders.
	bool m_logReminders;

	SafeBuf m_masterPwds;

	// these are the new master ips
	SafeBuf m_connectIps;

	// should we generate similarity/content vector for titleRecs lacking?
	// this takes a ~100+ ms, very expensive, so it is just meant for
	// testing.
	bool m_generateVectorAtQueryTime;

	char m_redirect[MAX_URL_LEN];
	char m_useCompressionProxy;
	char m_gzipDownloads;

	// used by proxy to make proxy point to the temp cluster while
	// the original cluster is updated
	char m_useTmpCluster;

	char m_timeSyncProxy;

	Xml   m_xml;

	// . for specifying if this is an interface machine
	//   messages are rerouted from this machine to the main
	//   cluster set in the hosts.conf.
	bool m_interfaceMachine;

	// allow scaling up of hosts by removing recs not in the correct
	// group. otherwise a sanity check will happen.
	char  m_allowScale;
	// . timeout on dead hosts, only set when we know a host is dead and
	//   will not come back online.  Messages will timeout on the dead
	//   host, but not error, allowing outstanding spidering to finish
	//   to the twin
	char  m_giveupOnDeadHosts;
	char  m_bypassValidation;

	int32_t  m_maxHardDriveTemp;

	int32_t  m_maxHeartbeatDelay;
	int32_t  m_maxCallbackDelay;

	// used by Repair.cpp
	char  m_repairingEnabled  ;
	int32_t  m_maxRepairSpiders  ;
	int64_t  m_repairMem;
	SafeBuf m_collsToRepair;
	char  m_fullRebuild       ;
	char  m_rebuildAddOutlinks;
	char  m_rebuildRecycleLinkInfo  ;
	char  m_rebuildTitledb    ;
	char  m_rebuildPosdb    ;
	char  m_rebuildClusterdb  ;
	char  m_rebuildSpiderdb   ;
	char  m_rebuildLinkdb     ;
	char  m_rebuildRoots      ;
	char  m_rebuildNonRoots   ;
};

extern class Conf g_conf;

#endif // GB_CONF_H
