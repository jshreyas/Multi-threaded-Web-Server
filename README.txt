
Steps to follow:

Simply type "make" in the directory. A binary by name "myhttpd" will be created.
Execute it with the options mentioned in the project document.
"make clean" will clean up the dot o's and the binary

Assumptions:

The program assumes the current directory as the default root directory. 
Every file access(including the home directories) will be off this root directory. 
Regarding HEAD, currently a HTML OK or FAIL is returned with response size.

Design:

A "job_details" data structure is defined to have all the i
nformation regarding a job request.
The data structure has a pair of double link list of prev and next.
One set of prev and next is for queueing thread to schedular thread.
Another pair is for schedular thread to rearrange based on schedular policy.
Queuing thread will allocate  dynamic memory for the job.
Queuing thread always places the request on the head.
Schedular thread reads from the tail.
Schedular thread rearranges the jobs based on the schedular policy.
Worker thread reads from tail.
Mutex's are used to protect this common data structure across threads.
Semaphores are used to signal other threads thta a job is ready to be picked up.
Once a job is done, worker thread will close the client socket and frees up the memory associated with job.
