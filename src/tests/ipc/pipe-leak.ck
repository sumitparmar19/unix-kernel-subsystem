# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(pipe-leak) begin
(pipe-leak) created and closed 500 pipes
(pipe-leak) ran 50 redirected children
(pipe-leak) end
EOF
pass;
