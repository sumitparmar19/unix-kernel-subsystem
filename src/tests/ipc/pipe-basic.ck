# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-basic) begin
(pipe-basic) pipe
(pipe-basic) two new descriptors
(pipe-basic) write 6 bytes
(pipe-basic) read 6 bytes
(pipe-basic) data matches
(pipe-basic) end
pipe-basic: exit(0)
EOF
pass;
