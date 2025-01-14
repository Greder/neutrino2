#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>

#include <syscall.h>

#include <cerrno>

#include <dmx_cs.h>	/* libdvbapi */

#include "Debug.hpp"
#include "PacketQueue.hpp"
#include "semaphore.h"
#include "helpers.hpp"
#include "dvbsubtitle.h"

#include <zapit/frontend_c.h>


//// globals
Debug sub_debug;
static PacketQueue packet_queue;
//
static pthread_t threadReader;
static pthread_t threadDvbsub;
//
static pthread_cond_t readerCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t readerMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t packetCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t packetMutex = PTHREAD_MUTEX_INITIALIZER;
//
static int reader_running;
static int dvbsub_running;
static int dvbsub_paused = true;
static int dvbsub_pid;
static int dvbsub_stopped;
static int pid_change_req;
//
cDvbSubtitleConverter *dvbSubtitleConverter;
//
static void* reader_thread(void *arg);
static void* dvbsub_thread(void* arg);
static void clear_queue();
////
extern CFrontend * live_fe;

//
int dvbsub_init() 
{
	printf("dvbsub_init: starting... tid %ld\n", syscall(__NR_gettid));
	
	int trc;

	sub_debug.set_level(1);

	reader_running = true;
	dvbsub_stopped = 1;
	pid_change_req = 1;
	
	// reader-Thread starten
	trc = pthread_create(&threadReader, 0, reader_thread, (void *) NULL);
	
	if (trc) 
	{
		fprintf(stderr, "[dvb-sub] failed to create reader-thread (rc=%d)\n", trc);
		reader_running = false;
		return -1;
	}

	dvbsub_running = true;
	// subtitle decoder-Thread starten
	trc = pthread_create(&threadDvbsub, 0, dvbsub_thread, NULL);

	if (trc) 
	{
		fprintf(stderr, "[dvb-sub] failed to create dvbsub-thread (rc=%d)\n", trc);
		dvbsub_running = false;
		return -1;
	}

	return(0);
}

int dvbsub_pause()
{
	if(reader_running) 
	{
		dvbsub_paused = true;
		if(dvbSubtitleConverter)
			dvbSubtitleConverter->Pause(true);

		printf("[dvb-sub] paused\n");
	}

	return 0;
}

int dvbsub_start(int pid)
{
	if(!dvbsub_paused && (pid == 0)) 
	{
		return 0;
	}

	if(pid) 
	{
		if(pid != dvbsub_pid) 
		{
			dvbsub_pause();
			if(dvbSubtitleConverter)
				dvbSubtitleConverter->Reset();
			dvbsub_pid = pid;
			pid_change_req = 1;
		}
	}
	
	printf("[dvb-sub] start, stopped %d pid %x\n", dvbsub_stopped, dvbsub_pid);

	if(dvbsub_pid > 0) 
	{
		dvbsub_stopped = 0;
		dvbsub_paused = false;
		if(dvbSubtitleConverter)
			dvbSubtitleConverter->Pause(false);
		pthread_mutex_lock(&readerMutex);
		pthread_cond_broadcast(&readerCond);
		pthread_mutex_unlock(&readerMutex);
		printf("[dvb-sub] started with pid 0x%x\n", pid);
	}

	return 1;
}

int dvbsub_stop()
{
	dvbsub_pid = 0;
	
	if(reader_running) 
	{
		dvbsub_stopped = 1;
		dvbsub_pause();
		pid_change_req = 1;
	}

	return 0;
}

int dvbsub_getpid()
{
	return dvbsub_pid;
}

void dvbsub_setpid(int pid)
{
	dvbsub_pid = pid;

	if(dvbsub_pid == 0)
		return;

	clear_queue();

	if(dvbSubtitleConverter)
		dvbSubtitleConverter->Reset();

	pid_change_req = 1;
	dvbsub_stopped = 0;

	pthread_mutex_lock(&readerMutex);
	pthread_cond_broadcast(&readerCond);
	pthread_mutex_unlock(&readerMutex);
}

int dvbsub_close()
{
	if(threadReader) 
	{
		dvbsub_pause();
		reader_running = false;
		dvbsub_stopped = 1;

		pthread_mutex_lock(&readerMutex);
		pthread_cond_broadcast(&readerCond);
		pthread_mutex_unlock(&readerMutex);

		pthread_join(threadReader, NULL);
		threadReader = 0;
	}
	
	if(threadDvbsub) 
	{
		dvbsub_running = false;

		pthread_mutex_lock(&packetMutex);
		pthread_cond_broadcast(&packetCond);
		pthread_mutex_unlock(&packetMutex);

		pthread_join(threadDvbsub, NULL);
		threadDvbsub = 0;
	}
	printf("[dvb-sub] stopped\n");

	return 0;
}

static cDemux * dmx;

void dvbsub_get_stc(int64_t * STC)
{
	if(dmx)
		dmx->getSTC(STC);
}

static int64_t get_pts(unsigned char * packet)
{
	int64_t pts;
	int pts_dts_flag;

	pts_dts_flag = getbits(packet, 7*8, 2);
	if ((pts_dts_flag == 2) || (pts_dts_flag == 3)) 
	{
		pts = (uint64_t)getbits(packet, 9*8+4, 3) << 30;  /* PTS[32..30] */
		pts |= getbits(packet, 10*8, 15) << 15;           /* PTS[29..15] */
		pts |= getbits(packet, 12*8, 15);                 /* PTS[14..0] */
	} 
	else 
	{
		pts = 0;
	}
	return pts;
}

#define LimitTo32Bit(n) (n & 0x00000000FFFFFFFFL)

static int64_t get_pts_stc_delta(int64_t pts)
{
	int64_t stc, delta;

	dvbsub_get_stc(&stc);
	delta = LimitTo32Bit(pts) - LimitTo32Bit(stc);
	delta /= 90;
	return delta;
}

static void clear_queue()
{
	uint8_t* packet;

	pthread_mutex_lock(&packetMutex);
	while(packet_queue.size()) {
		packet = packet_queue.pop();
		free(packet);
	}
	pthread_mutex_unlock(&packetMutex);
}

static void* reader_thread(void * /*arg*/)
{
	uint8_t tmp[16];  /* actually 6 should be enough */
	int count;
	int len;
	uint16_t packlen;
	uint8_t* buf;

        dmx = new cDemux();
	
	dmx->Open(DMX_PES_CHANNEL, 64*1024, live_fe);	

	while (reader_running) 
	{
		if(dvbsub_stopped) 
		{
			sub_debug.print(Debug::VERBOSE, "%s stopped\n", __FUNCTION__);
			dmx->Stop();

			pthread_mutex_lock(&packetMutex);
			pthread_cond_broadcast(&packetCond);
			pthread_mutex_unlock(&packetMutex);

			pthread_mutex_lock(&readerMutex );
			int ret = pthread_cond_wait(&readerCond, &readerMutex);
			pthread_mutex_unlock(&readerMutex);

			if (ret) 
			{
				sub_debug.print(Debug::VERBOSE, "pthread_cond_timedwait fails with %d\n", ret);
			}
			if(!reader_running)
				break;
			dvbsub_stopped = 0;
			sub_debug.print(Debug::VERBOSE, "%s (re)started with pid 0x%x\n", __FUNCTION__, dvbsub_pid);
		}

		if(pid_change_req) 
		{
			pid_change_req = 0;
			clear_queue();
			dmx->Stop();
			//			
			dmx->Open(DMX_PES_CHANNEL, 64*1024, live_fe);				
			//
			dmx->pesFilter(dvbsub_pid);
			dmx->Start();
			sub_debug.print(Debug::VERBOSE, "%s changed to pid 0x%x\n", __FUNCTION__, dvbsub_pid);
		}

		len = 0;
		count = 0;

		len = dmx->Read(tmp, 6, 1000);
		
		if(len <= 0)
			continue;

		if(memcmp(tmp, "\x00\x00\x01\xbd", 4)) 
		{
			sub_debug.print(Debug::VERBOSE, "[subtitles] bad start code: %02x%02x%02x%02x\n", tmp[0], tmp[1], tmp[2], tmp[3]);
			continue;
		}
		count = 6;

		packlen =  getbits(tmp, 4*8, 16) + 6;

		buf = (uint8_t*) malloc(packlen);

		memcpy(buf, tmp, 6);
		
		/* read rest of the packet */
		while((count < packlen) /* && !dvbsub_paused*/) 
		{
			len = dmx->Read(buf+count, packlen-count, 1000);
			
			if (len < 0) 
			{
				continue;
			} 
			else 
			{
				count += len;
			}
		}

		if(!dvbsub_stopped) 
		{
			/* Packet now in memory */
			packet_queue.push(buf);
			/* TODO: allocation exception */
			// wake up dvb thread
			pthread_mutex_lock(&packetMutex);
			pthread_cond_broadcast(&packetCond);
			pthread_mutex_unlock(&packetMutex);
		} 
		else 
		{
			free(buf);
			buf=NULL;
		}
	}

	dmx->Stop();
	delete dmx;
	dmx = NULL;

	sub_debug.print(Debug::VERBOSE, "%s shutdown\n", __FUNCTION__);
	pthread_exit(NULL);
}

static void* dvbsub_thread(void* /*arg*/)
{
	struct timespec restartWait;
	struct timeval now;

	sub_debug.print(Debug::VERBOSE, "%s started\n", __FUNCTION__);
	
	if (!dvbSubtitleConverter)
		dvbSubtitleConverter = new cDvbSubtitleConverter;

	int timeout = 1000000;
	
	while(dvbsub_running) 
	{
		uint8_t* packet;
		int64_t pts;
		int dataoffset;
		int packlen;

		gettimeofday(&now, NULL);

		int ret = 0;
		now.tv_usec += (timeout == 0) ? 1000000 : timeout;   // add the timeout
		
		while (now.tv_usec >= 1000000) 
		{   
			// take care of an overflow
			now.tv_sec++;
			now.tv_usec -= 1000000;
		}
		
		restartWait.tv_sec = now.tv_sec;          // seconds
		restartWait.tv_nsec = now.tv_usec * 1000; // nano seconds

		pthread_mutex_lock( &packetMutex );
		ret = pthread_cond_timedwait( &packetCond, &packetMutex, &restartWait );
		pthread_mutex_unlock( &packetMutex );

		timeout = dvbSubtitleConverter->Action();

		if(packet_queue.size() == 0) {
			continue;
		}

		//
		if(dvbsub_stopped) 
		{
			clear_queue();
			continue;
		}

		pthread_mutex_lock(&packetMutex);
		packet = packet_queue.pop();
		pthread_mutex_unlock(&packetMutex);

		if (!packet) {
			sub_debug.print(Debug::VERBOSE, "Error no packet found\n");
			continue;
		}
		packlen = (packet[4] << 8 | packet[5]) + 6;

		pts = get_pts(packet);

		dataoffset = packet[8] + 8 + 1;
		if (packet[dataoffset] != 0x20) 
		{
			goto next_round;
		}

		if (packlen <= dataoffset + 3) 
		{
			sub_debug.print(Debug::INFO, "Packet too short, discard\n");
			
			goto next_round;
		}

		if (packet[dataoffset + 2] == 0x0f) 
		{
			dvbSubtitleConverter->Convert(&packet[dataoffset + 2], packlen - (dataoffset + 2), pts);
		} 
		else 
		{
			sub_debug.print(Debug::INFO, "End_of_PES is missing\n");
		}
		timeout = dvbSubtitleConverter->Action();

next_round:
		free(packet);
	}

	delete dvbSubtitleConverter;

	sub_debug.print(Debug::VERBOSE, "%s shutdown\n", __FUNCTION__);
	
	pthread_exit(NULL);
}

