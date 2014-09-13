/*
 * CSE 421 / 521 - Operating Systems
 * myhttpd: A simple multi-threaded web server
 * Author: Shreyas Jukanti
 * Date: 3/Nov/'13
 */

#include "myhttpd.h"

#define RESPERR 404
#define RESPOK  200

#define HTMLOK_STR   "HTTP/1.0 200 OK\nDate:%s\nServer:jukanti.com\nLast-Modified:%s\nContent-Type:text/html\nContent-Length:%d\n\n"
#define IMAGEOK_STR  "HTTP/1.0 200 OK\nDate:%s\nServer:jukanti.com\nLast-Modified:%s\nContent-Type:image/gif\nContent-Length:%d\n\n"
#define HTMLERR_STR  "HTTP/1.0 404 Not Found\nDate:%s\nServer:jukanti.com\nLast-Modified:%s\nContent-Type:text/html\nContent-Length:%d\n\n"

static struct job_details *
from_worker_thread (void)
{
    struct job_details *job;

    /* take a job from queue thread */
    sem_wait(&worker_sem);
    pthread_mutex_lock(&worker_mutex);
    job = job_tail2;
    job_tail2 = job->prev2;
    if (job_tail2) {
        job_tail2->next2 = NULL;
    }
    if (job_head2 == job) {
        job_head2 = NULL;
    }
    pthread_mutex_unlock(&worker_mutex);
    my_printf("%s: got job %d\n", __FUNCTION__, job->seqno);
    job->prev2 = job->next2 = NULL;
    return job;
}

static void
send_response (struct job_details *job, char *buf, int buflen)
{
    my_printf("%s: sending out %d bytes on soc %d\n", __FUNCTION__, buflen, job->client_soc);
    send(job->client_soc, buf, buflen, 0);
}

/* return the header */
static void
send_head_dir(struct job_details *job)
{
    int  buflen;
    char buf[BUFFERSIZE];
    int  fd;
    char curr_time_str[LINE_SIZE];
    time_t curr_time;

    my_printf("%s \n", __FUNCTION__);
    curr_time = time(NULL);
    strftime(curr_time_str, sizeof(curr_time_str), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&curr_time));
    buf[0] = '\0';
    fd = open(job->filename, O_RDONLY);
    if (fd < 0) {
        my_printf("%s: open(%s) failed\n", __FUNCTION__, job->filename);
        job->response_status = RESPERR;
        sprintf(buf, HTMLERR_STR, curr_time_str, "", 0);
        send_response(job, buf, strlen(buf));
        return;
    }
    close(fd);
    job->response_status = RESPOK;
    /* send the header */
    if (job->image) {
        sprintf(buf, IMAGEOK_STR, curr_time_str, job->lastmodtime, job->response_size);
        send_response(job, buf, strlen(buf));
    } else {
        sprintf(buf, HTMLOK_STR, curr_time_str, job->lastmodtime, job->response_size );
        send_response(job, buf, strlen(buf));
    }
}

static void
send_get_dir (struct job_details *job)
{
    DIR *dp;
    struct dirent *dirp;
    int index_file_found = 0;
    int fd, buflen;
    char buf[BUFFERSIZE];
    char curr_time_str[LINE_SIZE];
    time_t curr_time;

    my_printf("%s \n", __FUNCTION__);
    curr_time = time(NULL);
    strftime(curr_time_str, sizeof(curr_time_str), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&curr_time));
    buf[0] = '\0';
    dp = opendir(job->filename);
    if (dp == NULL) {
        my_printf("%s: opendir(%s) failed \n", __FUNCTION__, job->filename);
        job->response_status = RESPERR;
        sprintf(buf, HTMLERR_STR, curr_time_str, "", 0);
        send_response(job, buf, strlen(buf));
        return;
    }
    if (dp) {
        while ((dirp = readdir(dp)) != NULL) {
            if (strcmp(dirp->d_name, "index.html") == 0) {
                index_file_found = 1;
                break;
            }
        }
        closedir(dp);
    }
    
    /* send header */
    sprintf(buf, HTMLOK_STR, curr_time_str, job->lastmodtime, job->response_size);
    send_response(job, buf, strlen(buf));

    /* send contents of index file if found */
    if (index_file_found) {
        job->response_status = RESPOK;

        /* send the content */
        fd = open(job->filename, O_RDONLY);
        buflen = 1;
        while (1) {
            memset((char *)buf, 0, sizeof(buf));
            buflen = read(fd, buf, sizeof(buf));
            if (buflen > 0) {
                send_response(job, buf, buflen);
            } else {
                break;
            }
        }
        close(fd);
    } else {
        /* send list of files */
        dp = opendir(job->filename);
        if (dp) {
            memset((char *)buf, 0, sizeof(buf));
            while ((dirp = readdir(dp)) != NULL) {
                if (dirp->d_name[0] != '.') {
                    if ((strlen(buf) + strlen(dirp->d_name)) > sizeof(buf)) {
                        send_response(job, buf, strlen(buf));
                        memset((char *)buf, 0, sizeof(buf));
                    }
                    strcat(buf, dirp->d_name);
                    buf[strlen(buf)] = '\n';
                }
            }
            if (strlen(buf)) {
                send_response(job, buf, strlen(buf));
            }
            closedir(dp);
        }
    }
}

/* return the header */
static void
send_head_file (struct job_details *job)
{
    int  buflen;
    char buf[BUFFERSIZE];
    int  fd;
    char curr_time_str[LINE_SIZE];
    time_t curr_time;

    my_printf("%s \n", __FUNCTION__);
    curr_time = time(NULL);
    strftime(curr_time_str, sizeof(curr_time_str), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&curr_time));
    buf[0] = '\0';
    fd = open(job->filename, O_RDONLY);
    if (fd < 0) {
        my_printf("%s: open(%s) failed\n", __FUNCTION__, job->filename);
        job->response_status = RESPERR;
        sprintf(buf, HTMLERR_STR, curr_time_str, "", 0);
        send_response(job, buf, strlen(buf));
        return;
    }
    close(fd);
    job->response_status = RESPOK;
    /* send the header */
    if (job->image) {
        sprintf(buf, IMAGEOK_STR, curr_time_str, job->lastmodtime, job->response_size);
        send_response(job, buf, strlen(buf));
    } else {
        sprintf(buf, HTMLOK_STR, curr_time_str, job->lastmodtime, job->response_size );
        send_response(job, buf, strlen(buf));
    }
}

static void
send_get_file (struct job_details *job)
{
    int fd, buflen;
    char buf[BUFFERSIZE];
    char curr_time_str[LINE_SIZE];
    time_t curr_time;

    my_printf("%s \n", __FUNCTION__);
    curr_time = time(NULL);
    strftime(curr_time_str, sizeof(curr_time_str), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&curr_time));
    buf[0] = '\0';
    fd = open(job->filename, O_RDONLY);
    if (fd < 0) {
        my_printf("%s: open(%s) failed\n", __FUNCTION__, job->filename);
        job->response_status = RESPERR;
        sprintf(buf, HTMLERR_STR, curr_time_str, "", 0);
        send_response(job, buf, strlen(buf));
        return;
    }
    job->response_status = RESPOK;
    /* send the header */
    if (job->image) {
        sprintf(buf, IMAGEOK_STR, curr_time_str, job->lastmodtime, job->response_size);
        send_response(job, buf, strlen(buf));
    } else {
        sprintf(buf, HTMLOK_STR, curr_time_str, job->lastmodtime, job->response_size );
        send_response(job, buf, strlen(buf));
    }
    /* send the content */
    while (1) {
        memset((char *)buf, 0, sizeof(buf));
        buflen = read(fd, buf, sizeof(buf));
        if (buflen > 0) {
            send_response(job, buf, buflen);
        } else {
            break;
        }
    }
    close(fd);
}

static void
process_job (struct job_details *job)
{
    time_t curr_time;

    curr_time = time(NULL);
    strftime(job->scheduled_time, sizeof(job->scheduled_time), "%d/%b/%Y:%H:%M:%S %z", gmtime(&curr_time));
    if (job->req_type == REQ_HEAD) {
        if (job->content_type == CONTENT_FILE) {
            send_head_file(job);
        } else if (job->content_type == CONTENT_DIR) {
            send_head_dir(job);
        }
    }
    if (job->req_type == REQ_GET) {
        if (job->content_type == CONTENT_FILE) {
            send_get_file(job);
        } else if (job->content_type == CONTENT_DIR) {
            send_get_dir(job);
        }
    }
}

void *
worker_thread (void *t)
{
    struct job_details *job;
    int tno = (int)t;

    tno += 1; /* make it 1-based */
    while (1) {
        my_printf("Worker thread %d waiting for job\n", tno);
        job = from_worker_thread();
        my_printf("%s: thread %d working on job %d\n", __FUNCTION__, tno, job->seqno);
        process_job(job);
        if (logging_fp) {
            fprintf(logging_fp, "%s - [%s] [%s] \"%s\" %d %d\n", job->remote_ip,
                    job->arrival_time, job->scheduled_time, job->first_line,
                    job->response_status, job->response_size);
            fflush(logging_fp);
        }
        my_printf("%s - [%s] [%s] \"%s\" %d %d\n", job->remote_ip,
                   job->arrival_time, job->scheduled_time, job->first_line,
                   job->response_status, job->response_size);
        close(job->client_soc);
        free(job);
    }
    pthread_exit(NULL);
}