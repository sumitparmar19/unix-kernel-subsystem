# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(getlimit) begin
(getlimit) fd limit starts unlimited
(getlimit) lower fd limit to 32
(getlimit) getlimit reports 32
(getlimit) raising the fd limit is refused
(getlimit) fd limit is still 32
(getlimit) lowering it again to 8 works
(getlimit) getlimit reports 8
(getlimit) end
getlimit: exit(0)
EOF
pass;
