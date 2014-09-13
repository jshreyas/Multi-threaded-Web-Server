/*
 * CSE 421 / 521 - Operating Systems
 * myhttpd: A simple multi-threaded web server
 * Author: Shreyas Jukanti
 * Date: 3/Nov/'13
 */

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>

#define MAX_WORKER_THREADS 128
#define MAX_CLIENTS   1024
#define FILENAME_SIZE 1024
#define LINE_SIZE     80
#define BUFFERSIZE    1024

enum {
    SCHED_FCFS = 0,
    SCHED_SJF,
};

extern int sched_policy;
extern int num_of_threads;
extern int debug_mode;
extern int listening_port;
extern int warmup_time;
extern char logging_file[FILENAME_SIZE];
extern char root_directory[FILENAME_SIZE];
extern FILE *logging_fp, *verbose_fp;

/* mutex/semaphore bet'n sched and queue threads */
extern pthread_mutex_t sched_mutex;
extern pthread_cond_t  sched_cond_mutex;
/* mutex/semaphore bet'n sched and worker threads */
extern pthread_mutex_t worker_mutex;
extern pthread_cond_t  worker_cond_mutex;

extern sem_t sched_sem;
extern sem_t worker_sem;

enum {
    CONTENT_FILE = 0,
    CONTENT_DIR,
};
enum {
    REQ_HEAD = 0,
    REQ_GET,
};

struct job_details {
    /* for logging */
    char   remote_ip[LINE_SIZE];
    char   arrival_time[LINE_SIZE];
    char   scheduled_time[LINE_SIZE];
    char   first_line[LINE_SIZE];
    int    response_status;
    int    response_size;

    int    image;
    char   root[FILENAME_SIZE];
    char   filename[FILENAME_SIZE];
    int    client_soc;
    int    content_type;
    int    req_type;
    char   lastmodtime[LINE_SIZE];

    int    seqno;

    struct job_details *prev1, *next1; /* queue thread to sched thread */
    struct job_details *prev2, *next2; /* sched thread to worker thread based on sched policy */
};
extern struct job_details *job_head1, *job_tail1;
extern struct job_details *job_head2, *job_tail2;

extern int myhttpd_seqno;

extern void my_printf(const char *fmt, ...);
extern void *queue_thread(void);
extern void *schedule_thread(void);
extern void *worker_thread(void *);