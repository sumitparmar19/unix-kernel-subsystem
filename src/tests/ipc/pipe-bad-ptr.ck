# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-bad-ptr) begin
pipe-bad-ptr: exit(-1)
EOF
pass;
