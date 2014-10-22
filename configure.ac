dnl $Id: configure.ac,v 1.26 2003/10/17 14:35:04 cco Exp $
dnl Process this file with autoconf to produce a configure script
dnl autoconf version: 2.54, automake 1.7.1, libtool 1.4.3
dnl WARNING: libtool 1.4.2 generates a libtool script that won't work
dnl  on *BSD (it will eat -lc_r)
dnl
dnl History:
dnl --------
dnl 2003-03-24  created by andrei
dnl 2003-03-26  fixed "./" path for cc*.sh (andrei)
dnl 2003-03-27  added libltdl support (andrei)
dnl 2003-04-06  fixed sched_yield (added AC_CHECK_FUNCS) (andrei)
dnl             added automake 1.4 backward compatibility stuff (andrei)
dnl             added big endian check & config.cache (andrei)
dnl 2003-04-09  added config.h generation preliminary support
dnl             moved __CPU_... & __OS_... to CPPFLAGS (andrei)
dnl 2003-04-10  debug build without optimizations (andrei)
dnl
dnl TODO:
dnl -----
dnl  -add locking tests (define HAVE_blabla)



AC_PREREQ(2.53)
AC_REVISION($Id: configure.ac,v 1.26 2003/10/17 14:35:04 cco Exp $)
AC_INIT(disc, 0.2.0)
AC_CONFIG_SRCDIR([src/aaa_lock.c])
##AC_CONFIG_HEADERS([ac_config.h])
AC_CONFIG_SUBDIRS(libltdl)
# as suggested by libtoolize
AC_CONFIG_MACRO_DIR([m4])
#AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION, 
#					[nostdinc dist-bzip2 dist-shar dist-zip dist-tarZ])
AM_INIT_AUTOMAKE
# AM_MAINTAINER_MODE

dnl use a default cache file (autoconf2 uses /dev/null by default)
if test "$cache_file" = /dev/null; then
	cache_file="config.cache"
	# WARNING: a cache file is valid only for one compiler a.s.o
fi
AC_CACHE_LOAD

AC_DEFINE(NAME, ["]AC_PACKAGE_NAME["], [display name])
AC_DEFINE(VERSION, ["]AC_PACKAGE_VERSION["], [display version])


dnl Command line options
AC_ARG_ENABLE(debug_build,
				AC_HELP_STRING([--enable-debug_build],
							[compile with debugging support (default is NO)]))
if test "$enable_debug_build" = no; then
	enable_debug_build=""
fi

AC_ARG_ENABLE(debug,
				AC_HELP_STRING([--disable-debug],
							[disable verbose debugging info (default is NO)]))
if test -z "$enable_debug"; then
	enable_debug=yes
fi
if test "$enable_debug" = no; then
	AC_DEFINE(NO_DEBUG,,[all DBG prints are disabled])
fi

AC_ARG_ENABLE(extra_debug,
				AC_HELP_STRING([--enable-extra_debug],
							[enable special debugging code (default is NO)]))
if test -z "$enable_extra_debug"; then
	enable_extra_debug=no
fi
if test "$enable_extra_debug" = yes; then
	AC_DEFINE(EXTRA_DEBUG,,[extra debugging information and checks])
fi

AC_ARG_ENABLE(malloc_debug,
				AC_HELP_STRING([--disable-malloc_debug],
							[disable malloc debugging code (default is NO)]))
if test -z "$enable_malloc_debug"; then
	enable_malloc_debug=yes
fi
if test "$enable_malloc_debug" = yes; then
	AC_DEFINE(DBG_QM_MALLOC,,[malloc debugging support])
fi

AC_ARG_ENABLE(log,
				AC_HELP_STRING([--disable-log],
						[disables loging (not a good ideea, default is NO)]) )
if test -z "$enable_log"; then
	enable_log=yes
fi
if test "$enable_log" = no; then
	AC_DEFINE(NO_LOG,,[all LOGs are disabled, WARNING: nothing will be printed])
fi

AC_ARG_ENABLE(ipv6, 
				AC_HELP_STRING([--disable-ipv6], 
								[disable ipv6 support (default is NO)]) )
if test -z "$enable_ipv6"; then
	enable_ipv6=yes
fi
if test "$enable_ipv6" = yes; then
	AC_DEFINE(USE_IPV6,,[compile in ipv6 support])
fi

AC_ARG_ENABLE(dns_ip_hack, 
				AC_HELP_STRING([--disable-dns_ip_hack],
                        [disable fast ip address conversion (default is NO)]))
if test -z "$enable_dns_ip_hack"; then
	enable_dns_ip_hack=yes
fi
if test "$enable_dns_ip_hack" = yes; then
	AC_DEFINE(DNS_IP_HACK,,[use fast string to ip address conversion])
fi
	
dnl modules 

dnl init to nothing

server_modules=""
client_modules=""
server_module_dir=server
client_module_dir=client
	
dnl AC_ARG_WITH(mipv6_modules, 
dnl 				AC_HELP_STRING([--with-mipv6_modules],
dnl                         [compile both the mipv6 server and client module (default is NO)]))
dnl 
dnl if test -z "$with_mipv6_modules"; then
dnl 	with_mipv6_modules=no
dnl fi
dnl if test "$with_mipv6_modules" = yes; then
dnl 	with_mipv6_server_module=yes
dnl 	with_mipv6_client_module=yes
dnl fi
dnl 
dnl AC_ARG_WITH(mipv6_server_module, 
dnl 				AC_HELP_STRING([--with-mipv6_server_module],
dnl                         [compile the mipv6 server module (default is NO)]))
dnl if test -z "$with_mipv6_server_module"; then
dnl 	with_mipv6_server_module=no
dnl fi
dnl 
dnl if test "$with_mipv6_server_module" = yes; then
dnl 	extra_output="$extra_output src/server/modules/mipv6_server/Makefile \
dnl 					src/server/modules/mipv6_server/asm/Makefile \
dnl 					src/server/modules/mipv6_server/asm/copsApi/Makefile"
dnl 	AC_CONFIG_FILES([ src/server/modules/mipv6_server/Makefile
dnl 					  src/server/modules/mipv6_server/asm/Makefile
dnl 					  src/server/modules/mipv6_server/asm/copsApi/Makefile ])
dnl 	server_modules="$server_modules mipv6_server"
dnl 	ssl_check=yes
dnl fi

dnl AC_ARG_WITH(mipv6_client_module, 
dnl 				AC_HELP_STRING([--with-mipv6_client_module],
dnl                         [compile the mipv6 client module (default is NO)]))
dnl if test -z "$with_mipv6_client_module"; then
dnl 	with_mipv6_client_module=no
dnl fi
dnl 
dnl if test "$with_mipv6_client_module" = yes; then
dnl 	extra_output="$extra_output src/client/modules/mipv6/Makefile"
dnl 	AC_CONFIG_FILES([src/client/modules/mipv6/Makefile
dnl 						src/client/modules/mipv6/libmeter/Makefile
dnl 						src/client/modules/mipv6/logger/Makefile
dnl 						src/client/modules/mipv6/logger/common/Makefile
dnl 						src/client/modules/mipv6/logger/regLogger/Makefile ])
dnl 	client_modules="$client_modules mipv6"
dnl 	ssl_check=yes
dnl dnl add here mipv6/urp autoconf ?
dnl     AC_CONFIG_SUBDIRS(src/client/modules/mipv6/urp-0.0.7)
dnl fi

dnl FIXME: replace the following defines as above

#AC_DEFINE(PKG_MALLOC)
AC_DEFINE(SHM_MEM,,[enable shared memory support (shm_malloc)])
AC_DEFINE(SHM_MMAP,,[use mmap to obtain shared memory (not needed for threads)])
AC_DEFINE(ADAPTIVE_WAIT,,[fastlock: busy wait for a while and then yield])
AC_DEFINE(ADAPTIVE_WAIT_LOOPS, 1024, [how many loops to busy wait])
#AC_DEFINE(F_MALLOC)
dnl FIXME: add the missing ones

dnl cpu specific stuff

AC_CANONICAL_HOST
case "${host_cpu}" in
	i[[3456]]86)	cpu=i386
						;;
	sparc*)			cpu=sparc
						;;
	arm*)			cpu=arm
						;;
	*) cpu=${host_cpu} ;;
esac

os=`echo $host_os|sed -e 'y/-./__/'`
AC_DEFINE_UNQUOTED([ARCH], ["${cpu}"], [cpu type] )
##AC_DEFINE_UNQUOTED([__CPU_${cpu}],, [cpu type used internally])
AC_DEFINE_UNQUOTED([OS],   ["${host_os}"], [os name])
##AC_DEFINE_UNQUOTED([__OS_${os}],, [os name used internally])
dnl  -D__CPU_ cannot be added to ac_config.h
CPPFLAGS="${CPPFLAGS} -D__CPU_${cpu} -D__OS_${os} \
			-DCFG_DIR='\"${sysconfdir}/${PACKAGE}\"' \
			-DCFG_FILE='\"${sysconfdir}/${PACKAGE}/disc.cfg\"'"

dnl Checks for programs
AC_PROG_CC

AC_PROG_CXX
AC_PROG_CXXCPP

AM_PROG_LEX
#AC_DECL_YYTEXT
AC_PROG_YACC
dnl libtool & libltdl
AC_LIBTOOL_DLOPEN
AC_LIBLTDL_CONVENIENCE
AC_PROG_LIBTOOL
AC_PROG_MAKE_SET
AC_PROG_INSTALL

dnl get/set compiler flags
export srcdir
if test -z "$enable_debug_build" ; then
	CFLAGS=`$srcdir/ccopts.sh $CC`
else
	CFLAGS= -g
fi
CCVER_OUTPUT=`$srcdir/ccver.sh $CC`
CCVER=`echo $CCVER_OUTPUT| cut -d" " -f2`
CCNAME=`echo $CCVER_OUTPUT| cut -d" " -f1`

dnl compiler specific stuff
case "${CCNAME}" in
	suncc)
			LIBS="$LIBS -lfast"
			;;
	gcc|icc)
			case "${cpu}" in
				i386|sparc|arm)
				dnl FAST_LOCK inline asm doesn't work with all the compilers
							fast_lock=yes
							;;
			esac
			;;
		*)
			AC_MSG_WARN([*** unknown compiler $CCNAME, trying defaults ***])
			;;
esac

if test "$fast_lock" = yes ; then
	AC_DEFINE(FAST_LOCK,,[enable fast locks])
else
	AC_MSG_WARN([*** disabling fast locking for $CCNAME $cpu ***])
fi

AC_DEFINE_UNQUOTED([COMPILER], ["$CC $CCVER"], [compile name & version])

dnl Endianess check
AC_C_BIGENDIAN


dnl Checks for libraries (warning for HAVE_* run AC_CHECK_FUNCTION)
#AC_SEARCH_LIBS([dlopen], [dl c_r])
AC_SEARCH_LIBS([pthread_create], [pthread c_r])
#AC_SEARCH_LIBS([res_search], [resolv])
dnl solaris libs
AC_SEARCH_LIBS([sendto], [socket xnet])
AC_SEARCH_LIBS([gethostbyname], [nsl xnet])
AC_SEARCH_LIBS([sched_yield], [rt])
dnl gethostbyname2/getipnodebyname!
AC_SEARCH_LIBS([gethostbyname2], [nsl xnet])
AC_SEARCH_LIBS([getipnodebyname], [nsl xnet])
dnl ?libfast on Solaris??


dnl Check for openssl -- needed by mipv6 module FIXME - move it to 
dnl mipv6 module cfg
if test "$ssl_check" = yes; then
	AC_CHECK_LIB(ssl, main)
fi
dnl Checks for header files
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
#AC_CHECK_HEADERS([sys/sockio.h])
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netdb.h netinet/in.h stddef.h stdlib.h string.h strings.h sys/ioctl.h sys/socket.h sys/time.h syslog.h unistd.h ], , AC_MSG_ERROR(["*** not all required *.h files found - please fix ***"]))


dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_HEADER_TIME
AC_STRUCT_TM
AC_CHECK_TYPE([union semun], AC_DEFINE(HAVE_UNION_SEMUN,,
										[is union semun present?]),,
				[#include <sys/sem.h> ])

dnl  Checks for library functions.
## AC_FUNC_FORK
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_MMAP
## AC_FUNC_REALLOC
AC_TYPE_SIGNAL
## AC_FUNC_STRFTIME
## AC_FUNC_VPRINTF

 #AC_CHECK_FUNCS([gethostbyaddr gettimeofday inet_ntoa localtime_r memchr memmove memset mkfifo munmap regcomp select socket strcasecmp strchr strdup strerror strncasecmp strtol uname pthread_create res_search sendto], ,
 AC_CHECK_FUNCS([gethostbyaddr gettimeofday inet_ntoa localtime_r memchr memmove memset mkfifo munmap regcomp select socket strcasecmp strchr strdup strerror strncasecmp strtol uname pthread_create sendto], ,
 AC_MSG_ERROR(["*** not all required functions found - please fix ***"]))
 AC_CHECK_FUNCS([sched_yield],,)

dnl Checks for "special" functions
AC_CHECK_FUNCS([gethostbyname], ,
			AC_MSG_ERROR(["*** gethostbyname not found - please fix ***"]))
if test "$enable_ipv6" = yes ; then
	AC_CHECK_FUNCS([gethostbyname2], [use_gethostbyname2=yes],
									[no_gethostbyname2=yes] )
	if test "$no_gethostbyname2" = yes ; then
		AC_CHECK_FUNCS([getipnodebyname], [use_getipnodebyname=yes],
				AC_MSG_ERROR(["*** neither gethostbyname2 or getipnodebyaddr present - fix or compile without ipv6 support ***"]))
	fi
fi


dnl Checks for structure members
AC_CHECK_MEMBERS([struct sockaddr.sa_len], AC_DEFINE([HAVE_SOCKADDR_SA_LEN],,
					[ does sockaddr have a sa_len member? (common on *BSDs)]),,
[ 
  #if HAVE_SYS_TYPES_H
  #include <sys/types.h>
  #endif
  #if HAVE_SYS_SOCKET_H
  #include <sys/socket.h>
  #endif
 ]
)

dnl substitutions
AC_SUBST(INCLTDL)
AC_SUBST(LIBLTDL)
AC_SUBST(server_modules)
AC_SUBST(client_modules)
AC_SUBST(server_module_dir)
AC_SUBST(client_module_dir)

AC_OUTPUT(Makefile src/Makefile src/client/Makefile src/server/Makefile 
			src/diameter_msg/Makefile
			src/diameter_api/Makefile
			src/mem/Makefile src/transport/Makefile
			src/client/modules/Makefile
			src/client/modules/print/Makefile
			src/server/modules/Makefile
			src/server/modules/print/Makefile
			)
