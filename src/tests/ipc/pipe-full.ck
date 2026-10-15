# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(pipe-full) begin
(pipe-full) pipe
(pipe-full) start reader with stdin = pipe
(pipe-full) write 8192 bytes into a 4096-byte pipe
(pipe-full) reader got all bytes in order (status 0)
(pipe-full) end
EOF
pass;
