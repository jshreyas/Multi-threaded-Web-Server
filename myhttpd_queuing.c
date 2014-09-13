/*
 * CSE 421 / 521 - Operating Systems
 * myhttpd: A simple multi-threaded web server
 * Author: Shreyas Jukanti
 * Date: 3/Nov/'13
 */

#include "myhttpd.h"

/*
 * Open socket, bind etc. to accept requests from clients
 */
static int
open_communication (void)
{
    int soc;
    struct sockaddr_in addr;

    if ((soc = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
        my_printf("%s: socket failed %s\n", __FUNCTION__, strerror(errno));
        return -1;
    }

    my_printf("%s: Socket created\n", __FUNCTION__);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listening_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(soc, (struct sockaddr *)&addr, sizeof(addr)) < 0){
        my_printf("%s: bind failed %s\n", __FUNCTION__, strerror(errno));
        close(soc);
        return -1;
    }
    my_printf("%s: Socket bound to port %d\n ", __FUNCTION__, listening_port);
    if (listen(soc, MAX_CLIENTS) < 0){
        my_printf("%s: listen failed %s\n", __FUNCTION__, strerror(errno));
        close(soc);
        return -1;
    }
    my_printf("%s: Socket listened \n ", __FUNCTION__);
    return soc;
}

/* add the new job request to the head of the list */
static void
to_sched_queue (struct job_details *job)
{
    pthread_mutex_lock(&sched_mutex);
    my_printf("%s: queuing job %d\n", __FUNCTION__, job->seqno);
    if (job_head1) {
        job->next1 = job_head1;
        job_head1->prev1 = job;
    }
    job_head1 = job;
    if (job_tail1 == NULL) {
        job_tail1 = job;
    }
    sem_post(&sched_sem);
    pthread_cond_signal(&sched_cond_mutex);
    pthread_mutex_unlock(&sched_mutex);
}


void *
queue_thread (void)
{
    int ssoc, csoc;
    struct sockaddr_in caddr;
    int clen;
    struct job_details *job;
    char buffer[BUFFERSIZE], buflen;
    char method[BUFFERSIZE], uri[BUFFERSIZE], version[BUFFERSIZE];
    char filename[BUFFERSIZE];
    struct stat stat_buf;
    time_t curr_time;

    my_printf("%s: \n", __FUNCTION__);
    if ((ssoc = open_communication()) < 0) {
        my_printf("%s: cannot open communication to clients\n", __FUNCTION__);
        exit(1);
    }

    /* wait for requests and process them accordingly */
    while (1) {
        my_printf("waiting to accept connections from client\n");
        clen = sizeof(caddr);
        memset((char *)&caddr, 0, sizeof(caddr));
        csoc = accept(ssoc, (struct sockaddr *)&caddr, &clen);
        if (csoc < 0) {
            my_printf("%s: accept failed\n", __FUNCTION__);
            continue;
        }
        my_printf("%s: got a request from client soc %d\n", __FUNCTION__, csoc);
        if ((buflen = recv(csoc, buffer, BUFFERSIZE, 0)) < 0) {
            my_printf("%s: recv failed\n", __FUNCTION__);
            close(csoc);
            continue;
        }
        my_printf("Req: %s\n", buffer);
        if (strstr(buffer, "favicon.ico")) {
            my_printf("%s: bad request %s \n", __FUNCTION__, method);
            close(csoc);
            continue;
        }

        sscanf(buffer, "%s %s %s", method, uri, version);
        /* check if it is http 1.0 request */
        if ((strcmp(version, "HTTP/1.0") != 0) && (strcmp(version, "HTTP/1.1") != 0)) {
            my_printf("%s: bad request %s \n", __FUNCTION__, method);
            close(csoc);
            continue;
        }
        /* check if it is GET or HEAD */
        my_printf("%s: method '%s' uri '%s'\n", __FUNCTION__, method, uri);
        if (strcmp(method, "GET") && strcmp(method, "HEAD")) {
            my_printf("%s: bad request %s \n", __FUNCTION__, method);
            close(csoc);
            continue;
        }

        /* allocate an instance of job */
        job = (struct job_details *)malloc(sizeof(struct job_details));
        if (job == NULL) {
            my_printf("%s: malloc failed\n", __FUNCTION__);
            continue;
        }
        memset((char *)job, 0, sizeof(struct job_details));

        /* parse in case the request starts with ~ and replace it with /home/<>/myhttpd */
        if (uri[0] == '~') {
            char *s;
            s = strstr(uri, "/");
            if (s) {
                *s = '\0';
                sprintf(filename, "/home/%s/myhttpd/%s", &uri[1], s+1); 
            } else {
                sprintf(filename, "/home/%s/myhttpd/", &uri[1]); 
            }
            sprintf(job->filename, "%s", filename);
        } else {
            strcpy(filename, uri);
        }
        sprintf(job->filename, "%s/%s", root_directory, filename);
        myhttpd_seqno += 1;
        job->seqno = myhttpd_seqno;
        curr_time = time(NULL);
        strftime(job->arrival_time, sizeof(job->arrival_time), "%d/%b/%Y:%H:%M:%S %z", gmtime(&curr_time));
        strncpy(job->first_line, buffer, sizeof(job->first_line));
        strcpy(job->remote_ip, inet_ntoa(caddr.sin_addr));
        if (strstr(job->filename, ".jpg") || strstr(job->filename, ".gif") || strstr(job->filename, ".png")) {
            job->image = 1;
        }
        job->client_soc = csoc;
        /* get the relavent infromation for sched policy decision */
        memset((char *)&stat_buf, 0, sizeof(stat_buf));
        my_printf("stat ing %s \n", job->filename);
        stat(job->filename, &stat_buf);
        strftime(job->lastmodtime, sizeof(job->lastmodtime), "%a, %d %b %Y %H:%M:%S GMT",
                 gmtime(&stat_buf.st_mtime));
        if (S_ISREG(stat_buf.st_mode)) {
            job->response_size = stat_buf.st_size;
            job->content_type = CONTENT_FILE;
        } else if (S_ISDIR(stat_buf.st_mode)) {
            job->response_size = 0;
            job->content_type = CONTENT_DIR;
        }

        if (strcasecmp(method, "GET") == 0) { 
            job->req_type = REQ_GET;
        } else if (strcasecmp(method, "HEAD") == 0) {
            job->response_size = 0;
            job->req_type = REQ_HEAD;
        }

        my_printf("%s: request from %s \n", __FUNCTION__, job->remote_ip);

        /* add this job to the end of list */
        to_sched_queue(job);
    }
    pthread_exit(NULL);
}