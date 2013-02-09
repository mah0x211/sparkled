/*
 *  conf.h
 *  sparkled
 *
 *  Created by Masatoshi Teruya on 13/02/09.
 *  Copyright 2013 Masatoshi Teruya. All rights reserved.
 *
 */
#ifndef ___CONF___
#define ___CONF___

#include <unistd.h>
#include <stdint.h>
#include <limits.h>
#include <sys/types.h>

typedef struct {
    // TODO: working dir
    // const char *cwd;
    const char *addr;
    const char *portstr;
    uint16_t port;
    uint8_t nthd;
    uint16_t bktsize;
    uint32_t mapsize;
    const char *dbdir;
    unsigned int flgs;
    mode_t perm;
} conf_t;

// FQDN maximum length: 255 bytes(include extension delimiter)
#define FQDN_MAX_LEN  255

conf_t *cfg_alloc( int argc, const char *argv[] );
void cfg_dealloc( conf_t *cfg );

#endif
