%include "macros/patch.inc"
%include "macros/hack.inc"
%include "macros/extern.inc"
%include "macros/datatypes.inc"

cextern recording_mode

@HACK 0x0054D656, _Replay_Add_No_Force_Open
    test byte [recording_mode], 0x02
    jnz 0x0054D65B
    call 0x0054DA70
    jmp 0x0054D65B
@ENDHACK

@HACK 0x0054D88F, _Replay_Sidebar_Draw_Empty_Building_Strip
    test byte [recording_mode], 0x02
    jz .Draw_Original
    push eax
    push dword [eax+0x35]
    mov dword [eax+0x35], 0
    call 0x0054E6FC
    pop ecx
    pop edx
    mov [edx+0x35], ecx
    jmp 0x0054D894
.Draw_Original:
    call 0x0054E6FC
    jmp 0x0054D894
@ENDHACK

@HACK 0x0054D8A0, _Replay_Sidebar_Draw_Empty_Unit_Strip
    test byte [recording_mode], 0x02
    jz .Draw_Original
    push eax
    push dword [eax+0x35]
    mov dword [eax+0x35], 0
    call 0x0054E6FC
    pop ecx
    pop edx
    mov [edx+0x35], ecx
    jmp 0x0054D8A5
.Draw_Original:
    call 0x0054E6FC
    jmp 0x0054D8A5
@ENDHACK
