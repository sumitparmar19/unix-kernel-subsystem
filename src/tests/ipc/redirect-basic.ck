# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(redirect-basic) begin
(redirect-basic) pipe
(redirect-basic) start child with stdout = pipe
(redirect-basic) child exited 0
(redirect-basic) read "hello from child"
(redirect-basic) end
EOF
pass;
