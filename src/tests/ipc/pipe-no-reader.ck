# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-no-reader) begin
(pipe-no-reader) pipe
(pipe-no-reader) write with no reader returns -1
(pipe-no-reader) end
pipe-no-reader: exit(0)
EOF
pass;
