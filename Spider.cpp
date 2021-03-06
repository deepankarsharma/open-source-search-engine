// . TODO: do not cache if less than the 20k thing again.

// . TODO: nuke doledb every couple hours.
//   CollectionRec::m_doledbRefreshRateInSecs. but how would this work
//   for crawlbot jobs where we got 10,000 collections? i'd turn this off.
//   we could selectively update certain firstips in doledb that have
//   been in doledb for a long time.
//   i'd like to see how many collections are actually active
//   for diffbot first though.



// TODO: add m_downloadTimeTable to measure download speed of an IP
// TODO: consider a "latestpubdateage" in url filters for pages that are
//       adding new dates (not clocks) all the time

#include "gb-include.h"
#include "Spider.h"
#include "SpiderLoop.h"
#include "SpiderColl.h"
#include "Doledb.h"
#include "Msg5.h"
#include "Collectiondb.h"
#include "XmlDoc.h"    // score8to32()
#include "Stats.h"
#include "SafeBuf.h"
#include "Repair.h"
#include "CountryCode.h"
#include "DailyMerge.h"
#include "Process.h"
#include "JobScheduler.h"
#include "XmlDoc.h"
#include "HttpServer.h"
#include "Pages.h"
#include "Parms.h"
#include "Rebalance.h"

void testWinnerTreeKey ( ) ;

int32_t g_corruptCount = 0;

char s_countsAreValid = 1;

/////////////////////////
/////////////////////////      SPIDEREC
/////////////////////////

void SpiderRequest::setKey (int32_t firstIp, int64_t parentDocId, int64_t uh48, bool isDel) {

	// sanity
	if ( firstIp == 0 || firstIp == -1 ) { char *xx=NULL;*xx=0; }

	m_key = g_spiderdb.makeKey ( firstIp, uh48, true, parentDocId, isDel );
	// set dataSize too!
	setDataSize();
}

void SpiderRequest::setDataSize ( ) {
	m_dataSize = (m_url - (char *)this) + gbstrlen(m_url) + 1 
		// subtract m_key and m_dataSize
		- sizeof(key128_t) - 4 ;
}

int32_t SpiderRequest::print ( SafeBuf *sbarg ) {
	SafeBuf tmp;
	SafeBuf *sb = sbarg ?: &tmp;

	sb->safePrintf("k=%s ", KEYSTR( this, getKeySizeFromRdbId( RDB_SPIDERDB ) ) );

	// indicate it's a request not a reply
	sb->safePrintf("REQ ");
	sb->safePrintf("uh48=%" PRIu64" ",getUrlHash48());
	// if negtaive bail early now
	if ( (m_key.n0 & 0x01) == 0x00 ) {
		sb->safePrintf("[DELETE]");
		if ( ! sbarg ) printf("%s",sb->getBufStart() );
		return sb->length();
	}

	sb->safePrintf("recsize=%" PRId32" ",getRecSize());
	sb->safePrintf("parentDocId=%" PRIu64" ",getParentDocId());

	sb->safePrintf("firstip=%s ",iptoa(m_firstIp) );
	sb->safePrintf("hostHash32=0x%" PRIx32" ",m_hostHash32 );
	sb->safePrintf("domHash32=0x%" PRIx32" ",m_domHash32 );
	sb->safePrintf("siteHash32=0x%" PRIx32" ",m_siteHash32 );
	sb->safePrintf("siteNumInlinks=%" PRId32" ",m_siteNumInlinks );

	// print time format: 7/23/1971 10:45:32
	struct tm *timeStruct ;
	char time[256];

	time_t ts = (time_t)m_addedTime;
	timeStruct = gmtime ( &ts );
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb->safePrintf("addedTime=%s(%" PRIu32") ",time,(uint32_t)m_addedTime );

	//sb->safePrintf("parentFirstIp=%s ",iptoa(m_parentFirstIp) );
	sb->safePrintf("pageNumInlinks=%i ",(int)m_pageNumInlinks);
	sb->safePrintf("parentHostHash32=0x%" PRIx32" ",m_parentHostHash32 );
	sb->safePrintf("parentDomHash32=0x%" PRIx32" ",m_parentDomHash32 );
	sb->safePrintf("parentSiteHash32=0x%" PRIx32" ",m_parentSiteHash32 );

	sb->safePrintf("hopCount=%" PRId32" ",(int32_t)m_hopCount );

	//timeStruct = gmtime ( &m_spiderTime );
	//time[0] = 0;
	//if ( m_spiderTime ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	//sb->safePrintf("spiderTime=%s(%" PRIu32") ",time,m_spiderTime);

	//timeStruct = gmtime ( &m_pubDate );
	//time[0] = 0;
	//if ( m_pubDate ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	//sb->safePrintf("pubDate=%s(%" PRIu32") ",time,m_pubDate );

	sb->safePrintf("ufn=%" PRId32" ", (int32_t)m_ufn);
	// why was this unsigned?
	sb->safePrintf("priority=%" PRId32" ", (int32_t)m_priority);

	//sb->safePrintf("errCode=%s(%" PRIu32") ",mstrerror(m_errCode),m_errCode );
	//sb->safePrintf("crawlDelay=%" PRId32"ms ",m_crawlDelay );
	//sb->safePrintf("httpStatus=%" PRId32" ",(int32_t)m_httpStatus );
	//sb->safePrintf("retryNum=%" PRId32" ",(int32_t)m_retryNum );
	//sb->safePrintf("langId=%s(%" PRId32") ",
	//	       getLanguageString(m_langId),(int32_t)m_langId );
	//sb->safePrintf("percentChanged=%" PRId32"%% ",(int32_t)m_percentChanged );

	if ( m_isNewOutlink ) sb->safePrintf("ISNEWOUTLINK ");
	if ( m_isAddUrl ) sb->safePrintf("ISADDURL ");
	if ( m_isPageReindex ) sb->safePrintf("ISPAGEREINDEX ");
	if ( m_isPageParser ) sb->safePrintf("ISPAGEPARSER ");
	if ( m_urlIsDocId ) sb->safePrintf("URLISDOCID ");
	if ( m_isRSSExt ) sb->safePrintf("ISRSSEXT ");
	if ( m_isUrlPermalinkFormat ) sb->safePrintf("ISURLPERMALINKFORMAT ");
	if ( m_isPingServer ) sb->safePrintf("ISPINGSERVER ");
	if ( m_fakeFirstIp ) sb->safePrintf("ISFAKEFIRSTIP ");
	if ( m_isInjecting ) sb->safePrintf("ISINJECTING ");
	if ( m_forceDelete ) sb->safePrintf("FORCEDELETE ");
	if ( m_sameDom ) sb->safePrintf("SAMEDOM ");
	if ( m_sameHost ) sb->safePrintf("SAMEHOST ");
	if ( m_sameSite ) sb->safePrintf("SAMESITE ");
	if ( m_wasParentIndexed ) sb->safePrintf("WASPARENTINDEXED ");
	if ( m_parentIsRSS ) sb->safePrintf("PARENTISRSS ");
	if ( m_parentIsPermalink ) sb->safePrintf("PARENTISPERMALINK ");
	if ( m_parentIsPingServer ) sb->safePrintf("PARENTISPINGSERVER ");
	if ( m_parentIsSiteMap ) sb->safePrintf("PARENTISSITEMAP ");
	if ( m_isMenuOutlink ) sb->safePrintf("MENUOUTLINK ");

	if ( m_hasAuthorityInlink ) sb->safePrintf("HASAUTHORITYINLINK ");

	if ( m_isWWWSubdomain  ) sb->safePrintf("WWWSUBDOMAIN ");
	if ( m_avoidSpiderLinks ) sb->safePrintf("AVOIDSPIDERLINKS ");

	//if ( m_inOrderTree ) sb->safePrintf("INORDERTREE ");
	//if ( m_doled ) sb->safePrintf("DOLED ");

	int32_t shardNum = g_hostdb.getShardNum( RDB_SPIDERDB, this );
	sb->safePrintf("shardnum=%" PRIu32" ",(uint32_t)shardNum);

	sb->safePrintf("url=%s",m_url);

	if ( ! sbarg ) {
		printf( "%s", sb->getBufStart() );
	}

	return sb->length();
}

void SpiderReply::setKey ( int32_t firstIp, int64_t parentDocId, int64_t uh48, bool isDel ) {
	m_key = g_spiderdb.makeKey ( firstIp, uh48, false, parentDocId, isDel );
	// set dataSize too!
	m_dataSize = sizeof(SpiderReply) - sizeof(key128_t) - 4;
}

int32_t SpiderReply::print ( SafeBuf *sbarg ) {

	SafeBuf *sb = sbarg;
	SafeBuf tmp;
	if ( ! sb ) sb = &tmp;

	//sb->safePrintf("k.n1=0x%llx ",m_key.n1);
	//sb->safePrintf("k.n0=0x%llx ",m_key.n0);
	sb->safePrintf("k=%s ",KEYSTR(this,sizeof(SPIDERDBKEY)));

	// indicate it's a reply
	sb->safePrintf("REP ");

	sb->safePrintf("uh48=%" PRIu64" ",getUrlHash48());
	sb->safePrintf("parentDocId=%" PRIu64" ",getParentDocId());


	// if negtaive bail early now
	if ( (m_key.n0 & 0x01) == 0x00 ) {
		sb->safePrintf("[DELETE]");
		if ( ! sbarg ) printf("%s",sb->getBufStart() );
		return sb->length();
	}

	sb->safePrintf("firstip=%s ",iptoa(m_firstIp) );
	sb->safePrintf("percentChangedPerDay=%.02f%% ",m_percentChangedPerDay);

	// print time format: 7/23/1971 10:45:32
	struct tm *timeStruct ;
	char time[256];
	time_t ts = (time_t)m_spideredTime;
	timeStruct = gmtime ( &ts );
	time[0] = 0;
	if ( m_spideredTime ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	sb->safePrintf("spideredTime=%s(%" PRIu32") ",time,
		       (uint32_t)m_spideredTime);

	sb->safePrintf("siteNumInlinks=%" PRId32" ",m_siteNumInlinks );

	time_t ts2 = (time_t)m_pubDate;
	timeStruct = gmtime ( &ts2 );
	time[0] = 0;
	if ( m_pubDate != 0 && m_pubDate != -1 ) 
		strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	sb->safePrintf("pubDate=%s(%" PRId32") ",time,m_pubDate );

	//sb->safePrintf("newRequests=%" PRId32" ",m_newRequests );
	sb->safePrintf("ch32=%" PRIu32" ",(uint32_t)m_contentHash32);

	sb->safePrintf("crawldelayms=%" PRId32"ms ",m_crawlDelayMS );
	sb->safePrintf("httpStatus=%" PRId32" ",(int32_t)m_httpStatus );
	sb->safePrintf("langId=%s(%" PRId32") ",
		       getLanguageString(m_langId),(int32_t)m_langId );

	if ( m_errCount )
		sb->safePrintf("errCount=%" PRId32" ",(int32_t)m_errCount);

	sb->safePrintf("errCode=%s(%" PRIu32") ",mstrerror(m_errCode),
		       (uint32_t)m_errCode );

	//if ( m_isSpam ) sb->safePrintf("ISSPAM ");
	if ( m_isRSS ) sb->safePrintf("ISRSS ");
	if ( m_isPermalink ) sb->safePrintf("ISPERMALINK ");
	if ( m_isPingServer ) sb->safePrintf("ISPINGSERVER ");
	//if ( m_deleted ) sb->safePrintf("DELETED ");
	if ( ! m_isIndexedINValid && m_isIndexed ) sb->safePrintf("ISINDEXED ");


	//sb->safePrintf("url=%s",m_url);

	if ( ! sbarg ) 
		printf("%s",sb->getBufStart() );

	return sb->length();
}



int32_t SpiderRequest::printToTable ( SafeBuf *sb , const char *status ,
				   XmlDoc *xd , int32_t row ) {

	sb->safePrintf("<tr bgcolor=#%s>\n",LIGHT_BLUE);

	// show elapsed time
	if ( xd ) {
		int64_t now = gettimeofdayInMilliseconds();
		int64_t elapsed = now - xd->m_startTime;
		sb->safePrintf(" <td>%" PRId32"</td>\n",row);
		sb->safePrintf(" <td>%" PRId64"ms</td>\n",elapsed);
		collnum_t collnum = xd->m_collnum;
		CollectionRec *cr = g_collectiondb.getRec(collnum);
		char *cs = ""; if ( cr ) cs = cr->m_coll;
		// sb->safePrintf(" <td><a href=/crawlbot?c=%s>%" PRId32"</a></td>\n",
		// 	       cs,(int32_t)collnum);
		//sb->safePrintf(" <td><a href=/crawlbot?c=%s>%s</a></td>\n",
		//	       cs,cs);
		sb->safePrintf(" <td><a href=/search?c=%s&q=url%%3A%s>%s</a>"
			       "</td>\n",cs,m_url,cs);
	}

	sb->safePrintf(" <td><a href=%s><nobr>",m_url);
	sb->safeTruncateEllipsis ( m_url , 64 );
	sb->safePrintf("</nobr></a></td>\n");
	sb->safePrintf(" <td><nobr>%s</nobr></td>\n",status );

	sb->safePrintf(" <td>%" PRId32"</td>\n",(int32_t)m_priority);
	sb->safePrintf(" <td>%" PRId32"</td>\n",(int32_t)m_ufn);

	sb->safePrintf(" <td>%s</td>\n",iptoa(m_firstIp) );
	sb->safePrintf(" <td>%" PRId32"</td>\n",(int32_t)m_errCount );

	sb->safePrintf(" <td>%" PRIu64"</td>\n",getUrlHash48());

	//sb->safePrintf(" <td>0x%" PRIx32"</td>\n",m_hostHash32 );
	//sb->safePrintf(" <td>0x%" PRIx32"</td>\n",m_domHash32 );
	//sb->safePrintf(" <td>0x%" PRIx32"</td>\n",m_siteHash32 );

	sb->safePrintf(" <td>%" PRId32"</td>\n",m_siteNumInlinks );
	//sb->safePrintf(" <td>%" PRId32"</td>\n",m_pageNumInlinks );
	sb->safePrintf(" <td>%" PRId32"</td>\n",(int32_t)m_hopCount );

	// print time format: 7/23/1971 10:45:32
	struct tm *timeStruct ;
	char time[256];

	time_t ts3 = (time_t)m_addedTime;
	timeStruct = gmtime ( &ts3 );
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb->safePrintf(" <td><nobr>%s(%" PRIu32")</nobr></td>\n",time,
		       (uint32_t)m_addedTime);

	//timeStruct = gmtime ( &m_pubDate );
	//time[0] = 0;
	//if ( m_pubDate ) strftime (time,256,"%b %e %T %Y UTC",timeStruct);
	//sb->safePrintf(" <td>%s(%" PRIu32")</td>\n",time,m_pubDate );

	//sb->safePrintf(" <td>%s(%" PRIu32")</td>\n",mstrerror(m_errCode),m_errCode);
	//sb->safePrintf(" <td>%" PRId32"ms</td>\n",m_crawlDelay );
	sb->safePrintf(" <td>%i</td>\n",(int)m_pageNumInlinks);
	sb->safePrintf(" <td>%" PRIu64"</td>\n",getParentDocId() );

	//sb->safePrintf(" <td>0x%" PRIx32"</td>\n",m_parentHostHash32);
	//sb->safePrintf(" <td>0x%" PRIx32"</td>\n",m_parentDomHash32 );
	//sb->safePrintf(" <td>0x%" PRIx32"</td>\n",m_parentSiteHash32 );

	//sb->safePrintf(" <td>%" PRId32"</td>\n",(int32_t)m_httpStatus );
	//sb->safePrintf(" <td>%" PRId32"</td>\n",(int32_t)m_retryNum );
	//sb->safePrintf(" <td>%s(%" PRId32")</td>\n",
	//	       getLanguageString(m_langId),(int32_t)m_langId );
	//sb->safePrintf(" <td>%" PRId32"%%</td>\n",(int32_t)m_percentChanged );

	sb->safePrintf(" <td><nobr>");

	if ( m_isNewOutlink ) sb->safePrintf("ISNEWOUTLINK ");
	if ( m_isAddUrl ) sb->safePrintf("ISADDURL ");
	if ( m_isPageReindex ) sb->safePrintf("ISPAGEREINDEX ");
	if ( m_isPageParser ) sb->safePrintf("ISPAGEPARSER ");
	if ( m_urlIsDocId ) sb->safePrintf("URLISDOCID ");
	if ( m_isRSSExt ) sb->safePrintf("ISRSSEXT ");
	if ( m_isUrlPermalinkFormat ) sb->safePrintf("ISURLPERMALINKFORMAT ");
	if ( m_isPingServer ) sb->safePrintf("ISPINGSERVER ");
	if ( m_isInjecting ) sb->safePrintf("ISINJECTING ");
	if ( m_forceDelete ) sb->safePrintf("FORCEDELETE ");
	if ( m_sameDom ) sb->safePrintf("SAMEDOM ");
	if ( m_sameHost ) sb->safePrintf("SAMEHOST ");
	if ( m_sameSite ) sb->safePrintf("SAMESITE ");
	if ( m_wasParentIndexed ) sb->safePrintf("WASPARENTINDEXED ");
	if ( m_parentIsRSS ) sb->safePrintf("PARENTISRSS ");
	if ( m_parentIsPermalink ) sb->safePrintf("PARENTISPERMALINK ");
	if ( m_parentIsPingServer ) sb->safePrintf("PARENTISPINGSERVER ");
	if ( m_parentIsSiteMap ) sb->safePrintf("PARENTISSITEMAP ");
	if ( m_isMenuOutlink ) sb->safePrintf("MENUOUTLINK ");

	//if ( m_fromSections ) sb->safePrintf("FROMSECTIONS ");
	if ( m_hasAuthorityInlink ) sb->safePrintf("HASAUTHORITYINLINK ");


	//if ( m_inOrderTree ) sb->safePrintf("INORDERTREE ");
	//if ( m_doled ) sb->safePrintf("DOLED ");



	sb->safePrintf("</nobr></td>\n");

	sb->safePrintf("</tr>\n");

	return sb->length();
}


int32_t SpiderRequest::printTableHeaderSimple ( SafeBuf *sb , 
					     bool currentlySpidering) {

	sb->safePrintf("<tr bgcolor=#%s>\n",DARK_BLUE);

	// how long its been being spidered
	if ( currentlySpidering ) {
		sb->safePrintf(" <td><b>#</b></td>\n");
		sb->safePrintf(" <td><b>elapsed</b></td>\n");
		sb->safePrintf(" <td><b>coll</b></td>\n");
	}

	sb->safePrintf(" <td><b>url</b></td>\n");
	sb->safePrintf(" <td><b>status</b></td>\n");
	sb->safePrintf(" <td><b>first IP</b></td>\n");
	sb->safePrintf(" <td><b>crawlDelay</b></td>\n");
	sb->safePrintf(" <td><b>pri</b></td>\n");
	sb->safePrintf(" <td><b>errCount</b></td>\n");
	sb->safePrintf(" <td><b>hops</b></td>\n");
	sb->safePrintf(" <td><b>addedTime</b></td>\n");
	//sb->safePrintf(" <td><b>flags</b></td>\n");
	sb->safePrintf("</tr>\n");

	return sb->length();
}

int32_t SpiderRequest::printToTableSimple ( SafeBuf *sb , const char *status ,
					 XmlDoc *xd , int32_t row ) {

	sb->safePrintf("<tr bgcolor=#%s>\n",LIGHT_BLUE);

	// show elapsed time
	if ( xd ) {
		int64_t now = gettimeofdayInMilliseconds();
		int64_t elapsed = now - xd->m_startTime;
		sb->safePrintf(" <td>%" PRId32"</td>\n",row);
		sb->safePrintf(" <td>%" PRId64"ms</td>\n",elapsed);
		// print collection
		CollectionRec *cr = g_collectiondb.getRec ( xd->m_collnum );
		char *coll = "";
		if ( cr ) coll = cr->m_coll;
		sb->safePrintf("<td>%s</td>",coll);
	}

	sb->safePrintf(" <td><nobr>");
	sb->safeTruncateEllipsis ( m_url , 64 );
	sb->safePrintf("</nobr></td>\n");
	sb->safePrintf(" <td><nobr>%s</nobr></td>\n",status );

	sb->safePrintf(" <td>%s</td>\n",iptoa(m_firstIp));

	if ( xd->m_crawlDelayValid && xd->m_crawlDelay >= 0 )
		sb->safePrintf(" <td>%" PRId32" ms</td>\n",xd->m_crawlDelay);
	else
		sb->safePrintf(" <td>--</td>\n");

	sb->safePrintf(" <td>%" PRId32"</td>\n",(int32_t)m_priority);

	sb->safePrintf(" <td>%" PRId32"</td>\n",(int32_t)m_errCount );

	sb->safePrintf(" <td>%" PRId32"</td>\n",(int32_t)m_hopCount );

	// print time format: 7/23/1971 10:45:32
	struct tm *timeStruct ;
	char time[256];

	time_t ts4 = (time_t)m_addedTime;
	timeStruct = gmtime ( &ts4 );
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb->safePrintf(" <td><nobr>%s(%" PRIu32")</nobr></td>\n",time,
		       (uint32_t)m_addedTime);

	sb->safePrintf("</tr>\n");

	return sb->length();
}


int32_t SpiderRequest::printTableHeader ( SafeBuf *sb , bool currentlySpidering) {

	sb->safePrintf("<tr bgcolor=#%s>\n",DARK_BLUE);

	// how long its been being spidered
	if ( currentlySpidering ) {
		sb->safePrintf(" <td><b>#</b></td>\n");
		sb->safePrintf(" <td><b>elapsed</b></td>\n");
		sb->safePrintf(" <td><b>coll</b></td>\n");
	}

	sb->safePrintf(" <td><b>url</b></td>\n");
	sb->safePrintf(" <td><b>status</b></td>\n");

	sb->safePrintf(" <td><b>pri</b></td>\n");
	sb->safePrintf(" <td><b>ufn</b></td>\n");

	sb->safePrintf(" <td><b>firstIp</b></td>\n");
	sb->safePrintf(" <td><b>errCount</b></td>\n");
	sb->safePrintf(" <td><b>urlHash48</b></td>\n");
	//sb->safePrintf(" <td><b>hostHash32</b></td>\n");
	//sb->safePrintf(" <td><b>domHash32</b></td>\n");
	//sb->safePrintf(" <td><b>siteHash32</b></td>\n");
	sb->safePrintf(" <td><b>siteInlinks</b></td>\n");
	//sb->safePrintf(" <td><b>pageNumInlinks</b></td>\n");
	sb->safePrintf(" <td><b>hops</b></td>\n");
	sb->safePrintf(" <td><b>addedTime</b></td>\n");
	//sb->safePrintf(" <td><b>lastAttempt</b></td>\n");
	//sb->safePrintf(" <td><b>pubDate</b></td>\n");
	//sb->safePrintf(" <td><b>errCode</b></td>\n");
	//sb->safePrintf(" <td><b>crawlDelay</b></td>\n");
	sb->safePrintf(" <td><b>parentIp</b></td>\n");
	sb->safePrintf(" <td><b>parentDocId</b></td>\n");
	//sb->safePrintf(" <td><b>parentHostHash32</b></td>\n");
	//sb->safePrintf(" <td><b>parentDomHash32</b></td>\n");
	//sb->safePrintf(" <td><b>parentSiteHash32</b></td>\n");
	//sb->safePrintf(" <td><b>httpStatus</b></td>\n");
	//sb->safePrintf(" <td><b>retryNum</b></td>\n");
	//sb->safePrintf(" <td><b>langId</b></td>\n");
	//sb->safePrintf(" <td><b>percentChanged</b></td>\n");
	sb->safePrintf(" <td><b>flags</b></td>\n");
	sb->safePrintf("</tr>\n");

	return sb->length();
}


/////////////////////////
/////////////////////////      SPIDERDB
/////////////////////////


// a global class extern'd in .h file
Spiderdb g_spiderdb;
Spiderdb g_spiderdb2;

// reset rdb
void Spiderdb::reset() { m_rdb.reset(); }

// print the spider rec
int32_t Spiderdb::print( char *srec , SafeBuf *sb ) {
	// get if request or reply and print it
	if ( isSpiderRequest ( (key128_t *)srec ) )
		((SpiderRequest *)srec)->print(sb);
	else
		((SpiderReply *)srec)->print(sb);
	return 0;
}


bool Spiderdb::init ( ) {
	char      priority   = 12;
	int32_t      spiderTime = 0x3fe96610;
	int64_t urlHash48  = 0x1234567887654321LL & 0x0000ffffffffffffLL;

	// doledb key test
	key_t dk = g_doledb.makeKey(priority,spiderTime,urlHash48,false);
	if(g_doledb.getPriority(&dk)!=priority){char*xx=NULL;*xx=0;}
	if(g_doledb.getSpiderTime(&dk)!=spiderTime){char*xx=NULL;*xx=0;}
	if(g_doledb.getUrlHash48(&dk)!=urlHash48){char*xx=NULL;*xx=0;}
	if(g_doledb.getIsDel(&dk)!= 0){char*xx=NULL;*xx=0;}

	// spiderdb key test
	int64_t docId = 123456789;
	int32_t firstIp = 0x23991688;
	key128_t sk = g_spiderdb.makeKey ( firstIp, urlHash48, 1, docId, false );
	if ( ! g_spiderdb.isSpiderRequest (&sk) ) { char *xx=NULL;*xx=0; }
	if ( g_spiderdb.getUrlHash48(&sk) != urlHash48){char *xx=NULL;*xx=0;}
	if ( g_spiderdb.getFirstIp(&sk) != firstIp) {char *xx=NULL;*xx=0;}

	testWinnerTreeKey();

	// . what's max # of tree nodes?
	// . assume avg spider rec size (url) is about 45
	// . 45 + 33 bytes overhead in tree is 78
	int32_t maxTreeNodes  = g_conf.m_spiderdbMaxTreeMem  / 78;

	// initialize our own internal rdb
	return m_rdb.init ( g_hostdb.m_dir ,
			    "spiderdb"   ,
			    true    , // dedup
			    -1      , // fixedDataSize
			    // now that we have MAX_WINNER_NODES allowed in doledb
			    // we don't have to keep spiderdb so tightly merged i guess..
			    // MDW: it seems to slow performance when not tightly merged
			    // so put this back to "2"...
			    -1,//g_conf.m_spiderdbMinFilesToMerge , mintomerge
			    g_conf.m_spiderdbMaxTreeMem ,
			    maxTreeNodes                ,
			    true                        , // balance tree?
			    0,//g_conf.m_spiderdbMaxCacheMem,
			    0,//maxCacheNodes               ,
			    false                       , // half keys?
			    false                       , // save cache?
			    NULL,//&m_pc                       ,
			    false                       ,
			    false                       ,
			    sizeof(key128_t)            );
}



// init the rebuild/secondary rdb, used by PageRepair.cpp
bool Spiderdb::init2 ( int32_t treeMem ) {
	// . what's max # of tree nodes?
	// . assume avg spider rec size (url) is about 45
	// . 45 + 33 bytes overhead in tree is 78
	int32_t maxTreeNodes  = treeMem  / 78;
	// initialize our own internal rdb
	return m_rdb.init ( g_hostdb.m_dir ,
			    "spiderdbRebuild"   ,
			    true          , // dedup
			    -1            , // fixedDataSize
			    200           , // g_conf.m_spiderdbMinFilesToMerge
			    treeMem       , // g_conf.m_spiderdbMaxTreeMem ,
			    maxTreeNodes  ,
			    true          , // balance tree?
			    0             , // m_spiderdbMaxCacheMem,
			    0             , // maxCacheNodes               ,
			    false         , // half keys?
			    false         , // save cache?
			    NULL          , // &m_pc 
			    false         , // isTitledb?
			    false         , // preload diskpagecache
			    sizeof(key128_t));
}



bool Spiderdb::verify ( char *coll ) {
	//return true;
	log ( LOG_DEBUG, "db: Verifying Spiderdb for coll %s...", coll );
	g_jobScheduler.disallow_new_jobs();

	Msg5 msg5;
	RdbList list;
	key128_t startKey;
	key128_t endKey;
	startKey.setMin();
	endKey.setMax();
	//int32_t minRecSizes = 64000;
	CollectionRec *cr = g_collectiondb.getRec(coll);
	
	if ( ! msg5.getList ( RDB_SPIDERDB  ,
			      cr->m_collnum  ,
			      &list         ,
			      (char *)&startKey      ,
			      (char *)&endKey        ,
			      64000         , // minRecSizes   ,
			      true          , // includeTree   ,
			      false         , // add to cache?
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          ,
			      0             ,
			      -1            ,
			      true          ,
			      -1LL          ,
			      true          )) {
		g_jobScheduler.allow_new_jobs();
		return log("db: HEY! it did not block");
	}

	int32_t count = 0;
	int32_t got   = 0;
	for ( list.resetListPtr() ; ! list.isExhausted() ;
	      list.skipCurrentRecord() ) {
		char *k = list.getCurrentRec();
		//key_t k = list.getCurrentKey();
		count++;
		// what group's spiderdb should hold this rec
		//uint32_t groupId = g_hostdb.getGroupId ( RDB_SPIDERDB , k );
		//if ( groupId == g_hostdb.m_groupId ) got++;
		int32_t shardNum = g_hostdb.getShardNum(RDB_SPIDERDB,k);
		if ( shardNum == g_hostdb.getMyShardNum() ) got++;
	}
	if ( got != count ) {
		// tally it up
		g_rebalance.m_numForeignRecs += count - got;
		log ("db: Out of first %" PRId32" records in spiderdb, "
		     "only %" PRId32" belong to our shard.",count,got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) log("db: Are you sure you have the "
					   "right "
					   "data in the right directory? "
					   "Exiting.");
		log ( "db: Exiting due to Spiderdb inconsistency." );
		g_jobScheduler.allow_new_jobs();
		return g_conf.m_bypassValidation;
	}
	log (LOG_DEBUG,"db: Spiderdb passed verification successfully for %" PRId32" "
	      "recs.", count );
	// DONE
	g_jobScheduler.allow_new_jobs();
	return true;
}



key128_t Spiderdb::makeKey ( int32_t      firstIp     ,
			     int64_t urlHash48   , 
			     bool      isRequest   ,
			     // MDW: now we use timestamp instead of parentdocid
			     // for spider replies. so they do not dedup...
			     int64_t parentDocId ,
			     bool      isDel       ) {
	key128_t k;
	k.n1 = (uint32_t)firstIp;
	// push ip to top 32 bits
	k.n1 <<= 32;
	// . top 32 bits of url hash are in the lower 32 bits of k.n1
	// . often the urlhash48 has top bits set that shouldn't be so mask
	//   it to 48 bits
	k.n1 |= (urlHash48 >> 16) & 0xffffffff;
	// remaining 16 bits
	k.n0 = urlHash48 & 0xffff;
	// room for isRequest
	k.n0 <<= 1;
	if ( isRequest ) k.n0 |= 0x01;
	// parent docid
	k.n0 <<= 38;
	// if we are making a spider reply key just leave the parentdocid as 0
	// so we only store one reply per url. the last reply we got.
	// if ( isRequest ) k.n0 |= parentDocId & DOCID_MASK;
	k.n0 |= parentDocId & DOCID_MASK;
	// reserved (padding)
	k.n0 <<= 8;
	// del bit
	k.n0 <<= 1;
	if ( ! isDel ) k.n0 |= 0x01;
	return k;
}


/////////////////////////
/////////////////////////      SpiderCache
/////////////////////////


// . reload everything this many seconds
// . this was originally done to as a lazy compensation for a bug but
//   now i do not add too many of the same domain if the same domain wait
//   is ample and we know we'll be refreshed in X seconds anyway
//#define DEFAULT_SPIDER_RELOAD_RATE (3*60*60)


// for caching in s_ufnTree
//#define MAX_NODES (30)

// a global class extern'd in .h file
SpiderCache g_spiderCache;

SpiderCache::SpiderCache ( ) {
	//m_numSpiderColls   = 0;
	//m_isSaving = false;
}

// returns false and set g_errno on error
bool SpiderCache::init ( ) {

	//for ( int32_t i = 0 ; i < MAX_COLL_RECS ; i++ )
	//	m_spiderColls[i] = NULL;

	// success
	return true;
}

/*
static void doneSavingWrapper ( void *state ) {
	SpiderCache *THIS = (SpiderCache *)state;
	log("spcache: done saving something");
	//THIS->doneSaving();
	// . call the callback if any
	// . this let's PageMaster.cpp know when we're closed
	//if (THIS->m_closeCallback) THIS->m_closeCallback(THIS->m_closeState);
}
void SpiderCache::doneSaving ( ) {
	// bail if g_errno was set
	if ( g_errno ) {
		log("spider: Had error saving waitingtree.dat or doleiptable: "
		    "%s.",
		    mstrerror(g_errno));
		g_errno = 0;
	}
	else {
		// display any error, if any, otherwise prints "Success"
		logf(LOG_INFO,"db: Successfully saved waitingtree and "
		     "doleiptable");
	}
	// if still more need to save, not done yet
	if ( needsSave  ( ) ) return;
	// ok, call callback that initiaed the save
	if ( m_callback ) m_callback ( THIS->m_state );
	// ok, we are done!
	//m_isSaving = false;
}
*/


// return false if any tree save blocked
void SpiderCache::save ( bool useThread ) {
	// bail if already saving
	//if ( m_isSaving ) return true;
	// assume saving
	//m_isSaving = true;
	// loop over all SpiderColls and get the best
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		SpiderColl *sc = getSpiderCollIffNonNull(i);//m_spiderColls[i];
		if ( ! sc ) continue;
		RdbTree *tree = &sc->m_waitingTree;
		if ( ! tree->m_needsSave ) continue;
		// if already saving from a thread
		if ( tree->m_isSaving ) continue;
		char *filename = "waitingtree";
		char dir[1024];
		sprintf(dir,"%scoll.%s.%" PRId32,g_hostdb.m_dir,
			sc->m_coll,(int32_t)sc->m_collnum);
		// log it for now
		log("spider: saving waiting tree for cn=%" PRId32,(int32_t)i);
		// returns false if it blocked, callback will be called
		tree->fastSave ( dir, // g_hostdb.m_dir ,
				 filename ,
				 useThread ,
				 NULL,//this ,
				 NULL);//doneSavingWrapper );
		// also the doleIpTable
		/*
		filename = "doleiptable.dat";
		sc->m_doleIpTable.fastSave(useThread,
					   dir,
					   filename,
					   NULL,
					   0,
					   NULL,//this,
					   NULL);//doneSavingWrapper );
		*/
		// . crap, this is made at startup from waitintree!
		/*
		// waiting table
		filename = "waitingtable.dat";
		if ( sc->m_waitingTable.m_needsSave )
			logf(LOG_INFO,"db: Saving %s/%s",dir,
			     filename);
		sc->m_waitingTable.fastSave(useThread,
					    dir,
					    filename,
					    NULL,
					    0,
					    NULL,//this,
					    NULL );//doneSavingWrapper );
		*/
	}
	// if still needs save, not done yet, return false to indicate blocked
	//if ( blocked ) return false;
	// all done
	//m_isSaving = false;
	// did not block
	//return true;
}

bool SpiderCache::needsSave ( ) {
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		SpiderColl *sc = getSpiderCollIffNonNull(i);//m_spiderColls[i];
		if ( ! sc ) continue;
		if ( sc->m_waitingTree.m_needsSave ) return true;
		// also the doleIpTable
		//if ( sc->m_doleIpTable.m_needsSave ) return true;
	}
	return false;
}

void SpiderCache::reset ( ) {
	log(LOG_DEBUG,"spider: resetting spidercache");
	// loop over all SpiderColls and get the best
	for ( int32_t i = 0 ; i < g_collectiondb.m_numRecs ; i++ ) {
		SpiderColl *sc = getSpiderCollIffNonNull(i);
		if ( ! sc ) continue;
		sc->reset();
		mdelete ( sc , sizeof(SpiderColl) , "SpiderCache" );
		delete ( sc );
		//m_spiderColls[i] = NULL;
		CollectionRec *cr = g_collectiondb.getRec(i);
		cr->m_spiderColl = NULL;
	}
	//m_numSpiderColls = 0;
}

SpiderColl *SpiderCache::getSpiderCollIffNonNull ( collnum_t collnum ) {
	// "coll" must be invalid
	if ( collnum < 0 ) return NULL;
	if ( collnum >= g_collectiondb.m_numRecs ) return NULL;
	// shortcut
	CollectionRec *cr = g_collectiondb.m_recs[collnum];
	// empty?
	if ( ! cr ) return NULL;
	// return it if non-NULL
	return cr->m_spiderColl;
}

bool tryToDeleteSpiderColl ( SpiderColl *sc , const char *msg ) {
	// if not being deleted return false
	if ( ! sc->m_deleteMyself ) return false;
	// otherwise always return true
	if ( sc->m_msg5b.m_waitingForList ) {
		log("spider: deleting sc=0x%" PTRFMT" for collnum=%" PRId32" "
		    "waiting1",
		    (PTRTYPE)sc,(int32_t)sc->m_collnum);
		return true;
	}
	// if ( sc->m_msg1.m_mcast.m_inUse ) {
	// 	log("spider: deleting sc=0x%" PTRFMT" for collnum=%" PRId32" "
	// 	    "waiting2",
	// 	    (PTRTYPE)sc,(int32_t)sc->m_collnum);
	// 	return true;
	// }
	if ( sc->m_isLoading ) {
		log("spider: deleting sc=0x%" PTRFMT" for collnum=%" PRId32" "
		    "waiting3",
		    (PTRTYPE)sc,(int32_t)sc->m_collnum);
		return true;
	}
	// this means msg5 is out
	if ( sc->m_msg5.m_waitingForList ) {
		log("spider: deleting sc=0x%" PTRFMT" for collnum=%" PRId32" "
		    "waiting4",
		    (PTRTYPE)sc,(int32_t)sc->m_collnum);
		return true;
	}
	// if ( sc->m_gettingList1 ) {
	// 	log("spider: deleting sc=0x%" PTRFMT" for collnum=%" PRId32"
	//"waiting5",
	// 	    (int32_t)sc,(int32_t)sc->m_collnum);
	// 	return true;
	// }
	// if ( sc->m_gettingList2 ) {
	// 	log("spider: deleting sc=0x%" PTRFMT" for collnum=%" PRId32"
	//"waiting6",
	// 	    (int32_t)sc,(int32_t)sc->m_collnum);
	// 	return true;
	// }
	// there's still a core of someone trying to write to someting
	// in "sc" so we have to try to fix that. somewhere in xmldoc.cpp
	// or spider.cpp. everyone should get sc from cr everytime i'd think
	log("spider: deleting sc=0x%" PTRFMT" for collnum=%" PRId32" (msg=%s)",
	    (PTRTYPE)sc,(int32_t)sc->m_collnum,msg);
	// . make sure nobody has it
	// . cr might be NULL because Collectiondb.cpp::deleteRec2() might
	//   have nuked it
	//CollectionRec *cr = sc->m_cr;
	// use fake ptrs for easier debugging
	//if ( cr ) cr->m_spiderColl = (SpiderColl *)0x987654;//NULL;
	mdelete ( sc , sizeof(SpiderColl),"postdel1");
	delete ( sc );
	return true;
}

// . get SpiderColl for a collection
// . if it is NULL for that collection then make a new one
SpiderColl *SpiderCache::getSpiderColl ( collnum_t collnum ) {
	// "coll" must be invalid
	if ( collnum < 0 ) return NULL;
	// return it if non-NULL
	//if ( m_spiderColls [ collnum ] ) return m_spiderColls [ collnum ];
	// if spidering disabled, do not bother creating this!
	//if ( ! g_conf.m_spideringEnabled ) return NULL;
	// shortcut
	CollectionRec *cr = g_collectiondb.m_recs[collnum];
	// collection might have been reset in which case collnum changes
	if ( ! cr ) return NULL;
	// return it if non-NULL
	SpiderColl *sc = cr->m_spiderColl;
	if ( sc ) return sc;
	// if spidering disabled, do not bother creating this!
	//if ( ! cr->m_spideringEnabled ) return NULL;
	// cast it
	//SpiderColl *sc;
	// make it
	try { sc = new(SpiderColl); }
	catch ( ... ) {
		log("spider: failed to make SpiderColl for collnum=%" PRId32,
		    (int32_t)collnum);
		return NULL;
	}
	// register it
	mnew ( sc , sizeof(SpiderColl), "spcoll" );
	// store it
	//m_spiderColls [ collnum ] = sc;
	cr->m_spiderColl = sc;
	// note it
	logf(LOG_DEBUG,"spider: made spidercoll=%" PTRFMT" for cr=%" PTRFMT"",
	    (PTRTYPE)sc,(PTRTYPE)cr);
	// update this
	//if ( m_numSpiderColls < collnum + 1 )
	//	m_numSpiderColls = collnum + 1;
	// set this
	sc->m_collnum = collnum;
	// save this
	strcpy ( sc->m_coll , cr->m_coll );
	// set this
	if ( ! strcmp ( cr->m_coll,"qatest123" ) ) sc->m_isTestColl = true;
	else                                  sc->m_isTestColl = false;
	
	// set this
	sc->setCollectionRec ( cr ); // sc->m_cr = cr;

	// set first doledb scan key
	sc->m_nextDoledbKey.setMin();

	// turn off quickpolling while loading incase a parm update comes in
	bool saved = g_conf.m_useQuickpoll;
	g_conf.m_useQuickpoll = false;

	// mark it as loading so it can't be deleted while loading
	sc->m_isLoading = true;
	// . load its tables from disk
	// . crap i think this might call quickpoll and we get a parm
	//   update to delete this spider coll!
	sc->load();
	// mark it as loading
	sc->m_isLoading = false;

	// restore
	g_conf.m_useQuickpoll = saved;

	// did crawlbottesting delete it right away?
	if ( tryToDeleteSpiderColl( sc, "1" ) ) return NULL;

	// note it!
	log(LOG_DEBUG,"spider: adding new spider collection for %s", cr->m_coll);
	// that was it
	return sc;
}



////////
//
// winner tree key. holds the top/best spider requests for a firstIp
// for spidering purposes.
//
////////

// key bitmap (192 bits):
//
// ffffffff ffffffff ffffffff ffffffff  f=firstIp
// pppppppp pppppppp HHHHHHHH HHHHHHHH  p=255-priority  H=hopcount
// tttttttt tttttttt tttttttt tttttttt  t=spiderTimeMS
// tttttttt tttttttt tttttttt tttttttt  h=urlHash48
// hhhhhhhh hhhhhhhh hhhhhhhh hhhhhhhh 
// hhhhhhhh hhhhhhhh 00000000 00000000

key192_t makeWinnerTreeKey ( int32_t firstIp ,
			     int32_t priority ,
			     int32_t hopCount,
			     int64_t spiderTimeMS ,
			     int64_t uh48 ) {
	key192_t k;
	k.n2 = firstIp;
	k.n2 <<= 16;
	k.n2 |= (255-priority);
	k.n2 <<= 16;
	// query reindex is still using hopcount -1...
	if ( hopCount == -1 ) hopCount = 0;
	if ( hopCount < 0 ) { char *xx=NULL;*xx=0; }
	if ( hopCount > 0xffff ) hopCount = 0xffff;
	k.n2 |= hopCount;

	k.n1 = spiderTimeMS;

	k.n0 = uh48;
	k.n0 <<= 16;

	return k;
}

void parseWinnerTreeKey ( key192_t  *k ,
			  int32_t      *firstIp ,
			  int32_t      *priority ,
			  int32_t *hopCount,
			  int64_t  *spiderTimeMS ,
			  int64_t *uh48 ) {
	*firstIp = (k->n2) >> 32;
	*priority = 255 - ((k->n2 >> 16) & 0xffff);
	*hopCount = (k->n2 & 0xffff);

	*spiderTimeMS = k->n1;

	*uh48 = (k->n0 >> 16);
}

void testWinnerTreeKey ( ) {
	int32_t firstIp = 1234567;
	int32_t priority = 123;
	int64_t spiderTimeMS = 456789123LL;
	int64_t uh48 = 987654321888LL;
	int32_t hc = 4321;
	key192_t k = makeWinnerTreeKey (firstIp,priority,hc,spiderTimeMS,uh48);
	int32_t firstIp2;
	int32_t priority2;
	int64_t spiderTimeMS2;
	int64_t uh482;
	int32_t hc2;
	parseWinnerTreeKey(&k,&firstIp2,&priority2,&hc2,&spiderTimeMS2,&uh482);
	if ( firstIp != firstIp2 ) { char *xx=NULL;*xx=0; }
	if ( priority != priority2 ) { char *xx=NULL;*xx=0; }
	if ( spiderTimeMS != spiderTimeMS2 ) { char *xx=NULL;*xx=0; }
	if ( uh48 != uh482 ) { char *xx=NULL;*xx=0; }
	if ( hc != hc2 ) { char *xx=NULL;*xx=0; }
}

void removeExpiredLocks ( int32_t hostId );





/////////////////////////
/////////////////////////      UTILITY FUNCTIONS
/////////////////////////

// . map a spiderdb rec to the shard # that should spider it
// . "sr" can be a SpiderRequest or SpiderReply
// . shouldn't this use Hostdb::getShardNum()?
/*
uint32_t getShardToSpider ( char *sr ) {
	// use the url hash
	int64_t uh48 = g_spiderdb.getUrlHash48 ( (key128_t *)sr );
	// host to dole it based on ip
	int32_t hostId = uh48 % g_hostdb.m_numHosts ;
	// get it
	Host *h = g_hostdb.getHost ( hostId ) ;
	// and return groupid
	return h->m_groupId;
}
*/

// does this belong in our spider cache?
bool isAssignedToUs ( int32_t firstIp ) {
	// sanity check... must be in our group.. we assume this much
	//if ( g_spiderdb.getGroupId(firstIp) != g_hostdb.m_myHost->m_groupId){
	//	char *xx=NULL;*xx=0; }
	// . host to dole it based on ip
	// . ignore lower 8 bits of ip since one guy often owns a whole block!
	//int32_t hostId=(((uint32_t)firstIp) >> 8) % g_hostdb.getNumHosts();

	if( !g_hostdb.getMyHost()->m_spiderEnabled ) return false;
	
	// get our group
	//Host *group = g_hostdb.getMyGroup();
	Host *shard = g_hostdb.getMyShard();
	// pick a host in our group

	// if not dead return it
	//if ( ! g_hostdb.isDead(hostId) ) return hostId;
	// get that host
	//Host *h = g_hostdb.getHost(hostId);
	// get the group
	//Host *group = g_hostdb.getGroup ( h->m_groupId );
	// and number of hosts in the group
	int32_t hpg = g_hostdb.getNumHostsPerShard();
	// let's mix it up since spider shard was selected using this
	// same mod on the firstIp method!!
	uint64_t h64 = firstIp;
	unsigned char c = firstIp & 0xff;
	h64 ^= g_hashtab[c][0];
	// select the next host number to try
	//int32_t next = (((uint32_t)firstIp) >> 16) % hpg ;
	// hash to a host
	int32_t i = ((uint32_t)h64) % hpg;
	Host *h = &shard[i];
	// return that if alive
	if ( ! g_hostdb.isDead(h) && h->m_spiderEnabled) {
		return (h->m_hostId == g_hostdb.m_hostId);
	}
	// . select another otherwise
	// . put all alive in an array now
	Host *alive[64];
	int32_t upc = 0;

	for ( int32_t j = 0 ; j < hpg ; j++ ) {
		Host *h = &shard[j];
		if ( g_hostdb.isDead(h) ) continue;
		if( ! h->m_spiderEnabled ) continue;
		alive[upc++] = h;
	}
	// if none, that is bad! return the first one that we wanted to
	if ( upc == 0 ) {
		log("spider: no hosts can handle spider request for ip=%s", iptoa(firstIp));
		return false;
		//return (h->m_hostId == g_hostdb.m_hostId);
	}
	// select from the good ones now
	i  = ((uint32_t)firstIp) % upc;
	// get that
	h = alive[i]; //&shard[i];
	// guaranteed to be alive... kinda
	return (h->m_hostId == g_hostdb.m_hostId);
}





/////////////////////////
/////////////////////////      PAGESPIDER
/////////////////////////

// don't change name to "State" cuz that might conflict with another
class State11 {
public:
	int32_t          m_numRecs;
	Msg5          m_msg5;
	RdbList       m_list;
	TcpSocket    *m_socket;
	HttpRequest   m_r;
	collnum_t     m_collnum;
	const char   *m_coll;
	int32_t          m_count;
	key_t         m_startKey;
	key_t         m_endKey;
	int32_t          m_minRecSizes;
	bool          m_done;
	SafeBuf       m_safeBuf;
	int32_t          m_priority;
};

static bool loadLoop ( class State11 *st ) ;

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . make a web page displaying the urls we got in doledb
// . doledb is sorted by priority complement then spider time
// . do not show urls in doledb whose spider time has not yet been reached,
//   so only show the urls spiderable now
// . call g_httpServer.sendDynamicPage() to send it
bool sendPageSpiderdb ( TcpSocket *s , HttpRequest *r ) {
	// set up a msg5 and RdbLists to get the urls from spider queue
	State11 *st ;
	try { st = new (State11); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("PageSpiderdb: new(%i): %s", 
		    (int)sizeof(State11),mstrerror(g_errno));
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));}
	mnew ( st , sizeof(State11) , "PageSpiderdb" );
	// get the priority/#ofRecs from the cgi vars
	st->m_numRecs  = r->getLong ("n", 20  );
	st->m_r.copy ( r );
	// get collection name
	const char *coll = st->m_r.getString ( "c" , NULL , NULL );
	// get the collection record to see if they have permission
	//CollectionRec *cr = g_collectiondb.getRec ( coll );

	// the socket read buffer will remain until the socket is destroyed
	// and "coll" points into that
	st->m_coll = coll;
	CollectionRec *cr = g_collectiondb.getRec(coll);
	if ( cr ) st->m_collnum = cr->m_collnum;
	else      st->m_collnum = -1;
	// set socket for replying in case we block
	st->m_socket = s;
	st->m_count = 0;
	st->m_priority = MAX_SPIDER_PRIORITIES - 1;
	// get startKeys/endKeys/minRecSizes
	st->m_startKey    = g_doledb.makeFirstKey2 (st->m_priority);
	st->m_endKey      = g_doledb.makeLastKey2  (st->m_priority);
	st->m_minRecSizes = 20000;
	st->m_done        = false;
	// returns false if blocked, true otherwise
	return loadLoop ( st ) ;
}

static void gotListWrapper3 ( void *state , RdbList *list , Msg5 *msg5 ) ;
static bool sendPage        ( State11 *st );
static bool printList       ( State11 *st );

bool loadLoop ( State11 *st ) {
 loop:
	// let's get the local list for THIS machine (use msg5)
	if ( ! st->m_msg5.getList  ( RDB_DOLEDB          ,
				     st->m_collnum       ,
				     &st->m_list         ,
				     st->m_startKey      ,
				     st->m_endKey        ,
				     st->m_minRecSizes   ,
				     true                , // include tree
				     false               , // add to cache
				     0                   , // max age
				     0                   , // start file #
				     -1                  , // # files
				     st                  , // callback state
				     gotListWrapper3     ,
				     0                   , // niceness
				     true               )) // do err correction
		return false;
	// print it. returns false on error
	if ( ! printList ( st ) ) st->m_done = true;
	// check if done
	if ( st->m_done ) {
		// send the page back
		sendPage ( st );
		// bail
		return true;
	}
	// otherwise, load more
	goto loop;
}

void gotListWrapper3 ( void *state , RdbList *list , Msg5 *msg5 ) {
	// cast it
	State11 *st = (State11 *)state;
	// print it. returns false on error
	if ( ! printList ( st ) ) st->m_done = true;
	// check if done
	if ( st->m_done ) {
		// send the page back
		sendPage ( st );
		// bail
		return;
	}
	// otherwise, load more
	loadLoop( (State11 *)state );
}


// . make a web page from results stored in msg40
// . send it on TcpSocket "s" when done
// . returns false if blocked, true otherwise
// . sets g_errno on error
bool printList ( State11 *st ) {
	// useful
	time_t nowGlobal ;
	if ( isClockInSync() ) nowGlobal = getTimeGlobal();
	else                   nowGlobal = getTimeLocal();
	// print the spider recs we got
	SafeBuf *sbTable = &st->m_safeBuf;
	// shorcuts
	RdbList *list = &st->m_list;
	// row count
	int32_t j = 0;
	// put it in there
	for ( ; ! list->isExhausted() ; list->skipCurrentRecord() ) {
		// stop if we got enough
		if ( st->m_count >= st->m_numRecs )  break;
		// get the doledb key
		key_t dk = list->getCurrentKey();
		// update to that
		st->m_startKey = dk;
		// inc by one
		st->m_startKey += 1;
		// get spider time from that
		int32_t spiderTime = g_doledb.getSpiderTime ( &dk );
		// skip if in future
		if ( spiderTime > nowGlobal ) continue;
		// point to the spider request *RECORD*
		char *rec = list->getCurrentData();
		// skip negatives
		if ( (dk.n0 & 0x01) == 0 ) continue;
		// count it
		st->m_count++;
		// what is this?
		if ( list->getCurrentRecSize() <= 16 ) { char *xx=NULL;*xx=0;}
		// sanity check. requests ONLY in doledb
		if ( ! g_spiderdb.isSpiderRequest ( (key128_t *)rec )) {
			log("spider: not printing spiderreply");
			continue;
			//char*xx=NULL;*xx=0;
		}
		// get the spider rec, encapsed in the data of the doledb rec
		SpiderRequest *sreq = (SpiderRequest *)rec;
		// print it into sbTable
		if ( ! sreq->printToTable ( sbTable,"ready",NULL,j))
			return false;
		// count row
		j++;
	}
	// need to load more?
	if ( st->m_count >= st->m_numRecs ||
	     // if list was a partial, this priority is short then
	     list->getListSize() < st->m_minRecSizes ) {
		// . try next priority
		// . if below 0 we are done
		if ( --st->m_priority < 0 ) st->m_done = true;
		// get startKeys/endKeys/minRecSizes
		st->m_startKey    = g_doledb.makeFirstKey2 (st->m_priority);
		st->m_endKey      = g_doledb.makeLastKey2  (st->m_priority);
		// if we printed something, print a blank line after it
		if ( st->m_count > 0 )
			sbTable->safePrintf("<tr><td colspan=30>..."
					    "</td></tr>\n");
		// reset for each priority
		st->m_count = 0;
	}


	return true;
}

bool sendPage ( State11 *st ) {
	// sanity check
	//if ( ! g_errno ) { char *xx=NULL;*xx=0; }
	//SafeBuf sb; sb.safePrintf("Error = %s",mstrerror(g_errno));

	// shortcut
	SafeBuf *sbTable = &st->m_safeBuf;

	// generate a query string to pass to host bar
	char qs[64]; sprintf ( qs , "&n=%" PRId32, st->m_numRecs );

	// store the page in here!
	SafeBuf sb;
	sb.reserve ( 64*1024 );

	g_pages.printAdminTop ( &sb, st->m_socket , &st->m_r , qs );


	// get spider coll
	collnum_t collnum = g_collectiondb.getCollnum ( st->m_coll );
	// and coll rec
	CollectionRec *cr = g_collectiondb.getRec ( collnum );

	if ( ! cr ) {
		// get the socket
		TcpSocket *s = st->m_socket;
		// then we can nuke the state
		mdelete ( st , sizeof(State11) , "PageSpiderdb" );
		delete (st);
		// erase g_errno for sending
		g_errno = 0;
		// now encapsulate it in html head/tail and send it off
		return g_httpServer.sendDynamicPage (s, sb.getBufStart(),
						     sb.length() );
	}

	// print reason why spiders are not active for this collection
	int32_t tmp2;
	SafeBuf mb;
	if ( cr ) getSpiderStatusMsg ( cr , &mb , &tmp2 );
	if ( mb.length() && tmp2 != SP_INITIALIZING )
		sb.safePrintf(//"<center>"
			      "<table cellpadding=5 "
			      //"style=\""
			      //"border:2px solid black;"
			      "max-width:600px\" "
			      "border=0"
			      ">"
			      "<tr>"
			      //"<td bgcolor=#ff6666>"
			      "<td>"
			      "For collection <i>%s</i>: "
			      "<b><font color=red>%s</font></b>"
			      "</td>"
			      "</tr>"
			      "</table>\n"
			      , cr->m_coll
			      , mb.getBufStart() );


	// begin the table
	sb.safePrintf ( "<table %s>\n"
			"<tr><td colspan=50>"
			//"<center>"
			"<b>Currently Spidering on This Host</b>"
			" (%" PRId32" spiders)"
			//" (%" PRId32" locks)"
			//"</center>"
			"</td></tr>\n"
			, TABLE_STYLE
			, (int32_t)g_spiderLoop.m_numSpidersOut
			//, g_spiderLoop.m_lockTable.m_numSlotsUsed
			);
	// the table headers so SpiderRequest::printToTable() works
	if ( ! SpiderRequest::printTableHeader ( &sb , true ) ) return false;
	// shortcut
	XmlDoc **docs = g_spiderLoop.m_docs;
	// count # of spiders out
	int32_t j = 0;
	// first print the spider recs we are spidering
	for ( int32_t i = 0 ; i < (int32_t)MAX_SPIDERS ; i++ ) {
		// get it
		XmlDoc *xd = docs[i];
		// skip if empty
		if ( ! xd ) continue;
		// sanity check
		if ( ! xd->m_sreqValid ) { char *xx=NULL;*xx=0; }
		// grab it
		SpiderRequest *oldsr = &xd->m_sreq;
		// get status
		const char *status = xd->m_statusMsg;
		// show that
		if ( ! oldsr->printToTable ( &sb , status,xd,j) ) return false;
		// inc count
		j++;
	}
	// now print the injections as well!
	XmlDoc *xd = getInjectHead ( ) ;
	for ( ; xd ; xd = xd->m_nextInject ) {
		// how does this happen?
		if ( ! xd->m_sreqValid ) continue;
		// grab it
		SpiderRequest *oldsr = &xd->m_sreq;
		// get status
		SafeBuf xb;
		xb.safePrintf("[<font color=red><b>injecting</b></font>] %s",
			      xd->m_statusMsg);
		char *status = xb.getBufStart();
		// show that
		if ( ! oldsr->printToTable ( &sb , status,xd,j) ) return false;
		// inc count
		j++;
	}

	// end the table
	sb.safePrintf ( "</table>\n" );
	sb.safePrintf ( "<br>\n" );

	// then spider collection
	//SpiderColl *sc = g_spiderCache.m_spiderColls[collnum];
	SpiderColl *sc = g_spiderCache.getSpiderColl(collnum);


	//
	// spiderdb rec stats, from scanning spiderdb
	//

	// if not there, forget about it
	if ( sc ) sc->printStats ( sb );

	//
	// Spiders Table
	//
	int64_t totalPoints = g_stats.m_totalSpiderSuccessNew +
				g_stats.m_totalSpiderErrorsNew +
				g_stats.m_totalSpiderSuccessOld +
				g_stats.m_totalSpiderErrorsOld;
	int64_t totalNew = g_stats.m_totalSpiderSuccessNew +
			     g_stats.m_totalSpiderErrorsNew;
	int64_t totalOld = g_stats.m_totalSpiderSuccessOld +
			     g_stats.m_totalSpiderErrorsOld;
	double tsr = 100.00;
	double nsr = 100.00;
	double osr = 100.00;
	if ( totalPoints > 0 ) {
		tsr = 100.00*
			(double)(g_stats.m_totalSpiderSuccessNew +
				 g_stats.m_totalSpiderSuccessOld) /
			(double)totalPoints;
		if ( totalNew > 0 )
			nsr= 100.00*(double)(g_stats.m_totalSpiderSuccessNew) /
				     (double)(totalNew);
		if ( totalOld > 0 )
			osr= 100.00*(double)(g_stats.m_totalSpiderSuccessOld) /
				     (double)(totalOld);
	}
	int32_t points = g_stats.m_spiderSample;
	if ( points > 1000 ) points = 1000;
	int32_t sampleNew = g_stats.m_spiderNew;
	int32_t sampleOld = points - g_stats.m_spiderNew;
	double tssr = 100.00;
	double nssr = 100.00;
	double ossr = 100.00;
	if ( points > 0 ) {
		tssr = 100.00*
			(double)(points -
				 g_stats.m_spiderErrors) / (double)points ;
		if ( sampleNew > 0 )
			nssr = 100.00*(double)(sampleNew -
					       g_stats.m_spiderErrorsNew) /
				      (double)(sampleNew);
		if ( sampleOld > 0 )
			ossr = 100.00*(double)(sampleOld -
					       (g_stats.m_spiderErrors -
						g_stats.m_spiderErrorsNew)) /
				      (double)(sampleOld);
	}

	sb.safePrintf(
		      "<style>"
		      ".poo { background-color:#%s;}\n"
		      "</style>\n" ,
		      LIGHT_BLUE );

	sb.safePrintf (

		       "<table %s>"
		       "<tr>"
		       "<td colspan=7>"
		       "<center><b>Spider Stats</b></td></tr>\n"
		       "<tr bgcolor=#%s><td>"
		       "</td><td><b>Total</b></td>"
		       "<td><b>Total New</b></td>"
		       "<td><b>Total Old</b></td>"
		       "<td><b>Sample</b></td>"
		       "<td><b>Sample New</b></td>"
		       "<td><b>Sample Old</b></b>"
		       "</td></tr>"

		       "<tr class=poo><td><b>Total Spiders</n>"
		       "</td><td>%" PRId64"</td><td>%" PRId64"</td><td>%" PRId64"</td>\n"
		       "</td><td>%" PRId32"</td><td>%" PRId32"</td><td>%" PRId32"</td></tr>\n"
		       //"<tr class=poo><td><b>Successful Spiders</n>"
		       //"</td><td>%" PRId64"</td><td>%" PRId64"</td><td>%" PRId64"</td>\n"
		       //"</td><td>%" PRId32"</td><td>%" PRId32"</td><td>%" PRId32"</td></tr>\n"
		       //"<tr class=poo><td><b>Failed Spiders</n>"
		       //"</td><td>%" PRId64"</td><td>%" PRId64"</td><td>%" PRId64"</td>\n"
		       //"</td><td>%" PRId32"</td><td>%" PRId32"</td><td>%" PRId32"</td></tr>\n"
		       "<tr class=poo><td><b>Success Rate</b>"
		       "</td><td>%.02f%%</td><td>%.02f%%</td>"
		       "</td><td>%.02f%%</td><td>%.02f%%</td>"
		       "</td><td>%.02f%%</td><td>%.02f%%</td></tr>",
		       TABLE_STYLE,  
		       DARK_BLUE,
		       totalPoints,
		       totalNew,
		       totalOld,
		       points,
		       sampleNew,
		       sampleOld,

		       //g_stats.m_totalSpiderSuccessNew +
		       //g_stats.m_totalSpiderSuccessOld,
		       //g_stats.m_totalSpiderSuccessNew,
		       //g_stats.m_totalSpiderSuccessOld,
		       //g_stats.m_spiderSuccessNew +
		       //g_stats.m_spiderSuccessOld,
		       //g_stats.m_spiderSuccessNew,
		       //g_stats.m_spiderSuccessOld,

		       //g_stats.m_totalSpiderErrorsNew +
		       //g_stats.m_totalSpiderErrorsOld,
		       //g_stats.m_totalSpiderErrorsNew,
		       //g_stats.m_totalSpiderErrorsOld,
		       //g_stats.m_spiderErrorsNew +
		       //g_stats.m_spiderErrorsOld,
		       //g_stats.m_spiderErrorsNew,
		       //g_stats.m_spiderErrorsOld,

		       tsr, nsr, osr, tssr, nssr, ossr );

	int32_t bucketsNew[65536];
	int32_t bucketsOld[65536];
	memset ( bucketsNew , 0 , 65536*4 );
	memset ( bucketsOld , 0 , 65536*4 );
	for ( int32_t i = 0 ; i < points; i++ ) {
		int32_t n = g_stats.m_errCodes[i];
		if ( n < 0 || n > 65535 ) {
			log("admin: Bad spider error code.");
			continue;
		}
		if ( g_stats.m_isSampleNew[i] )
			bucketsNew[n]++;
		else
			bucketsOld[n]++;
	}
	for ( int32_t i = 0 ; i < 65536 ; i++ ) {
		if ( g_stats.m_allErrorsNew[i] == 0 &&
		     g_stats.m_allErrorsOld[i] == 0 &&
		     bucketsNew[i] == 0 && bucketsOld[i] == 0 ) continue;
		sb.safePrintf (
			       "<tr bgcolor=#%s>"
			       "<td><b><a href=/search?c=%s&q=gbstatusmsg%%3A"
			       "%%22"
			       ,
			       LIGHT_BLUE , cr->m_coll );
		sb.urlEncode(mstrerror(i));
		sb.safePrintf ("%%22>"
			       "%s"
			       "</a>"
			       "</b></td>"
			       "<td>%" PRId64"</td>"
			       "<td>%" PRId64"</td>"
			       "<td>%" PRId64"</td>"
			       "<td>%" PRId32"</td>"
			       "<td>%" PRId32"</td>"
			       "<td>%" PRId32"</td>"
			       "</tr>\n" ,
			       mstrerror(i),
			       g_stats.m_allErrorsNew[i] +
			       g_stats.m_allErrorsOld[i],
			       g_stats.m_allErrorsNew[i],
			       g_stats.m_allErrorsOld[i],
			       bucketsNew[i] + bucketsOld[i] ,
			       bucketsNew[i] ,
			       bucketsOld[i] );
	}

	sb.safePrintf ( "</table><br>\n" );



	// describe the various parms
	/*
	sb.safePrintf ( 
		       "<table width=100%% bgcolor=#%s "
		       "cellpadding=4 border=1>"
		       "<tr class=poo>"
		       "<td colspan=2 bgcolor=#%s>"
		       "<b>Field descriptions</b>"
		       "</td>"
		       "</tr>\n"
		       "<tr class=poo>"
		       "<td>hits</td><td>The number of  attempts that were "
		       "made by the spider to read a url from the spider "
		       "queue cache.</td>"
		       "</tr>\n"


		       "<tr class=poo>"
		       "<td>misses</td><td>The number of those attempts that "
		       "failed to get a url to spider.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>cached</td><td>The number of urls that are "
		       "currently in the spider queue cache.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>water</td><td>The number of urls that were in the "
		       "spider queue cache at any one time, since the start "
		       "of the last disk scan.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>kicked</td><td>The number of urls that were "
		       "replaced in the spider queue cache with urls loaded "
		       "from disk, since the start of the last disk scan.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>added</td><td>The number of urls that were added "
		       "to the spider queue cache since the start of the last "
		       "disk scan. After a document is spidered its url "
		       "if often added again to the spider queue cache.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>attempted</td><td>The number of urls that "
		       "Gigablast attempted to add to the spider queue cache "
		       "since the start of the last disk scan. In "
		       "a distributed environment, urls are distributed "
		       "between twins so not all urls read will "
		       "make it into the spider queue cache. Also includes "
		       "spider recs attempted to be re-added to spiderdb "
		       "after being spidering, but usually with a different "
		       "spider time.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>nl</td><td>This is 1 iff Gigablast currently "
		       "needs to reload the spider queue cache from disk.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>rnl</td><td>This is 1 iff Gigablast currently "
		       "really needs to reload the spider queue cache from "
		       "disk.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>more</td><td>This is 1 iff there are urls on "
		       "the disk that are not in the spider queue cache.</td>"
		       "</tr>\n"


		       "<tr class=poo>"
		       "<td>loading</td><td>This is 1 iff Gigablast is "
		       "currently loading this spider cache queue from "
		       "disk.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>scanned</td><td>The number of bytes that were "
		       "read from disk since the start of the last disk "
		       "scan.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>reads</td><td>The number of disk read "
		       "operations since the start of the last disk "
		       "scan.</td>"
		       "</tr>\n"

		       "<tr class=poo>"
		       "<td>elapsed</td><td>The time in seconds that has "
		       "elapsed since the start or end of the last disk "
		       "scan, depending on if a scan is currently in "
		       "progress.</td>"
		       "</tr>\n"

		       "</table>\n",

		       LIGHT_BLUE ,
		       DARK_BLUE  );
	*/

	// done if no sc
	if ( ! sc ) {
		// get the socket
		TcpSocket *s = st->m_socket;
		// then we can nuke the state
		mdelete ( st , sizeof(State11) , "PageSpiderdb" );
		delete (st);
		// erase g_errno for sending
		g_errno = 0;
		// now encapsulate it in html head/tail and send it off
		return g_httpServer.sendDynamicPage (s, sb.getBufStart(),
						     sb.length() );
	}

	/////
	//
	// READY TO SPIDER table
	//
	/////

	int32_t ns = 0;
	if ( sc ) ns = sc->m_doleIpTable.getNumSlotsUsed();

	// begin the table
	sb.safePrintf ( "<table %s>\n"
			"<tr><td colspan=50>"
			"<b>URLs Ready to Spider for collection "
			"<font color=red><b>%s</b>"
			"</font>"
			" (%" PRId32" ips in doleiptable)"
			,
			TABLE_STYLE,
			st->m_coll ,
			ns );

	// print time format: 7/23/1971 10:45:32
	time_t nowUTC = getTimeGlobal();
	struct tm *timeStruct ;
	char time[256];
	timeStruct = gmtime ( &nowUTC );
	strftime ( time , 256 , "%b %e %T %Y UTC", timeStruct );
	sb.safePrintf("</b>" //  (current time = %s = %" PRIu32") "
		      "</td></tr>\n" 
		      //,time,nowUTC
		      );

	// the table headers so SpiderRequest::printToTable() works
	if ( ! SpiderRequest::printTableHeader ( &sb ,false ) ) return false;
	// the the doledb spider recs
	char *bs = sbTable->getBufStart();
	if ( bs && ! sb.safePrintf("%s",bs) ) return false;
	// end the table
	sb.safePrintf ( "</table>\n" );
	sb.safePrintf ( "<br>\n" );



	/////////////////
	//
	// PRINT WAITING TREE
	//
	// each row is an ip. print the next url to spider for that ip.
	//
	/////////////////
	sb.safePrintf ( "<table %s>\n"
			"<tr><td colspan=50>"
			"<b>IPs Waiting for Selection Scan for collection "
			"<font color=red><b>%s</b>"
			"</font>"
			,
			TABLE_STYLE,
			st->m_coll );
	// print time format: 7/23/1971 10:45:32
	int64_t timems = gettimeofdayInMillisecondsGlobal();
	sb.safePrintf("</b> (current time = %" PRIu64")(totalcount=%" PRId32")"
		      "(waittablecount=%" PRId32")",
		      timems,
		      sc->m_waitingTree.getNumUsedNodes(),
		      sc->m_waitingTable.getNumUsedSlots());

	double a = (double)g_spiderdb.getUrlHash48 ( &sc->m_firstKey );
	double b = (double)g_spiderdb.getUrlHash48 ( &sc->m_endKey );
	double c = (double)g_spiderdb.getUrlHash48 ( &sc->m_nextKey );
	double percent = (100.0 * (c-a)) ;
	if ( b-a > 0 ) percent /= (b-a);
	if ( percent > 100.0 ) percent = 100.0;
	if ( percent < 0.0 ) percent = 0.0;
	sb.safePrintf("(spiderdb scan for ip %s is %.2f%% complete)",
		      iptoa(sc->m_scanningIp),
		      (float)percent );

	sb.safePrintf("</td></tr>\n");
	sb.safePrintf("<tr bgcolor=#%s>",DARK_BLUE);
	sb.safePrintf("<td><b>spidertime (MS)</b></td>\n");
	sb.safePrintf("<td><b>firstip</b></td>\n");
	sb.safePrintf("</tr>\n");
	// the the waiting tree
	int32_t node = sc->m_waitingTree.getFirstNode();
	int32_t count = 0;
	//uint64_t nowMS = gettimeofdayInMillisecondsGlobal();
	for ( ; node >= 0 ; node = sc->m_waitingTree.getNextNode(node) ) {
		// breathe
		QUICKPOLL(MAX_NICENESS);
		// get key
		key_t *key = (key_t *)sc->m_waitingTree.getKey(node);
		// get ip from that
		int32_t firstIp = (key->n0) & 0xffffffff;
		// get the time
		uint64_t spiderTimeMS = key->n1;
		// shift upp
		spiderTimeMS <<= 32;
		// or in
		spiderTimeMS |= (key->n0 >> 32);
		char *note = "";
		// if a day more in the future -- complain
		// no! we set the repeat crawl to 3000 days for crawl jobs that
		// do not repeat...
		// if ( spiderTimeMS > nowMS + 1000 * 86400 )
		// 	note = " (<b><font color=red>This should not be "
		// 		"this far into the future. Probably a corrupt "
		// 		"SpiderRequest?</font></b>)";
		// get the rest of the data
		sb.safePrintf("<tr bgcolor=#%s>"
			      "<td>%" PRId64"%s</td>"
			      "<td>%s</td>"
			      "</tr>\n",
			      LIGHT_BLUE,
			      (int64_t)spiderTimeMS,
			      note,
			      iptoa(firstIp));
		// stop after 20
		if ( ++count == 20 ) break;
	}
	// ...
	if ( count ) 
		sb.safePrintf("<tr bgcolor=#%s>"
			      "<td colspan=10>...</td></tr>\n",
			      LIGHT_BLUE);
	// end the table
	sb.safePrintf ( "</table>\n" );
	sb.safePrintf ( "<br>\n" );

	// get the socket
	TcpSocket *s = st->m_socket;
	// then we can nuke the state
	mdelete ( st , sizeof(State11) , "PageSpiderdb" );
	delete (st);
	// erase g_errno for sending
	g_errno = 0;
	// now encapsulate it in html head/tail and send it off
	return g_httpServer.sendDynamicPage (s, sb.getBufStart(),sb.length() );
}



///////////////////////////////////
//
// URLFILTERS
//
///////////////////////////////////

#define SIGN_EQ 1
#define SIGN_NE 2
#define SIGN_GT 3
#define SIGN_LT 4
#define SIGN_GE 5
#define SIGN_LE 6


class PatternData {
public:
	// hash of the subdomain or domain for this line in sitelist
	int32_t m_thingHash32;
	// ptr to the line in CollectionRec::m_siteListBuf
	int32_t m_patternStrOff;
	// offset of the url path in the pattern, 0 means none
	int16_t m_pathOff;
	int16_t m_pathLen;
	// offset into buffer. for 'tag:shallow site:walmart.com' type stuff
	int32_t  m_tagOff;
	int16_t m_tagLen;
};

void doneAddingSeedsWrapper ( void *state ) {
	// note it
	log("basic: done adding seeds using msg4");
}

// . Collectiondb.cpp calls this when any parm flagged with
//   PF_REBUILDURLFILTERS is updated
// . it only adds sites via msg4 that are in "siteListArg" but NOT in the
//   current CollectionRec::m_siteListBuf
// . updates SpiderColl::m_siteListDomTable to see what doms we can spider
// . updates SpiderColl::m_negSubstringBuf and m_posSubStringBuf to
//   see what substrings in urls are disallowed/allowable for spidering
// . this returns false if it blocks
// . returns true and sets g_errno on error
// . uses msg4 to add seeds to spiderdb if necessary if "siteListArg"
//   has new urls that are not currently in cr->m_siteListBuf
// . only adds seeds for the shard we are on iff we are responsible for
//   the fake firstip!!! that way only one shard does the add.
bool updateSiteListBuf ( collnum_t collnum ,
                         bool addSeeds ,
                         char *siteListArg ) {

	CollectionRec *cr = g_collectiondb.getRec ( collnum );
	if ( ! cr ) return true;

	// tell spiderloop to update the active list in case this
	// collection suddenly becomes active
	g_spiderLoop.m_activeListValid = false;

	// this might make a new spidercoll...
	SpiderColl *sc = g_spiderCache.getSpiderColl ( cr->m_collnum );

	// sanity. if in use we should not even be here
	if ( sc->m_msg4x.m_inUse ) {
		log( LOG_WARN, "basic: trying to update site list while previous update still outstanding.");
		g_errno = EBADENGINEER;
		return true;
	}

	// when sitelist is update Parms.cpp should invalidate this flag!
	//if ( sc->m_siteListTableValid ) return true;

	// hash current sitelist entries, each line so we don't add
	// dup requests into spiderdb i guess...
	HashTableX dedup;
	if ( ! dedup.set ( 4,0,1024,NULL,0,false,0,"sldt") ) {
		return true;
	}

	// this is a safebuf PARM in Parms.cpp now HOWEVER, not really
	// because we set it here from a call to CommandUpdateSiteList()
	// because it requires all this computational crap.
	char *op = cr->m_siteListBuf.getBufStart();

	// scan and hash each line in it
	for ( ; ; ) {
		// done?
		if ( ! *op ) break;
		// skip spaces
		if ( is_wspace_a(*op) ) op++;
		// done?
		if ( ! *op ) break;
		// get end
		char *s = op;
		// skip to end of line marker
		for ( ; *op && *op != '\n' ; op++ ) ;
		// keep it simple
		int32_t h32 = hash32 ( s , op - s );
		// for deduping
		if ( ! dedup.addKey ( &h32 ) ) {
			return true;
		}
	}

	// get the old sitelist Domain Hash to PatternData mapping table
	// which tells us what domains, subdomains or paths we can or
	// can not spider...
	HashTableX *dt = &sc->m_siteListDomTable;

	// reset it
	if ( ! dt->set ( 4 ,
	                 sizeof(PatternData),
	                 1024 ,
	                 NULL ,
	                 0 ,
	                 true , // allow dup keys?
	                 0 , // niceness - at least for now
	                 "sldt" ) ) {
		return true;
	}


	// clear old shit
	sc->m_posSubstringBuf.purge();
	sc->m_negSubstringBuf.purge();

	// we can now free the old site list methinks
	//cr->m_siteListBuf.purge();

	// reset flags
	//sc->m_siteListAsteriskLine = NULL;
	sc->m_siteListHasNegatives = false;
	sc->m_siteListIsEmpty = true;

	sc->m_siteListIsEmptyValid = true;

	// use this so it will be free automatically when msg4 completes!
	SafeBuf *spiderReqBuf = &sc->m_msg4x.m_tmpBuf;

	//char *siteList = cr->m_siteListBuf.getBufStart();

	// scan the list
	char *pn = siteListArg;

	// completely empty?
	if ( ! pn ) return true;

	int32_t lineNum = 1;

	int32_t added = 0;

	Url u;

	for ( ; *pn ; lineNum++ ) {

		// get end
		char *s = pn;
		// skip to end of line marker
		for ( ; *pn && *pn != '\n' ; pn++ ) ;

		// point to the pattern (skips over "tag:xxx " if there)
		char *patternStart = s;

		// back p up over spaces in case ended in spaces
		char *pe = pn;
		for ( ; pe > s && is_wspace_a(pe[-1]) ; pe-- );

		// skip over the \n so pn points to next line for next time
		if ( *pn == '\n' ) pn++;

		// make hash of the line
		int32_t h32 = hash32 ( s , pe - s );

		bool seedMe = true;
		bool isUrl = true;
		bool isNeg = false;
		bool isFilter = true;

		// skip spaces at start of line
		for ( ; *s && *s == ' ' ; s++ );

		// comment?
		if ( *s == '#' ) continue;

		// empty line?
		if ( s[0] == '\r' && s[1] == '\n' ) { s++; continue; }

		// empty line?
		if ( *s == '\n' ) continue;

		// all?
		//if ( *s == '*' ) {
		//	sc->m_siteListAsteriskLine = start;
		//	continue;
		//}

		char *tag = NULL;
		int32_t tagLen = 0;

		innerLoop:

		// skip spaces
		for ( ; *s && *s == ' ' ; s++ );


		// exact:?
		//if ( strncmp(s,"exact:",6) == 0 ) {
		//	s += 6;
		//	goto innerLoop;
		//}

		// these will be manual adds and should pass url filters
		// because they have the "ismanual" directive override
		if ( strncmp(s,"seed:",5) == 0 ) {
			s += 5;
			isFilter = false;
			goto innerLoop;
		}


		// does it start with "tag:xxxxx "?
		if ( *s == 't' &&
		     s[1] == 'a' &&
		     s[2] == 'g' &&
		     s[3] == ':' ) {
			tag = s+4;
			for ( ; *s && ! is_wspace_a(*s) ; s++ );
			tagLen = s - tag;
			// skip over white space after tag:xxxx so "s"
			// point to the url or contains: or whatever
			for ( ; *s && is_wspace_a(*s) ; s++ );
			// set pattern start to AFTER the tag stuff
			patternStart = s;
		}

		if ( *s == '-' ) {
			sc->m_siteListHasNegatives = true;
			isNeg = true;
			s++;
		}

		if ( strncmp(s,"site:",5) == 0 ) {
			s += 5;
			seedMe = false;
			goto innerLoop;
		}

		if ( strncmp(s,"contains:",9) == 0 ) {
			s += 9;
			seedMe = false;
			isUrl = false;
			goto innerLoop;
		}

		int32_t slen = pe - s;

		// empty line?
		if ( slen <= 0 )
			continue;

		// add to string buffers
		if ( ! isUrl && isNeg ) {
			if ( !sc->m_negSubstringBuf.safeMemcpy(s,slen))
				return true;
			if ( !sc->m_negSubstringBuf.pushChar('\0') )
				return true;
			if ( ! tagLen ) continue;
			// append tag
			if ( !sc->m_negSubstringBuf.safeMemcpy("tag:",4))
				return true;
			if ( !sc->m_negSubstringBuf.safeMemcpy(tag,tagLen) )
				return true;
			if ( !sc->m_negSubstringBuf.pushChar('\0') )
				return true;
		}
		if ( ! isUrl ) {
			// add to string buffers
			if ( ! sc->m_posSubstringBuf.safeMemcpy(s,slen) )
				return true;
			if ( ! sc->m_posSubstringBuf.pushChar('\0') )
				return true;
			if ( ! tagLen ) continue;
			// append tag
			if ( !sc->m_posSubstringBuf.safeMemcpy("tag:",4))
				return true;
			if ( !sc->m_posSubstringBuf.safeMemcpy(tag,tagLen) )
				return true;
			if ( !sc->m_posSubstringBuf.pushChar('\0') )
				return true;
			continue;
		}


		u.set( s, slen );

		// error? skip it then...
		if ( u.getHostLen() <= 0 ) {
			log("basic: error on line #%" PRId32" in sitelist",lineNum);
			continue;
		}

		// is fake ip assigned to us?
		int32_t firstIp = getFakeIpForUrl2 ( &u );

		if ( ! isAssignedToUs( firstIp ) ) continue;

		// see if in existing table for existing site list
		if ( addSeeds &&
		     // a "site:" directive mean no seeding
		     // a "contains:" directive mean no seeding
		     seedMe &&
		     // do not seed stuff after tag:xxx directives
		     // no, we need to seed it to avoid confusion. if
		     // they don't want it seeded they can use site: after
		     // the tag:
		     //! tag &&
		     ! dedup.isInTable ( &h32 ) ) {
			// make spider request
			SpiderRequest sreq;
			sreq.setFromAddUrl ( u.getUrl() );
			if (
				// . add this url to spiderdb as a spiderrequest
				// . calling msg4 will be the last thing we do
					!spiderReqBuf->safeMemcpy(&sreq,sreq.getRecSize()))
				return true;
			// count it
			added++;

		}

		// if it is a "seed: xyz.com" thing it is seed only
		// do not use it for a filter rule
		if ( ! isFilter ) continue;


		// make the data node used for filtering urls during spidering
		PatternData pd;
		// hash of the subdomain or domain for this line in sitelist
		pd.m_thingHash32 = u.getHostHash32();
		// . ptr to the line in CollectionRec::m_siteListBuf.
		// . includes pointing to "exact:" too i guess and tag: later.
		// . store offset since CommandUpdateSiteList() passes us
		//   a temp buf that will be freed before copying the buf
		//   over to its permanent place at cr->m_siteListBuf
		pd.m_patternStrOff = patternStart - siteListArg;
		// offset of the url path in the pattern, 0 means none
		pd.m_pathOff = 0;
		// did we have a tag?
		if ( tag ) {
			pd.m_tagOff = tag - siteListArg;
			pd.m_tagLen = tagLen;
		}
		else {
			pd.m_tagOff = -1;
			pd.m_tagLen = 0;
		}
		// scan url pattern, it should start at "s"
		char *x = s;
		// go all the way to the end
		for ( ; *x && x < pe ; x++ ) {
			// skip ://
			if ( x[0] == ':' && x[1] =='/' && x[2] == '/' ) {
				x += 2;
				continue;
			}
			// stop if we hit another /, that is path start
			if ( x[0] != '/' ) continue;
			x++;
			// empty path besides the /?
			if (  x >= pe   ) break;
			// ok, we got something here i think
			// no, might be like http://xyz.com/?poo
			//if ( u.getPathLen() <= 1 ) { char *xx=NULL;*xx=0; }
			// calc length from "start" of line so we can
			// jump to the path quickly for compares. inc "/"
			pd.m_pathOff = (x-1) - patternStart;
			pd.m_pathLen = pe - (x-1);
			break;
		}

		// add to new dt
		int32_t domHash32 = u.getDomainHash32();
		if ( ! dt->addKey ( &domHash32 , &pd ) )
			return true;

		// we have some patterns in there
		sc->m_siteListIsEmpty = false;
	}

	// go back to a high niceness
	dt->m_niceness = MAX_NICENESS;

	if ( ! addSeeds ) return true;

	log( "spider: adding %" PRId32" seed urls", added );

	// use spidercoll to contain this msg4 but if in use it
	// won't be able to be deleted until it comes back..
	return sc->m_msg4x.addMetaList ( spiderReqBuf, sc->m_collnum, sc, doneAddingSeedsWrapper, MAX_NICENESS, RDB_SPIDERDB );
}

// . Spider.cpp calls this to see if a url it wants to spider is
//   in our "site list"
// . we should return the row of the FIRST match really
// . the url patterns all contain a domain now, so this can use the domain
//   hash to speed things up
// . return ptr to the start of the line in case it has "tag:" i guess
char *getMatchingUrlPattern ( SpiderColl *sc, SpiderRequest *sreq, char *tagArg ) { // tagArg can be NULL
	logTrace( g_conf.m_logTraceSpider, "BEGIN" );

	// if it has * and no negatives, we are in!
	//if ( sc->m_siteListAsteriskLine && ! sc->m_siteListHasNegatives )
	//	return sc->m_siteListAsteriskLine;

	// if it is just a bunch of comments or blank lines, it is empty
	if ( sc->m_siteListIsEmptyValid && sc->m_siteListIsEmpty ) {
		logTrace( g_conf.m_logTraceSpider, "END. Empty. Returning NULL" );
		return NULL;
	}

	// if we had a list of contains: or regex: directives in the sitelist
	// we have to linear scan those
	char *nb = sc->m_negSubstringBuf.getBufStart();
	char *nbend = nb + sc->m_negSubstringBuf.getLength();
	for ( ; nb && nb < nbend ; ) {
		// return NULL if matches a negative substring
		if ( strstr ( sreq->m_url , nb ) ) {
			logTrace( g_conf.m_logTraceSpider, "END. Matches negative substring. Returning NULL" );
			return NULL;
		}
		// skip it
		nb += strlen(nb) + 1;
	}


	char *myPath = NULL;

	// check domain specific tables
	HashTableX *dt = &sc->m_siteListDomTable;

	// get this
	CollectionRec *cr = sc->getCollectionRec();

	// need to build dom table for pattern matching?
	if ( dt->getNumSlotsUsed() == 0 && cr ) {
		// do not add seeds, just make siteListDomTable, etc.
		updateSiteListBuf ( sc->m_collnum ,
		                    false , // add seeds?
		                    cr->m_siteListBuf.getBufStart() );
	}

	if ( dt->getNumSlotsUsed() == 0 ) {
		// empty site list -- no matches
		logTrace( g_conf.m_logTraceSpider, "END. No slots. Returning NULL" );
		return NULL;
		//char *xx=NULL;*xx=0; }
	}

	// this table maps a 32-bit domain hash of a domain to a
	// patternData class. only for those urls that have firstIps that
	// we handle.
	int32_t slot = dt->getSlot ( &sreq->m_domHash32 );

	char *buf = cr->m_siteListBuf.getBufStart();

	// loop over all the patterns that contain this domain and see
	// the first one we match, and if we match a negative one.
	for ( ; slot >= 0 ; slot = dt->getNextSlot(slot,&sreq->m_domHash32)) {
		// get pattern
		PatternData *pd = (PatternData *)dt->getValueFromSlot ( slot );
		// point to string
		char *patternStr = buf + pd->m_patternStrOff;
		// is it negative? return NULL if so so url will be ignored
		//if ( patternStr[0] == '-' )
		//	return NULL;
		// otherwise, it has a path. skip if we don't match path ptrn
		if ( pd->m_pathOff ) {
			if ( ! myPath ) myPath = sreq->getUrlPath();
			if ( strncmp (myPath, patternStr + pd->m_pathOff, pd->m_pathLen ) ) {
				continue;
			}
		}

		// for entries like http://domain.com/ we have to match
		// protocol and url can NOT be like www.domain.com to match.
		// this is really like a regex like ^http://xyz.com/poo/boo/
		if ( (patternStr[0]=='h' ||
		      patternStr[0]=='H') &&
		     ( patternStr[1]=='t' ||
		       patternStr[1]=='T' ) &&
		     ( patternStr[2]=='t' ||
		       patternStr[2]=='T' ) &&
		     ( patternStr[3]=='p' ||
		       patternStr[3]=='P' ) ) {
			char *x = patternStr+4;
			// is it https:// ?
			if ( *x == 's' || *x == 'S' ) x++;
			// watch out for subdomains like http.foo.com
			if ( *x != ':' ) {
				goto nomatch;
			}
			// ok, we have to substring match exactly. like
			// ^http://xyssds.com/foobar/
			char *a = patternStr;
			char *b = sreq->m_url;
			for ( ; ; a++, b++ ) {
				// stop matching when pattern is exhausted
				if ( is_wspace_a(*a) || ! *a ) {
					logTrace( g_conf.m_logTraceSpider, "END. Pattern is exhausted. Returning '%s'", patternStr );
					return patternStr;
				}
				if ( *a != *b ) {
					break;
				}
			}
			// we failed to match "pd" so try next line
			continue;
		}

		nomatch:
		// if caller also gave a tag we'll want to see if this
		// "pd" has an entry for this domain that has that tag
		if ( tagArg ) {
			// skip if entry has no tag
			if ( pd->m_tagLen <= 0 ) {
				continue;
			}

			// skip if does not match domain or host
			if ( pd->m_thingHash32 != sreq->m_domHash32 &&
			     pd->m_thingHash32 != sreq->m_hostHash32 ) {
				continue;
			}

			// compare tags
			char *pdtag = pd->m_tagOff + buf;
			if ( strncmp(tagArg,pdtag,pd->m_tagLen) ) {
				continue;
			}

			// must be nothing after
			if ( is_alnum_a(tagArg[pd->m_tagLen]) ) {
				continue;
			}

			// that's a match
			logTrace( g_conf.m_logTraceSpider, "END. Match tag. Returning '%s'", patternStr );
			return patternStr;
		}

		// was the line just a domain and not a subdomain?
		if ( pd->m_thingHash32 == sreq->m_domHash32 ) {
			// this will be false if negative pattern i guess
			logTrace( g_conf.m_logTraceSpider, "END. Match domain. Returning '%s'", patternStr );
			return patternStr;
		}

		// was it just a subdomain?
		if ( pd->m_thingHash32 == sreq->m_hostHash32 ) {
			// this will be false if negative pattern i guess
			logTrace( g_conf.m_logTraceSpider, "END. Match subdomain. Returning '%s'", patternStr );
			return patternStr;
		}
	}


	// if we had a list of contains: or regex: directives in the sitelist
	// we have to linear scan those
	char *pb = sc->m_posSubstringBuf.getBufStart();
	char *pend = pb + sc->m_posSubstringBuf.length();
	for ( ; pb && pb < pend ; ) {
		// return NULL if matches a negative substring
		if ( strstr ( sreq->m_url , pb ) ) {
			logTrace( g_conf.m_logTraceSpider, "END. Match. Returning '%s'", pb );
			return pb;
		}
		// skip it
		pb += strlen(pb) + 1;
	}


	// is there an '*' in the patterns?
	//if ( sc->m_siteListAsteriskLine ) return sc->m_siteListAsteriskLine;

	return NULL;
}

// . this is called by SpiderCache.cpp for every url it scans in spiderdb
// . we must skip certain rules in getUrlFilterNum() when doing to for Msg20
//   because things like "parentIsRSS" can be both true or false since a url
//   can have multiple spider recs associated with it!
int32_t getUrlFilterNum ( 	SpiderRequest	*sreq,
							SpiderReply		*srep,
							int32_t			nowGlobal,
							bool			isForMsg20,
							int32_t			niceness,
							CollectionRec	*cr,
							bool			isOutlink,
							HashTableX		*quotaTable,
							int32_t			langIdArg ) {
	logTrace( g_conf.m_logTraceSpider, "BEGIN" );
		
	if ( ! sreq ) {
		log("spider: sreq is NULL!");
	}

	int32_t langId = langIdArg;
	if ( srep ) langId = srep->m_langId;

	// convert lang to string
	const char *lang    = NULL;
	int32_t  langLen = 0;
	if ( langId >= 0 ) { // if ( srep ) {
		// this is NULL on corruption
		lang = getLanguageAbbr ( langId );//srep->m_langId );	
		if (lang) langLen = gbstrlen(lang);
	}

	// . get parent language in the request
	// . primarpy language of the parent page that linked to this url
	const char *plang = NULL;
	int32_t  plangLen = 0;
	plang = getLanguageAbbr(sreq->m_parentLangId);
	if ( plang ) plangLen = gbstrlen(plang);

	const char *tld = (char *)-1;
	int32_t  tldLen;

	int32_t  urlLen = sreq->getUrlLen();
	char *url    = sreq->m_url;

	char *row;
	bool checkedRow = false;
	//SpiderColl *sc = cr->m_spiderColl;
	SpiderColl *sc = g_spiderCache.getSpiderColl(cr->m_collnum);

	if ( ! quotaTable ) quotaTable = &sc->m_localTable;

	// shortcut
	char *ucp = cr->m_diffbotUrlCrawlPattern.getBufStart();
	char *upp = cr->m_diffbotUrlProcessPattern.getBufStart();

	if ( upp && ! upp[0] ) upp = NULL;
	if ( ucp && ! ucp[0] ) ucp = NULL;

	// get the compiled regular expressions
	regex_t *ucr = &cr->m_ucr;
	regex_t *upr = &cr->m_upr;
	if ( ! cr->m_hasucr ) ucr = NULL;
	if ( ! cr->m_hasupr ) upr = NULL;


	char *ext;
	//char *special;

	// CONSIDER COMPILING FOR SPEED:
	// 1) each command can be combined into a bitmask on the spiderRequest
	//    bits, or an access to m_siteNumInlinks, or a substring match
	// 2) put all the strings we got into the list of Needles
	// 3) then generate the list of needles the SpiderRequest/url matches
	// 4) then reduce each line to a list of needles to have, a
	//    min/max/equal siteNumInlinks, min/max/equal hopCount,
	//    and a bitMask to match the bit flags in the SpiderRequest

	// stop at first regular expression it matches
	for ( int32_t i = 0 ; i < cr->m_numRegExs ; i++ ) {
		// breathe
		QUICKPOLL ( niceness );
		// get the ith rule
		SafeBuf *sb = &cr->m_regExs[i];
		//char *p = cr->m_regExs[i];
		char *p = sb->getBufStart();

checkNextRule:

		// skip leading whitespace
		while ( *p && isspace(*p) ) p++;

		// do we have a leading '!'
		bool val = 0;
		if ( *p == '!' ) { val = 1; p++; }
		// skip whitespace after the '!'
		while ( *p && isspace(*p) ) p++;

		// new rules for when to download (diffbot) page
		if ( *p ==  'm' && 
		     p[1]== 'a' &&
		     p[2]== 't' &&
		     p[3]== 'c' &&
		     p[4]== 'h' &&
		     p[5]== 'e' &&
		     p[6]== 's' &&
		     p[7]== 'u' &&
		     p[8]== 'c' &&
		     p[9]== 'p' ) {
			// . skip this expression row if does not match
			// . url must match one of the patterns in there. 
			// . inline this for speed
			// . "ucp" is a ||-separated list of substrings
			// . "ucr" is a regex
			// . regexec returns 0 for a match
			if ( ucr && regexec(ucr,url,0,NULL,0) &&
			     // seed or other manual addition always matches
			     ! sreq->m_isAddUrl &&
			     ! sreq->m_isPageReindex &&
			     ! sreq->m_isInjecting )
				continue;
			// do not require a match on ucp if ucr is given
			if ( ucp && ! ucr &&
			     ! doesStringContainPattern(url,ucp) &&
			     // seed or other manual addition always matches
			     ! sreq->m_isAddUrl &&
			     ! sreq->m_isPageReindex &&
			     ! sreq->m_isInjecting )
				continue;
			p += 10;
			p = strstr(p,"&&");
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		// new rules for when to "process" (diffbot) page
		if ( *p ==  'm' && 
		     p[1]== 'a' &&
		     p[2]== 't' &&
		     p[3]== 'c' &&
		     p[4]== 'h' &&
		     p[5]== 'e' &&
		     p[6]== 's' &&
		     p[7]== 'u' &&
		     p[8]== 'p' &&
		     p[9]== 'p' ) {
			// . skip this expression row if does not match
			// . url must match one of the patterns in there. 
			// . inline this for speed
			// . "upp" is a ||-separated list of substrings
			// . "upr" is a regex
			// . regexec returns 0 for a match
			if ( upr && regexec(upr,url,0,NULL,0) ) 
				continue;
			if ( upp && !upr &&!doesStringContainPattern(url,upp))
				continue;
			p += 10;
			p = strstr(p,"&&");
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}


		if ( *p=='h' && strncmp(p,"hasauthorityinlink",18) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if not valid (pageaddurl? injection?)
			if ( ! sreq->m_hasAuthorityInlinkValid ) continue;
			// if no match continue
			if ( (bool)sreq->m_hasAuthorityInlink==val)continue;
			// skip
			p += 18;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		if ( *p=='h' && strncmp(p,"hasreply",8) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if we got a reply, we are not new!!
			//if ( (bool)srep == (bool)val ) continue;
			if ( (bool)(sreq->m_hadReply) == (bool)val ) continue;
			// skip it for speed
			p += 8;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// hastmperror, if while spidering, the last reply was
		// like EDNSTIMEDOUT or ETCPTIMEDOUT or some kind of
		// usually temporary condition that warrants a retry
		if ( *p=='h' && strncmp(p,"hastmperror",11) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}
			// skip for msg20
			if ( isForMsg20 ) continue;
			// reply based
			if ( ! srep ) continue;
			// get our error code
			int32_t errCode = srep->m_errCode;
			// . make it zero if not tmp error
			// . now have EDOCUNCHANGED and EDOCNOGOODDATE from
			//   Msg13.cpp, so don't count those here...
			if ( errCode != EDNSTIMEDOUT &&
			     errCode != ETCPTIMEDOUT &&
			     errCode != EDNSDEAD &&
			     // add this here too now because we had some
			     // seeds that failed one time and the crawl
			     // never repeated after that!
			     errCode != EBADIP &&
			     // assume diffbot is temporarily experiencing errs
			     // but the crawl, if recurring, should retry these
			     // at a later point
			     errCode != EDIFFBOTUNABLETOAPPLYRULES &&
			     errCode != EDIFFBOTCOULDNOTPARSE &&
			     errCode != EDIFFBOTCOULDNOTDOWNLOAD &&
			     errCode != EDIFFBOTINVALIDAPI &&
			     errCode != EDIFFBOTVERSIONREQ &&
			     errCode != EDIFFBOTURLPROCESSERROR &&
			     errCode != EDIFFBOTTOKENEXPIRED &&
			     errCode != EDIFFBOTUNKNOWNERROR &&
			     errCode != EDIFFBOTINTERNALERROR &&
			     // if diffbot received empty content when d'lding
			     errCode != EDIFFBOTEMPTYCONTENT &&
			     // or diffbot tcp timed out when d'lding the url
			     errCode != EDIFFBOTREQUESTTIMEDOUT &&
			     // if diffbot closed the socket on us...
			     errCode != EDIFFBOTMIMEERROR &&
			     // of the diffbot reply itself was not 200 (OK)
			     errCode != EDIFFBOTBADHTTPSTATUS &&
			     // out of memory while crawling?
			     errCode != ENOMEM &&
			     errCode != ENETUNREACH &&
			     errCode != EHOSTUNREACH )
				errCode = 0;
			// if no match continue
			if ( (bool)errCode == val ) continue;
			// skip
			p += 11;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		if ( *p != 'i' ) goto skipi;

		if ( strncmp(p,"isinjected",10) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_isInjecting==val ) continue;
			// skip
			p += 10;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp(p,"isdocidbased",12) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			//if ( (bool)sreq->m_urlIsDocId==val ) continue;
			if ( (bool)sreq->m_isPageReindex==val ) continue;
			// skip
			p += 12;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp(p,"isreindex",9) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			//if ( (bool)sreq->m_urlIsDocId==val ) continue;
			if ( (bool)sreq->m_isPageReindex==val ) continue;
			// skip
			p += 9;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		// is it in the big list of sites?
		if ( strncmp(p,"insitelist",10) == 0 ) {
			// rebuild site list
			if ( !sc->m_siteListIsEmptyValid ) {
				updateSiteListBuf( sc->m_collnum, false, cr->m_siteListBuf.getBufStart() );
			}

			// if there is no domain or url explicitly listed
			// then assume user is spidering the whole internet
			// and we basically ignore "insitelist"
			if ( sc->m_siteListIsEmptyValid && sc->m_siteListIsEmpty ) {
				// use a dummy row match
				row = (char *)1;
			} else if ( ! checkedRow ) {
				// only do once for speed
				checkedRow = true;
				// this function is in PageBasic.cpp
				row = getMatchingUrlPattern ( sc, sreq ,NULL);
			}

			// if we are not submitted from the add url api, skip
			if ( (bool)row == val ) {
				continue;
			}
			// skip
			p += 10;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		// . was it submitted from PageAddUrl.cpp?
		// . replaces the "add url priority" parm
		if ( strncmp(p,"isaddurl",8) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if we are not submitted from the add url api, skip
			if ( (bool)sreq->m_isAddUrl == val ) continue;
			// skip
			p += 8;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		if ( p[0]=='i' && strncmp(p,"ismanualadd",11) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// . if we are not submitted from the add url api, skip
			// . if we have '!' then val is 1
			if ( sreq->m_isAddUrl    || 
			     sreq->m_isInjecting ||
			     sreq->m_isPageReindex ||
			     sreq->m_isPageParser ) {
				if ( val ) continue;
			}
			else {
				if ( ! val ) continue;
			}
			// skip
			p += 11;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		// does it have an rss inlink? we want to expedite indexing
		// of such pages. i.e. that we gather from an rss feed that
		// we got from a pingserver...
		if ( strncmp(p,"isparentrss",11) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if we have no such inlink
			if ( (bool)sreq->m_parentIsRSS == val ) continue;
			// skip
			p += 11;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp(p,"isparentsitemap",15) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_parentIsSiteMap == val) continue;
			// skip
			p += 15;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		// does it have an rss inlink? we want to expedite indexing
		// of such pages. i.e. that we gather from an rss feed that
		// we got from a pingserver...
		if ( strncmp(p,"isroot",6) == 0 ) {
			// skip for msg20
			//if ( isForMsg20 ) continue;
			// this is a docid only url, no actual url, so skip
			if ( sreq->m_isPageReindex ) continue;
			// a fast check
			char *u = sreq->m_url;
			// skip http
			u += 4;
			// then optional s for https
			if ( *u == 's' ) u++;
			// then ://
			u += 3;
			// scan until \0 or /
			for ( ; *u && *u !='/' ; u++ );
			// if \0 we are root
			bool isRoot = true;
			if ( *u == '/' ) {
				u++;
				if ( *u ) isRoot = false;
			}
			// if we are not root
			if ( isRoot == val ) continue;
			// skip
			p += 6;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			
			p += 2;
			goto checkNextRule;
		}

		// we can now handle this guy since we have the latest
		// SpiderReply, pretty much guaranteed
		if ( strncmp(p,"isindexed",9) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}
			
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if reply does not KNOW because of an error
			// since XmDoc::indexDoc() called
			// XmlDoc::getNewSpiderReply() and did not have this
			// info...
			if ( srep && (bool)srep->m_isIndexedINValid ) continue;
			// if no match continue
			if ( srep && (bool)srep->m_isIndexed==val ) continue;
			// allow "!isindexed" if no SpiderReply at all
			if ( ! srep && val == 0 ) continue;
			// skip
			p += 9;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}


		// . check to see if a page is linked to by
		//   www.weblogs.com/shortChanges.xml and if it is we put
		//   it into a queue that has a respider rate no faster than
		//   30 days, because we don't need to spider it quick since
		//   it is in the ping server!
		if ( strncmp(p,"isparentpingserver",18) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_parentIsPingServer == val) continue;
			// skip
			p += 18;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp(p,"ispingserver",12) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_isPingServer == val ) continue;
			// skip
			p += 12;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp ( p , "isonsamesubdomain",17 ) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			if ( val == 0 &&
			     sreq->m_parentHostHash32 != sreq->m_hostHash32 ) 
				continue;
			if ( val == 1 &&
			     sreq->m_parentHostHash32 == sreq->m_hostHash32 ) 
				continue;
			p += 6;
			p = strstr(p, "&&");
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp ( p , "isfakeip",8 ) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if no match continue
			if ( (bool)sreq->m_fakeFirstIp == val ) continue;
			p += 8;
			p = strstr(p, "&&");
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		if ( strncmp ( p , "isonsamedomain",14 ) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			if ( val == 0 &&
			     sreq->m_parentDomHash32 != sreq->m_domHash32 ) 
				continue;
			if ( val == 1 &&
			     sreq->m_parentDomHash32 == sreq->m_domHash32 ) 
				continue;
			p += 6;
			p = strstr(p, "&&");
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}

		// jpg JPG gif GIF wmv mpg css etc.
		if ( strncmp ( p , "ismedia",7 ) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;

			// the new way is much faster, but support the
			// old way below for a while since this bit is new
			if ( sreq->m_hasMediaExtension )
				goto gotOne;
			// if that bit is valid, and zero, then we do not match
			if ( sreq->m_hasMediaExtensionValid )
				continue;

			// check the extension
			if ( urlLen<=5 ) continue;
			ext = url + urlLen - 4;
			if ( ext[0] == '.' ) {
				if ( to_lower_a(ext[1]) == 'c' &&
				     to_lower_a(ext[2]) == 's' &&
				     to_lower_a(ext[3]) == 's' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'm' &&
				     to_lower_a(ext[2]) == 'p' &&
				     to_lower_a(ext[3]) == 'g' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'p' &&
				     to_lower_a(ext[2]) == 'n' &&
				     to_lower_a(ext[3]) == 'g' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'w' &&
				     to_lower_a(ext[2]) == 'm' &&
				     to_lower_a(ext[3]) == 'v' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'w' &&
				     to_lower_a(ext[2]) == 'a' &&
				     to_lower_a(ext[3]) == 'v' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'j' &&
				     to_lower_a(ext[2]) == 'p' &&
				     to_lower_a(ext[3]) == 'g' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'g' &&
				     to_lower_a(ext[2]) == 'i' &&
				     to_lower_a(ext[3]) == 'f' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'i' &&
				     to_lower_a(ext[2]) == 'c' &&
				     to_lower_a(ext[3]) == 'o' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'm' &&
				     to_lower_a(ext[2]) == 'p' &&
				     to_lower_a(ext[3]) == '3' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'm' &&
				     to_lower_a(ext[2]) == 'p' &&
				     to_lower_a(ext[3]) == '4' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'm' &&
				     to_lower_a(ext[2]) == 'o' &&
				     to_lower_a(ext[3]) == 'v' )
					goto gotOne;
				if ( to_lower_a(ext[1]) == 'a' &&
				     to_lower_a(ext[2]) == 'v' &&
				     to_lower_a(ext[3]) == 'i' )
					goto gotOne;
			}
			else if ( ext[-1] == '.' ) {
				if ( to_lower_a(ext[0]) == 'm' &&
				     to_lower_a(ext[1]) == 'p' &&
				     to_lower_a(ext[2]) == 'e' &&
				     to_lower_a(ext[3]) == 'g' )
					goto gotOne;
				if ( to_lower_a(ext[0]) == 'j' &&
				     to_lower_a(ext[1]) == 'p' &&
				     to_lower_a(ext[2]) == 'e' &&
				     to_lower_a(ext[3]) == 'g' )
					goto gotOne;
			}

			// try to make detecting .css? super fast
			if ( ext[0] != '.' &&
			     ext[1] != '.' &&
			     urlLen > 10 ) {
				for(register int32_t k=urlLen-10;k<urlLen;k++){
					if ( url[k] != '.' ) continue;
					if ( url[k+1] == 'c' &&
					     url[k+2] == 's' &&
					     url[k+3] == 's' &&
					     url[k+4] == '?' )
						goto gotOne;
				}
			}

			// no match, try the next rule
			continue;
		gotOne:
			p += 7;
			p = strstr(p, "&&");
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}


		// check for "isrss" aka "rss"
		if ( strncmp(p,"isrss",5) == 0 ) {
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}
			// must have a reply
			if ( ! srep ) continue;
			// if we are not rss, we do not match this rule
			if ( (bool)srep->m_isRSS == val ) continue; 
			// skip it
			p += 5;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// check for "isrss" aka "rss"
		if ( strncmp(p,"isrssext",8) == 0 ) {
			// if we are not rss, we do not match this rule
			if ( (bool)sreq->m_isRSSExt == val ) continue; 
			// skip it
			p += 8;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// check for permalinks. for new outlinks we *guess* if its
		// a permalink by calling isPermalink() function.
		if (!strncmp(p,"ispermalink",11) ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}
			// must have a reply
			if ( ! srep ) continue;
			// if we are not rss, we do not match this rule
			if ( (bool)srep->m_isPermalink == val ) continue; 
			// skip it
			p += 11;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}

			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// supports LF_ISPERMALINK bit for outlinks that *seem* to
		// be permalinks but might not
		if (!strncmp(p,"ispermalinkformat",17) ) {
			// if we are not rss, we do not match this rule
			if ( (bool)sreq->m_isUrlPermalinkFormat == val ) {
				continue;
			}

			// check for &&
			p = strstr(p, "&&");

			// if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// check for this
		if ( strncmp(p,"isnewoutlink",12) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if we do not match this rule
			if ( (bool)sreq->m_isNewOutlink == val ) continue;
			// skip it
			p += 10;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// check for this
		if ( strncmp(p,"isnewrequest",12) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}
			// skip for msg20
			if ( isForMsg20 ) continue;
			// skip if we are a new request and val is 1 (has '!')
			if ( ! srep && val ) continue;
			// skip if we are a new request and val is 1 (has '!')
			if(srep&&sreq->m_addedTime>srep->m_spideredTime &&val)
				continue;
			// skip if we are old and val is 0 (does not have '!')
			if(srep&&sreq->m_addedTime<=srep->m_spideredTime&&!val)
				continue;
			// skip it for speed
			p += 12;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// kinda like isnewrequest, but has no reply. use hasreply?
		if ( strncmp(p,"isnew",5) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}
			// skip for msg20
			if ( isForMsg20 ) continue;
			// if we got a reply, we are not new!!
			if ( (bool)sreq->m_hadReply != (bool)val ) continue;
			// skip it for speed
			p += 5;
			// check for &&
			p = strstr(p, "&&");
			// if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}
		// iswww, means url is like www.xyz.com/...
		if ( strncmp(p,"iswww", 5) == 0 ) {
			// now this is a bit - doesn't seem to be working yet
			//if ( (bool)sreq->m_isWWWSubdomain == (bool)val ) 
			//	continue;
			// skip "iswww"
			p += 5;
			// skip over http:// or https://
			char *u = sreq->m_url;
			if ( u[4] == ':' ) u += 7;
			if ( u[5] == ':' ) u += 8;
			// url MUST be a www url
			char isWWW = 0;
			if( u[0] == 'w' &&
			    u[1] == 'w' &&
			    u[2] == 'w' ) isWWW = 1;
			// skip if no match
			if ( isWWW == val ) continue;
			// TODO: fix www.knightstown.skepter.com
			// maybe just have a bit in the spider request
			// another rule?
			p = strstr(p,"&&");
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			// skip the '&&'
			p += 2;
			goto checkNextRule;
		}

		// non-boolen junk
 skipi:

		// . we always match the "default" reg ex
		// . this line must ALWAYS exist!
		if ( *p=='d' && ! strcmp(p,"default" ) ) {
			logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
			return i;
		}

		// is it in the big list of sites?
		if ( *p == 't' && strncmp(p,"tag:",4) == 0 ) {
			// skip for msg20
			//if ( isForMsg20 ) continue;
			// if only seeds in the sitelist and no

			// if there is no domain or url explicitly listed
			// then assume user is spidering the whole internet
			// and we basically ignore "insitelist"
			if ( sc->m_siteListIsEmpty && sc->m_siteListIsEmptyValid ) {
				row = NULL;// no row
			} else if ( ! checkedRow ) {
				// only do once for speed
				checkedRow = true;
				// this function is in PageBasic.cpp
				// . it also has to match "tag" at (p+4)
				row = getMatchingUrlPattern ( sc, sreq ,p+4);
			}
			// if we are not submitted from the add url api, skip
			if ( (bool)row == val ) continue;
			// skip tag:
			p += 4;
			// skip to next constraint
			p = strstr(p, "&&");
			// all done?
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			p += 2;
			goto checkNextRule;
		}
		



		// set the sign
		char *s = p;
		// skip s to after
		while ( *s && is_alpha_a(*s) ) s++;

		// skip white space before the operator
		//char *saved = s;
		while ( *s && is_wspace_a(*s) ) s++;

		char sign = 0;
		if ( *s == '=' ) {
			s++;
			if ( *s == '=' ) s++;
			sign = SIGN_EQ;
		}
		else if ( *s == '!' && s[1] == '=' ) {
			s += 2;
			sign = SIGN_NE;
		}
		else if ( *s == '<' ) {
			s++;
			if ( *s == '=' ) { sign = SIGN_LE; s++; }
			else               sign = SIGN_LT; 
		} 
		else if ( *s == '>' ) {
			s++;
			if ( *s == '=' ) { sign = SIGN_GE; s++; }
			else               sign = SIGN_GT; 
		} 

		// skip whitespace after the operator
		while ( *s && is_wspace_a(*s) ) s++;


		// seed counts. how many seeds this subdomain has. 'siteadds'
		if ( *p == 's' &&
		     p[1] == 'i' &&
		     p[2] == 't' &&
		     p[3] == 'e' &&
		     p[4] == 'a' &&
		     p[5] == 'd' &&
		     p[6] == 'd' &&
		     p[7] == 's' ) {
			// need a quota table for this
			if ( ! quotaTable ) continue;
			// a special hack so it is seeds so we can use same tbl
			int32_t h32 = sreq->m_siteHash32 ^ 0x123456;
			int32_t *valPtr =(int32_t *)quotaTable->getValue(&h32);
			int32_t a;
			// if no count in table, that is strange, i guess
			// skip for now???
			// this happens if INJECTING a url from the
			// "add url" function on homepage
			if ( ! valPtr ) a=0;//continue;//{char *xx=NULL;*xx=0;}
			// shortcut
			else a = *valPtr;
			//log("siteadds=%" PRId32" for %s",a,sreq->m_url);
			// what is the provided value in the url filter rule?
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// domain seeds. 'domainadds'
		if ( *p == 'd' &&
		     p[1] == 'o' &&
		     p[2] == 'm' &&
		     p[3] == 'a' &&
		     p[4] == 'i' &&
		     p[5] == 'n' &&
		     p[6] == 'a' &&
		     p[7] == 'd' &&
		     p[8] == 'd' &&
		     p[9] == 's' ) {
			// need a quota table for this
			if ( ! quotaTable ) continue;
			// a special hack so it is seeds so we can use same tbl
			int32_t h32 = sreq->m_domHash32 ^ 0x123456;
			int32_t *valPtr ;
			valPtr = (int32_t *)quotaTable->getValue(&h32);
			// if no count in table, that is strange, i guess
			// skip for now???
			int32_t a;
			if ( ! valPtr ) a = 0;//{ char *xx=NULL;*xx=0; }
			else a = *valPtr;
			// what is the provided value in the url filter rule?
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}



		// new quotas. 'sitepages' = pages from site.
		// 'sitepages > 20 && seedcount <= 1 --> FILTERED'
		if ( *p == 's' &&
		     p[1] == 'i' &&
		     p[2] == 't' &&
		     p[3] == 'e' &&
		     p[4] == 'p' &&
		     p[5] == 'a' &&
		     p[6] == 'g' &&
		     p[7] == 'e' &&
		     p[8] == 's' ) {
			// need a quota table for this
			if ( ! quotaTable ) continue;
			int32_t *valPtr ;
		       valPtr=(int32_t*)quotaTable->getValue(&sreq->m_siteHash32);
			// if no count in table, that is strange, i guess
			// skip for now???
			int32_t a;
			if ( ! valPtr ) a = 0;//{ char *xx=NULL;*xx=0; }
			else a = *valPtr;
			// shortcut
			//log("sitepgs=%" PRId32" for %s",a,sreq->m_url);
			// what is the provided value in the url filter rule?
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// domain quotas. 'domainpages > 10 && hopcount >= 1 --> FILTERED'
		if ( *p == 'd' &&
		     p[1] == 'o' &&
		     p[2] == 'm' &&
		     p[3] == 'a' &&
		     p[4] == 'i' &&
		     p[5] == 'n' &&
		     p[6] == 'p' &&
		     p[7] == 'a' &&
		     p[8] == 'g' &&
		     p[9] == 'e' &&
		     p[10] == 's' ) {
			// need a quota table for this. this only happens
			// when trying to shortcut things to avoid adding
			// urls to spiderdb... like XmlDoc.cpp calls
			// getUrlFtilerNum() to see if doc is banned or
			// if it should harvest links.
			if ( ! quotaTable ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}

			int32_t *valPtr;
			valPtr=(int32_t*)quotaTable->getValue(&sreq->m_domHash32);
			// if no count in table, that is strange, i guess
			// skip for now???
			int32_t a;
			if ( ! valPtr ) a = 0;//{ char *xx=NULL;*xx=0; }
			else a = *valPtr;
			// what is the provided value in the url filter rule?
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// tld:cn 
		if ( *p=='t' && strncmp(p,"tld",3)==0){
			// set it on demand
			if ( tld == (char *)-1 )
				tld = getTLDFast ( sreq->m_url , &tldLen );
			// no match if we have no tld. might be an IP only url,
			// or not in our list in Domains.cpp::isTLD()
			if ( ! tld || tldLen == 0 ) continue;
			// set these up
			//char *a    = tld;
			//int32_t  alen = tldLen;
			char *b    = s;
			// loop for the comma-separated list of tlds
			// like tld:us,uk,fr,it,de
		subloop1:
			// get length of it in the regular expression box
			char *start = b;
			while ( *b && !is_wspace_a(*b) && *b!=',' ) b++;
			int32_t  blen = b - start;
			//char sm;
			// if we had tld==com,org,...
			if ( sign == SIGN_EQ &&
			     blen == tldLen && 
			     strncasecmp(start,tld,tldLen)==0 ) 
				// if we matched any, that's great
				goto matched1;
			// if its tld!=com,org,...
			// and we equal the string, then we do not matcht his
			// particular rule!!!
			if ( sign == SIGN_NE &&
			     blen == tldLen && 
			     strncasecmp(start,tld,tldLen)==0 ) 
				// we do not match this rule if we matched
				// and of the tlds in the != list
				continue;
			// might have another tld in a comma-separated list
			if ( *b != ',' ) {
				// if that was the end of the list and the
				// sign was == then skip this rule
				if ( sign == SIGN_EQ ) continue;
				// otherwise, if the sign was != then we win!
				if ( sign == SIGN_NE ) goto matched1;
				// otherwise, bad sign?
				continue;
			}
			// advance to next tld if there was a comma after us
			b++;
			// and try again
			goto subloop1;
			// otherwise
			// do we match, if not, try next regex
			//sm = strncasecmp(a,b,blen);
			//if ( sm != 0 && sign == SIGN_EQ ) goto miss1;
			//if ( sm == 0 && sign == SIGN_NE ) goto miss1;
			// come here on a match
		matched1:
			// we matched, now look for &&
			p = strstr ( b , "&&" );
			// if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
			// come here if we did not match the tld
		}


		// lang:en,zh_cn
		if ( *p=='l' && strncmp(p,"lang",4)==0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}
			
			// must have a reply
			if ( langId == -1 ) continue;
			// skip if unknown? no, we support "xx" as unknown now
			//if ( srep->m_langId == 0 ) continue;
			// set these up
			char *b = s;
			// loop for the comma-separated list of langids
			// like lang==en,es,...
		subloop2:
			// get length of it in the regular expression box
			char *start = b;
			while ( *b && !is_wspace_a(*b) && *b!=',' ) b++;
			int32_t  blen = b - start;
			//char sm;
			// if we had lang==en,es,...
			if ( sign == SIGN_EQ &&
			     blen == langLen && 
			     strncasecmp(start,lang,langLen)==0 ) 
				// if we matched any, that's great
				goto matched2;
			// if its lang!=en,es,...
			// and we equal the string, then we do not matcht his
			// particular rule!!!
			if ( sign == SIGN_NE &&
			     blen == langLen && 
			     strncasecmp(start,lang,langLen)==0 ) 
				// we do not match this rule if we matched
				// and of the langs in the != list
				continue;
			// might have another in the comma-separated list
			if ( *b != ',' ) {
				// if that was the end of the list and the
				// sign was == then skip this rule
				if ( sign == SIGN_EQ ) continue;
				// otherwise, if the sign was != then we win!
				if ( sign == SIGN_NE ) goto matched2;
				// otherwise, bad sign?
				continue;
			}
			// advance to next list item if was a comma after us
			b++;
			// and try again
			goto subloop2;
			// come here on a match
		matched2:
			// we matched, now look for &&
			p = strstr ( b , "&&" );
			// if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
			// come here if we did not match the tld
		}


		// parentlang=en,zh_cn
		if ( *p=='p' && strncmp(p,"parentlang",10)==0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}
			// must have a reply
			//if ( ! srep ) continue;
			// skip if unknown? no, we support "xx" as unknown now
			//if ( srep->m_langId == 0 ) continue;
			// set these up
			char *b = s;
			// loop for the comma-separated list of langids
			// like parentlang==en,es,...
		subloop2b:
			// get length of it in the expression box
			char *start = b;
			while ( *b && !is_wspace_a(*b) && *b!=',' ) b++;
			int32_t  blen = b - start;
			//char sm;
			// if we had parentlang==en,es,...
			if ( sign == SIGN_EQ &&
			     blen == plangLen && 
			     strncasecmp(start,plang,plangLen)==0 ) 
				// if we matched any, that's great
				goto matched2b;
			// if its parentlang!=en,es,...
			// and we equal the string, then we do not matcht his
			// particular rule!!!
			if ( sign == SIGN_NE &&
			     blen == plangLen && 
			     strncasecmp(start,plang,plangLen)==0 ) 
				// we do not match this rule if we matched
				// and of the langs in the != list
				continue;
			// might have another in the comma-separated list
			if ( *b != ',' ) {
				// if that was the end of the list and the
				// sign was == then skip this rule
				if ( sign == SIGN_EQ ) continue;
				// otherwise, if the sign was != then we win!
				if ( sign == SIGN_NE ) goto matched2b;
				// otherwise, bad sign?
				continue;
			}
			// advance to next list item if was a comma after us
			b++;
			// and try again
			goto subloop2b;
			// come here on a match
		matched2b:
			// we matched, now look for &&
			p = strstr ( b , "&&" );
			// if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			// skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
			// come here if we did not match the tld
		}


		// hopcount == 20 [&&]
		if ( *p=='h' && strncmp(p, "hopcount", 8) == 0){
			// skip if not valid
			if ( ! sreq->m_hopCountValid ) continue;
			// shortcut
			int32_t a = sreq->m_hopCount;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// the last time it was spidered
		if ( *p=='l' && strncmp(p,"lastspidertime",14) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}
			// skip for msg20
			if ( isForMsg20 ) continue;
			// reply based
			int32_t a = 0;
			// if no spider reply we can't match this rule!
			if ( ! srep ) continue;
			// shortcut
			if ( srep ) a = srep->m_spideredTime;
			// make it point to the retry count
			int32_t b ;
			// now "s" can be "{roundstart}"
			if ( s[0]=='{' && strncmp(s,"{roundstart}",12)==0)
				b = cr->m_spiderRoundStartTime;//Num;
			else
				b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// selector using the first time it was added to the Spiderdb
		// added by Sam, May 5th 2015
		if ( *p=='u' && strncmp(p,"urlage",6) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) {
				//log("was for message 20");
				continue;

			}
			// get the age of the spider_request. 
			// (substraction of uint with int, hope
			// every thing goes well there)
			int32_t sreq_age = 0;

			// if m_discoveryTime is available, we use it. Otherwise we use m_addedTime
			if ( sreq && sreq->m_discoveryTime!=0) sreq_age = nowGlobal-sreq->m_discoveryTime;
			if ( sreq && sreq->m_discoveryTime==0) sreq_age = nowGlobal-sreq->m_addedTime;
			//log("spiderage=%d",sreq_age);
			// the argument entered by user
			int32_t argument_age=atoi(s) ;
			if ( sign == SIGN_EQ && sreq_age != argument_age ) continue;
			if ( sign == SIGN_NE && sreq_age == argument_age ) continue;
			if ( sign == SIGN_GT && sreq_age <= argument_age ) continue;
			if ( sign == SIGN_LT && sreq_age >= argument_age ) continue;
			if ( sign == SIGN_GE && sreq_age <  argument_age ) continue;
			if ( sign == SIGN_LE && sreq_age >  argument_age ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}


		if ( *p=='e' && strncmp(p,"errorcount",10) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}
			// skip for msg20
			if ( isForMsg20 ) continue;
			// reply based
			if ( ! srep ) continue;
			// shortcut
			int32_t a = srep->m_errCount;
			// make it point to the retry count
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			// skip fast
			p += 10;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// EBADURL malformed url is ... 32880
		if ( *p=='e' && strncmp(p,"errorcode",9) == 0 ) {
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}
			// skip for msg20
			if ( isForMsg20 ) continue;
			// reply based
			if ( ! srep ) continue;
			// shortcut
			int32_t a = srep->m_errCode;
			// make it point to the retry count
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			// skip fast
			p += 9;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		if ( *p == 'n' && strncmp(p,"numinlinks",10) == 0 ) {
			// skip for msg20
			if ( isForMsg20 ) continue;
			// these are -1 if they are NOT valid
			int32_t a = sreq->m_pageNumInlinks;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			// skip fast
			p += 10;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// siteNumInlinks >= 300 [&&]
		if ( *p=='s' && strncmp(p, "sitenuminlinks", 14) == 0){
			// these are -1 if they are NOT valid
			int32_t a1 = sreq->m_siteNumInlinks;
			// only assign if valid
			int32_t a2 = -1; 
			if ( srep ) a2 = srep->m_siteNumInlinks;
			// assume a1 is the best
			int32_t a ;
			// assign to the first valid one
			if      ( a1 != -1 ) a = a1;
			else if ( a2 != -1 ) a = a2;
			// swap if both are valid, but srep is more recent
			if ( a1 != -1 && a2 != -1 &&
			     srep->m_spideredTime > sreq->m_addedTime )
				a = a2;
			// skip if nothing valid
			if ( a == -1 ) continue;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			// skip fast
			p += 14;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// how many days have passed since it was last attempted
		// to be spidered? used in conjunction with percentchanged
		// to assign when to re-spider it next
		if ( *p=='s' && strncmp(p, "spiderwaited", 12) == 0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1");
				return -1;
			}

			// must have a reply
			if ( ! srep ) continue;

			// skip for msg20
			if ( isForMsg20 ) continue;

			// shortcut
			int32_t a = nowGlobal - srep->m_spideredTime;

			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// percentchanged >= 50 [&&] ...
		if ( *p=='p' && strncmp(p, "percentchangedperday", 20) == 0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}
			// must have a reply
			if ( ! srep ) continue;
			// skip for msg20
			if ( isForMsg20 ) continue;
			// shortcut
			float a = srep->m_percentChangedPerDay;
			// make it point to the priority
			float b = atof(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// httpStatus == 400
		if ( *p=='h' && strncmp(p, "httpstatus", 10) == 0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}
			// must have a reply
			if ( ! srep ) continue;
			// shortcut (errCode doubles as g_errno)
			int32_t a = srep->m_errCode;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && a != b ) continue;
			if ( sign == SIGN_NE && a == b ) continue;
			if ( sign == SIGN_GT && a <= b ) continue;
			if ( sign == SIGN_LT && a >= b ) continue;
			if ( sign == SIGN_GE && a <  b ) continue;
			if ( sign == SIGN_LE && a >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// how old is the doc in seconds? age is the pubDate age
		if ( *p =='a' && strncmp(p, "age", 3) == 0){
			// if we do not have enough info for outlink, all done
			if ( isOutlink ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning -1" );
				return -1;
			}
			// must have a reply
			if ( ! srep ) continue;
			// shortcut
			int32_t age;
			if ( srep->m_pubDate <= 0 ) age = -1;
			else age = nowGlobal - srep->m_pubDate;
			// we can not match if invalid
			if ( age <= 0 ) continue;
			// make it point to the priority
			int32_t b = atoi(s);
			// compare
			if ( sign == SIGN_EQ && age != b ) continue;
			if ( sign == SIGN_NE && age == b ) continue;
			if ( sign == SIGN_GT && age <= b ) continue;
			if ( sign == SIGN_LT && age >= b ) continue;
			if ( sign == SIGN_GE && age <  b ) continue;
			if ( sign == SIGN_LE && age >  b ) continue;
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) 
			{
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

		// our own regex thing (match front of url)
		if ( *p=='^' ) {
			// advance over caret
			p++;
			// now pstart pts to the string we will match
			char *pstart = p;
			// make "p" point to one past the last char in string
			while ( *p && ! is_wspace_a(*p) ) p++;
			// how long is the string to match?
			int32_t plen = p - pstart;
			// empty? that's kinda an error
			if ( plen == 0 ) 
				continue;
			int32_t m = 1;
			// check to see if we matched if url was int32_t enough
			if ( urlLen >= plen )
				m = strncmp(pstart,url,plen);
			if ( ( m == 0 && val == 0 ) ||
			     // if they used the '!' operator and we
			     // did not match the string, that's a 
			     // row match
			     ( m && val == 1 ) ) {
				// another expression follows?
				p = strstr(s, "&&");
				//if nothing, else then it is a match
				if ( ! p ) {
					logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
					return i;
				}
				//skip the '&&' and go to next rule
				p += 2;
				goto checkNextRule;
			}
			// no match
			continue;
		}

		// our own regex thing (match end of url)
		if ( *p=='$' ) {
			// advance over dollar sign
			p++;
			// a hack for $\.css, skip over the backslash too
			if ( *p=='\\' && *(p+1)=='.' ) p++;
			// now pstart pts to the string we will match
			char *pstart = p;
			// make "p" point to one past the last char in string
			while ( *p && ! is_wspace_a(*p) ) p++;
			// how long is the string to match?
			int32_t plen = p - pstart;
			// empty? that's kinda an error
			if ( plen == 0 ) 
				continue;
			// . do we match it?
			// . url has to be at least as big
			// . match our tail
			int32_t m = 1;
			// check to see if we matched if url was int32_t enough
			if ( urlLen >= plen )
				m = strncmp(pstart,url+urlLen-plen,plen);
			if ( ( m == 0 && val == 0 ) ||
			     // if they used the '!' operator and we
			     // did not match the string, that's a 
			     // row match
			     ( m && val == 1 ) ) {
				// another expression follows?
				p = strstr(s, "&&");
				//if nothing, else then it is a match
				if ( ! p ) {
					logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
					return i;
				}

				//skip the '&&' and go to next rule
				p += 2;
				goto checkNextRule;
			}
			// no match
			continue;
		}

		// . by default a substring match
		// . action=edit
		// . action=history

		// now pstart pts to the string we will match
		char *pstart = p;
		// make "p" point to one past the last char in string
		while ( *p && ! is_wspace_a(*p) ) p++;
		// how long is the string to match?
		int32_t plen = p - pstart;
		// need something...
		if ( plen <= 0 ) continue;
		// must be at least as big
		//if ( urlLen < plen ) continue;
		// nullilfy it temporarily
		char c = *p;
		*p     = '\0';
		// does url contain it? haystack=u needle=p
		char *found = strstr ( url , pstart );
		// put char back
		*p     = c;

		// kinda of a hack fix. if they inject a filtered url
		// into test coll, do not filter it! fixes the fact that
		// we filtered facebook, but still add it in our test
		// collection injection in urls.txt
		if ( found && 
		     sreq->m_isInjecting &&
		     cr->m_coll[0]=='t' &&
		     cr->m_coll[1]=='e' &&
		     cr->m_coll[2]=='s' &&
		     cr->m_coll[3]=='t' &&
		     cr->m_coll[4]=='\0' &&
		     cr->m_spiderPriorities[i] < 0 )
			continue;

		// support "!company" meaning if it does NOT match
		// then do this ...
		if ( ( found && val == 0 ) ||
		     // if they used the '!' operator and we
		     // did not match the string, that's a 
		     // row match
		     ( ! found && val == 1 ) ) {
			// another expression follows?
			p = strstr(s, "&&");
			//if nothing, else then it is a match
			if ( ! p ) {
				logTrace( g_conf.m_logTraceSpider, "END, returning i (%" PRId32")", i );
				return i;
			}
			//skip the '&&' and go to next rule
			p += 2;
			goto checkNextRule;
		}

	}

	// return -1 if no match, caller should use a default
	logTrace( g_conf.m_logTraceSpider, "END, returning -1" );

	return -1;
}

// . dedup for spiderdb
// . TODO: we can still have spider request dups in this if they are
//   sandwiched together just right because we only compare to the previous
//   SpiderRequest we added when looking for dups. just need to hash the
//   relevant input bits and use that for deduping.
// . TODO: we can store ufn/priority/spiderTime in the SpiderRequest along
//   with the date now, so if url filters do not change then 
//   gotSpiderdbList() can assume those to be valid and save time. BUT it does
//   have siteNumInlinks...
void dedupSpiderdbList ( RdbList *list, bool removeNegRecs ) {
	char *newList = list->m_list;

	char *dst          = newList;
	char *restorePoint = newList;
	int64_t reqUh48  = 0LL;
	int64_t repUh48  = 0LL;
	SpiderReply   *oldRep = NULL;
	//SpiderRequest *oldReq = NULL;
	char *lastKey     = NULL;

	int32_t oldSize = list->m_listSize;
	int32_t corrupt = 0;
	// debug
	// static int32_t s_count = 0;
	// s_count++;
	// if ( s_count == 2524 )
	// 	log("gotit");

	int32_t numToFilter = 0;

	class Link {
	public:
		uint32_t m_srh;
		SpiderRequest *m_sreq;
		class Link *m_prev;
		class Link *m_next;
	};
#define MAXLINKS 30
	Link *headLink = NULL;
	Link *tailLink = NULL;
	Link  links[MAXLINKS];
	int32_t numLinks = 0;

	// reset it
	list->resetListPtr();

	for ( ; ! list->isExhausted() ; ) {
		// get rec
		char *rec = list->getCurrentRec();

		// pre skip it
		list->skipCurrentRec();

		// skip if negative, just copy over
		if ( ( rec[0] & 0x01 ) == 0x00 ) {
			// should not be in here if this was true...
			if ( removeNegRecs ) {
				log("spider: filter got negative key");
				char *xx=NULL;*xx=0;
			}
			// otherwise, keep it
			lastKey = dst;
			memmove ( dst , rec , sizeof(key128_t) );
			dst += sizeof(key128_t);
			continue;
		}

		// is it a reply?
		if ( g_spiderdb.isSpiderReply ( (key128_t *)rec ) ) {
			// cast it
			SpiderReply *srep = (SpiderReply *)rec;

			// shortcut
			int64_t uh48 = srep->getUrlHash48();

			// crazy?
			if ( ! uh48 ) { 
				//uh48 = hash64b ( srep->m_url );
				uh48 = 12345678;
				log("spider: got uh48 of zero for spider req. computing now.");
			}

			// does match last reply?
			if ( repUh48 == uh48 ) {
				// if he's a later date than us, skip us!
				if ( oldRep->m_spideredTime >= srep->m_spideredTime ) {
					// skip us!
					continue;
				}
				// otherwise, erase him
				dst     = restorePoint;
			}
			// save in case we get erased
			restorePoint = dst;
			// get our size
			int32_t recSize = srep->getRecSize();
			// and add us
			lastKey = dst;
			memmove ( dst , rec , recSize );
			// advance
			dst += recSize;
			// update this crap for comparing to next reply
			repUh48 = uh48;
			oldRep  = srep;
			// get next spiderdb record
			continue;
		}

		// shortcut
		SpiderRequest *sreq = (SpiderRequest *)rec;

		// might as well filter out corruption
		if ( sreq->isCorrupt() ) {
			corrupt += sreq->getRecSize();
			continue;
		}

		// shortcut
		int64_t uh48 = sreq->getUrlHash48();

		// crazy?
		if ( ! uh48 ) {
			//uh48 = hash64b ( sreq->m_url );
			uh48 = 12345678;
			log("spider: got uh48 of zero for spider req. computing now.");
		}

		// update request with SpiderReply if newer, because ultimately
		// ::getUrlFilterNum() will just look at SpiderRequest's 
		// version of these bits!
		if ( oldRep && repUh48 == uh48 &&
		     oldRep->m_spideredTime > sreq->m_addedTime ) {

			// if request was a page reindex docid based request 
			// and url has since been spidered, nuke it!
			//if ( sreq->m_urlIsDocId ) continue;
			if ( sreq->m_isPageReindex ) continue;

			// same if indexcode was EFAKEFIRSTIP which XmlDoc.cpp
			// re-adds to spiderdb with the right firstip. once
			// those guys have a reply we can ignore them.
			// TODO: what about diffbotxyz spider requests? those
			// have a fakefirstip... they should not have requests
			// though, since their parent url has that.
			if ( sreq->m_fakeFirstIp ) continue;

			SpiderReply *old = oldRep;
			sreq->m_hasAuthorityInlink = old->m_hasAuthorityInlink;
		}

		// if we are not the same url as last request, then
		// we will not need to dedup, but should add ourselves to
		// the linked list, which we also reset here.
		if ( uh48 != reqUh48 ) {
			numLinks = 0;
			headLink = NULL;
			tailLink = NULL;
			// we are the new banner carrier
			reqUh48 = uh48;
		}

		// try to kinda grab the min hop count as well
		// do not alter spiderdb!
		// if ( sreq->m_hopCountValid && oldReq->m_hopCountValid ) {
		// 	if ( oldReq->m_hopCount < sreq->m_hopCount )
		// 		sreq->m_hopCount = oldReq->m_hopCount;
		// 	else
		// 		oldReq->m_hopCount = sreq->m_hopCount;
		// }

		// why does sitehash32 matter really?
		uint32_t srh = sreq->m_siteHash32;
		if ( sreq->m_isNewOutlink  ) srh ^= 0xb714d3a3;
		if ( sreq->m_isInjecting   ) srh ^= 0x42538909;
		//if ( sreq->m_hasContent    ) srh ^= 0xbbbefd59;
		if ( sreq->m_isAddUrl      ) srh ^= 0x587c5a0b;
		if ( sreq->m_isPageReindex ) srh ^= 0x70fb3911;
		if ( sreq->m_forceDelete   ) srh ^= 0x4e6e9aee;

		if ( sreq->m_parentIsSiteMap    ) srh ^= 0xe0c20e3f;
		if ( sreq->m_urlIsDocId         ) srh ^= 0xee015b07;
		if ( sreq->m_fakeFirstIp        ) srh ^= 0x95b8d376;
		if ( sreq->m_parentIsRSS        ) srh ^= 0xb08c7545;
		if ( sreq->m_parentIsPermalink  ) srh ^= 0xbd688268;
		if ( sreq->m_parentIsPingServer ) srh ^= 0xb4c8a811;
		if ( sreq->m_isMenuOutlink      ) srh ^= 0xd97bb80b;

		// we may assign url filter priority based on parent langid
		srh ^= (uint32_t)g_hashtab[0][(uint8_t)sreq->m_parentLangId];

		// if he's essentially different input parms but for the
		// same url, we want to keep him because he might map the
		// url to a different url priority!
		bool skipUs = false;
		Link *myLink = NULL;
		Link *link = headLink;

		// debug point. should be deduped by
		// if ( sreq->m_key.n0==7199823231990374913LL &&
		//      sreq->m_key.n1==6511615362168588088 )
		// 	log("hey1");
		// if ( sreq->m_key.n0==7199823542662487041LL &&
		//      sreq->m_key.n1==6511615362168588088 )
		// 	log("hey2");

		// now we keep a list of the last ten
		for ( ; link ; link = link->m_next ) {
			if ( srh != link->m_srh ) continue;
			SpiderRequest *prevReq = link->m_sreq;
			// if we are better, replace him and stop
			if ( sreq->m_hopCount < prevReq->m_hopCount ) 
				goto replacePrevReq;
			// skip us if previous guy is better
			if ( sreq->m_hopCount > prevReq->m_hopCount ) {
				skipUs = true;
				break;
			}

			// TODO: for pro, base on parentSiteNumInlinks here,
			// and hash hopcounts, but only 0,1,2,3. use 3
			// for all that are >=3. we can also have two hashes,
			// m_srh and m_srh2 in the Link class, and if your
			// new secondary hash is unique we can let you in
			// if your parentpageinlinks is the highest of all.

			// resort to added time if hopcount is tied
			// . if the same check who has the most recentaddedtime
			// . if we are not the most recent, just do not add us
			// . no, now i want the oldest so we can do 
			//   gbssDiscoveryTime and set sreq->m_discoveryTime 
			//   accurately, above
			if ( sreq->m_addedTime >= prevReq->m_addedTime ) {
				skipUs = true;
				break;
			}
			// otherwise, replace him
		replacePrevReq:
			// it could be a docid indicating a query reindex,
			// in which case it won't start with 'h'
			// and we should always just add it and not bother
			// with deduping these. well, no, let's dedup
			// as normal. they should have the urlIsDocId bit
			// set which contributes to their hash.
			// if ( prevReq->m_url[0] != 'h' )
			// 	goto justAddIt;
			prevReq->m_url[0] = 'x'; // mark for removal. xttp://
			myLink = link;
			// make a note of this so we physically remove these
			// entries after we are done with this scan.
			numToFilter++;
			goto promoteLinkToHead;
		}
		// if we were not as good as someone that was basically the
		// same SpiderRequest before us, keep going
		if ( skipUs )
			continue;

		// add to linked list
		if ( numLinks < MAXLINKS ) {
			myLink = &links[numLinks++];
			myLink->m_prev = NULL;
			myLink->m_next = NULL;
			// if first one, we are head and tail
			if ( numLinks == 1 ) {
				headLink = myLink;
				tailLink = myLink;
			}
		}
		// if full, just supplant the tail link
		else 
			myLink = tailLink;

	promoteLinkToHead:

		myLink->m_srh  = srh;
		myLink->m_sreq = (SpiderRequest *)dst;//sreq;

		// move link to head if not already
		if ( myLink != headLink ) {
			// if we are the tail, there will be a new tail
			if ( myLink == tailLink ) tailLink = myLink->m_prev;
			// make previous link point over us
			if ( myLink->m_prev )
				myLink->m_prev->m_next = myLink->m_next;
			// make next link ptr point backward over us
			if ( myLink->m_next )
				myLink->m_next->m_prev = myLink->m_prev;
			// make current head point backward to us
			headLink->m_prev = myLink;
			// and we point forward to him
			myLink->m_next = headLink;
			// and backward to nobody
			myLink->m_prev = NULL;
			// and we are the head now
			headLink = myLink;
		}

		//	justAddIt:

		// get our size
		int32_t recSize = sreq->getRecSize();

		// and add us
		lastKey = dst;
		memmove ( dst , rec , recSize );
		// advance
		dst += recSize;

		// get next spiderdb record
		continue;

		/*
		if ( oldReq->m_siteHash32    != sreq->m_siteHash32    ||
		     oldReq->m_isNewOutlink  != sreq->m_isNewOutlink  ||
		     //  use hopcount now too!
		     oldReq->m_hopCount      != sreq->m_hopCount      ||
		     // we prefer the most recent spider request
		     // from thsi site in the logic above, so this is not
		     // necessary. mdw commented out.
		     //oldReq->m_wasParentIndexed != sreq->m_wasParentIndexed||
		     oldReq->m_isInjecting   != sreq->m_isInjecting   ||
		     oldReq->m_isAddUrl      != sreq->m_isAddUrl      ||
		     oldReq->m_isPageReindex != sreq->m_isPageReindex ||
		     oldReq->m_forceDelete   != sreq->m_forceDelete    )
			// we are different enough to coexist
			goto addIt;
		// . if the same check who has the most recent added time
		// . if we are not the most recent, just do not add us
		// . no, now i want the oldest so we can do gbssDiscoveryTime
		//   and set sreq->m_discoveryTime accurately, above
		if ( sreq->m_addedTime >= oldReq->m_addedTime ) continue;

		// otherwise, erase over him
		dst     = restorePoint;
		lastKey = prevLastKey;
		// and add us over top of him
		goto addIt;
		*/

	}

	// sanity check
	if ( dst < list->m_list || dst > list->m_list + list->m_listSize ) {
		char *xx=NULL;*xx=0;
	}


	/////////
	//
	// now remove xttp:// urls if we had some
	//
	/////////
	if ( numToFilter > 0 ) {
		// update list so for-loop below works
		list->m_listSize  = dst - newList;
		list->m_listPtr   = newList;//dst;
		list->m_listEnd   = list->m_list + list->m_listSize;
		list->m_listPtrHi = NULL;
		// and we'll re-write everything back into itself at "dst"
		dst = newList;
	}
	for ( ; ! list->isExhausted() ; ) {
		// breathe. NO! assume in thread!!
		//QUICKPOLL(niceness);
		// get rec
		char *rec = list->getCurrentRec();
		// pre skip it
		list->skipCurrentRec();
		// skip if negative, just copy over
		if ( ( rec[0] & 0x01 ) == 0x00 ) {
			lastKey = dst;
			memmove ( dst , rec , sizeof(key128_t) );
			dst += sizeof(key128_t);
			continue;
		}
		// is it a reply?
		if ( g_spiderdb.isSpiderReply ( (key128_t *)rec ) ) {
			SpiderReply *srep = (SpiderReply *)rec;
			int32_t recSize = srep->getRecSize();
			lastKey = dst;
			memmove ( dst , rec , recSize );
			dst += recSize;
			continue;
		}
		SpiderRequest *sreq = (SpiderRequest *)rec;
		// skip if filtered out
		if ( sreq->m_url[0] == 'x' ) 
			continue;
		int32_t recSize = sreq->getRecSize();
		lastKey = dst;
		memmove ( dst , rec , recSize );
		dst += recSize;
		// if ( sreq->getUrlHash48() == 49553538838LL )
		// 	log("napkins");
	}


	// and stick our newly filtered list in there
	list->m_listSize  = dst - newList;
	// set to end i guess
	list->m_listPtr   = dst;
	list->m_listEnd   = list->m_list + list->m_listSize;
	list->m_listPtrHi = NULL;

	// log("spiderdb: remove ME!!!");
	// // check it
	// list->checkList_r(false,false,RDB_SPIDERDB);
	// list->resetListPtr();

	int32_t delta = oldSize - list->m_listSize;
	log("spider: deduped %i bytes (of which %i were corrupted) "
	    "out of %i",(int)delta,(int)corrupt,(int)oldSize);

	if ( lastKey ) {
		KEYSET( list->m_lastKey, lastKey, list->m_ks );
	}
}




bool getSpiderStatusMsg ( CollectionRec *cx , SafeBuf *msg , int32_t *status ) {

	if ( ! g_conf.m_spideringEnabled && ! cx->m_isCustomCrawl ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("Spidering disabled in "
				       "master controls. You can turn it "
				       "back on there.");
	}

	if ( g_conf.m_readOnlyMode ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("In read-only mode. Spidering off.");
	}

	if ( g_dailyMerge.m_mergeMode ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("Daily merge engaged, spidering "
				       "paused.");
	}

	// if ( g_udpServer.getNumUsedSlotsIncoming() >= MAXUDPSLOTS ) {
	// 	*status = SP_ADMIN_PAUSED;
	// 	return msg->safePrintf("Too many UDP slots in use, "
	// 			       "spidering paused.");
	// }

	if ( g_repairMode ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("In repair mode, spidering paused.");
	}

	// do not spider until collections/parms in sync with host #0
	if ( ! g_parms.m_inSyncWithHost0 ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("Parms not in sync with host #0, "
				       "spidering paused");
	}

	// don't spider if not all hosts are up, or they do not all
	// have the same hosts.conf.
	if ( g_pingServer.m_hostsConfInDisagreement ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("Hosts.conf discrepancy, "
				       "spidering paused.");
	}

	uint32_t now = (uint32_t)getTimeGlobal();

	// try to fix crawlbot nightly test complaining about job status
	// for TestRepeatCrawlWithMaxToCrawl
	if ( (cx->m_spiderStatus == SP_MAXTOCRAWL ||
	      cx->m_spiderStatus == SP_MAXTOPROCESS ) &&
	     cx->m_collectiveRespiderFrequency > 0.0 &&
	     now < cx->m_spiderRoundStartTime &&
	     cx->m_spiderRoundNum >= cx->m_maxCrawlRounds ) {
		*status = SP_MAXROUNDS;
		return msg->safePrintf ( "Job has reached maxRounds "
					 "limit." );
	}		

	// . 0 means not to RE-crawl
	// . indicate if we are WAITING for next round...
	if ( cx->m_spiderStatus == SP_MAXTOCRAWL &&
	     cx->m_collectiveRespiderFrequency > 0.0 &&
	     now < cx->m_spiderRoundStartTime ) {
		*status = SP_ROUNDDONE;
		return msg->safePrintf("Jobs has reached maxToCrawl limit. "
				       "Next crawl round to start "
				       "in %" PRId32" seconds.",
				       (int32_t)(cx->m_spiderRoundStartTime-
						 now));
	}

	if ( cx->m_spiderStatus == SP_MAXTOPROCESS &&
	     cx->m_collectiveRespiderFrequency > 0.0 &&
	     now < cx->m_spiderRoundStartTime ) {
		*status = SP_ROUNDDONE;
		return msg->safePrintf("Jobs has reached maxToProcess limit. "
				       "Next crawl round to start "
				       "in %" PRId32" seconds.",
				       (int32_t)(cx->m_spiderRoundStartTime-
						 now));
	}


	if ( cx->m_spiderStatus == SP_MAXTOCRAWL ) {
		*status = SP_MAXTOCRAWL;
		return msg->safePrintf ( "Job has reached maxToCrawl "
					 "limit." );
	}

	if ( cx->m_spiderStatus == SP_MAXTOPROCESS ) {
		*status = SP_MAXTOPROCESS;
		return msg->safePrintf ( "Job has reached maxToProcess "
					 "limit." );
	}

	if ( cx->m_spiderStatus == SP_MAXROUNDS ) {
		*status = SP_MAXROUNDS;
		return msg->safePrintf ( "Job has reached maxRounds "
					 "limit." );
	}

	if ( ! cx->m_spideringEnabled ) {
		*status = SP_PAUSED;
		if ( cx->m_isCustomCrawl )
			return msg->safePrintf("Job paused.");
		else
			return msg->safePrintf("Spidering disabled "
					       "in spider controls.");
	}

	// . 0 means not to RE-crawl
	// . indicate if we are WAITING for next round...
	if ( cx->m_collectiveRespiderFrequency > 0.0 &&
	     now < cx->m_spiderRoundStartTime ) {
		*status = SP_ROUNDDONE;
		return msg->safePrintf("Next crawl round to start "
				       "in %" PRId32" seconds.",
				       (int32_t)(cx->m_spiderRoundStartTime-
						 now) );
	}

	// if spiderdb is empty for this coll, then no url
	// has been added to spiderdb yet.. either seed or spot
	//CrawlInfo *cg = &cx->m_globalCrawlInfo;
	//if ( cg->m_pageDownloadAttempts == 0 ) {
	//	*status = SP_NOURLS;
	//	return msg->safePrintf("Crawl is waiting for urls.");
	//}

	if ( cx->m_spiderStatus == SP_INITIALIZING ) {
		*status = SP_INITIALIZING;
		return msg->safePrintf("Job is initializing.");
	}

	// if we had seeds and none were successfully crawled, do not just
	// print that the crawl completed.
	if ( cx->m_collectiveRespiderFrequency <= 0.0 &&
	     cx->m_isCustomCrawl &&
	     ! cx->m_globalCrawlInfo.m_hasUrlsReadyToSpider &&
	     cx->m_globalCrawlInfo.m_pageDownloadAttempts > 0 &&
	     cx->m_globalCrawlInfo.m_pageDownloadSuccesses == 0 ) {
		*status = SP_SEEDSERROR;
		return msg->safePrintf("Failed to crawl any seed.");
	}

	// if we sent an email simply because no urls
	// were left and we are not recrawling!
	if ( cx->m_collectiveRespiderFrequency <= 0.0 &&
	     cx->m_isCustomCrawl &&
	     ! cx->m_globalCrawlInfo.m_hasUrlsReadyToSpider ) {
		*status = SP_COMPLETED;
		return msg->safePrintf("Job has completed and no "
			"repeat is scheduled.");
	}

	if ( cx->m_spiderStatus == SP_ROUNDDONE && ! cx->m_isCustomCrawl ) {
		*status = SP_ROUNDDONE;
		return msg->safePrintf ( "Nothing currently "
					 "available to spider. "
					 "Change your url filters, try "
					 "adding new urls, or wait for "
					 "existing urls to be respidered.");
	}

	// let's pass the qareindex() test in qa.cpp... it wasn't updating
	// the status to done. it kept saying in progress.
	if ( ! cx->m_isCustomCrawl && 
	     ! cx->m_globalCrawlInfo.m_hasUrlsReadyToSpider ) {
		//*status = SP_COMPLETED;
		*status = SP_INPROGRESS;
		return msg->safePrintf ( "Nothing currently "
					 "available to spider. "
					 "Change your url filters, try "
					 "adding new urls, or wait for "
					 "existing urls to be respidered.");
	}
		

	if ( cx->m_spiderStatus == SP_ROUNDDONE ) {
		*status = SP_ROUNDDONE;
		return msg->safePrintf ( "Job round completed.");
	}


	if ( ! g_conf.m_spideringEnabled ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("All crawling temporarily paused "
				       "by root administrator for "
				       "maintenance.");
	}

	// out CollectionRec::m_globalCrawlInfo counts do not have a dead
	// host's counts tallied into it, which could make a difference on
	// whether we have exceed a maxtocrawl limit or some such, so wait...
	if ( ! s_countsAreValid && g_hostdb.hasDeadHost() ) {
		*status = SP_ADMIN_PAUSED;
		return msg->safePrintf("All crawling temporarily paused "
				       "because a shard is down.");
	}



	// otherwise in progress?
	*status = SP_INPROGRESS;
	if ( cx->m_isCustomCrawl )
		return msg->safePrintf("Job is in progress.");
	else
		return msg->safePrintf("Spider is in progress.");
}



bool hasPositivePattern ( char *pattern ) {
	char *p = pattern;
	// scan the " || " separated substrings
	for ( ; *p ; ) {
		// get beginning of this string
		char *start = p;
		// skip white space
		while ( *start && is_wspace_a(*start) ) start++;
		// done?
		if ( ! *start ) break;
		// find end of it
		char *end = start;
		while ( *end && end[0] != '|' )
			end++;
		// advance p for next guy
		p = end;
		// should be two |'s
		if ( *p ) p++;
		if ( *p ) p++;
		// skip if negative pattern
		if ( start[0] == '!' && start[1] && start[1]!='|' )
			continue;
		// otherwise it's a positive pattern
		return true;
	}
	return false;
}



// pattern is a ||-separted list of substrings
bool doesStringContainPattern ( char *content , char *pattern ) {
				//bool checkForNegatives ) {

	char *p = pattern;

	int32_t matchedOne = 0;
	bool hadPositive = false;

	int32_t count = 0;
	// scan the " || " separated substrings
	for ( ; *p ; ) {
		// get beginning of this string
		char *start = p;
		// skip white space
		while ( *start && is_wspace_a(*start) ) start++;
		// done?
		if ( ! *start ) break;
		// find end of it
		char *end = start;
		while ( *end && end[0] != '|' )
			end++;
		// advance p for next guy
		p = end;
		// should be two |'s
		if ( *p ) p++;
		if ( *p ) p++;
		// temp null this
		char c = *end;
		*end = '\0';
		// count it as an attempt
		count++;

		bool matchFront = false;
		if ( start[0] == '^' ) { start++; matchFront = true; }

		// if pattern is NOT/NEGATIVE...
		bool negative = false;
		if ( start[0] == '!' && start[1] && start[1]!='|' ) {
			start++;
			negative = true;
		}
		else
			hadPositive = true;

		// . is this substring anywhere in the document
		// . check the rawest content before converting to utf8 i guess
		// . suuport the ^ operator
		char *foundPtr = NULL;
		if ( matchFront ) {
			// if we match the front, set to bogus 0x01
			if ( strncmp(content,start,end-start)==0 ) 
				foundPtr =(char *)0x01;
		}
		else {
			foundPtr = strstr ( content , start ) ;
		}

		// debug log statement
		//if ( foundPtr )
		//	log("build: page %s matches ppp of \"%s\"",
		//	    m_firstUrl.m_url,start);
		// revert \0
		*end = c;

		// negative mean we should NOT match it
		if ( negative ) {
			// so if its matched, that is bad
			if ( foundPtr ) return false;
			continue;
		}

		// skip if not found
		if ( ! foundPtr ) continue;
		// did we find it?
		matchedOne++;
		// if no negatives, done
		//if ( ! checkForNegatives )
		//return true;
	}
	// if we had no attempts, it is ok
	if ( count == 0 ) return true;
	// must have matched one at least
	if ( matchedOne ) return true;
	// if all negative? i.e. !category||!author
	if ( ! hadPositive ) return true;
	// if we had an unfound substring...
	return false;
}



int32_t getFakeIpForUrl1 ( char *url1 ) {
	// make the probable docid
	int64_t probDocId = g_titledb.getProbableDocId ( url1 );
	// make one up, like we do in PageReindex.cpp
	int32_t firstIp = (probDocId & 0xffffffff);
	return firstIp;
}



int32_t getFakeIpForUrl2 ( Url *url2 ) {
	// make the probable docid
	int64_t probDocId = g_titledb.getProbableDocId ( url2 );
	// make one up, like we do in PageReindex.cpp
	int32_t firstIp = (probDocId & 0xffffffff);
	return firstIp;
}



// returns false and sets g_errno on error
bool SpiderRequest::setFromAddUrl ( char *url ) {
	logTrace( g_conf.m_logTraceSpider, "BEGIN. url [%s]", url );
		
	// reset it
	reset();
	// make the probable docid
	int64_t probDocId = g_titledb.getProbableDocId ( url );

	// make one up, like we do in PageReindex.cpp
	int32_t firstIp = (probDocId & 0xffffffff);
	//int32_t firstIp = getFakeIpForUrl1 ( url );

	// ensure not crazy
	if ( firstIp == -1 || firstIp == 0 ) firstIp = 1;

	// . now fill it up
	// . TODO: calculate the other values... lazy!!! (m_isRSSExt, 
	//         m_siteNumInlinks,...)
	m_isNewOutlink = 1;
	m_isAddUrl     = 1;
	m_addedTime    = (uint32_t)getTimeGlobal();//now;
	m_fakeFirstIp   = 1;
	//m_probDocId     = probDocId;
	m_firstIp       = firstIp;
	m_hopCount      = 0;

	// new: validate it?
	m_hopCountValid = 1;

	// its valid if root
	Url uu; uu.set ( url );
	if ( uu.isRoot() ) m_hopCountValid = true;
	// too big?
	if ( gbstrlen(url) > MAX_URL_LEN ) {
		g_errno = EURLTOOLONG;
		logTrace( g_conf.m_logTraceSpider, "END, EURLTOOLONG" );
		return false;
	}
	// the url! includes \0
	strcpy ( m_url , url );
	// call this to set m_dataSize now
	setDataSize();
	// make the key dude -- after setting url
	setKey ( firstIp , 0LL, false );

	// how to set m_firstIp? i guess addurl can be throttled independently
	// of the other urls???  use the hash of the domain for it!
	int32_t  dlen;
	char *dom = getDomFast ( url , &dlen );

	// sanity
	if ( ! dom ) {
		g_errno = EBADURL;
		logTrace( g_conf.m_logTraceSpider, "END, EBADURL" );
		return false;
		//return sendReply ( st1 , true );
	}

	m_domHash32 = hash32 ( dom , dlen );
	// and "site"
	int32_t hlen = 0;
	char *host = getHostFast ( url , &hlen );
	m_siteHash32 = hash32 ( host , hlen );
	m_hostHash32 = m_siteHash32;

	logTrace( g_conf.m_logTraceSpider, "END, done" );
	return true;
}



bool SpiderRequest::setFromInject ( char *url ) {
	// just like add url
	if ( ! setFromAddUrl ( url ) ) return false;
	// but fix this
	m_isAddUrl = 0;
	m_isInjecting = 1;
	return true;
}



bool SpiderRequest::isCorrupt ( ) {

	// more corruption detection
	if ( m_hopCount < -1 ) {
		log("spider: got corrupt 5 spiderRequest");
		return true;
	}

	if ( m_dataSize > (int32_t)sizeof(SpiderRequest) ) {
		log("spider: got corrupt oversize spiderrequest %i", (int)m_dataSize);
		return true;
	}

	if ( m_dataSize <= 0 ) {
		log("spider: got corrupt undersize spiderrequest %i", (int)m_dataSize);
 		return true;
 	}

	// sanity check. check for http(s)://
	if ( m_url[0] == 'h' && m_url[1]=='t' && m_url[2]=='t' &&
	     m_url[3] == 'p' ) 
		return false;
	// to be a docid as url must have this set
	if ( ! m_isPageReindex && ! m_urlIsDocId ) {
		log("spider: got corrupt 3 spiderRequest");
		return true;
	}	// might be a docid from a pagereindex.cpp
	if ( ! is_digit(m_url[0]) ) { 
		log("spider: got corrupt 1 spiderRequest");
		return true;
	}
	// if it is a digit\0 it is ok, not corrupt
	if ( ! m_url[1] )
		return false;
	// if it is not a digit after the first digit, that is bad
	if ( ! is_digit(m_url[1]) ) { 
		log("spider: got corrupt 2 spiderRequest");
		return true;
	}
	char *p    = m_url + 2;
	char *pend = m_url + getUrlLen();
	for ( ; p < pend && *p ; p++ ) {
		// the whole url must be digits, a docid
		if ( ! is_digit(*p) ) {
			log("spider: got corrupt 13 spiderRequest");
			return true;
		}
	}

	return false;
}

