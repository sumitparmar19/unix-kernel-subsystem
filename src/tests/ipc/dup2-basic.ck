# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(dup2-basic) begin
(dup2-basic) create "dup.txt" (6 bytes)
(dup2-basic) open "dup.txt"
(dup2-basic) dup2 (fd, 7)
(dup2-basic) write through fd 7
(dup2-basic) the original fd shares the offset
(dup2-basic) dup2 of a descriptor onto itself
(dup2-basic) dup2 from a closed descriptor fails
(dup2-basic) dup2 to a negative descriptor fails
(dup2-basic) save stdout as fd 9
(dup2-basic) stdout restored
(dup2-basic) file holds 6 bytes
(dup2-basic) file reads "abcdef"
(dup2-basic) end
dup2-basic: exit(0)
EOF
pass;
