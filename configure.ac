#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.71])
AC_INIT([FULL-PACKAGE-NAME], [VERSION], [BUG-REPORT-ADDRESS])
AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_HEADERS([config.h])


AM_INIT_AUTOMAKE


# Checks for programs.
AC_PROG_CC

# Checks for libraries.


# Checks for header files.
AC_CHECK_HEADERS([fcntl.h libintl.h malloc.h sys/ioctl.h sys/time.h unistd.h])
AC_CHECK_HEADER([ext2_fs.h],[],[AC_CHECK_HEADER([ext2fs/ext2_fs.h],[],[AC_MSG_ERROR([Couldn't find or include ext2_fs.h])],[])],[])
AC_CHECK_HEADER([ext2fs.h],[],[AC_CHECK_HEADER([ext2fs/ext2fs.h],[],[AC_MSG_ERROR([Couldn't find or include ext2fs.h])],[])],[])
AC_CHECK_HEADER([e2p.h],[],[AC_CHECK_HEADER([e2p/e2p.h],[],[AC_MSG_ERROR([Couldn't find or include e2p.h])],[])],[])


# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE
AC_TYPE_OFF_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([ftruncate gettimeofday memset setlocale strchr strdup strtol strtoul strtoull])


AC_CONFIG_FILES([Makefile])
AC_OUTPUT
