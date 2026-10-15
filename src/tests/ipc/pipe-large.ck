# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(pipe-large) begin
(pipe-large) pipe
(pipe-large) start checksum child
(pipe-large) wrote 1048576 bytes
(pipe-large) child's checksum matches
(pipe-large) end
EOF
pass;
