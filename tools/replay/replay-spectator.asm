%include "macros/patch.inc"
%include "macros/hack.inc"
%include "macros/extern.inc"
%include "macros/datatypes.inc"

cextern recording_mode

hack 0x00568F2E, 0x00568F38
    mov edx, dword [0x00669958]
    cmp dword [edx+0x17B0], 1
    jz .Spectator
    test byte [recording_mode], 2
    jnz .Spectator
    cmp dword [ebp-0x60], 0
    je 0x0056900B
    jmp hackend
.Spectator:
    jmp hackend

hack 0x005625D7, 0x005625DF
    mov edx, dword [0x00669958]
    cmp dword [edx+0x17B0], 1
    jz .Spectator
    test byte [recording_mode], 2
    jnz .Spectator
    test dword [esi+0x8B], eax
    je 0x0056260F
    jmp hackend
.Spectator:
    jmp hackend

hack 0x0045551C, 0x00455528
    mov edx, dword [0x00669958]
    cmp dword [edx+0x17B0], 1
    jz .Spectator
    test byte [recording_mode], 2
    jnz .Spectator
    test dword [esi+0x8B], eax
    je 0x0045560E
    jmp hackend
.Spectator:
    jmp hackend

hack 0x0054D8A5, 0x0054D8B0
    pushad
    mov eax, dword [0x00669958]
    cmp dword [eax+0x17B0], 1
    jz .Draw_Stats
    test byte [recording_mode], 2
    jz .out
.Draw_Stats:
    call 0x006F1390
.out:
    popad
    test ebx, ebx
    jne 0x0054D8B2
    test byte [esi+0x15F6], 2
    jmp hackend

@HACK 0x00532855, _Replay_Credits_Count
    push eax
    mov eax, dword [0x00669958]
    cmp dword [eax+0x17B0], 1
    jz .Is_Spectator
    test byte [recording_mode], 2
    jnz .Is_Spectator
    pop eax
    add eax, edi
.Ret:
    cmp cl, 0x14
    jmp 0x0053285A
.Draw_Credits_Count:
    mov eax, ebx
    call 0x004D5E00
    jmp .Ret
.Is_Spectator:
    pop eax
    jmp .Draw_Credits_Count
@ENDHACK

@HACK 0x005326A2, _Replay_Credits_Text
    mov eax, dword [0x00669958]
    cmp dword [eax+0x17B0], 1
    jz .Draw_Credits_Text
    test byte [recording_mode], 2
    jnz .Draw_Credits_Text
    push 0x12A
.Ret:
    jmp 0x005326A7
.Draw_Credits_Text:
    push 0xDF
    jmp .Ret
@ENDHACK

hack 0x0053DFD7, 0x0053DFDD
    mov dword [0x00669958], edi
    test byte [recording_mode], 2
    jnz .Replay
    cmp byte [0x0070147D], 0
    jz .Ret
    test byte [eax+0x43], 1
    jz .Ret
    mov dword [0x0065D7F0], 1
    mov dword [0x0067F315], 1
.Ret:
    jmp 0x0053DFDD
.Replay:
    mov dword [0x0065D7F0], 1
    jmp 0x0053DFDD

hack 0x0052D76E, 0x0052D775
    test byte [recording_mode], 2
    jz .Original
    pushad
    mov eax, 0x00668250
    mov edx, 3
    call 0x0052D790
    popad
.Original:
    lea esp, [ebp-0x8]
    pop edx
    pop ebx
    pop ebp
    ret

hack 0x004D4D5C, 0x004D4D67
    test byte [recording_mode], 2
    jnz .Replay_GPS
    mov cl, byte [ecx+0x44]
    mov [ebp-0x3C], eax
    test cl, 8
    je 0x004D4D71
    jmp .GPS_Active
.Replay_GPS:
.GPS_Active:
    xor esi, esi
    mov [ebp-0x3C], esi
    jmp 0x004D4DF2

hack 0x004D4E78, 0x004D4E82
    test byte [recording_mode], 2
    jnz 0x004D4F02
    test byte [eax+0x44], 8
    jnz 0x004D4F02
    jmp 0x004D4E82

hack 0x004D4E8E, 0x004D4E98
    test byte [recording_mode], 2
    jnz 0x004D4F02
    test byte [eax+0x44], 8
    jnz 0x004D4F02
    jmp 0x004D4E98
