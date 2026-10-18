# -*- perl -*-
use strict; use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(pin-write) begin
(pin-write) create the small file
(pin-write) open to write
(pin-write) write capped at the file's 200 bytes
(pin-write) reopen to verify
(pin-write) read the 200 bytes back
(pin-write) contents intact; the kernel survived the huge-buffer write
(pin-write) end
pin-write: exit(0)
EOF

pass;
