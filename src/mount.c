/*
 * Get root filehandles from NFS server
 */


#include "nfsping.h"
#include "rpc.h"
#include "util.h"

/* local prototypes */
static void usage(void);
static void mount_perror(mountstat3);
static mountres3 *get_root_filehandle(CLIENT *, char *, char *, unsigned long *);
static int print_exports(char *, struct exportnode *);

/* globals */
int verbose = 0;


void usage() {
    printf("Usage: nfsmount [options] host[:mountpoint]\n\
    -e       print exports (like showmount -e)\n\
    -h       display this help and exit\n\
    -l       loop forever\n\
    -m       use multiple target IP addresses if found\n\
    -S addr  set source address\n\
    -T       use TCP (default UDP)\n\
    -v       verbose output\n");

    exit(3);
}


/* print an error message for mount results */
/* for now just print the error name from the header */
void mount_perror(mountstat3 fhs_status) {
    static const char *labels[] = {
        [MNT3ERR_NOENT]       = "MNT3ERR_NOENT",
        [MNT3ERR_ACCES]       = "MNT3ERR_ACCES",
        [MNT3ERR_NOTDIR]      = "MNT3ERR_NOTDIR",
        [MNT3ERR_INVAL]       = "MNT3ERR_INVAL",
        [MNT3ERR_NAMETOOLONG] = "MNT3ERR_NAMETOOLONG",
        [MNT3ERR_NOTSUPP]     = "MNT3ERR_NOTSUPP",
        [MNT3ERR_SERVERFAULT] = "MNT3ERR_SERVERFAULT",
    };

    if (fhs_status && fhs_status != MNT3_OK) {
        fprintf(stderr, "%s\n", labels[fhs_status]);
    }
}


/* get the root filehandle from the server */
/* take a pointer to usec so we can return the elapsed call time */
mountres3 *get_root_filehandle(CLIENT *client, char *hostname, char *path, unsigned long *usec) {
    struct rpc_err clnt_err;
    mountres3 *mountres = NULL;
    struct timespec call_start, call_end, call_elapsed;

    if (path[0] == '/') {
        if (client) {
            /* first time marker */
#ifdef CLOCK_MONOTONIC_RAW
            clock_gettime(CLOCK_MONOTONIC_RAW, &call_start);
#else
            clock_gettime(CLOCK_MONOTONIC, &call_start);
#endif

            /* the actual RPC call */
            mountres = mountproc_mnt_3(&path, client);

            /* second time marker */
#ifdef CLOCK_MONOTONIC_RAW
            clock_gettime(CLOCK_MONOTONIC_RAW, &call_end);
#else
            clock_gettime(CLOCK_MONOTONIC, &call_end);
#endif

            /* calculate elapsed microseconds */
            timespecsub(&call_end, &call_start, &call_elapsed);
            *usec = ts2us(call_elapsed);
        }

        if (mountres) {
            if (mountres->fhs_status != MNT3_OK) {
                fprintf(stderr, "%s:%s: ", hostname, path);
                /* check if we get an access error, this probably means the server wants us to use a reserved port */
                /* TODO do this check in mount_perror? */
                if (mountres->fhs_status == MNT3ERR_ACCES && geteuid()) {
                    fprintf(stderr, "Unable to mount filesystem, consider running as root\n");
                } else {
                    mount_perror(mountres->fhs_status);
                }
            }
        /* RPC error */
        } else {
            clnt_geterr(client, &clnt_err);
            if  (clnt_err.re_status) {
                fprintf(stderr, "%s:%s: ", hostname, path);
                clnt_perror(client, "mountproc_mnt_3");
            }
        }
    } else {
        fprintf(stderr, "%s: Invalid path: %s\n", hostname, path);
        /* create an empty result */
        mountres = malloc(sizeof(mountres));
        mountres->fhs_status = MNT3ERR_INVAL;
    }

    return mountres;
}


/* print a list of exports, like showmount -e */
int print_exports(char *host, struct exportnode *ex) {
    int i = 0;
    size_t max = 0;
    exports first = ex;
    groups gr;

    while (ex) {
        i++;
        if (strlen(ex->ex_dir) > max) {
            max = strlen(ex->ex_dir);
        }

        ex = ex->ex_next;
    }

    ex = first;
    max++; /* spacing */

    while (ex) {
        printf("%s:%-*s", host, (int)max, ex->ex_dir);
        gr = ex->ex_groups;
        if (gr) {
            printf("%s", gr->gr_name);
            gr = gr->gr_next;
            while (gr) {
                printf(",%s", gr->gr_name);
                gr = gr->gr_next;
            }
        } else {
            /* no groups means open to everyone */
            printf("(everyone)");
        }
        printf("\n");
        ex = ex->ex_next;
    }

    return i;
}


int main(int argc, char **argv) {
    mountres3 *mountres;
    struct addrinfo hints = {
        .ai_family = AF_INET,
        /* default to UDP */
        .ai_socktype = SOCK_DGRAM
    };
    char *host;
    char *path;
    exports ex;
    targets_t *targets;
    targets_t *current;
    targets_t target_dummy;
    int exports_count = 0, exports_ok = 0;
    int ch;
    /* command line options */
    uint16_t port = 0; /* 0 = use portmapper */
    int dns = 0, ip = 0;
    int loop = 0;
    int multiple = 0, showmount = 0;
    u_long version = 3;
    struct timeval timeout = NFS_TIMEOUT;
    /* source ip address for packets */
    struct sockaddr_in src_ip = {
        .sin_family = AF_INET,
        .sin_addr = 0
    };
    unsigned long usec;
    struct timespec wall_clock;
    JSON_Object *json;

    /* no arguments passed */
    if (argc == 1)
        usage();

    while ((ch = getopt(argc, argv, "ehlmS:Tv")) != -1) {
        switch(ch) {
            /* output like showmount -e */
            case 'e':
                showmount = 1;
                break;
            case 'l':
                loop = 1;
                break;
            /* specify source address */
            case 'S':
                if (inet_pton(AF_INET, optarg, &src_ip.sin_addr) != 1) {
                    fatal("Invalid source IP address!\n");
                }
                break;
            /* use multiple IP addresses if found */
            /* TODO in this case do we also want to default to showing IP addresses instead of names? */
            case 'm':
                multiple = 1;
                break;
            /* use TCP */
            case 'T':
                hints.ai_socktype = SOCK_STREAM;
                break;
            /* verbose */
            case 'v':
                verbose = 1;
                break;
            case 'h':
            case '?':
            default:
                usage();
        }
    }

    /* pointer to head of list */
    current = &target_dummy;
    targets = current;

    /* loop through arguments and create targets */
    while (optind < argc) {
        /* split host:path arguments, path is optional */
        host = strtok(argv[optind], ":");
        path = strtok(NULL, ":");

        current->next = make_target(host, &hints, port, dns, ip, multiple);
        current = current->next;
        current->path = path;

        optind++;
    }

    /* skip the first dummy entry */
    targets = targets->next;

    while(1) {
        /* reset to head of list */
        current = targets;

        while(current) {
            /* create an rpc connection */
            current->client = create_rpc_client(current->client_sock, &hints, MOUNTPROG, version, timeout, src_ip);
            /* mounts don't need authentication because they return a list of authentication flavours supported so leave it as default (AUTH_NONE) */

            if (current->client) {
                if (current->path && !showmount) {
                    exports_count++;

                    /* get the current timestamp */
                    clock_gettime(CLOCK_REALTIME, &wall_clock);

                    mountres = get_root_filehandle(current->client, current->name, current->path, &usec);

                    if (mountres && mountres->fhs_status == MNT3_OK) {
                        exports_ok++;

                        json = json_value_get_object(current->json_root);

                        json_object_set_number(json, "timestamp", wall_clock.tv_sec);

                        /* print the filehandle in hex */
                        print_fhandle3(current->json_root, current->client_sock, current->path, mountres->mountres3_u.mountinfo.fhandle, usec, wall_clock);
                    }
                } else {
                    /* get the list of all exported filesystems from the server */
                    ex = *mountproc_export_3(NULL, current->client);

                    if (ex) {
                        if (showmount) {
                            exports_count = print_exports(current->name, ex);
                            /* if the call succeeds at all it can't return individual bad results */
                            exports_ok = exports_count;
                        } else {
                            while (ex) {
                                exports_count++;

                                /* get the current timestamp */
                                clock_gettime(CLOCK_REALTIME, &wall_clock);
                                
                                mountres = get_root_filehandle(current->client, current->path, ex->ex_dir, &usec);

                                if (mountres && mountres->fhs_status == MNT3_OK) {
                                    exports_ok++;
                                    json = json_value_get_object(current->json_root);
                                    json_object_set_number(json, "timestamp", wall_clock.tv_sec);
                                    /* print the filehandle in hex */
                                    print_fhandle3(current->json_root, current->client_sock, ex->ex_dir, mountres->mountres3_u.mountinfo.fhandle, usec, wall_clock);
                                }
                                ex = ex->ex_next;
                            }
                        }
                    }
                }
            }
            current = current->next;
        } /* while(current) */

        if (loop == 0) {
            break;
        }
    } /* while(1) */

    if (exports_count && exports_count == exports_ok) {
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}
