#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
AC_PREREQ([2.71])
AC_INIT([inode_count_modifier], [0.1], [https://github.com/danim7/inode_count_modifier])
AC_CONFIG_SRCDIR([reduce_inode_count.c])
AC_CONFIG_HEADERS([config.h])


AM_INIT_AUTOMAKE


# Checks for programs.
AC_PROG_CC

# Checks for libraries.
AC_CHECK_LIB([ext2fs],[ext2fs_get_library_version],[],[AC_MSG_ERROR([Couldn't find or link ext2fs library])],[])
AC_CHECK_LIB([com_err],[add_error_table],[],[AC_MSG_ERROR([Couldn't find or link com_err library])],[])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h libintl.h malloc.h sys/ioctl.h sys/time.h unistd.h])
AC_CHECK_HEADER([ext2_fs.h],[],[AC_CHECK_HEADER([ext2fs/ext2_fs.h],[],[AC_MSG_ERROR([Couldn't find or include ext2_fs.h])],[])],[])
AC_CHECK_HEADER([ext2fs.h],[],[AC_CHECK_HEADER([ext2fs/ext2fs.h],[],[AC_MSG_ERROR([Couldn't find or include ext2fs.h])],[])],[])


# Checks for typedefs, structures, and compiler characteristics.
AC_C_INLINE

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([gettimeofday memset setlocale strchr strdup strtoull])


AC_CONFIG_FILES([Makefile])
AC_OUTPUT
