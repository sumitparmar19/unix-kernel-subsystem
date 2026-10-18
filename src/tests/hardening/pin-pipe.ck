# -*- perl -*-
use strict; use warnings;
use tests::tests;
our ($test);
my @output = read_text_file ("$test.output");
# The 8 readers exit at EOF in an unpredictable order.
@output = grep (!/^pin-pipe-rd: exit\([-0-9]+\)\s*$/, @output);
common_checks ("run", @output);
compare_output ("run", \@output, [<<'EOF']);
(pin-pipe) begin
(pin-pipe) pipe
(pin-pipe) all 8 blocked huge-buffer readers exited cleanly
(pin-pipe) kernel survived the blocked readers
(pin-pipe) end
pin-pipe: exit(0)
EOF
pass;
