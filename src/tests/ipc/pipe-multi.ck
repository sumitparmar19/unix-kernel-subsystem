# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(pipe-multi) begin
(pipe-multi) pipe
(pipe-multi) started 3 writers
(pipe-multi) all writers exited 0
(pipe-multi) read 3000 bytes from each writer
(pipe-multi) end
EOF
pass;
