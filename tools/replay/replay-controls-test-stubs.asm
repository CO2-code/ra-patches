%include "macros/extern.inc"

cglobal ReplayControlsPaused
cglobal ReplayControls_Pump
cglobal ReplayPanel_Frame
cglobal ReplayPanel_Draw
cglobal ReplayPanel_Poll

[section .bss]
ReplayControlsPaused: resd 1

[section .text]
ReplayControls_Pump:
    ret

ReplayPanel_Frame:
    ret

ReplayPanel_Draw:
    ret

ReplayPanel_Poll:
    ret
