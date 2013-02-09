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

#define DEFAULT_LISTEN      "127.0.0.1:1977"
#define DEFAULT_NTHREAD     1
#define DEFAULT_BKTSIZE     4096
#define DEFAULT_DBMAPSIZE   1024*1024*10
#define DEFAULT_DBDIR       "dbdir"
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


conf_t *cfg_alloc( int argc, const char *argv[] )
{
    conf_t *cfg = palloc( conf_t );
    
    if( !cfg ){
        pfelog( malloc );
        exit( EXIT_FAILURE );
    }
    
    // set default configration
    cfg->addr = DEFAULT_LISTEN;
    cfg->nthd = DEFAULT_NTHREAD;
    cfg->bktsize = DEFAULT_BKTSIZE;
    cfg->mapsize = DEFAULT_DBMAPSIZE;
    cfg->dir = DEFAULT_DBDIR;
    cfg->flgs = DEFAULT_DBFLAGS;
    cfg->perm = DEFAULT_DBPERM;
    
    if( argc > 1 )
    {
        char *opt,*val;
        char *needle;
        int i = 1;
        
        for(; i < argc; i++ )
        {
            // listen-address
            if( i + 1 == argc ){
                cfg->addr = (char*)argv[i];
            }
            else
            {
                opt = (char*)argv[i];
                val = (char*)argv[++i];
                
                // check opt-type
                if( *opt != '-' || !isalpha( opt[1] ) || opt[2] ){
                    config_eexit( opt, "|%s|", strerror(EINVAL) );
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
                            cfg->mapsize = 10;
                        }
                        cfg->mapsize *= 1024 * 1024;
                    break;
                    // db dir
                    case 'd':
                        cfg->dir = val;
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
                            cfg->bktsize = DEFAULT_BKTSIZE;
                        }
                    break;
                    default:
                        config_eexit( opt, "%s", strerror(EINVAL) );
                }
            }
        }
    }
    
    return cfg;
}

void cfg_dealloc( conf_t *cfg )
{
    pdealloc( cfg );
}

