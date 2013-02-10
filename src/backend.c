/*
 *  backend.c
 *  sparkled
 *
 *  Created by Masatoshi Teruya on 13/02/10.
 *  Copyright 2013 Masatoshi Teruya. All rights reserved.
 *
 */

#include "backend.h"
#include "lmdb.h"
#include "sparkled.h"

struct _backend_t {
    conf_t *cfg;
    MDB_env *env;
};

backend_t *be_alloc( conf_t *c )
{
    backend_t *b = pcalloc( 1, backend_t );
    
    if( !b ){
        pfelog( pcalloc );
    }
    else
    {
        int rc = mdb_env_create( &b->env );
        
        if( rc != 0 ){
            errno = 0;
            pfelog( mdb_env_create, "%s", mdb_strerror( rc ) );
        }
        else if( ( rc = mdb_env_set_mapsize( b->env, c->mapsize ) ) != 0 ){
            errno = 0;
            pfelog( mdb_env_create, "%s", mdb_strerror( rc ) );
        }
        else if( ( rc = mdb_env_open( b->env, c->dbdir, c->flgs, c->perm ) ) != 0 ){
            errno = 0;
            pfelog( mdb_env_open, "%s -- %s", c->dbdir, mdb_strerror( rc ) );
        }
        else {
            b->cfg = c;
            return b;
        }
        
        pdealloc( b );
        b = NULL;
    }
    
    return b;
}

void be_dealloc( backend_t *b )
{
    if( b->env ){
        mdb_env_close( b->env );
    }
    pdealloc( b );
}

int be_operate( backend_t *b, int fd )
{
    int rc = BE_OK;
    char buf[b->cfg->bktsize];
    ssize_t len = read( fd, buf, b->cfg->bktsize );
    
    if( len ){
        write( fd, buf, len );
        return BE_CLOSE;
    }
    // some error occurred
    else if( len == -1 ){
        return BE_CLOSE;
    }
    // close by peer
    else {
        return BE_CLOSE;
    }
    
    return rc;
}

