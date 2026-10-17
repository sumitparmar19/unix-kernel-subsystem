# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(fd-exhaust) begin
(fd-exhaust) create a file to open repeatedly
(fd-exhaust) set descriptor limit to 16
(fd-exhaust) open was capped at 14 descriptors
(fd-exhaust) process still alive after hitting the fd cap
(fd-exhaust) end
fd-exhaust: exit(0)
EOF
pass;
