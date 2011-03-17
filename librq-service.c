// librq-service
// RISP-based queue system

#include "rq-service.h"


#include <assert.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>


#if (LIBRQ_SERVICE_VERSION != 0x00010000)
#error "Incorrect rq-service.h header version."
#endif


// RQ services use have some default parameters that are setup.  These constants are there to be consistant.
#define DEFAULT_OPT_DAEMON     'D'
#define DEFAULT_OPT_IMPORT     'X'
#define DEFAULT_OPT_CONTROLLER 'C'
#define DEFAULT_OPT_PID        'P'
#define DEFAULT_OPT_USER       'U'
#define DEFAULT_OPT_VERBOSE    'V'
#define DEFAULT_OPT_HELP       'h'


//-----------------------------------------------------------------------------
// Since we will be limiting the number of connections we have, we will want to 
// make sure that the required number of file handles are avaialable.  For a 
// 'server', the default number of file handles per process might not be 
// 'enough, and this function will attempt to increase them, if necessary.
void rq_set_maxconns(int maxconns) 
{
	struct rlimit rlim;
	
	assert(maxconns > 5);

	if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
		fprintf(stderr, "failed to getrlimit number of files\n");
		exit(1);
	} else {
	
		// we need to allocate twice as many handles because we may be receiving data from a file for each node.
		if (rlim.rlim_cur < maxconns)
			rlim.rlim_cur = (2 * maxconns) + 3;
			
		if (rlim.rlim_max < rlim.rlim_cur)
			rlim.rlim_max = rlim.rlim_cur;
		if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
			fprintf(stderr, "failed to set rlimit for open files. Try running as root or requesting smaller maxconns value.\n");
			exit(1);
		}
	}
}

void rq_daemon(const char *username, const char *pidfile, const int noclose)
{
	struct passwd *pw;
	struct sigaction sa;
	int fd;
	FILE *fp;
	
	if (username != NULL) {
		assert(username[0] != 0);
		if (getuid() == 0 || geteuid() == 0) {
			if (username == 0 || *username == 0) {
				fprintf(stderr, "can't run as root without the -u switch\n");
				exit(EXIT_FAILURE);
			}
			pw = getpwnam((const char *)username);
			if (pw == NULL) {
				fprintf(stderr, "can't find the user %s to switch to\n", username);
				exit(EXIT_FAILURE);
			}
			if (setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0) {
				fprintf(stderr, "failed to assume identity of user %s\n", username);
				exit(EXIT_FAILURE);
			}
		}
	}

	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	if (sigemptyset(&sa.sa_mask) == -1 || sigaction(SIGPIPE, &sa, 0) == -1) {
		perror("failed to ignore SIGPIPE; sigaction");
		exit(EXIT_FAILURE);
	}

	switch (fork()) {
		case -1:
			exit(EXIT_FAILURE);
		case 0:
			break;
		default:
			_exit(EXIT_SUCCESS);
	}

	if (setsid() == -1)
			exit(EXIT_FAILURE);

	(void)chdir("/");

	if (noclose == 0 && (fd = open("/dev/null", O_RDWR, 0)) != -1) {
		(void)dup2(fd, STDIN_FILENO);
		(void)dup2(fd, STDOUT_FILENO);
		(void)dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO)
			(void)close(fd);
	}

	// save the PID in if we're a daemon, do this after thread_init due to a
	// file descriptor handling bug somewhere in libevent
	if (pidfile != NULL) {
		if ((fp = fopen(pidfile, "w")) == NULL) {
			fprintf(stderr, "Could not open the pid file %s for writing\n", pidfile);
			exit(EXIT_FAILURE);
		}

		fprintf(fp,"%ld\n", (long)getpid());
		if (fclose(fp) == -1) {
			fprintf(stderr, "Could not close the pid file %s.\n", pidfile);
			exit(EXIT_FAILURE);
		}
	}
}


//-----------------------------------------------------------------------------
// Given an address structure, will create a socket handle and set it for 
// non-blocking mode.
int rq_new_socket(struct addrinfo *ai) {
	int sfd = INVALID_HANDLE;
	int flags;
	
	assert(ai != NULL);
	
	// bind the socket, and then set the socket to non-blocking mode.
	if ((sfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) >= 0) {
		// TODO: use libevent non-blocking function instead.
		if ((flags = fcntl(sfd, F_GETFL, 0)) < 0 || fcntl(sfd, F_SETFL, flags | O_NONBLOCK) < 0) {
			perror("setting O_NONBLOCK");
			close(sfd);
			sfd = INVALID_HANDLE;
		}
	}
	
	return sfd;
}



///----------------------------------------------------------------------------
/// Service management.
///----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Initialise a new rq_service_t object and return a pointer to it.  It should be cleaned up with rq_service_cleanup()
rq_service_t *rq_svc_new(void)
{
	rq_service_t *service;
	int i;

	service = (rq_service_t *) malloc(sizeof(rq_service_t));
	
	service->verbose = 0;

	service->rq = (rq_t *) malloc(sizeof(rq_t));
	rq_init(service->rq);
	assert(service->rq->evbase == NULL);

	for (i=0; i<RQ_MAX_HELPOPTIONS; i++) {
		service->help_options[i] = NULL;
	}

	service->svcname = NULL;
	rq_svc_setoption(service, DEFAULT_OPT_IMPORT,     "filename",         "Config file which contains parameters");
	rq_svc_setoption(service, DEFAULT_OPT_CONTROLLER, "ip:port,ip:port",  "Controllers to connect to.");
	rq_svc_setoption(service, DEFAULT_OPT_DAEMON,     NULL,               "Run as a daemon");
	rq_svc_setoption(service, DEFAULT_OPT_PID,        "file",             "save PID in <file>, only used with -D option");
	rq_svc_setoption(service, DEFAULT_OPT_USER,       "username",         "assume identity of <username> (only when run as root)");
	rq_svc_setoption(service, DEFAULT_OPT_VERBOSE,    NULL,               "verbose (print errors/warnings to stdout)");
	rq_svc_setoption(service, DEFAULT_OPT_HELP,       NULL,               "print this help and exit");

	return(service);
}

//-----------------------------------------------------------------------------
// Cleanup the service object and free its resources and itself.
void rq_svc_cleanup(rq_service_t *service)
{
	rq_svc_helpoption_t *help;
	int i;
	
	assert(service);
	
	assert(service->rq);
	rq_cleanup(service->rq);
	free(service->rq);
	service->rq = NULL;

	// if we are in daemon mode, and a pidfile is provided, then we need to delete the pidfile
	assert(service->help_options[DEFAULT_OPT_DAEMON]);
	assert(service->help_options[DEFAULT_OPT_PID]);
	if (service->help_options[DEFAULT_OPT_DAEMON]->count && service->help_options[DEFAULT_OPT_PID]->value) {
		assert(service->help_options[DEFAULT_OPT_PID]->value[0] != 0);
		unlink(service->help_options[DEFAULT_OPT_PID]->value);
	}

	// free all the memory used by the help-options, command-line values.
	for (i=0; i<RQ_MAX_HELPOPTIONS; i++) {
		if (service->help_options[i]) {
			help = service->help_options[i];
			if (help->value)   free(help->value);
			free(help);
		}
	}

	assert(service);
	service->svcname = NULL;

	free(service);
}


void rq_svc_setname(rq_service_t *service, const char *name)
{
	assert(service);
	assert(name);

	assert(service->svcname == NULL);
	service->svcname = (char *) name;
}


// add a help option to the list.
void rq_svc_setoption(rq_service_t *service, char tag, const char *param, const char *details)
{
	rq_svc_helpoption_t *help;

	assert(service);
	assert(details);
	assert(tag > 0 && tag < RQ_MAX_HELPOPTIONS);
	assert(service->help_options[(int)tag] == NULL);
		
	help = calloc(1, sizeof(*help));
	help->param = (char *) param;
	help->details = (char *) details;
	help->value = NULL;
	help->count = 0;

	service->help_options[(int)tag] = help;
}


static void rq_svc_usage(rq_service_t *service)
{
	int i;
	int largest;
	int len;
	rq_svc_helpoption_t *entry;
	
	assert(service);

	// go through the options list and determine the largest param field.
	largest = 0;
	for (i=0; i<RQ_MAX_HELPOPTIONS; i++) {
		entry = service->help_options[i];
		if (entry) {
			if (entry->param) {
				len = strlen(entry->param);
				if (len > largest) { largest = len; }
			}
		}
	}

	// if we have any options with parameters, then we need to account for the <> that we will be putting around them.
	if (largest > 0) {
		largest += 2;
	}

	// now go through the list and display the info.
	printf("Usage:\n");
	for (i=0; i<RQ_MAX_HELPOPTIONS; i++) {
		entry = service->help_options[i];
		if (entry) {
			assert(entry->details);
			if (largest == 0) {
				printf(" -%c %s\n", i, entry->details);
			}
			else {

			/// TODO: Need to use the 'largest' to space out the detail from the param.
			
				if (entry->param) {
					printf(" -%c <%s> %s\n", i, entry->param, entry->details);
				}
				else {
					printf(" -%c %s %s\n", i, "", entry->details);
				}
			}
		}
	}
}


static void load_param_file(rq_service_t *service, const char *filename) 
{
	FILE *fp;
	char buffer[1024];
	int len;
	int tok;
	rq_svc_helpoption_t *entry;
	int c;
	
	
	assert(service && filename);
	
	// open the file.
	fp = fopen(filename, "r");
	if (fp) {
		
		// read in each line at a time.
		while (fgets(buffer, 1024, fp)) {
			
			tok = 0;
			len = strlen(buffer);

			// skip spaces and tabs.
			while (buffer[tok] == ' ' || buffer[tok] == '\t') { tok ++; }

			if (buffer[tok] != '#' && buffer[tok] != '\n' && buffer[tok] != 0) {
				assert(tok < len);
				
				c = buffer[tok];
				tok++;

				// check the first char of the line.
				if ((entry = service->help_options[c])) {
					if (entry->param) {

						// skip spaces and tabs.
						while (buffer[tok] == ' ' || buffer[tok] == '\t') { tok ++; }
						
						// take param as rest of the line.  (trim off linefeeds and carriage returns).
						assert(tok < len);
						while (len > tok && (buffer[len-1] == '\n' || buffer[len-1] == '\r')) { len --; }
						assert(len > tok);
						buffer[len] = 0;
						
						if (c == DEFAULT_OPT_IMPORT) {
							// if we got the X param, then it means that we need to 
							// load the contents of the file, and parse them as if 
							// they were command line parameters.
							if (strcmp(&buffer[tok], filename) != 0) {
								load_param_file(service, &buffer[tok]); 
							}
						}
						else {
							assert(entry->count == 0);
							if (entry->value) { free(entry->value); }
							entry->value = strdup(&buffer[tok]);
						}
					}
					else {
						entry->count ++;
					}
				}
				else {
					fprintf(stderr, "Illegal argument \"%c\" in config file: \"%s\"\n", c, filename);
					exit(EXIT_FAILURE);
				}
			}
		}
		
		// close file.
		fclose(fp);
	}
	else {
		fprintf(stderr, "Unable to open parameter file \"%s\"\n", filename);
		exit(EXIT_FAILURE);
	}
}

#define MAX_OPTSTR ((RQ_MAX_HELPOPTIONS) * 4)
void rq_svc_process_args(rq_service_t *service, int argc, char **argv)
{
	int c;
	rq_svc_helpoption_t *entry;
	int i;
	char optstr[MAX_OPTSTR + 1];
	int len;

	assert(service);
	assert(argc > 0);
	assert(argv);

	// build the getopt string.
	len = 0;
	for (i=0; i<RQ_MAX_HELPOPTIONS; i++) {
		entry = service->help_options[i];
		if (entry) {
			assert(entry->value == NULL);
			optstr[len++] = i;
			if (entry->param != NULL) {
				optstr[len++] = ':';
			}
		}
		assert(len < MAX_OPTSTR);
	}
	optstr[len++] = '\0';
	
	while (-1 != (c = getopt(argc, argv, optstr))) {
		if ((entry = service->help_options[c])) {
			if (entry->param) {
				
				if (c == DEFAULT_OPT_IMPORT) {
					// if we got the X param, then it means that we need to 
					// load the contents of the file, and parse them as if 
					// they were command line parameters.
					load_param_file(service, optarg); 
				}
				else {
					assert(entry->count == 0);
					
// 					if (entry->value) {
// 						printf("option %c: previous value: '%s', new value: '%s'\n", c, entry->value, optarg);
// 					}
					
					if (entry->value) { free(entry->value); }
					entry->value = strdup(optarg);
				}
			}
			else {
				entry->count ++;
			}
		}
		else {
			fprintf(stderr, "Illegal argument \"%c\"\n", c);
			exit(EXIT_FAILURE);
		}
	}

	// check for -h, display help
	assert(service->help_options[DEFAULT_OPT_HELP]);
	if (service->help_options[DEFAULT_OPT_HELP]->count > 0) {
		rq_svc_usage(service);
		exit(EXIT_SUCCESS);
	}

	// check for -v, for verbosity.
	assert(service->help_options[DEFAULT_OPT_VERBOSE]);
	assert(service->help_options[DEFAULT_OPT_VERBOSE]->value == NULL);
	assert(service->help_options[DEFAULT_OPT_VERBOSE]->param == NULL);
	service->verbose = service->help_options[DEFAULT_OPT_VERBOSE]->count;
	assert(service->verbose >= 0);
}
#undef MAX_OPTSTR


// Function is used to initialise a shutdown of the rq service.
void rq_svc_shutdown(rq_service_t *service)
{
	assert(service);

	assert(service->rq);
	rq_shutdown(service->rq);
}

//-----------------------------------------------------------------------------
// This function is used to initialise the service using the parameters that
// were supplied and already processed.   Therefore rq_svc_process_args()
// should be run before this function.  If the options state to be in daemon
// mode, it will fork and set the username it is running;
void rq_svc_initdaemon(rq_service_t *service)
{
	char *username;
	char *pidfile;
	int noclose;
	
	assert(service);
	assert(service->help_options[DEFAULT_OPT_DAEMON]);

	if (service->help_options[DEFAULT_OPT_DAEMON]->count > 0) {

		assert(service->help_options[DEFAULT_OPT_USER]);
		assert(service->help_options[DEFAULT_OPT_PID]);
		username = service->help_options[DEFAULT_OPT_USER]->value;
		pidfile = service->help_options[DEFAULT_OPT_PID]->value;
		noclose = service->verbose;
	
		// if we are supplied with a username, drop privs to it.  This will only
		// work if we are running as root, and is really only needed when running as 
		// a daemon.
		rq_daemon((const char *)username, (const char *)pidfile, noclose);
	}
}


void rq_svc_setevbase(rq_service_t *service, struct event_base *evbase)
{
	assert(service);
	assert(service->rq);

	rq_setevbase(service->rq, evbase);
}

//-----------------------------------------------------------------------------
// return the value of the stored option value.
char * rq_svc_getoption(rq_service_t *service, char tag)
{
	assert(service);
	assert(service->help_options[(int)tag]);
	return(service->help_options[(int)tag]->value);
}

//-----------------------------------------------------------------------------
// connect to the controllers that were specified in the controller parameter.
int rq_svc_connect(
	rq_service_t *service,
	void (*connect_handler)(rq_service_t *service, void *arg),
	void (*dropped_handler)(rq_service_t *service, void *arg),
	void *arg)
{
	char *str;
	char *copy;
	char *argument;
	char *next;
	
	assert(service);
	assert((arg != NULL && (connect_handler || dropped_handler)) || (arg == NULL));
	assert(service->rq);

	str = rq_svc_getoption(service, DEFAULT_OPT_CONTROLLER);
	if (str == NULL) {
		return -1;
	}	
	
// 	printf("Controller: %s\n", str);

	// make a copy of the supplied string, because we will be splitting it into
	// its key/value pairs. We dont want to mangle the string that was supplied.
	assert(str);
	copy = strdup(str);
	assert(copy);

	next = copy;
	while (next != NULL && *next != 0) {
		argument = strsep(&next, ",");
		if (argument) {
		
			// remove spaces from the begining of the key.
			while(*argument==' ' && *argument!='\0') { argument++; }
			
			if (strlen(argument) > 0) {
				rq_addcontroller(service->rq, argument, connect_handler, dropped_handler, arg);
			}
		}
	}
	
	free(copy);
	return 0;
}
