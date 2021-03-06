#include "gb-include.h"

#include "TcpServer.h"
#include "Pages.h"
#include "JobScheduler.h"
#include "SafeBuf.h"
#include "Profiler.h"


static const char *thread_type_name(thread_type_t tt) {
	switch(tt) {
		case thread_type_query_read:         return "query-read";
		case thread_type_query_constrain:    return "query-constrain";
		case thread_type_query_merge:        return "query-merge";
		case thread_type_query_intersect:    return "query-intersect";
		case thread_type_query_summary:      return "query-summary";
		case thread_type_spider_read:        return "spider-read";
		case thread_type_spider_write:       return "spider-write";
		case thread_type_spider_filter:      return "spider-filter";
		case thread_type_spider_query:       return "spider-query";
		case thread_type_replicate_write:    return "replicate-write";
		case thread_type_replicate_read:     return "replicate-read";
		case thread_type_file_merge:         return "file-merge";
		case thread_type_file_meta_data:     return "file-meta-data";
		case thread_type_statistics:         return "statistis";
		case thread_type_unspecified_io:     return "unspecified IO";
		case thread_type_unlink:             return "unlink()";
		case thread_type_twin_sync:          return "twin-sync";
		case thread_type_hdtemp:             return "hdtemp";
		case thread_type_generate_thumbnail: return "generate-thumbnail";
		default: return "?";
	}
}


bool sendPageThreads ( TcpSocket *s , HttpRequest *r ) {
	char  buf [ 64*1024 ];
	SafeBuf p(buf, 64*1024);
	g_pages.printAdminTop ( &p , s , r );
	
	
	std::vector<JobDigest> job_digests = g_jobScheduler.query_job_digests();
	int64_t now = gettimeofdayInMilliseconds();
	
	
	//print summary and count per thread type
	
	p.safePrintf("<table %s>", TABLE_STYLE);
	p.safePrintf("  <tr class=hdrow>\n");
	p.safePrintf("    <td colspan=\"1\"><b>Job type</b></td>\n");
	p.safePrintf("    <td colspan=\"6\"><b>State</b></td>\n");
	p.safePrintf("  </tr>\n");
	p.safePrintf("  <tr class=hdrow>\n");
	p.safePrintf("    <td colspan=\"1\"></td>\n");
	p.safePrintf("    <td colspan=\"2\"><b>Queued</b></td>\n");
	p.safePrintf("    <td colspan=\"2\"><b>Running</b></td>\n");
	p.safePrintf("    <td colspan=\"2\"><b>Stopped</b></td>\n");
	p.safePrintf("  </tr>\n");
	p.safePrintf("  <tr class=hdrow>\n");
	p.safePrintf("    <td colspan=\"1\"></td>\n");
	p.safePrintf("    <td colspan=\"1\"><b>Count</b></td>\n");
	p.safePrintf("    <td colspan=\"1\"><b>Avg. time</b></td>\n");
	p.safePrintf("    <td colspan=\"1\"><b>Count</b></td>\n");
	p.safePrintf("    <td colspan=\"1\"><b>Avg time</b></td>\n");
	p.safePrintf("    <td colspan=\"1\"><b>Count</b></td>\n");
	p.safePrintf("    <td colspan=\"1\"><b>Avg time</b></td>\n");
	p.safePrintf("  </tr>\n");
	
	for(int thread_type=thread_type_query_read; thread_type<=thread_type_generate_thumbnail; thread_type++) {
		int queued_count=0;
		uint64_t queued_time=0;
		int running_count=0;
		uint64_t running_time=0;
		int stopped_count=0;
		uint64_t stopped_time=0;
		for(const auto &jd : job_digests) {
			if(jd.thread_type==thread_type) {
				if(jd.job_state==JobDigest::job_state_queued) {
					queued_count++;
					queued_time += (now-jd.queue_enter_time);
				}
				if(jd.job_state==JobDigest::job_state_running) {
					running_count++;
					running_time += (now-jd.start_time);
				}
				if(jd.job_state==JobDigest::job_state_stopped) {
					stopped_count++;
					stopped_time += (now-jd.stop_time);
				}
			}
		}
		
		p.safePrintf("  <tr bgcolor=#%s>\n",LIGHT_BLUE);
		p.safePrintf("    <td>%s</td>\n", thread_type_name((thread_type_t)thread_type));
		if(queued_count)
			p.safePrintf("    <td>%d</td><td>%" PRIu64"</td>\n", queued_count, queued_time/queued_count);
		else
			p.safePrintf("    <td>-</td><td>-</td>\n");
		if(running_count)
			p.safePrintf("    <td>%d</td><td>%" PRIu64"</td>\n", running_count, running_time/running_count);
		else
			p.safePrintf("    <td>-</td><td>-</td>\n");
		if(stopped_count)
			p.safePrintf("    <td>%d</td><td>%" PRIu64"</td>\n", stopped_count, stopped_time/stopped_count);
		else
			p.safePrintf("    <td>-</td><td>-</td>\n");
	}
	
	p.safePrintf("</table><br><br>");
	
	
	// print details per job
	
	p.safePrintf("<table %s>", TABLE_STYLE);
	p.safePrintf("  <tr class=hdrow>\n");
	p.safePrintf("    <td><b>Job type</b></td>\n");
	p.safePrintf("    <td><b>Routine</b></td>\n");
	p.safePrintf("    <td><b>Queued time</b></td>\n");
	p.safePrintf("    <td><b>Running time</b></td>\n");
	p.safePrintf("    <td><b>Stopped time</b></td>\n");
	p.safePrintf("   </tr>\n");
	for(const auto &jd : job_digests) {
		p.safePrintf("  <tr bgcolor=#%s>\n",LIGHT_BLUE);
		p.safePrintf("    <td>%s</td>", thread_type_name(jd.thread_type));
		p.safePrintf("    <td>%s</td>", g_profiler.getFnName((PTRTYPE)jd.start_routine));
		p.safePrintf("    <td>%" PRIu64"</td>", now-jd.queue_enter_time);
		if(jd.job_state==JobDigest::job_state_running || jd.job_state==JobDigest::job_state_stopped)
			p.safePrintf("    <td>%" PRIu64"</td>", now-jd.start_time);
		else
			p.safePrintf("    <td></td>");
		if(jd.job_state==JobDigest::job_state_stopped)
			p.safePrintf("    <td>%" PRIu64"</td>", now-jd.stop_time);
		else
			p.safePrintf("    <td></td>");
		p.safePrintf("   </tr>\n");
	}
	
	p.safePrintf("</table><br><br>");

	return g_httpServer.sendDynamicPage ( s , (char*) p.getBufStart() ,
						p.length() );
}
