# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(redir-pipeline) begin
(redir-pipeline) two pipes
PIPES CONNECT PROCESSES
(redir-pipeline) all three processes exited 0
(redir-pipeline) end
EOF
pass;
