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
#include <libasyncfd.h>
#include "conf.h"
#include "backend.h"

typedef struct {
    conf_t *cfg;
    backend_t *b;
    afd_sock_t *as;
    int8_t nch;
    pid_t *pids;
} sparkled_t;

typedef struct {
    backend_t *b;
    struct sigaction sa;
    // event
    afd_loop_t *loop;
    afd_watch_t sock_ev;
} chd_t;

typedef struct {
    int fd;
    afd_watch_t read_ev;
} cli_t;

static chd_t *child;

static void cli_shutdown( cli_t *c )
{
    afd_unwatch( child->loop, AS_YES, &c->read_ev );
    pdealloc( c );
}

static void cli_read( afd_loop_t *loop, afd_watch_t *w, int flg, int hup )
{
    cli_t *c = (cli_t*)w->udata;
    char buf[8092];
    size_t blen = 8091;
    ssize_t len = 0;
    
    afd_edge_start();
    // TODO: parse protocol
    if( ( len = read( w->fd, buf, blen ) ) > 0 )
    {
        buf[len] = 0;
        plog( "[%zd]: %s", len, buf );
        afd_edge_again();
    }
    else if( len == 0 || ( errno != EAGAIN && errno != EWOULDBLOCK ) ){
        cli_shutdown( c );
    }
    
    /*
    switch( be_operate( child->b, c->fd ) ) {
        // close by peer or some error occurred
        case BE_CLOSE:
            cli_shutdown( c );
        break;
        default:
            break;
    }
    */
}

static void cli_accept( afd_loop_t *loop, afd_watch_t *w, int flg, int hup )
{
    cli_t *c = NULL;
    int fd = 0;
    
	switch( afd_accept( &fd, w->fd, NULL, NULL, AS_NO ) )
    {
        // do nothing: could not accept client socket
        case -1:
        break;
        // failed to set flags
        case 0:
            pfelog( afd_accept );
            close( fd );
        break;
        default:
            if( !( c = palloc( cli_t ) ) ){
                pfelog( palloc );
                close( fd );
            }
            else {
                c->fd = fd;
                // assign event
                afd_watch_init( &c->read_ev, fd, AS_EV_READ|AS_EV_EDGE, 
                                cli_read, (void*)c );
                afd_watch( child->loop, &c->read_ev );
            }
    }
}

static void chd_sigusr1( int signo )
{
    plog( "catch child signal: %d", signo );
    afd_unloop( child->loop );
}

static int chd_init_signal( void )
{
    sigset_t ss;
    
    // init signal
    child->sa.sa_handler = chd_sigusr1;
    child->sa.sa_flags = SA_RESTART;
    // set all-signal
    if( sigfillset( &ss ) != 0 || 
        sigdelset( &ss, SIGUSR1 ) != 0 ||
        sigprocmask( SIG_BLOCK, &ss, NULL ) != 0 ||
        sigaction( SIGUSR1, &child->sa, NULL ) != 0 ){
        return -1;
    }
    
    return 0;
}

static void chd_init( conf_t *cfg, afd_sock_t *as, backend_t *b )
{
    if( !( child = pcalloc( 1, chd_t ) ) ){
        pfelog( palloc );
    }
    else if( chd_init_signal() ){
        pfelog( chd_init_signal );
    }
    // create event-loop
    else if( !( child->loop = afd_loop_alloc( as, SOMAXCONN, NULL, NULL ) ) ){
        pfelog( afd_loop_alloc );
    }
    else
    {
        int nevt = 0;
        
        child->b = b;
        afd_watch_init( &child->sock_ev, as->fd, AS_EV_READ, cli_accept, NULL );
        afd_watch( child->loop, &child->sock_ev );
        
        nevt = afd_loop( child->loop );
        
        afd_unwatch( child->loop, AS_NO, &child->sock_ev );
    }
    
    // cleanup
    if( child )
    {
        if( child->loop ){
            afd_loop_dealloc( child->loop );
        }
        pdealloc( child );
    }
    
    exit(0);
}

static int create_child( sparkled_t *s )
{
    // allocate thread array
    if( !( s->pids = pcalloc( s->cfg->nch, pid_t ) ) ){
        pfelog( pcalloc );
    }
    else
    {
        pid_t pid = 0;
        int8_t i = 0;
        
        for(; i < s->cfg->nch; i++ )
        {
            if( ( pid = fork() ) == -1 ){
                pfelog( fork );
                return -1;
            }
            else if( pid ){
                s->nch++;
                s->pids[i] = pid;
            }
            else {
                chd_init( s->cfg, s->as, s->b );
            }
        }
        
        return 0;
    }
    
    return -1;
}

static void dispose( sparkled_t *s )
{
    // cleanup child process
    if( s->pids )
    {
        uint8_t i = 0;
        
        // notify terminate event-loop signal to child process
        // wait until child process terminated
        for(; i < s->nch; i++ )
        {
            if( s->pids[i] ){
                kill( s->pids[i], SIGUSR1 );
                waitpid( s->pids[i], NULL, 0 );
            }
        }
        pdealloc( s->pids );
    }
    if( s->as ){
        afd_sock_dealloc( s->as );
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
        s->as = afd_sock_alloc( cfg->addr, strlen( cfg->addr ), AS_TYPE_STREAM );
        
        if( s->as && ( s->b = be_alloc( cfg ) ) ){
            return s;
        }
        
        dispose( s );
        s = NULL;
    }
    
    return s;
}

static int wait4signal( void )
{
    int rc = 0;
    int signo = 0;
    sigset_t ss;
    
    // set all-signal
    if( ( rc = sigfillset( &ss ) ) != 0 ){
        pfelog( sigfillset );
    }
    else if( ( rc = sigprocmask( SIG_BLOCK, &ss, NULL ) ) != 0 ){
        pfelog( sigprocmask );
    }
    else if( ( rc = sigwait( &ss, &signo ) ) != 0 ){
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
        if( afd_listen( s->as, SOMAXCONN ) == 0 && create_child( s ) == 0 ){
            wait4signal();
        }
        dispose( s );
    }
    
    cfg_dealloc( cfg );
    plog( "byebye!" );
    
    return 0;
}


