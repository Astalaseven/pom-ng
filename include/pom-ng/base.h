/*
 *  This file is part of pom-ng.
 *  Copyright (C) 2010-2013 Guy Martin <gmsoft@tuxicoman.be>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */


#ifndef __POM_NG_BASE_H__
#define __POM_NG_BASE_H__

// Default return values
#define POM_OK 0
#define POM_ERR -1

// Defines for packet directions
#define POM_DIR_UNK -1
#define POM_DIR_FWD 0
#define POM_DIR_REV 1
#define POM_DIR_TOT 2 // Total number of possible directions

#define POM_DIR_REVERSE(x) ((x) == POM_DIR_FWD ? POM_DIR_REV : POM_DIR_FWD)

#include <pom-ng/pomlog.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <inttypes.h>

#define POM_STRERROR_BUFF_SIZE 128

// Our own time structure which is the time in usec
typedef uint64_t ptime;
#define pom_timeval_to_ptime(x) (((uint64_t) ((x).tv_sec) * 1000000UL) + (uint64_t) (x).tv_usec)
#define pom_ptime_sec(x) ((x) / 1000000UL)
#define pom_ptime_usec(x) ((x) % 1000000UL)

// Get the current time in ptime form
ptime pom_gettimeofday();

// Thread safe version of strerror()
char *pom_strerror(int err_num);

// Out of memory handler
void pom_oom_internal(size_t size, char *file, unsigned int line);
#define pom_oom(x) pom_oom_internal(x, __FILE__, __LINE__)

// Locking handlers
#define pom_mutex_lock(x) {													\
	int err_num = pthread_mutex_lock(x);											\
	if (err_num) {														\
		pomlog(POMLOG_ERR "Error while locking mutex in %s:%u : %s", __FILE__, __LINE__, pom_strerror(err_num));	\
		abort();													\
	}															\
}

#define pom_mutex_unlock(x) {													\
	int err_num = pthread_mutex_unlock(x);											\
	if (err_num) {														\
		pomlog(POMLOG_ERR "Error while unlocking mutex in %s:%u : %s", __FILE__, __LINE__, pom_strerror(err_num));	\
		abort();													\
	}															\
}

#define pom_rwlock_rlock(x) {													\
	int err_num = pthread_rwlock_rdlock(x);											\
	if (err_num) {														\
		pomlog(POMLOG_ERR "Error while read locking rwlock in %s:%u : %s", __FILE__, __LINE__, pom_strerror(err_num));	\
		abort();													\
	}															\
}

#define pom_rwlock_wlock(x) {													\
	int err_num = pthread_rwlock_wrlock(x);											\
	if (err_num) {														\
		pomlog(POMLOG_ERR "Error while write locking rwlock in %s:%u : %s", __FILE__, __LINE__, pom_strerror(err_num));	\
		abort();													\
	}															\
}

#define pom_rwlock_unlock(x) {													\
	int err_num = pthread_rwlock_unlock(x);											\
	if (err_num) {														\
		pomlog(POMLOG_ERR "Error while unlocking rwlock in %s:%u : %s", __FILE__, __LINE__, pom_strerror(err_num));	\
		abort();													\
	}															\
}
// Wrapper for open() which creates the directory structure
int pom_open(const char *filename, int flags, mode_t mode);

// Wrapper for read() and write() that writes the whole buffer
int pom_write(int fd, const void *buf, size_t count);
int pom_read(int fd, void *buf, size_t count);

// Init a mutex with a specific type
int pom_mutex_init_type(pthread_mutex_t *lock, int type);

// Usefull macros for byte swapping
#ifndef bswap64
uint64_t bswap64(uint64_t x);
#endif

#ifndef bswap16
#define bswap16(x) \
	((((x) >> 8) & 0xffu) | (((x) & 0xffu) << 8))
#endif
#ifndef bswap32
#define bswap32(x) \
	((((x) & 0xff000000u) >> 24) | (((x) & 0x00ff0000u) >>  8) | \
	(((x) & 0x0000ff00u) <<  8) | (((x) & 0x000000ffu) << 24))
#endif

#if BYTE_ORDER == BIG_ENDIAN
#define le16(x)		bswap16(x)
#define le32(x)		bswap32(x)
#define le64(x)		bswap64(x)
#define ntohll(x)	(x)
#define htonll(x)	(x)
#elif BYTE_ORDER == LITTLE_ENDIAN
#define le16(x)		(x)
#define le32(x)		(x)
#define le64(x)		(x)
#define ntohll(x)	bswap64(x)
#define htonll(x)	bswap64(x)
#else
#error "Please define byte ordering"
#endif
#endif
