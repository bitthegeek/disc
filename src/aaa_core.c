/*
 * $Id: aaa_core.c,v 1.21 2003/05/09 16:35:17 andrei Exp $
 *
 * Copyright (C) 2002-2003 Fhg Fokus
 *
 * This file is part of disc, a free diameter server/client.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program; if not, write to the Free Software 
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* History:
 * --------
 *  2003-04-08  created by bogdan
 *  2003-04-08  peer & route support in the cfg. file (andrei)
 *  2003-04-09  cmd. line parsing, disable_ipv6 (andrei)
 *  2003-04-14  cmd. line switch for the maximum memory used (andrei)
 *  2003-04-16  daemonize, lots of startup params  (andrei)
 *  2003-05-09  closelog before openlog (solaris ready) (andrei)
 *  
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h> /* isprint */
#include <signal.h>
#include <pthread.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "mem/shm_mem.h"
#include "diameter_api/diameter_api.h"
#include "transport/peer.h"
#include "transport/tcp_shell.h"
#include "server.h"
#include "client.h"
#include "globals.h"
#include "timer.h"
#include "utils.h"
#include "msg_queue.h"
#include "aaa_module.h"
#include "cfg_init.h"
#include "route.h"


/*#define CFG_FILE "aaa.cfg"*/


static char id[]="$Id: aaa_core.c,v 1.21 2003/05/09 16:35:17 andrei Exp $";
static char version[]= NAME " " VERSION " (" ARCH "/" OS ")" ;
static char compiled[]= __TIME__ " " __DATE__;
static char flags[]=""
#ifdef USE_IPV6
"USE_IPV6 "
#endif
#ifdef NO_DEBUG
"NO_DEBUG "
#endif
#ifdef NO_LOG
"NO_LOG "
#endif
#ifdef EXTRA_DEBUG
"EXTRA_DEBUG "
#endif
#ifdef DNS_IP_HACK
"DNS_IP_HACK "
#endif
#ifdef SHM_MEM
"SHM_MEM "
#endif
#ifdef PKG_MALLOC
"PKG_MALLOC "
#endif
#ifdef VQ_MALLOC
"VQ_MALLOC "
#endif
#ifdef F_MALLOC
"F_MALLOC "
#endif
#ifdef DBG_QM_MALLOC
"DBG_QM_MALLOC "
#endif
#ifdef FAST_LOCK
"FAST_LOCK"
#ifdef BUSY_WAIT
"-BUSY_WAIT"
#endif
#ifdef ADAPTIVE_WAIT
"-ADAPTIVE_WAIT"
#endif
#ifdef NOSMP
"-NOSMP"
#endif
" "
#endif /*FAST LOCK*/
;


static char help_msg[]= "\
Usage: " NAME " -f file   \n\
Options:\n\
    -6          Disable ipv6 \n\
    -E          Log to stderr \n\
    -d          Debugging mode (multiple -d increase the level)\n\
    -D          Do not fork into daemon mode\n\
    -f file     Configuration file (default " CFG_FILE ")\n\
    -h          This help message \n\
    -p port     Listen on the specified port (default 1812) \n\
    -P file     Create a pid file \n\
    -m size     Maximum memory size to use in Mb (default 1) \n\
    -t dir      Chroot to \"dir\" \n\
    -u user     Change uid to \"user\" \n\
    -g group    Change gid to \"group\" \n\
    -V          Version number \n\
    -w          Change the working directory to \"dir\"  (default \"/\")\n\
";


#define MAX_FD 32 /* maximum number of inherited open file descriptors,
                    (normally it shouldn't  be bigger  than 3) */


/* thread-id of the original thread */
pthread_t main_thread;

/* cfg file name*/
static char* cfg_file=CFG_FILE;

/* shared mem. size*/
unsigned int shm_mem_size=0;

/* shm_mallocs log level */
int memlog=L_DBG;

/* default debuging level */
int debug=0;

/* use std error for loging - default value */
int log_stderr=0;

int dont_fork = 0;
char* chroot_dir = 0;
char* working_dir = 0;
char* user = 0;
char* group = 0;
int uid = 0;
int gid = 0;
char* pid_file = 0;

/* aaa identity */
str aaa_identity= {0, 0};

/* realm served */
str aaa_realm= {0, 0};

/* fqdn */
str aaa_fqdn= {0, 0};

/* product name */
str product_name = {"DISC - DIameter Server/Client",29};

/* vendor id */
unsigned int vendor_id = 0x0000D15C;

/* listening port */
unsigned int listen_port = DEFAULT_LISTENING_PORT;

/* if 1 do not use ipv6 */
int disable_ipv6=0;

/**/
unsigned int do_relay = 0;

/* my status - client, server, statefull server */
unsigned int my_aaa_status = AAA_UNDEFINED;

/* number of working threads */
unsigned int worker_threads = DEAFULT_WORKER_THREADS;

/* number of peer threads */
unsigned int reader_threads = DEFAULT_TCP_RECEIVE_THREADS;



#define AAAID_START          "aaa://"
#define AAAID_START_LEN      (sizeof(AAAID_START)-1)
#define TRANSPORT_PARAM      ";transport=tcp"
#define TRANSPORT_PARAM_LEN  (sizeof(TRANSPORT_PARAM)-1)


static pthread_t *worker_id = 0;
static int nr_worker_threads = 0;

int (*send_local_request)(AAAMessage*,struct trans*);


/* 0 on success, -1 on error */
static int parse_cmd_line(int argc, char** argv)
{
	char c;
	char* tmp;
	
	opterr=0;
	while((c=getopt(argc, argv, "f:p:m:P:t:u:g:w:6VhEdD"))!=-1){
		switch(c){
			case 'f':
				cfg_file=optarg;
				break;
			case  'd':
				debug++;
				break;
			case 'p':
				listen_port=strtol(optarg, &tmp, 10);
				if ((tmp==0)||(*tmp)||(listen_port<=0)){
					fprintf(stderr, "bad port number: -p %s\n", optarg);
					goto error;
				}
				break;
			case 'm':
				shm_mem_size=strtol(optarg, &tmp, 10);
				if ((tmp==0)||(*tmp)||(shm_mem_size<=0)){
					fprintf(stderr, "bad port memory size: -m %s\n", optarg);
					goto error;
				}
				shm_mem_size*=1024*1024; /* in megabytes */
				break;
			case 'E':
				log_stderr=1;
				break;
			case '6':
				disable_ipv6=1;
				break;
			case 'V':
				printf("version: %s\n", version);
				printf("flags: %s\n", flags );
				printf("%s\n", id);
				printf("%s compiled on %s with %s\n", __FILE__,
							compiled, COMPILER );
				exit(0);
				break;
			case 'h':
				printf("version: %s\n", version);
				printf("%s", help_msg);
				exit(0);
				break;
			case 'D':
				dont_fork=1;
				break;
			case 'w':
				working_dir=optarg;
				break;
			case 't':
				chroot_dir=optarg;
				break;
			case 'u':
				user=optarg;
				break;
			case 'g':
				group=optarg;
				break;
			case 'P':
				pid_file=optarg;
				break;
			case '?':
				if (isprint(optopt))
					fprintf(stderr, "Unknown option `-%c´\n", optopt);
				else
					fprintf(stderr, "Unknown character `\\x%x´\n", optopt);
				goto error;
				break;
			case ':':
				fprintf(stderr, "Option `-%c´ requires an argument.\n",
						optopt);
				goto error;
				break;
			default:
				/* we should never reach this */
				if (isprint(c))
					fprintf(stderr, "Unknown option `-%c´\n", c);
				else
					fprintf(stderr, "Unknown option `-\\x%x´\n", c);
				goto error;
				break;
		}
	}
	return 0;
error:
	return -1;
}



/* daemon init, return 0 on success, -1 on error */
int daemonize(char*  name)
{
	FILE *pid_stream;
	pid_t pid;
	int r, p;


	p=-1;


	if (chroot_dir&&(chroot(chroot_dir)<0)){
		LOG(L_CRIT, "Cannot chroot to %s: %s\n", chroot_dir, strerror(errno));
		goto error;
	}
	
	if (chdir(working_dir)<0){
		LOG(L_CRIT,"cannot chdir to %s: %s\n", working_dir, strerror(errno));
		goto error;
	}

	if (gid&&(setgid(gid)<0)){
		LOG(L_CRIT, "cannot change gid to %d: %s\n", gid, strerror(errno));
		goto error;
	}
	
	if(uid&&(setuid(uid)<0)){
		LOG(L_CRIT, "cannot change uid to %d: %s\n", uid, strerror(errno));
		goto error;
	}

	/* fork to become!= group leader*/
	if ((pid=fork())<0){
		LOG(L_CRIT, "Cannot fork:%s\n", strerror(errno));
		goto error;
	}else if (pid!=0){
		/* parent process => exit*/
		exit(0);
	}
	/* become session leader to drop the ctrl. terminal */
	if (setsid()<0){
		LOG(L_WARN, "setsid failed: %s\n",strerror(errno));
	}
	/* fork again to drop group  leadership */
	if ((pid=fork())<0){
		LOG(L_CRIT, "Cannot  fork:%s\n", strerror(errno));
		goto error;
	}else if (pid!=0){
		/*parent process => exit */
		exit(0);
	}

	/* added by noh: create a pid file for the main process */
	if (pid_file!=0){
		
		if ((pid_stream=fopen(pid_file, "r"))!=NULL){
			fscanf(pid_stream, "%d", &p);
			fclose(pid_stream);
			if (p==-1){
				LOG(L_CRIT, "pid file %s exists, but doesn't contain a valid"
					" pid number\n", pid_file);
				goto error;
			}
			if (kill((pid_t)p, 0)==0 || errno==EPERM){
				LOG(L_CRIT, "running process found in the pid file %s\n",
					pid_file);
				goto error;
			}else{
				LOG(L_WARN, "pid file contains old pid, replacing pid\n");
			}
		}
		pid=getpid();
		if ((pid_stream=fopen(pid_file, "w"))==NULL){
			LOG(L_WARN, "unable to create pid file %s: %s\n", 
				pid_file, strerror(errno));
			goto error;
		}else{
			fprintf(pid_stream, "%i\n", (int)pid);
			fclose(pid_stream);
		}
	}
	
	/* try to replace stdin, stdout & stderr with /dev/null */
	if (freopen("/dev/null", "r", stdin)==0){
		LOG(L_ERR, "unable to replace stdin with /dev/null: %s\n",
				strerror(errno));
		/* continue, leave it open */
	};
	if (freopen("/dev/null", "w", stdout)==0){
		LOG(L_ERR, "unable to replace stdout with /dev/null: %s\n",
				strerror(errno));
		/* continue, leave it open */
	};
	/* close stderr only if log_stderr=0 */
	if ((!log_stderr) &&(freopen("/dev/null", "w", stderr)==0)){
		LOG(L_ERR, "unable to replace stderr with /dev/null: %s\n",
				strerror(errno));
		/* continue, leave it open */
	};
	
	/* close any open file descriptors */
	closelog();
	for (r=3;r<MAX_FD; r++){
			close(r);
	}
	
	if (log_stderr==0)
		openlog(name, LOG_PID|LOG_CONS, LOG_DAEMON);
		/* LOG_CONS, LOG_PERRROR ? */
	return  0;

error:
	return -1;
}



void init_random_generator()
{
	unsigned int seed;
	int fd;

	/* init the random number generater by choosing a proper seed; first
	 * we try to read it from /dev/random; if it doesn't exist use a
	 * combination of current time and pid */
	seed=0;
	if ((fd=open("/dev/random", O_RDONLY))!=-1) {
		while(seed==0&&read(fd,(void*)&seed, sizeof(seed))==-1&&errno==EINTR);
		if (seed==0) {
			LOG(L_WARN,"WARNING:init_random_generator: could not read from"
				" /dev/random (%d)\n",errno);
		}
		close(fd);
	}else{
		LOG(L_WARN,"WARNING:init_random_generator: could not open "
			"/dev/random (%d)\n",errno);
	}
	seed+=getpid()+time(0);
	srand(seed);
}



int generate_aaaIdentity()
{
	char port_s[32];
	int  port_len;
	char *ptr;

	/* convert port into string */
	port_len = int2str( listen_port, port_s, 32 );

	/* compute the length */
	aaa_identity.len = AAAID_START_LEN + aaa_fqdn.len + 1/*:*/ +
		port_len + TRANSPORT_PARAM_LEN;

	/* allocate mem */
	aaa_identity.s = (char*)shm_malloc( aaa_identity.len );
	if (!aaa_identity.s) {
		LOG(L_CRIT,"ERROR:generate_aaaIdentity: no free memory -> cannot "
			"generate aaa_identity\n");
		return -1;
	}

	ptr = aaa_identity.s;
	memcpy( ptr, AAAID_START, AAAID_START_LEN );
	ptr += AAAID_START_LEN;

	memcpy( ptr, aaa_fqdn.s, aaa_fqdn.len );
	ptr += aaa_fqdn.len;

	*(ptr++) = ':';

	memcpy( ptr, port_s, port_len );
	ptr += port_len;

	memcpy( ptr, TRANSPORT_PARAM, TRANSPORT_PARAM_LEN );
	ptr += TRANSPORT_PARAM_LEN;

	LOG(L_INFO,"INFO:generate_aaaIdentity: [%.*s]\n",
		aaa_identity.len,aaa_identity.s);
	return 1;
}



int start_workers( void*(*worker)(void*), int nr_workers )
{
	int i;

	worker_id = (pthread_t*)shm_malloc( nr_workers*sizeof(pthread_t));
	if (!worker_id) {
		LOG(L_ERR,"ERROR:start_workers: cannot get free memory!\n");
		return -1;
	}

	for(i=0;i<nr_workers;i++) {
		if (pthread_create( &worker_id[i], /*&attr*/ 0, worker, 0)!=0){
			LOG(L_ERR,"ERROR:start_workers: cannot create worker thread\n");
			return -1;
		}
		nr_worker_threads++;
	}

	return 1;
}



void stop_workers()
{
	int i;

	if (worker_id) {
		for(i=0;i<nr_worker_threads;i++)
			pthread_cancel( worker_id[i]);
		shm_free( worker_id );
	}
}





void destroy_aaa_core()
{
	/* stop the worker threads */
	stop_workers();
	
	/* stop & destroy the modules */
	destroy_modules();
	
	/* destroy destination peers resolver */
	if (my_aaa_status==AAA_CLIENT) {
		/* something here? */
	} else {
		/* something for server ???? */
	}
	
	/* destroy the msg queue */
	destroy_msg_queue();
	
	/* close the libarary */
	AAAClose();
	
	/* stop the tcp layer */
	terminate_tcp_shell();
	
	/* destroy the transaction manager */
	destroy_trans_manager();
	
	/* destroy the peer manager */
	destroy_peer_manager( peer_table );
	
	/**/
	if (aaa_identity.s)
		shm_free(aaa_identity.s);
	
	/* destroy tge timer */
	destroy_timer();
	
	/* destroy route & peer lists */
	destroy_route_lst();
	destroy_cfg_peer_lst();
	
	/* free some globals*/
	if (aaa_realm.s) { shm_free(aaa_realm.s); aaa_realm.s=0; }
	if (aaa_fqdn.s) { shm_free(aaa_fqdn.s); aaa_fqdn.s=0; }
	
	/* just for debuging */
	shm_status();
	/* unload the modules - final step */
	unload_modules();
	/* destroy the shared mem. pool */
	shm_mem_destroy();
}



int init_aaa_core(char* name, char *cfg_file)
{
	void* shm_mempool;
	struct peer_entry* pl;
	struct peer* pe;
	char *tmp;
	struct passwd *pw_entry;
	struct group  *gr_entry;

	/* fix mem size */
	if (shm_mem_size<=0) shm_mem_size=SHM_MEM_SIZE;
	/* init mallocs */
	shm_mempool=malloc(shm_mem_size);
	if (shm_mempool==0){
		LOG(L_CRIT, "ERROR:main: could not allocate enough memory (%d)\n",
						shm_mem_size);
		exit(-1);
	};
	if (shm_mem_init_mallocs(shm_mempool, shm_mem_size)<0){
		LOG(L_CRIT,"ERROR:main: could not intialize shm. mallocs\n");
		exit(-1);
	};

	/* init rand() with a random as possible seed */
	init_random_generator();

	/* init modules loading */
	init_module_loading();

	/* read config file */
	if (read_config_file( cfg_file )!=0){
		/*fprintf(stderr, "bad config %s\n", cfg_file);*/
		goto error;
	}
	/* fix config stuff */
	if (listen_port<=0) listen_port=DEFAULT_LISTENING_PORT;
	if (my_aaa_status==AAA_UNDEFINED) {
		LOG(L_CRIT,"ERROR:init_aaa_core: mandatory param \"aaa_status\" "
			"not found in config file\n");
		goto error;
	}
	if (worker_threads==0 || worker_threads>MAX_THREADS) {
		LOG(L_WARN,"WARNING:init_aaa_core: param \"worker_threads\" has an "
			"invalid value %d (correct range is[1..%d]). Using default value "
			"of %d.\n",worker_threads,MAX_THREADS,DEAFULT_WORKER_THREADS);
		worker_threads = DEAFULT_WORKER_THREADS;
	}
	if (reader_threads==0 || reader_threads>MAX_THREADS) {
		LOG(L_WARN,"WARNING:init_aaa_core: param \"reader_threads\" has an "
			"invalid value %d (correct range is[1..%d]). Using default value "
			"of %d.\n",reader_threads,MAX_THREADS,DEFAULT_TCP_RECEIVE_THREADS);
		reader_threads = DEFAULT_TCP_RECEIVE_THREADS;
	}

	if (working_dir==0) working_dir="/";

	/* get uid/gid */
	if (user){
		uid=strtol(user, &tmp, 10);
		if ((tmp==0) ||(*tmp)){
			/* maybe it's a string */
			pw_entry=getpwnam(user);
			if (pw_entry==0){
				fprintf(stderr, "bad user name/uid number: -u %s\n", user);
				goto error;
			}
			uid=pw_entry->pw_uid;
			gid=pw_entry->pw_gid;
		}
	}
	if (group){
		gid=strtol(user, &tmp, 10);
		if ((tmp==0) ||(*tmp)){
			/* maybe it's a string */
			gr_entry=getgrnam(group);
			if (gr_entry==0){
				fprintf(stderr, "bad group name/gid number: -u %s\n", group);
				goto error;
			}
			gid=gr_entry->gr_gid;
		}
	}
	
	
	if (my_aaa_status==AAA_SERVER)
		do_relay=1;

	/* build the aaa_identity based on FQDN and port */
	if ( generate_aaaIdentity()==-1 ) {
		goto error;
	}

	/* init daemon */
	if (!dont_fork){
		if(daemonize(name)<0) goto error;
	}

	/* init the transaction manager */
	if (init_trans_manager()==-1) {
		goto error;
	}

	/* init the peer manager */
	if ( (peer_table=init_peer_manager(DEFAULT_TRANS_HASH_SIZE))==0) {
		goto error;
	}

	/* starts the transport layer - tcp */
	if (init_tcp_shell( reader_threads )==-1) {
		goto error;
	}

	/* init the diameter library */
	if( AAAOpen("aaa_lib.cfg")!=AAA_ERR_SUCCESS ) {
		goto error;
	}

	/* add the peers from config file */
	if (cfg_peer_lst==0){
		fprintf(stderr, "ERROR: empty peer list\n");
		goto error;
	}
	for (pl=cfg_peer_lst; pl; pl=pl->next){
		if ((pe=add_peer(&pl->full_uri, &pl->uri.host, pl->uri.port_no))==0){
			fprintf(stderr, "ERROR: failed to add peer <%.*s>\n",
					pl->full_uri.len, pl->full_uri.s);
			goto error;
		}else{
			pl->peer=pe; /* remember it for do_route */
		}
	}

	/* init the message queue between transport layer and session one */
	if (init_msg_queue()==-1) {
		goto error;
	}

	/* starts the worker threads */
	if (my_aaa_status==AAA_CLIENT) {
		if (start_workers( client_worker, worker_threads )==-1 )
			goto error;
	} else {
		if (start_workers( server_worker, worker_threads )==-1 )
			goto error;
	}

	/* init the destination peers resolver */
	if (my_aaa_status==AAA_CLIENT) {
		send_local_request = client_send_local_req;
	} else {
		/* something for server ???? */
		send_local_request = server_send_local_req;
	}

	return 1;
error:
	fprintf(stderr, "ERROR: cannot init the core\n");
	destroy_aaa_core();
	return -1;
}



static void sig_handler(int signo)
{
	if ( main_thread==pthread_self() ) {
		/* I'am the main thread */
		switch (signo) {
			case 0: break; /* do nothing*/
			case SIGPIPE:
				LOG(L_WARN, "WARNING: SIGPIPE received and ignored\n");
				break;
			case SIGINT:
			case SIGTERM:
				if (signo==SIGINT)
					DBG("SIGINT received, program terminates\n");
				else
					DBG("SIGTERM received, program terminates\n");
				destroy_aaa_core();
				LOG(L_CRIT,"Thank you for rolling the DISC...\n");
				exit(0);
				break;
			case SIGHUP: /* ignoring it*/
				DBG("SIGHUP received, ignoring it\n");
				break;
			case SIGUSR1:
				LOG(memlog, "Memory status (shm):\n");
				shm_status();
				break;
			default:
				LOG(L_CRIT, "WARNING: unhandled signal %d\n", signo);
		}
	}
	return;
}




int main(int argc, char *argv[])
{
	if (parse_cmd_line(argc, argv)!=0)
		goto error;

	/* to remember which one was the original thread */
	main_thread = pthread_self();

	/* install signal handler */
	if (signal(SIGINT, sig_handler)==SIG_ERR) {
		printf("ERROR:main: cannot install SIGINT signal handler\n");
		goto error;
	}
	if (signal(SIGPIPE, sig_handler) == SIG_ERR ) {
		printf("ERROR:main: cannot install SIGPIPE signal handler\n");
		goto error;
	}
	if (signal(SIGUSR1, sig_handler)  == SIG_ERR ) {
		printf("ERROR:main: cannot install SIGUSR1 signal handler\n");
		goto error;
	}
	if (signal(SIGTERM , sig_handler)  == SIG_ERR ) {
		printf("ERROR:main: cannot install SIGTERM signal handler\n");
		goto error;
	}
	if (signal(SIGHUP , sig_handler)  == SIG_ERR ) {
		printf("ERROR:main: cannot install SIGHUP signal handler\n");
		goto error;
	}
	if (signal(SIGUSR2 , sig_handler)  == SIG_ERR ) {
		printf("ERROR:main: cannot install SIGUSR2 signal handler\n");
		goto error;
	}

	/* init the aaa core */
	if ( init_aaa_core(argv[0], cfg_file)==-1 )
		goto error;

	/* start the tcp shell */
	start_tcp_accept();

	/* start the modules */
	init_modules();

	/* for the last of the execution, I will act as timer */
	timer_ticker();

	/* this call is just a trick to force the linker to add this function */
	AAASendMessage(0); // !!!!!!!!!!!!!!!!!!

	return 0;
error:
	return -1;
}
