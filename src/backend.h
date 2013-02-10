/*
 *  backend.h
 *  sparkled
 *
 *  Created by Masatoshi Teruya on 13/02/10.
 *  Copyright 2013 Masatoshi Teruya. All rights reserved.
 *
 */
#ifndef ___BACKEND___
#define ___BACKEND___

#include "conf.h"

typedef struct _backend_t backend_t;

backend_t *be_alloc( conf_t *c );
void be_dealloc( backend_t *b );

#define BE_OK       0
#define BE_CLOSE    -1

int be_operate( backend_t *b, int fd );

#endif

