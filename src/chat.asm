%include "macros/patch.inc"
%include "macros/datatypes.inc"
%include "patch.inc"
%ifdef REPLAY_NATIVE
cextern NativeReplay_RecordChat
%endif

hack 0x004A72AD ; outgoing global chat
    pushad
    call OutgoingMessageAllowed
    cmp al, 1
    popad
    jnz 0x004A731C
    
    call 0x004FAAAC
    jmp hackend

    
hack 0x004A72F4 ; outgoing private chat
    pushad
    call OutgoingMessageAllowed
    cmp al, 1
    popad
    jnz 0x004A731C

    push 0x0067FF53
    jmp hackend

    
hack 0x004A7935, 0x004A7962 ; incoming chat
    pushad
    call IncomingMessageAllowed
    cmp al, 1
    popad
    jnz 0x004A79CE

    %ifdef REPLAY_NATIVE
    pushad
    push dword [0x006680C4]
    movzx eax, byte [0x00680201]
    push eax
    push dword 0x00680189
    push dword 0x0068017D
    call NativeReplay_RecordChat
    add esp, 16
    popad
    %endif
    jmp hackend
