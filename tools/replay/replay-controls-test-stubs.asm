%include "macros/extern.inc"

cglobal ReplayControlsPaused
cglobal ReplayControls_Pump
cglobal ReplayPanel_Frame

[section .bss]
ReplayControlsPaused: resd 1

[section .text]
ReplayControls_Pump:
    ret

ReplayPanel_Frame:
    ret
