/*
 *  main.c
 *  sparkled
 *
 *  Created by Masatoshi Teruya on 13/02/09.
 *  Copyright 2013 Masatoshi Teruya. All rights reserved.
 *
 */
#include "sparkled.h"
#include <signal.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <ev.h>

#include "conf.h"
#include "backend.h"

typedef struct thd_t thd_t;

typedef struct {
    conf_t *cfg;
    backend_t *b;
    struct sockaddr info;
    socklen_t infolen;
    int sock;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    uint8_t nthd;
    thd_t **thds;
} sparkled_t;

struct thd_t {
    sparkled_t *s;
    pthread_t id;
    // event
    struct ev_loop *loop;
    ev_async term_ev;
    ev_io accept_ev;
};

typedef struct {
    thd_t *t;
    int fd;
    ev_io read_ev;
} cli_t;

#define fd_setnonblock(fd)({ \
    int rc = fcntl( fd, F_GETFD ); \
    if( rc != -1 ){ \
        rc |= O_NONBLOCK|FD_CLOEXEC; \
        rc = fcntl( fd, F_SETFD, rc ); \
    } \
    rc; \
})

#define fd_close(fd) (shutdown( fd, SHUT_RDWR ),close( fd ))

#define sock_settcp_nodelay(fd)({ \
    int opt = 1; \
    setsockopt( fd, IPPROTO_TCP, TCP_NODELAY, (void*)&opt, sizeof(int) ); \
})

#define BUF_MAX_LEN     8092

static void cli_shutdown( cli_t *c )
{
    ev_io_stop( c->t->loop, &c->read_ev );
    fd_close( c->fd );
    pdealloc( c );
}

static void cli_read( struct ev_loop *loop, ev_io *w, int event )
{
    #pragma unused(loop,event)
    cli_t *c = (cli_t*)w->data;
    
    switch( be_operate( c->t->s->b, c->fd ) ) {
        // close by peer or some error occurred
        case BE_CLOSE:
            cli_shutdown( c );
        break;
        default:
            break;
    }
}

static void cli_accept( struct ev_loop *loop, ev_io *w, int event )
{
    #pragma unused(event)
    thd_t *t = (thd_t*)ev_userdata( loop );
    int fd = accept( w->fd, NULL, NULL );
    
	if( fd > 0 )
    {
        int rc = -1;
        cli_t *c = pcalloc( 1, cli_t );
        
        if( !c ){
            pfelog( pcalloc );
            fd_close( fd );
        }
        else if( ( rc = fd_setnonblock( fd ) ) != 0 ){
            pfelog( fd_setnonblock );
            fd_close( fd );
            pdealloc( c );
        }
        else if( ( rc = sock_settcp_nodelay( fd ) ) != 0 ){
            pfelog( sock_settcp_nodelay );
            fd_close( fd );
            pdealloc( c );
        }
        else {
            c->t = t;
            c->fd = fd;
            // assign event
            c->read_ev.data = (void*)c;
            ev_io_init( &c->read_ev, cli_read, c->fd, EV_READ );
            ev_io_start( t->loop, &c->read_ev );
        }
    }
    else if( errno != EAGAIN && errno != EWOULDBLOCK ){
        pfelog( accept );
    }
}

static void thd_term( struct ev_loop *loop, ev_async *w, int revents )
{
    #pragma unused(loop,w,revents)
    ev_break( loop, EVBREAK_ALL );
}

static void *thd_listen( void *arg )
{
    thd_t *t = (thd_t*)arg;
    
    // create event-loop
    if( !( t->loop = ev_loop_new( ev_recommended_backends()|EVBACKEND_KQUEUE ) ) ){
        errno = ENOMEM;
        pfelog( ev_loop_new );
    }
    else
    {
        // set userdata
        ev_set_userdata( t->loop, arg );
        // assign events
        ev_async_init( &t->term_ev, thd_term );
        ev_async_start( t->loop, &t->term_ev );
        ev_io_init( &t->accept_ev, cli_accept, t->s->sock, EV_READ );
        ev_io_start( t->loop, &t->accept_ev );
        
        // run infinite-loop
        pthread_cond_signal( &t->s->cond );
        ev_run( t->loop, 0 );
        
        // stop events
        ev_async_stop( t->loop, &t->term_ev );
        ev_io_stop( t->loop, &t->accept_ev );
        goto thdCleanup;
    }
    
    // failed to initialize thread context
    t->id = NULL;
    pthread_cond_signal( &t->s->cond );
    
    thdCleanup:
        if( t->loop ){
            // cleanup
            ev_loop_destroy( t->loop );
        }

    
	return NULL;
}

static int create_thds( sparkled_t *s )
{
    int rc = -1;
    
    // allocate thread array
    if( !( s->thds = pcalloc( s->cfg->nthd, thd_t* ) ) ){
        pfelog( pcalloc );
    }
    else if( ( rc = pthread_mutex_lock( &s->mutex ) ) != 0 ){
        pfelog( pthread_mutex_lock );
    }
    else
    {
        thd_t *t = NULL;
        int8_t i = 0;
        
        for(; i < s->cfg->nthd; i++ )
        {
            if( !( t = pcalloc( 1, thd_t ) ) ){
                pfelog( pcalloc );
                rc = -1;
                break;
            }
            t->s = s;
            if( pthread_create( &t->id, NULL, thd_listen, (void*)t ) != 0 ){
                pfelog( pthread_create );
                rc = -1;
                break;
            }
            pthread_cond_wait( &s->cond, &s->mutex );
            if( !t->id ){
                rc = -1;
                break;
            }
            s->nthd++;
            s->thds[i] = t;
        }
        pthread_mutex_unlock( &s->mutex );
    }
    
    return rc;
}

static int sock_init( sparkled_t *s )
{
    const struct addrinfo hints = {
        .ai_flags = 0,
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
        // initialize
        .ai_addrlen = 0,
        .ai_canonname = NULL,
        .ai_addr = NULL,
        .ai_next = NULL
    };
    struct addrinfo *res = NULL;
    char host[FQDN_MAX_LEN] = {0};
    int rc;
    
    // hostname to address
    memcpy( host, s->cfg->addr, strlen( s->cfg->addr ) );
    rc = getaddrinfo( ( *host == '*' ) ? NULL : host, s->cfg->portstr, 
                      &hints, &res );
    if( rc != 0 ){
        errno = rc;
        pfelog( getaddrinfo, " -- address: %s", host );
    }
    else
    {
        struct addrinfo *ptr = res;
        
        rc = -1;
        // find addrinfo
        errno = 0;
        for(; ptr; ptr = ptr->ai_next )
        {
            // try to create socket descriptor
            s->sock = socket( ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol );
            if( s->sock != -1 )
            {
                int opt = 1;
                
                // copy address info and size
                memcpy( (void*)&(s->info), (void*)ptr->ai_addr, ptr->ai_addrlen );
                s->infolen = ptr->ai_addrlen;
                
                // set reuse addr
                if( ( rc = setsockopt( s->sock, SOL_SOCKET, SO_REUSEADDR, 
                                     (void*)&opt, sizeof(int) ) ) ){
                    pfelog( setsockopt );
                }
                // set non-block
                else if( ( rc = fd_setnonblock( s->sock ) ) != 0 ){
                    pfelog( fd_setnonblock );
                }
                // bind address
                else if( ( rc = bind( s->sock, &s->info, s->infolen ) ) != 0 ){
                    pfelog( bind );
                }
                // listen socket
                else if( ( rc = listen( s->sock, SOMAXCONN ) ) != 0 ){
                    pfelog( listen );
                }
                break;
            }
        }
        freeaddrinfo( res );
    }
    
    return rc;
}

static void dispose( sparkled_t *s )
{
    // cleanup threads
    if( s->thds )
    {
        // notify terminate event-loop signal to threads
        if( s->nthd )
        {
            uint8_t i = 0;
            
            // wait until threads terminated
            for(; i < s->nthd; i++ ){
                ev_async_send( s->thds[i]->loop, &s->thds[i]->term_ev );
                pthread_join( s->thds[i]->id, NULL );
                pdealloc( s->thds[i] );
            }
            pdealloc( s->thds );
        }
    }
    if( s->sock > 0 ){
        close( s->sock );
    }
    if( s->b ){
        be_dealloc( s->b );
    }
    
    pdealloc( s );
}


static sparkled_t *initialize( conf_t *cfg )
{
    sparkled_t *s = pcalloc( 1, sparkled_t );
    
    if( !s ){
        pfelog( malloc );
    }
    else
    {
        s->cfg = cfg;
        if( pthread_mutex_init( &s->mutex, NULL ) != 0 ){
            pfelog( pthread_mutex_init );
        }
        else if( pthread_cond_init( &s->cond, NULL ) != 0 ){
            pfelog( pthread_cond_init );
        }
        else if( ( s->b = be_alloc( cfg ) ) && sock_init( s ) == 0 ){
            return s;
        }
        
        dispose( s );
        s = NULL;
    }
    
    return s;
}


int wait4signal( void )
{
    int rc = 0;
    int signo = 0;
    sigset_t sw;
    
    // set all-signal
    if( ( rc = sigfillset( &sw ) ) != 0 ){
        pfelog( sigfillset );
    }
    else if( ( rc = sigwait( &sw, &signo ) ) != 0 ){
        pfelog( sigwait );
    }
    else {
        plog( "catch signal: %d", signo );
    }
    
    return rc;
}

int main( int argc, const char *argv[] )
{
    conf_t *cfg = cfg_alloc( argc, argv );
    sparkled_t *s = initialize( cfg );
    
    if( s )
    {
        plog( "starting" );
        if( create_thds( s ) == 0 ){
            wait4signal();
        }
        dispose( s );
    }
    
    cfg_dealloc( cfg );
    plog( "byebye!" );
    
    return 0;
}

