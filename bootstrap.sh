#!/bin/sh

set -x
aclocal
autoheader
automake --add-missing --include-deps --copy
autoconf
