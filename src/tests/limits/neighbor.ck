# -*- perl -*-
use strict; use warnings;
use tests::tests;

# The abusers and their children exit in an unpredictable order, each
# printing a "<name>: exit(N)" line.  Drop those before matching the
# neighbour test's own deterministic output.
our ($test);
my @output = read_text_file ("$test.output");
@output = grep (!/^(child-work|child-block|abuser-fork|abuser-spin): exit\(-?\d+\)\s*$/, @output);
common_checks ("run", @output);
compare_output ("run", \@output, [<<'EOF']);
(neighbor) begin
(neighbor) started the two abusers
(neighbor) started the well-behaved neighbour
(neighbor) neighbour computed the right answer
(neighbor) neighbour finished within the deadline
(neighbor) end
neighbor: exit(0)
EOF
pass;
