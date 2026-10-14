#!/bin/sh
# gcc wrapper used by the Dockerfile. It restores two defaults that Pintos-era
# GCC had and Ubuntu's modern GCC changed, for 32-bit (-m32) Pintos builds only:
#   -fno-pie -no-pie  a position-independent kernel triple-faults at boot;
#   -fcommon          stock filesys/filesys.h defines `fs_device` in a header,
#                     which links only with common symbols (GCC < 10 default).
# Host tools (utils/) are passed through untouched.
case " $* " in
  *" -m32 "*) exec /usr/bin/gcc -fno-pie -no-pie -fcommon "$@" ;;
  *)          exec /usr/bin/gcc "$@" ;;
esac
