# -*- perl -*-
use strict; use warnings;
use tests::tests;

# The 7 contained children (child-block) exit at close() in an
# unpredictable order, each printing "child-block: exit(0)".  Drop
# those lines before matching the parent's deterministic output.
our ($test);
my @output = read_text_file ("$test.output");
@output = grep (!/^child-block: exit\(0\)\s*$/, @output);
common_checks ("run", @output);
compare_output ("run", \@output, [<<'EOF']);
(fork-bomb) begin
(fork-bomb) set process limit to 8
(fork-bomb) pipe
(fork-bomb) exec was capped at 7 children
(fork-bomb) all 7 contained children exited cleanly
(fork-bomb) kernel still works after the bomb
(fork-bomb) end
fork-bomb: exit(0)
EOF
pass;
