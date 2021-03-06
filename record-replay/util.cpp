/****************************************************************************
 *   Copyright (C) 2006-2010 by Jason Ansel, Kapil Arya, and Gene Cooperman *
 *   jansel@csail.mit.edu, kapil@ccs.neu.edu, gene@ccs.neu.edu              *
 *                                                                          *
 *   This file is part of the dmtcp/src module of DMTCP (DMTCP:dmtcp/src).  *
 *                                                                          *
 *  DMTCP:dmtcp/src is free software: you can redistribute it and/or        *
 *  modify it under the terms of the GNU Lesser General Public License as   *
 *  published by the Free Software Foundation, either version 3 of the      *
 *  License, or (at your option) any later version.                         *
 *                                                                          *
 *  DMTCP:dmtcp/src is distributed in the hope that it will be useful,      *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *  GNU Lesser General Public License for more details.                     *
 *                                                                          *
 *  You should have received a copy of the GNU Lesser General Public        *
 *  License along with DMTCP:dmtcp/src.  If not, see                        *
 *  <http://www.gnu.org/licenses/>.                                         *
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <string>
#include <sstream>
#include <fcntl.h>
#include <errno.h>
#include <sys/syscall.h>
#include <linux/limits.h>
#include "constants.h"
#include  "util.h"
#include  "jassert.h"
#include  "jfilesystem.h"
#include  "fred_wrappers.h"

bool dmtcp::Util::strStartsWith(const char *str, const char *pattern)
{
  JASSERT(str != NULL && pattern != NULL);
  int len1 = strlen(str);
  int len2 = strlen(pattern);
  if (len1 >= len2) {
    return strncmp(str, pattern, len2) == 0;
  }
  return false;
}

bool dmtcp::Util::strEndsWith(const char *str, const char *pattern)
{
  JASSERT(str != NULL && pattern != NULL);
  int len1 = strlen(str);
  int len2 = strlen(pattern);
  if (len1 >= len2) {
    size_t idx = len1 - len2;
    return strncmp(str+idx, pattern, len2) == 0;
  }
  return false;
}

bool dmtcp::Util::strStartsWith(const dmtcp::string& str, const char *pattern)
{
  return strStartsWith(str.c_str(), pattern);
}

bool dmtcp::Util::strEndsWith(const dmtcp::string& str, const char *pattern)
{
  return strEndsWith(str.c_str(), pattern);
}

// Fails or does entire write (returns count)
ssize_t dmtcp::Util::writeAll(int fd, const void *buf, size_t count)
{
  const char *ptr = (const char *) buf;
  size_t num_written = 0;

  do {
    ssize_t rc = _real_write (fd, ptr + num_written, count - num_written);
    if (rc == -1) {
      if (errno == EINTR || errno == EAGAIN)
	continue;
      else
        return rc;
    }
    else if (rc == 0)
      break;
    else // else rc > 0
      num_written += rc;
  } while (num_written < count);
  JASSERT (num_written == count) (num_written) (count);
  return num_written;
}

// Fails, succeeds, or partial read due to EOF (returns num read)
// return value:
//    -1: unrecoverable error
//   <n>: number of bytes read
ssize_t dmtcp::Util::readAll(int fd, void *buf, size_t count)
{
  ssize_t rc;
  char *ptr = (char *) buf;
  size_t num_read = 0;
  for (num_read = 0; num_read < count;) {
    rc = _real_read (fd, ptr + num_read, count - num_read);
    if (rc == -1) {
      if (errno == EINTR || errno == EAGAIN)
	continue;
      else
        return -1;
    }
    else if (rc == 0)
      break;
    else // else rc > 0
      num_read += rc;
  }
  return num_read;
}

/* Begin miscellaneous/helper functions. */
// Reads from fd until count bytes are read, or newline encountered.
// Returns NULL at EOF.
// FIXME: count is unused. Buffer-overrun possible
int dmtcp::Util::readLine(int fd, char *buf, int count)
{
  int i = 0;
  char c;
  while (i < count) {
    if (_real_read(fd, &c, 1) == 0) {
      buf[i] = '\0';
      return '\0';
    }
    buf[i++] = c;
    if (c == '\n') break;
  }
  buf[i++] = '\0';
  return i;
}

/* Read decimal number, return value and terminating character */

char dmtcp::Util::readDec (int fd, VA *value)
{
  char c;
  unsigned long int v;

  v = 0;
  while (1) {
    c = readChar (fd);
    if ((c >= '0') && (c <= '9')) c -= '0';
    else break;
    v = v * 10 + c;
  }
  *value = (VA)v;
  return (c);
}

/* Read decimal number, return value and terminating character */

char dmtcp::Util::readHex (int fd, VA *value)
{
  char c;
  unsigned long int v;

  v = 0;
  while (1) {
    c = readChar (fd);
         if ((c >= '0') && (c <= '9')) c -= '0';
    else if ((c >= 'a') && (c <= 'f')) c -= 'a' - 10;
    else if ((c >= 'A') && (c <= 'F')) c -= 'A' - 10;
    else break;
    v = v * 16 + c;
  }
  *value = (VA)v;
  return (c);
}

/* Read non-null character, return null if EOF */

char dmtcp::Util::readChar (int fd)
{
  char c;
  int rc;

  do {
    rc = _real_read (fd, &c, 1);
  } while ( rc == -1 && errno == EINTR );
  if (rc <= 0) return (0);
  return (c);
}


int dmtcp::Util::readProcMapsLine(int mapsfd, dmtcp::Util::ProcMapsArea *area)
{
  char c, rflag, sflag, wflag, xflag;
  int i;
  unsigned int long devmajor, devminor, inodenum;
  VA startaddr, endaddr;

  c = readHex (mapsfd, &startaddr);
  if (c != '-') {
    if ((c == 0) && (startaddr == 0)) return (0);
    goto skipeol;
  }
  c = readHex (mapsfd, &endaddr);
  if (c != ' ') goto skipeol;
  if (endaddr < startaddr) goto skipeol;

  rflag = c = readChar (mapsfd);
  if ((c != 'r') && (c != '-')) goto skipeol;
  wflag = c = readChar (mapsfd);
  if ((c != 'w') && (c != '-')) goto skipeol;
  xflag = c = readChar (mapsfd);
  if ((c != 'x') && (c != '-')) goto skipeol;
  sflag = c = readChar (mapsfd);
  if ((c != 's') && (c != 'p')) goto skipeol;

  c = readChar (mapsfd);
  if (c != ' ') goto skipeol;

  c = readHex (mapsfd, (VA *)&devmajor);
  if (c != ' ') goto skipeol;
  area -> offset = (off_t)devmajor;

  c = readHex (mapsfd, (VA *)&devmajor);
  if (c != ':') goto skipeol;
  c = readHex (mapsfd, (VA *)&devminor);
  if (c != ' ') goto skipeol;
  c = readDec (mapsfd, (VA *)&inodenum);
  area -> name[0] = '\0';
  while (c == ' ') c = readChar (mapsfd);
  if (c == '/' || c == '[') { /* absolute pathname, or [stack], [vdso], etc. */
    i = 0;
    do {
      area -> name[i++] = c;
      if (i == sizeof area -> name) goto skipeol;
      c = readChar (mapsfd);
    } while (c != '\n');
    area -> name[i] = '\0';
  }
#if 0
  int rc;
  struct stat statbuf;
  unsigned int long devnum;

  if (mtcp_strstartswith(area -> name, nscd_mmap_str1)  ||
      mtcp_strstartswith(area -> name, nscd_mmap_str2) ||
      mtcp_strstartswith(area -> name, nscd_mmap_str3)) {
    /* if nscd is active */
  } else if ( mtcp_strstartswith(area -> name, sys_v_shmem_file) ) {
    /* System V Shared-Memory segments are handled by DMTCP. */
  } else if ( mtcp_strendswith(area -> name, DELETED_FILE_SUFFIX) ) {
    /* Deleted File */
  } else if (area -> name[0] == '/') {  /* if an absolute pathname */
    rc = stat (area -> name, &statbuf);
    if (rc < 0) {
      MTCP_PRINTF("ERROR: error %d statting %s\n", -rc, area -> name);
      return (1); /* 0 would mean last line of maps; could do mtcp_abort() */
    }
    devnum = makedev (devmajor, devminor);
    if ((devnum != statbuf.st_dev) || (inodenum != statbuf.st_ino)) {
      MTCP_PRINTF("ERROR: image %s dev:inode %X:%u not eq maps %X:%u\n",
                   area -> name, statbuf.st_dev, statbuf.st_ino,
		   devnum, inodenum);
      return (1); /* 0 would mean last line of maps; could do mtcp_abort() */
    }
  } else {
    /* Special area like [heap] or anonymous area. */
  }
#endif

  if (c != '\n') goto skipeol;

  area -> addr = startaddr;
  area -> size = endaddr - startaddr;
  area -> endAddr = endaddr;
  area -> prot = 0;
  if (rflag == 'r') area -> prot |= PROT_READ;
  if (wflag == 'w') area -> prot |= PROT_WRITE;
  if (xflag == 'x') area -> prot |= PROT_EXEC;
  area -> flags = MAP_FIXED;
  if (sflag == 's') area -> flags |= MAP_SHARED;
  if (sflag == 'p') area -> flags |= MAP_PRIVATE;
  if (area -> name[0] == '\0') area -> flags |= MAP_ANONYMOUS;

  return (1);

skipeol:
  JASSERT(false) .Text("Not Reached");
  return (0);  /* NOTREACHED : stop compiler warning */
}
