# See if the linker supports symbol versioning
AC_DEFUN([CHECK_LD_VERSIONSCRIPT],[
AC_MSG_CHECKING([whether the linker ($LD) supports symbol versioning])
ld_versionscript=no
if test "$ld_shlibs" = yes -a "$enable_shared" = yes; then
  if test "$with_gnu_ld" = yes; then
    if test -n "`$LD --help 2>/dev/null | grep version-script`"; then
      ld_versionscript=yes
      VERSIONSCRIPT_OPTS="-Wl,--version-script=libavfs.map"
    fi
  else
    case $host_os in
    solaris*|sunos4*)
      if test -n "`$LD --help 2>&1 | grep "M mapfile"`"; then
        ld_versionscript=yes
        VERSIONSCRIPT_OPTS="-Wl,-M,libavfs.map"
      fi
      ;;
    *)
      ;;
    esac
  fi
fi
AC_SUBST(VERSIONSCRIPT_OPTS)
AC_MSG_RESULT([$ld_versionscript])
])
