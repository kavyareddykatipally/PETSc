#ifdef PETSC_RCS_HEADER
"$Id: petscconf.h,v 1.1 1999/08/30 19:55:55 balay Exp balay $"
"Defines the configuration for this machine"
#endif

#if !defined(INCLUDED_PETSCCONF_H)
#define INCLUDED_PETSCCONF_H

#define PARCH_linux

#define PETSC_HAVE_PWD_H 
#define PETSC_HAVE_MALLOC_H 
#define PETSC_HAVE_STRING_H 
#define PETSC_HAVE_X11
#define PETSC_HAVE_GETDOMAINNAME
#define PETSC_HAVE_DRAND48 
#define PETSC_HAVE_UNAME 
#define PETSC_HAVE_UNISTD_H 
#define PETSC_HAVE_SYS_TIME_H 
#define PETSC_HAVE_STDLIB_H
#define PETSC_HAVE_UNISTD_H

#define PETSC_HAVE_FORTRAN_CAPS

#define PETSC_HAVE_READLINK
#define PETSC_HAVE_MEMMOVE

#define PETSC_HAVE_DOUBLE_ALIGN_MALLOC
#define PETSC_HAVE_MEMALIGN
#define PETSC_HAVE_SYS_RESOURCE_H
#define PETSC_SIZEOF_VOIDP 4
#define PETSC_SIZEOF_INT 4
#define PETSC_SIZEOF_DOUBLE 8

#if defined(fixedsobug)
#define PETSC_USE_DYNAMIC_LIBRARIES 1
#define PETSC_HAVE_RTLD_GLOBAL 1
#endif

#define PETSC_HAVE_SYS_UTSNAME_H
#endif
