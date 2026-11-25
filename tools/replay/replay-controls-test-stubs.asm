%include "macros/extern.inc"

cglobal ReplayControlsPaused
cglobal ReplayControls_Pump

[section .bss]
ReplayControlsPaused: resd 1

[section .text]
ReplayControls_Pump:
    ret
