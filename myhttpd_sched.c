/*
 * CSE 421 / 521 - Operating Systems
 * myhttpd: A simple multi-threaded web server
 * Author: Shreyas Jukanti
 * Date: 3/Nov/'13
 */

#include "myhttpd.h"

/*
 * Arrange as per schedular policy. Worker thread will always read from tail. 
 */
static void
to_worker_thread (struct job_details *job)
{
    struct job_details *ljob;

    my_printf("%s: giving job %d to workers sched: %s \n", __FUNCTION__, job->seqno,
               ((sched_policy == SCHED_FCFS) ? "fcfs" : "sjf"));
    pthread_mutex_lock(&worker_mutex);
    if (sched_policy == SCHED_FCFS) {
        if (job_head2) {
            job->next2 = job_head2;
            job_head2->prev2 = job;
        }
        job_head2 = job;
        if (job_tail2 == NULL) {
            job_tail2 = job;
        }
    }

    /* order max to min */
    if (sched_policy == SCHED_SJF) {
        /* no existing entry */
        if (!job_head2) {
            job_head2 = job_tail2 = job;
            goto done;
        }
        /* should be at the head of the list */
        if (job->response_size > job_head2->response_size) {
            job->next2 = job_head2;
            job_head2->prev2 = job;
            job_head2 = job;
            goto done;
        }
        /* should be at the tail of the list */
        if (job_tail2->response_size > job->response_size) {
            job->prev2 = job_tail2;
            job_tail2->next2 = job;
            job_tail2 = job;
            goto done;
        }
        /* somewhere in the middle. head and tail will not change */
        ljob = job_head2;
        while (ljob) {
            if (ljob->response_size < job->response_size) {
                break;
            }
            ljob = ljob->next2;
        }
        job->next2 = ljob;
        job->prev2 = ljob->prev2;
        ljob->prev2->next2 = job;
        ljob->prev2 = job;
    }
done:
    sem_post(&worker_sem);
    pthread_cond_signal(&worker_cond_mutex);
    pthread_mutex_unlock(&worker_mutex);
}

/*
 * Read a job from the end of the queue. Get the details of the job and queue to the work threads as
 * per scheduling policy
 */
static struct job_details *
from_sched_queue (void)
{
    struct job_details *job;

    /* take a job from queue thread */
    sem_wait(&sched_sem);
    pthread_mutex_lock(&sched_mutex);
    job = job_tail1;
    job_tail1 = job->prev1;
    if (job_tail1) {
        job_tail1->next1 = NULL;
    }
    if (job_head1 == job) {
        job_head1 = NULL;
    }
    pthread_mutex_unlock(&sched_mutex);
    job->prev1 = job->next1 = NULL;
    my_printf("schedular got job %d\n", job->seqno);
    return job;
}

void *
schedule_thread (void)
{
    struct job_details *job;

    my_printf("%s: warming up for %d sec\n", __FUNCTION__, warmup_time);
    sleep(warmup_time);
    my_printf("%s: ready to go \n", __FUNCTION__);

    while (1) {
        job = from_sched_queue();
        my_printf("%s: processing job %d\n", __FUNCTION__, job->seqno);
        to_worker_thread(job);
    }
    pthread_exit(NULL);
}