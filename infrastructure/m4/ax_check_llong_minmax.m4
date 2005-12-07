dnl @synopsis AX_CHECK_LLONG_MINMAX
dnl
dnl This macro will fix up LLONG_MIN and LLONG_MAX as appropriate. I'm finding
dnl it quite difficult to believe that so many hoops are necessary. The world
dnl seems to have gone quite mad.
dnl
dnl This gem is adapted from the OpenSSH configure script so here's
dnl the original copyright notice:
dnl
dnl Copyright (c) 1999-2004 Damien Miller
dnl
dnl Permission to use, copy, modify, and distribute this software for any
dnl purpose with or without fee is hereby granted, provided that the above
dnl copyright notice and this permission notice appear in all copies.
dnl
dnl @category C
dnl @author Martin Ebourne and Damien Miller
dnl @version 2005/07/07

AC_DEFUN([AX_CHECK_LLONG_MINMAX], [
  AC_CHECK_DECL([LLONG_MAX], [have_llong_max=1], , [[#include <limits.h>]])
  if test -z "$have_llong_max"; then
    AC_MSG_CHECKING([[for max value of long long]])
    AC_RUN_IFELSE([AC_LANG_SOURCE([[
      #include <stdio.h>
      /* Why is this so damn hard? */
      #undef __GNUC__
      #undef __USE_ISOC99
      #define __USE_ISOC99
      #include <limits.h>
      #define DATA "conftest.llminmax"
      int main(void) {
        FILE *f;
        long long i, llmin, llmax = 0;

        if((f = fopen(DATA,"w")) == NULL)
          exit(1);

        #if defined(LLONG_MIN) && defined(LLONG_MAX)
        fprintf(stderr, "Using system header for LLONG_MIN and LLONG_MAX\n");
        llmin = LLONG_MIN;
        llmax = LLONG_MAX;
        #else
        fprintf(stderr, "Calculating LLONG_MIN and LLONG_MAX\n");
        /* This will work on one's complement and two's complement */
        for (i = 1; i > llmax; i <<= 1, i++)
          llmax = i;
        llmin = llmax + 1LL;    /* wrap */
        #endif

        /* Sanity check */
        if (llmin + 1 < llmin || llmin - 1 < llmin || llmax + 1 > llmax || llmax - 1 > llmax) {
          fprintf(f, "unknown unknown\n");
          exit(2);
        }

        if (fprintf(f ,"%lld %lld", llmin, llmax) < 0)
          exit(3);

        exit(0);
      }
      ]])], [
      read llong_min llong_max < conftest.llminmax
      AC_MSG_RESULT([$llong_max])
      AC_DEFINE_UNQUOTED([LLONG_MAX], [${llong_max}LL],
                         [max value of long long calculated by configure])
      AC_MSG_CHECKING([[for min value of long long]])
      AC_MSG_RESULT([$llong_min])
      AC_DEFINE_UNQUOTED([LLONG_MIN], [${llong_min}LL],
                         [min value of long long calculated by configure])
      ],
      [AC_MSG_RESULT(not found)],
      [AC_MSG_WARN([[cross compiling: not checking]])]
      )
    fi
  ])dnl
