#ifndef __RQ_SERVICE_H
#define __RQ_SERVICE_H

#include <rq.h>


// This version indicates the version of the library so that developers of
// services can ensure that the correct version is installed.
// This version number should be incremented with every change that would
// effect logic.
#define LIBRQ_SERVICE_VERSION  0x00010000
#define LIBRQ_SERVICE_VERSION_NAME "v1.00.00"



typedef struct {
	char *param;
	char *details;
	char *value;
	int count;
} rq_svc_helpoption_t;

#define RQ_MAX_HELPOPTIONS 127

typedef struct {
	char *svcname;
	rq_t *rq;
	short verbose;
	rq_svc_helpoption_t *help_options[RQ_MAX_HELPOPTIONS];
} rq_service_t;


void rq_set_maxconns(int maxconns);
int  rq_new_socket(struct addrinfo *ai);
void rq_daemon(const char *username, const char *pidfile, const int noclose);


/*---------------------------------------------------------------------------*/
// Service control.  To make writing services for the RQ environment easier,
// we handle everything we can.



// Initialise a new rq_service_t object and return a pointer to it.  It should
// be cleaned up with rq_service_cleanup()
rq_service_t *rq_svc_new(void);

// Cleanup the service object and free its resources and itself.
void rq_svc_cleanup(rq_service_t *service);

void rq_svc_setname(rq_service_t *service, const char *name);
char * rq_svc_getoption(rq_service_t *service, char tag);
void   rq_svc_setoption(rq_service_t *service, char tag, const char *param, const char *details);

void rq_svc_process_args(rq_service_t *service, int argc, char **argv);

void rq_svc_shutdown(rq_service_t *service);

void rq_svc_initdaemon(rq_service_t *service);
void rq_svc_setevbase(rq_service_t *service, struct event_base *evbase);

int rq_svc_connect(
	rq_service_t *service,
	void (*connect_handler)(rq_service_t *service, void *arg),
	void (*dropped_handler)(rq_service_t *service, void *arg),
	void *arg);



#endif
