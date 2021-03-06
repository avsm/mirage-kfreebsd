/***********************************************************************/
/*                                                                     */
/*                           Objective Caml                            */
/*                                                                     */
/*           Xavier Leroy, projet Cristal, INRIA Rocquencourt          */
/*                                                                     */
/*  Copyright 2001 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

/* $Id: unix.c 10613 2010-07-02 08:44:04Z frisch $ */

/* Unix-specific stuff */

#define _GNU_SOURCE
           /* Helps finding RTLD_DEFAULT in glibc */

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/stat.h>
#include "fake.h"
#include "fake_io.h"
#else
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#endif
#include "config.h"
#ifdef SUPPORT_DYNAMIC_LINKING
#ifdef __CYGWIN32__
#include "flexdll.h"
#else
#include <dlfcn.h>
#endif
#endif
#ifdef HAS_UNISTD
#include <unistd.h>
#endif
#ifdef HAS_DIRENT
#include <dirent.h>
#else
#ifdef _KERNEL
#else
#include <sys/dir.h>
#endif
#endif
#include "memory.h"
#include "misc.h"
#include "osdeps.h"

#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#endif

char * caml_decompose_path(struct ext_table * tbl, char * path)
{
  char * p, * q;
  int n;

  if (path == NULL) return NULL;
  p = caml_stat_alloc(strlen(path) + 1);
  strcpy(p, path);
  q = p;
  while (1) {
    for (n = 0; q[n] != 0 && q[n] != ':'; n++) /*nothing*/;
    caml_ext_table_add(tbl, q);
    q = q + n;
    if (*q == 0) break;
    *q = 0;
    q += 1;
  }
  return p;
}

char * caml_search_in_path(struct ext_table * path, char * name)
{
  char * p, * fullname;
  int i;
  struct stat st;

  for (p = name; *p != 0; p++) {
    if (*p == '/') goto not_found;
  }
  for (i = 0; i < path->size; i++) {
    fullname = caml_stat_alloc(strlen((char *)(path->contents[i])) +
                               strlen(name) + 2);
    strcpy(fullname, (char *)(path->contents[i]));
    if (fullname[0] != 0) strcat(fullname, "/");
    strcat(fullname, name);
    if (stat(fullname, &st) == 0 && S_ISREG(st.st_mode)) return fullname;
    caml_stat_free(fullname);
  }
 not_found:
  fullname = caml_stat_alloc(strlen(name) + 1);
  strcpy(fullname, name);
  return fullname;
}

#ifdef __CYGWIN32__

/* Cygwin needs special treatment because of the implicit ".exe" at the
   end of executable file names */

static int cygwin_file_exists(char * name)
{
  int fd;
  /* Cannot use stat() here because it adds ".exe" implicitly */
  fd = open(name, O_RDONLY);
  if (fd == -1) return 0;
  close(fd);
  return 1;
}

static char * cygwin_search_exe_in_path(struct ext_table * path, char * name)
{
  char * p, * fullname;
  int i;

  for (p = name; *p != 0; p++) {
    if (*p == '/' || *p == '\\') goto not_found;
  }
  for (i = 0; i < path->size; i++) {
    fullname = caml_stat_alloc(strlen((char *)(path->contents[i])) +
                               strlen(name) + 6);
    strcpy(fullname, (char *)(path->contents[i]));
    strcat(fullname, "/");
    strcat(fullname, name);
    if (cygwin_file_exists(fullname)) return fullname;
    strcat(fullname, ".exe");
    if (cygwin_file_exists(fullname)) return fullname;
    caml_stat_free(fullname);
  }
 not_found:
  fullname = caml_stat_alloc(strlen(name) + 5);
  strcpy(fullname, name);
  if (cygwin_file_exists(fullname)) return fullname;
  strcat(fullname, ".exe");
  if (cygwin_file_exists(fullname)) return fullname;
  strcpy(fullname, name);
  return fullname;
}

#endif

char * caml_search_exe_in_path(char * name)
{
  struct ext_table path;
  char * tofree;
  char * res;

  caml_ext_table_init(&path, 8);
  tofree = caml_decompose_path(&path, getenv("PATH"));
#ifndef __CYGWIN32__
  res = caml_search_in_path(&path, name);
#else
  res = cygwin_search_exe_in_path(&path, name);
#endif
  caml_stat_free(tofree);
  caml_ext_table_free(&path, 0);
  return res;
}

char * caml_search_dll_in_path(struct ext_table * path, char * name)
{
  char * dllname = caml_stat_alloc(strlen(name) + 4);
  char * res;
  strcpy(dllname, name);
  strcat(dllname, ".so");
  res = caml_search_in_path(path, dllname);
  caml_stat_free(dllname);
  return res;
}

#ifdef SUPPORT_DYNAMIC_LINKING
#ifdef __CYGWIN32__
/* Use flexdll */

void * caml_dlopen(char * libname, int for_execution, int global)
{
  int flags = (global ? FLEXDLL_RTLD_GLOBAL : 0);
  if (!for_execution) flags |= FLEXDLL_RTLD_NOEXEC;
  return flexdll_dlopen(libname, flags);
}

void caml_dlclose(void * handle)
{
  flexdll_dlclose(handle);
}

void * caml_dlsym(void * handle, char * name)
{
  return flexdll_dlsym(handle, name);
}

void * caml_globalsym(char * name)
{
  return flexdll_dlsym(flexdll_dlopen(NULL,0), name);
}

char * caml_dlerror(void)
{
  return flexdll_dlerror();
}

#else
/* Use normal dlopen */

#ifndef RTLD_GLOBAL
#define RTLD_GLOBAL 0
#endif
#ifndef RTLD_LOCAL
#define RTLD_LOCAL 0
#endif
#ifndef RTLD_NODELETE
#define RTLD_NODELETE 0
#endif

void * caml_dlopen(char * libname, int for_execution, int global)
{
  return dlopen(libname, RTLD_NOW | (global ? RTLD_GLOBAL : RTLD_LOCAL) | RTLD_NODELETE);
  /* Could use RTLD_LAZY if for_execution == 0, but needs testing */
}

void caml_dlclose(void * handle)
{
  dlclose(handle);
}

void * caml_dlsym(void * handle, char * name)
{
#ifdef DL_NEEDS_UNDERSCORE
  char _name[1000] = "_";
  strncat (_name, name, 998);
  name = _name;
#endif
  return dlsym(handle, name);
}

void * caml_globalsym(char * name)
{
#ifdef RTLD_DEFAULT
  return caml_dlsym(RTLD_DEFAULT, name);
#else
  return NULL;
#endif
}

char * caml_dlerror(void)
{
  return (char*) dlerror();
}

#endif
#else

void * caml_dlopen(char * libname, int for_execution, int global)
{
  return NULL;
}

void caml_dlclose(void * handle)
{
}

void * caml_dlsym(void * handle, char * name)
{
  return NULL;
}

void * caml_globalsym(char * name)
{
  return NULL;
}

char * caml_dlerror(void)
{
  return "dynamic loading not supported on this platform";
}

#endif

/* Add to [contents] the (short) names of the files contained in
   the directory named [dirname].  No entries are added for [.] and [..].
   Return 0 on success, -1 on error; set errno in the case of error. */

int caml_read_directory(char * dirname, struct ext_table * contents)
{
#ifdef _KERNEL
  NOT_IMPLEMENTED(caml_read_directory);
#else
  DIR * d;
#ifdef HAS_DIRENT
  struct dirent * e;
#else
  struct direct * e;
#endif
  char * p;

  d = opendir(dirname);
  if (d == NULL) return -1;
  while (1) {
    e = readdir(d);
    if (e == NULL) break;
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    p = caml_stat_alloc(strlen(e->d_name) + 1);
    strcpy(p, e->d_name);
    caml_ext_table_add(contents, p);
  }
  closedir(d);
  return 0;
#endif
}

/* Recover executable name from /proc/self/exe if possible */

#ifdef __linux__

int caml_executable_name(char * name, int name_len)
{
  int retcode;
  struct stat st;

  retcode = readlink("/proc/self/exe", name, name_len);
  if (retcode == -1 || retcode >= name_len) return -1;
  name[retcode] = 0;
  /* Make sure that the contents of /proc/self/exe is a regular file.
     (Old Linux kernels return an inode number instead.) */
  if (stat(name, &st) != 0) return -1;
  if (! S_ISREG(st.st_mode)) return -1;
  return 0;
}

#endif
