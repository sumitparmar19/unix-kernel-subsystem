# -*- perl -*-
use strict; use warnings;
use tests::tests;
check_expected (IGNORE_USER_FAULTS => 1, [<<'EOF']);
(mem-hog) begin
(mem-hog) set page limit to 64
(mem-hog) growing memory past the page limit
mem-hog: exit(-1)
EOF
pass;
