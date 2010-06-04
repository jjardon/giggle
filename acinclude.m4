AC_DEFUN([IGE_PLATFORM_CHECK],[
    AC_REQUIRE([PKG_PROG_PKG_CONFIG])

    AC_MSG_CHECKING([which GTK+ platform to use...])

    gdk_target=`$PKG_CONFIG --variable=target gtk+-2.0`

    if test "x$gdk_target" = "xquartz"; then
        carbon_ok=no
        AC_TRY_CPP([
        #include <Carbon/Carbon.h>
        #include <CoreServices/CoreServices.h>
        ], carbon_ok=yes)
        if test $carbon_ok = yes; then
          IGE_PLATFORM=osx
          IGE_PLATFORM_NAME="GTK+ OS X"
          AC_DEFINE(IGE_PLATFORM_OSX, 1, [whether GTK+ OS X is available])
        fi
    elif test "x$gdk_target" = "xx11"; then
        IGE_PLATFORM=x11
        IGE_PLATFORM_NAME="GTK+ X11"
        AC_DEFINE(IGE_PLATFORM_X11, 1, [whether GTK+ X11 is available])
    elif test "x$gdk_target" = "xwin32"; then
        IGE_PLATFORM=win32
        IGE_PLATFORM_NAME="GTK+ Windows"
        AC_DEFINE(IGE_PLATFORM_WIN32, 1, [whether GTK+ WIN32 is available])
    else
        AC_MSG_ERROR([Could not detect the platform])
    fi

    AC_MSG_RESULT([$IGE_PLATFORM_NAME])

    AM_CONDITIONAL(IGE_PLATFORM_X11, test $IGE_PLATFORM = x11)
    AM_CONDITIONAL(IGE_PLATFORM_OSX, test $IGE_PLATFORM = osx)
    AM_CONDITIONAL(IGE_PLATFORM_WIN32, test $IGE_PLATFORM = win32)
])
