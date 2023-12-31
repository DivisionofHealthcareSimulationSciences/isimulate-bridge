/*
    based on avahi example client-browse-services.c 
*/
#include "service_discovery.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>

static AvahiSimplePoll *simple_poll = NULL;

char monitor_address[30];
uint16_t monitor_port = 0;
bool monitor_service_new = false;

static void resolve_callback(
    AvahiServiceResolver *r,
    AVAHI_GCC_UNUSED AvahiIfIndex interface,
    AVAHI_GCC_UNUSED AvahiProtocol protocol,
    AvahiResolverEvent event,
    const char *name,
    const char *type,
    const char *domain,
    const char *host_name,
    const AvahiAddress *address,
    uint16_t port,
    AvahiStringList *txt,
    AvahiLookupResultFlags flags,
    AVAHI_GCC_UNUSED void* userdata) {
    assert(r);
    /* Called whenever a service has been resolved successfully or timed out */
    switch (event) {
        case AVAHI_RESOLVER_FAILURE:
            fprintf(stderr, "(Resolver) Failed to resolve service '%s' of type '%s' in domain '%s': %s\n", name, type, domain, avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r))));
            break;
        case AVAHI_RESOLVER_FOUND: {
            char a[AVAHI_ADDRESS_STR_MAX], *t;
            fprintf(stderr, "Service '%s' of type '%s' in domain '%s':\n", name, type, domain);
            avahi_address_snprint(a, sizeof(a), address);
            t = avahi_string_list_to_string(txt);
            fprintf(stderr,
                    "\t%s:%u (%s)\n"
                    // "\tTXT=%s\n"
                    // "\tcookie is %u\n"
                    // "\tis_local: %i\n"
                    // "\tour_own: %i\n"
                    // "\twide_area: %i\n"
                    // "\tmulticast: %i\n"
                    // "\tcached: %i\n"
                    , host_name, port, a);
                    //, t,
                    // avahi_string_list_get_service_cookie(txt),
                    // !!(flags & AVAHI_LOOKUP_RESULT_LOCAL),
                    // !!(flags & AVAHI_LOOKUP_RESULT_OUR_OWN),
                    // !!(flags & AVAHI_LOOKUP_RESULT_WIDE_AREA),
                    // !!(flags & AVAHI_LOOKUP_RESULT_MULTICAST),
                    // !!(flags & AVAHI_LOOKUP_RESULT_CACHED));
            monitor_service_new = true;
            monitor_port = port;
            strcpy(monitor_address, a);
            avahi_free(t);
        }
    }
    // Do not destroy resolver. Resolver will be refired if txt record updates
    //avahi_service_resolver_free(r);
}

static void browse_callback(
    AvahiServiceBrowser *b,
    AvahiIfIndex interface,
    AvahiProtocol protocol,
    AvahiBrowserEvent event,
    const char *name,
    const char *type,
    const char *domain,
    AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
    void* userdata) {
    AvahiClient *c = userdata;
    assert(b);
    /* Called whenever a new services becomes available on the LAN or is removed from the LAN */
    switch (event) {
        case AVAHI_BROWSER_FAILURE:
            fprintf(stderr, "(Browser) %s\n", avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
            avahi_simple_poll_quit(simple_poll);
            return;
        case AVAHI_BROWSER_NEW:
            fprintf(stderr, "(Browser) NEW: service '%s' of type '%s' in domain '%s'\n", name, type, domain);
            /* We ignore the returned resolver object. In the callback
               function we free it. If the server is terminated before
               the callback function is called the server will free
               the resolver for us. */
            if (!(avahi_service_resolver_new(c, interface, protocol, name, type, domain, AVAHI_PROTO_UNSPEC, 0, resolve_callback, c)))
                fprintf(stderr, "Failed to resolve service '%s': %s\n", name, avahi_strerror(avahi_client_errno(c)));
            break;
        case AVAHI_BROWSER_REMOVE:
            fprintf(stderr, "(Browser) REMOVE: service '%s' of type '%s' in domain '%s'\n", name, type, domain);
            break;
        case AVAHI_BROWSER_ALL_FOR_NOW:
        case AVAHI_BROWSER_CACHE_EXHAUSTED:
            fprintf(stderr, "(Browser) %s\n", event == AVAHI_BROWSER_CACHE_EXHAUSTED ? "CACHE_EXHAUSTED" : "ALL_FOR_NOW");
            break;
    }
}

static void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata) {
    assert(c);
    /* Called whenever the client or server state changes */
    if (state == AVAHI_CLIENT_FAILURE) {
        fprintf(stderr, "Server connection failure: %s\n", avahi_strerror(avahi_client_errno(c)));
        avahi_simple_poll_quit(simple_poll);
    }
}

int service_discovery() {
    AvahiClient *client = NULL;
    AvahiServiceBrowser *sb = NULL;
    int error;
    int ret = 1;
    /* Allocate main loop object */
    if (!(simple_poll = avahi_simple_poll_new())) {
        fprintf(stderr, "Failed to create simple poll object.\n");
        goto fail;
    }
    /* Allocate a new client */
    client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0, client_callback, NULL, &error);
    /* Check wether creating the client object succeeded */
    if (!client) {
        fprintf(stderr, "Failed to create client: %s\n", avahi_strerror(error));
        goto fail;
    }
    /* Create the service browser */
    if (!(sb = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, "_realiti_v1._tcp", NULL, 0, browse_callback, client))) {
        fprintf(stderr, "Failed to create service browser: %s\n", avahi_strerror(avahi_client_errno(client)));
        goto fail;
    }
    /* Run the main loop */
    avahi_simple_poll_loop(simple_poll);
    ret = 0;
fail:
    /* Cleanup things */
    if (sb)
        avahi_service_browser_free(sb);
    if (client)
        avahi_client_free(client);
    if (simple_poll)
        avahi_simple_poll_free(simple_poll);
    return ret;
}
