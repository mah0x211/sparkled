/*
 *  sparkled.h
 *  sparkled
 *
 *  Created by Masatoshi Teruya on 13/02/09.
 *  Copyright 2013 Masatoshi Teruya. All rights reserved.
 *
 */
#ifndef ___SPARKLED___
#define ___SPARKLED___

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <libbuf.h>

// memory alloc/dealloc
#define palloc(t)     (t*)malloc( sizeof(t) )
#define pcalloc(n,t)  (t*)calloc( n, sizeof(t) )
#define pdealloc(p)   free((void*)p)

// print function error log
#define _pfelog(f,fmt,...) \
    printf( "failed to " #f "(): %s" fmt "\n", \
            ( errno ) ? strerror(errno) : "", ##__VA_ARGS__ )
#define pfelog(f,...) _pfelog(f,__VA_ARGS__)

#define plog(fmt,...) printf( fmt "\n", ##__VA_ARGS__ )

#endif

