/*
 * CSE 421 / 521 - Operating Systems
 * myhttpd: A simple multi-threaded web server
 * Author: Shreyas Jukanti
 * Date: 3/Nov/'13
 */

#include "myhttpd.h"

int myhttpd_seqno;
int sched_policy;
int num_of_threads;
int debug_mode;
int listening_port;
int warmup_time;
char logging_file[FILENAME_SIZE];
char root_directory[FILENAME_SIZE];
FILE *logging_fp, *verbose_fp;

/* mutex/semaphore bet'n sched and queue threads */
pthread_mutex_t sched_mutex;
pthread_cond_t  sched_cond_mutex;
/* mutex/semaphore bet'n sched and worker threads */
pthread_mutex_t worker_mutex;
pthread_cond_t  worker_cond_mutex;

sem_t sched_sem;
sem_t worker_sem;

struct job_details *job_head1, *job_tail1;
struct job_details *job_head2, *job_tail2;

/*
 * usage
 */
static void
usage (void)
{
    printf("Usage: myhttpd [-d] [-h] [-l file] [-p port] [-r dir] [-t time] [-n threadnum] [-s sched]\n");
    printf("\t-d: debug (non-daemonize) mode\n");
    printf("\t-l: log requests to the file\n");
    printf("\t-h: help\n");
    printf("\t-p: listenting port, else 8080\n");
    printf("\t-r: root directory for the httpd server\n");
    printf("\t-t: initial warm up time\n");
    printf("\t-n: number of worker threads to services client requests \n");
    printf("\t-s: scheduling policy of either fcfs or sjf\n");
}

/*
 * Gets the command line arguments and populates the required variables
 */
static void
get_command_line_opts (int argc, char *argv[])
{
    char ch;

    /* init to default values */
    strcpy(root_directory, ".");
    sched_policy = SCHED_FCFS;
    num_of_threads = 4;
    debug_mode = 0;
    listening_port = 8080;
    warmup_time = 60; /* sec */
    logging_file[0] = '\0';
    verbose_fp = logging_fp = NULL;
    myhttpd_seqno = 0;

    job_head1 = job_tail1 = NULL;
    job_head2 = job_tail2 = NULL;

    /* loop thro. the options */
    while ((ch = getopt(argc, argv, "dhl:p:r:t:n:s:")) != -1) {
        switch(ch) {
        case 's':
            if (strcmp(optarg, "fcfs") == 0) {
                sched_policy = SCHED_FCFS;
            } else {
                if (strcmp(optarg, "sjf") == 0) {
                    sched_policy = SCHED_SJF;
                } else {
                    fprintf(stderr, "Invalid -s option of %s\n", optarg);
                    usage();
                    exit(1);
                }
            }
            break;

        case 'n':
            num_of_threads = atoi(optarg);
            if ((num_of_threads <= 0) || (num_of_threads > 100)) {
                fprintf(stderr, "Invalid -n option of %d\n", num_of_threads);
                usage();
                exit(1);
            }
            break;

        case 't':
            warmup_time = atoi(optarg);
            if ((warmup_time < 0) || (warmup_time > 100)) {
                fprintf(stderr, "Invalid -t option of %d\n", warmup_time);
                usage();
                exit(1);
            }
            break;

        case 'r':
            strcpy(root_directory, optarg);
            break;

        case 'p':
            listening_port = atoi(optarg);
            if ((listening_port < 0) || (listening_port > 100000)) {
                fprintf(stderr, "Invalid -p option of %d\n", listening_port);
                usage();
                exit(1);
            }
            break;

        case 'd':
            if (logging_fp) {
                fprintf(stderr, "Invalid option when logging is enabled \n");
                usage();
                exit(1);
            }
            logging_fp = stdout;
            debug_mode = 1;
            break;

        case 'l':
            if (debug_mode) {
                fprintf(stderr, "Invalid option in debug mode\n");
                usage();
                exit(1);
            }
            strcpy(logging_file, optarg);
            logging_fp = fopen(logging_file, "w");
            break;

        case 'h':
        default:
            usage();
            exit(1);
        } /* switch */
    } /* while */

    /* for debugs */
    verbose_fp = stdout;
    if (!debug_mode) {
        verbose_fp = fopen("./verbose.log", "w");
    }
}

void
my_printf (const char *fmt, ...)
{
    va_list args;
    char buf[512];

    if (verbose_fp) {
        bzero(&args, sizeof(args));
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        fprintf(verbose_fp, "%s", buf);
        fflush(verbose_fp);
    }
}

main (int argc, char *argv[])
{
    int i;
    pthread_t      qt, st, wt[MAX_WORKER_THREADS];
    pthread_attr_t attr;

    get_command_line_opts(argc, argv);
    printf("Starting the myhttpd with below options:\n");
    printf("Server listenting port:\t\t %d\n", listening_port);
    printf("Sched policy:\t\t\t %s\n", ((sched_policy == SCHED_FCFS) ? "FCFS" : "SJF"));
    printf("Number of worker threads:\t %d\n", num_of_threads);
    printf("Root directory:\t\t\t %s\n", root_directory);
    printf("Scheduler thread warmup time:\t %d\n", warmup_time);
    printf("Debug (non-daemonize) mode:\t %s \n", (debug_mode ? "TRUE" : "FALSE"));
    if (debug_mode) {
        printf("Logging status:\t Enabled on stdout\n"); 
    } else {
        printf("Logging status:\t\t\t %s \n", (strlen(logging_file) ? logging_file : "Disabled"));
    }

    if (root_directory) {
        my_printf("Setting the root directory to %s\n", root_directory);
        /* chroot(root_directory); need root privilages */
    }

    if (!debug_mode) {
        my_printf("non debug mode - daemonizing the server \n");
        if (fork() > 0) {
            my_printf("killing parent process %d \n", getpid());
            exit(0);
        }
        fclose(stdin);
        fclose(stdout);
        fclose(stderr);

        my_printf("child process %d\n", getpid());
    }

    /* init the lock bet'n sched thread and queue thread */
    pthread_mutex_init(&sched_mutex, NULL);
    sem_init(&sched_sem, 0, 0);
    pthread_cond_init(&sched_cond_mutex, NULL);

    /* init the lock bet'n sched thread and worker thread */
    pthread_mutex_init(&worker_mutex, NULL);
    sem_init(&worker_sem, 0, 0);
    pthread_cond_init(&worker_cond_mutex, NULL);

    /* create all threads */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    if (pthread_create(&qt, &attr, (void *)&queue_thread, NULL)) {
        my_printf("ERROR creating queue thread\n");
        exit(1);
    }
    if (pthread_create(&st, &attr, (void *)&schedule_thread, NULL)) {
        my_printf("ERROR creating sched thread");
        exit(1);
    }
    for (i = 0; i < num_of_threads; i++) {
        if (pthread_create(&wt[i], &attr, (void *)&worker_thread, (void *)i)) {
            my_printf("ERROR creating worker thread");
            exit(1);
        }
    }
    pthread_join(qt, NULL);
    pthread_join(st, NULL);
    for (i = 0; i < num_of_threads; i++) {
        pthread_join(wt[i], NULL);
    }

    my_printf("main thread waiting for all threads to exit\n");
    /* Clean up and exit */
    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&sched_mutex);
    pthread_mutex_destroy(&worker_mutex);
    pthread_cond_destroy(&sched_cond_mutex);
    pthread_cond_destroy(&worker_cond_mutex);
    pthread_exit(NULL);

    exit(0);
}/*
 * CSE 421 / 521 - Operating Systems
 * myhttpd: A simple multi-threaded web server
 * Author: Shreyas Jukanti
 * Date: 3/Nov/'13
 */

#include "myhttpd.h"

int myhttpd_seqno;
int sched_policy;
int num_of_threads;
int debug_mode;
int listening_port;
int warmup_time;
char logging_file[FILENAME_SIZE];
char root_directory[FILENAME_SIZE];
FILE *logging_fp, *verbose_fp;

/* mutex/semaphore bet'n sched and queue threads */
pthread_mutex_t sched_mutex;
pthread_cond_t  sched_cond_mutex;
/* mutex/semaphore bet'n sched and worker threads */
pthread_mutex_t worker_mutex;
pthread_cond_t  worker_cond_mutex;

sem_t sched_sem;
sem_t worker_sem;

struct job_details *job_head1, *job_tail1;
struct job_details *job_head2, *job_tail2;

/*
 * usage
 */
static void
usage (void)
{
    printf("Usage: myhttpd [-d] [-h] [-l file] [-p port] [-r dir] [-t time] [-n threadnum] [-s sched]\n");
    printf("\t-d: debug (non-daemonize) mode\n");
    printf("\t-l: log requests to the file\n");
    printf("\t-h: help\n");
    printf("\t-p: listenting port, else 8080\n");
    printf("\t-r: root directory for the httpd server\n");
    printf("\t-t: initial warm up time\n");
    printf("\t-n: number of worker threads to services client requests \n");
    printf("\t-s: scheduling policy of either fcfs or sjf\n");
}

/*
 * Gets the command line arguments and populates the required variables
 */
static void
get_command_line_opts (int argc, char *argv[])
{
    char ch;

    /* init to default values */
    strcpy(root_directory, ".");
    sched_policy = SCHED_FCFS;
    num_of_threads = 4;
    debug_mode = 0;
    listening_port = 8080;
    warmup_time = 60; /* sec */
    logging_file[0] = '\0';
    verbose_fp = logging_fp = NULL;
    myhttpd_seqno = 0;

    job_head1 = job_tail1 = NULL;
    job_head2 = job_tail2 = NULL;

    /* loop thro. the options */
    while ((ch = getopt(argc, argv, "dhl:p:r:t:n:s:")) != -1) {
        switch(ch) {
        case 's':
            if (strcmp(optarg, "fcfs") == 0) {
                sched_policy = SCHED_FCFS;
            } else {
                if (strcmp(optarg, "sjf") == 0) {
                    sched_policy = SCHED_SJF;
                } else {
                    fprintf(stderr, "Invalid -s option of %s\n", optarg);
                    usage();
                    exit(1);
                }
            }
            break;

        case 'n':
            num_of_threads = atoi(optarg);
            if ((num_of_threads <= 0) || (num_of_threads > 100)) {
                fprintf(stderr, "Invalid -n option of %d\n", num_of_threads);
                usage();
                exit(1);
            }
            break;

        case 't':
            warmup_time = atoi(optarg);
            if ((warmup_time < 0) || (warmup_time > 100)) {
                fprintf(stderr, "Invalid -t option of %d\n", warmup_time);
                usage();
                exit(1);
            }
            break;

        case 'r':
            strcpy(root_directory, optarg);
            break;

        case 'p':
            listening_port = atoi(optarg);
            if ((listening_port < 0) || (listening_port > 100000)) {
                fprintf(stderr, "Invalid -p option of %d\n", listening_port);
                usage();
                exit(1);
            }
            break;

        case 'd':
            if (logging_fp) {
                fprintf(stderr, "Invalid option when logging is enabled \n");
                usage();
                exit(1);
            }
            logging_fp = stdout;
            debug_mode = 1;
            break;

        case 'l':
            if (debug_mode) {
                fprintf(stderr, "Invalid option in debug mode\n");
                usage();
                exit(1);
            }
            strcpy(logging_file, optarg);
            logging_fp = fopen(logging_file, "w");
            break;

        case 'h':
        default:
            usage();
            exit(1);
        } /* switch */
    } /* while */

    /* for debugs */
    verbose_fp = stdout;
    if (!debug_mode) {
        verbose_fp = fopen("./verbose.log", "w");
    }
}

void
my_printf (const char *fmt, ...)
{
    va_list args;
    char buf[512];

    if (verbose_fp) {
        bzero(&args, sizeof(args));
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        fprintf(verbose_fp, "%s", buf);
        fflush(verbose_fp);
    }
}

main (int argc, char *argv[])
{
    int i;
    pthread_t      qt, st, wt[MAX_WORKER_THREADS];
    pthread_attr_t attr;

    get_command_line_opts(argc, argv);
    printf("Starting the myhttpd with below options:\n");
    printf("Server listenting port:\t\t %d\n", listening_port);
    printf("Sched policy:\t\t\t %s\n", ((sched_policy == SCHED_FCFS) ? "FCFS" : "SJF"));
    printf("Number of worker threads:\t %d\n", num_of_threads);
    printf("Root directory:\t\t\t %s\n", root_directory);
    printf("Scheduler thread warmup time:\t %d\n", warmup_time);
    printf("Debug (non-daemonize) mode:\t %s \n", (debug_mode ? "TRUE" : "FALSE"));
    if (debug_mode) {
        printf("Logging status:\t Enabled on stdout\n"); 
    } else {
        printf("Logging status:\t\t\t %s \n", (strlen(logging_file) ? logging_file : "Disabled"));
    }

    if (root_directory) {
        my_printf("Setting the root directory to %s\n", root_directory);
        /* chroot(root_directory); need root privilages */
    }

    if (!debug_mode) {
        my_printf("non debug mode - daemonizing the server \n");
        if (fork() > 0) {
            my_printf("killing parent process %d \n", getpid());
            exit(0);
        }
        fclose(stdin);
        fclose(stdout);
        fclose(stderr);

        my_printf("child process %d\n", getpid());
    }

    /* init the lock bet'n sched thread and queue thread */
    pthread_mutex_init(&sched_mutex, NULL);
    sem_init(&sched_sem, 0, 0);
    pthread_cond_init(&sched_cond_mutex, NULL);

    /* init the lock bet'n sched thread and worker thread */
    pthread_mutex_init(&worker_mutex, NULL);
    sem_init(&worker_sem, 0, 0);
    pthread_cond_init(&worker_cond_mutex, NULL);

    /* create all threads */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    if (pthread_create(&qt, &attr, (void *)&queue_thread, NULL)) {
        my_printf("ERROR creating queue thread\n");
        exit(1);
    }
    if (pthread_create(&st, &attr, (void *)&schedule_thread, NULL)) {
        my_printf("ERROR creating sched thread");
        exit(1);
    }
    for (i = 0; i < num_of_threads; i++) {
        if (pthread_create(&wt[i], &attr, (void *)&worker_thread, (void *)i)) {
            my_printf("ERROR creating worker thread");
            exit(1);
        }
    }
    pthread_join(qt, NULL);
    pthread_join(st, NULL);
    for (i = 0; i < num_of_threads; i++) {
        pthread_join(wt[i], NULL);
    }

    my_printf("main thread waiting for all threads to exit\n");
    /* Clean up and exit */
    pthread_attr_destroy(&attr);
    pthread_mutex_destroy(&sched_mutex);
    pthread_mutex_destroy(&worker_mutex);
    pthread_cond_destroy(&sched_cond_mutex);
    pthread_cond_destroy(&worker_cond_mutex);
    pthread_exit(NULL);

    exit(0);
}