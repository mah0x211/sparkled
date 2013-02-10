/*
 *  conf.c
 *  sparkled
 *
 *  Created by Masatoshi Teruya on 13/02/09.
 *  Copyright 2013 Masatoshi Teruya. All rights reserved.
 *
 */

#include "conf.h"
#include "sparkled.h"
#include <ctype.h>

#define DEFAULT_ADDR        "127.0.0.1:1977"
#define DEFAULT_PORT        1977
#define DEFAULT_NTHREAD     1
#define DEFAULT_DBMAPSIZE   10
#define DEFAULT_DBFLAGS     0
#define DEFAULT_DBPERM      0644

// print argument error and exit(failure)
#define config_eexit(o,fmt,...) \
    printf( "invalid argument[%s]: " fmt "\n\n", o, ##__VA_ARGS__ ); \
    usage();

// TODO: print usage
static void usage( void )
{
    printf( 
        "sparkeld\n"
        "Usage: sparkeld [ <opt> <val> ] <listen>\n"
        "\n" );
    
    exit( EXIT_FAILURE );
}


static void check_addr( conf_t *cfg )
{
    char *addr = (char*)cfg->addr;
    size_t len = strlen( addr );
    // find port separator
    char *needle = (char*)memchr( addr, ':', len );
    
    // found separator
    if( needle )
    {
        char *tail = needle;
        
        cfg->portstr = tail + 1;
        errno = 0;
        // calc index and remain
        len -= (uintptr_t)tail - (uintptr_t)addr;
        // hostname or port number too large
        if( len > FQDN_MAX_LEN ){
            plog( "invalid address length: %s", strerror(ERANGE) );
            usage();
        }
        else if( !( cfg->port = buf_strudec2u16( (char*)cfg->portstr, needle ) ) ){
            plog( "invalid port number: %s", cfg->portstr );
            usage();
        }
        else if( errno ){
            plog( "invalid port number: %s -- %s", cfg->portstr, strerror(errno) );
            usage();
        }
        // set terminator
        *tail = 0;
    }
    // hostname or port number too large
    else if( len > FQDN_MAX_LEN ){
        plog( "invalid address length: %s", strerror(ERANGE) );
        usage();
    }
}

conf_t *cfg_alloc( int argc, const char *argv[] )
{
    conf_t *cfg = palloc( conf_t );
    char *val = rindex( argv[0], '/' );
    int pagesize = getpagesize();
    size_t mbytes = 1048576 / pagesize;
    
    *val = 0;
    if( !cfg ){
        pfelog( malloc );
        exit( EXIT_FAILURE );
    }
    else if( !val ){
        pdealloc( cfg );
        pfelog( rindex );
        exit( EXIT_FAILURE );
    }
    
    *val = 0;
    // set default configration
    if( !( cfg->dbdir = realpath( argv[0], NULL ) ) ){
        pfelog( realpath, "%s", val );
        exit( EXIT_FAILURE );
    }
    cfg->addr = DEFAULT_ADDR;
    cfg->port = DEFAULT_PORT;
    cfg->portstr = NULL;
    cfg->nthd = DEFAULT_NTHREAD;
    cfg->bktsize = pagesize;
    cfg->mapsize = DEFAULT_DBMAPSIZE * mbytes * pagesize;
    cfg->flgs = DEFAULT_DBFLAGS;
    cfg->perm = DEFAULT_DBPERM;
    
    if( argc > 1 )
    {
        char *opt;
        char *needle;
        int i = 1;
        
        for(; i < argc; i++ )
        {
            // listen-address
            if( i + 1 == argc ){
                cfg->addr = argv[i];
            }
            else
            {
                opt = (char*)argv[i];
                val = (char*)argv[++i];
                
                // check opt-type
                if( *opt != '-' || !isalpha( opt[1] ) || opt[2] ){
                    config_eexit( opt, "invalid option" );
                }
                else if( !val ){
                    config_eexit( opt, "undefined value" );
                }
                // check opt val
                errno = 0;
                opt++;
                switch( *opt )
                {
                    // mapsize
                    case 'm':
                        cfg->mapsize = buf_strudec2u16( val, needle );
                        // invalid argument
                        if( errno ){
                            config_eexit( opt, "%s -- %s", val, strerror(errno) );
                        }
                        else if( *needle ){
                            config_eexit( opt, "%s -- %s", val, strerror(EINVAL) );
                        }
                        else if( !cfg->mapsize ){
                            cfg->mapsize = mbytes * pagesize;
                        }
                        else {
                            cfg->mapsize = cfg->mapsize * mbytes * pagesize;
                        }
                    break;
                    // db dir
                    case 'd':
                        // remove default-dbdir
                        pdealloc( cfg->dbdir );
                        cfg->dbdir = realpath( val, NULL );
                        if( !cfg->dbdir ){
                            pfelog( realpath );
                            usage();
                        }
                    break;
                    /* TODO: flags
                    case 'f':
                    break;
                    */
                    // perm
                    case 'p':
                        cfg->perm = buf_stroct2u16( val, needle );
                        if( errno ){
                            config_eexit( opt, "%s -- %s", val, strerror(errno) );
                        }
                        else if( *needle ){
                            config_eexit( opt, "%s -- %s", val, strerror(EINVAL) );
                        }
                        else if( !cfg->perm || cfg->perm > 4095 ){
                            config_eexit( opt, "0 < |%d| < 4095", cfg->perm );
                        }
                    break;
                    // thread
                    case 't':
                        cfg->nthd = buf_strudec2u8( val, needle );
                        if( errno ){
                            config_eexit( opt, "%s -- %s", val, strerror(errno) );
                        }
                        else if( *needle ){
                            config_eexit( opt, "%s -- %s", val, strerror(EINVAL) );
                        }
                        else if( !cfg->nthd ){
                            cfg->nthd = DEFAULT_NTHREAD;
                        }
                    break;
                    // bucket-size
                    case 'b':
                        cfg->bktsize = buf_strudec2u16( val, needle );
                        if( errno ){
                            config_eexit( opt, "%s -- %s", val, strerror(errno) );
                            exit( EXIT_FAILURE );
                        }
                        else if( *needle ){
                            config_eexit( opt, "%s -- %s", val, strerror(EINVAL) );
                        }
                        else if( !cfg->bktsize ){
                            cfg->bktsize = pagesize;
                        }
                    break;
                    default:
                        config_eexit( opt, "%s", strerror(EINVAL) );
                }
            }
        }
    }
    
    // last check
    check_addr( cfg );
    
    return cfg;
}

void cfg_dealloc( conf_t *cfg )
{
    if( cfg->dbdir ){
        pdealloc( cfg->dbdir );
    }
    pdealloc( cfg );
}

