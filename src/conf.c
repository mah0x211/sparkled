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
#include "lmdb.h"

#define DEFAULT_ADDR        "inet://127.0.0.1:1977"
#define DEFAULT_NCHILD      1
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

static unsigned int parse_flgs( const char *val )
{
    unsigned int flgs = 0;
    char *ptr = NULL;
    char *token = strtok_r( (char*)val, ",", &ptr );
    
    while( token )
    {
        if( strncasecmp( token, "FIXEDMAP", strlen( token ) ) == 0 ){
            flgs |= MDB_FIXEDMAP;
        }
        else if( strncasecmp( token, "NOSUBDIR", strlen( token ) ) == 0 ){
            flgs |= MDB_NOSUBDIR;
        }
        else if( strncasecmp( token, "RDONLY", strlen( token ) ) == 0 ){
            flgs |= MDB_RDONLY;
        }
        else if( strncasecmp( token, "WRITEMAP", strlen( token ) ) == 0 ){
            flgs |= MDB_WRITEMAP;
        }
        else if( strncasecmp( token, "NOMETASYNC", strlen( token ) ) == 0 ){
            flgs |= MDB_NOMETASYNC;
        }
        else if( strncasecmp( token, "NOSYNC", strlen( token ) ) == 0 ){
            flgs |= MDB_NOSYNC;
        }
        else if( strncasecmp( token, "MAPASYNC", strlen( token ) ) == 0 ){
            flgs |= MDB_MAPASYNC;
        }
        else {
            plog( "invalid flag: %s", token );
            usage();
        }
        token = strtok_r( NULL, ",", &ptr );
    }

    return flgs;
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
    cfg->nch = DEFAULT_NCHILD;
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
                    // flags
                    case 'f':
                        cfg->flgs = parse_flgs( val );
                    break;
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
                        cfg->nch = buf_strudec2u8( val, needle );
                        if( errno ){
                            config_eexit( opt, "%s -- %s", val, strerror(errno) );
                        }
                        else if( *needle ){
                            config_eexit( opt, "%s -- %s", val, strerror(EINVAL) );
                        }
                        else if( !cfg->nch ){
                            cfg->nch = DEFAULT_NCHILD;
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
    
    return cfg;
}

void cfg_dealloc( conf_t *cfg )
{
    if( cfg->dbdir ){
        pdealloc( cfg->dbdir );
    }
    pdealloc( cfg );
}

