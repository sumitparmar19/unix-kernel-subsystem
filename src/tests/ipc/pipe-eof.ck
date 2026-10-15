# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pipe-eof) begin
(pipe-eof) pipe
(pipe-eof) write 3 bytes
(pipe-eof) read the 3 buffered bytes
(pipe-eof) then read returns 0 (EOF)
(pipe-eof) end
pipe-eof: exit(0)
EOF
pass;
