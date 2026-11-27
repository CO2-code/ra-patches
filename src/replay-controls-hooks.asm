; Hooks and Watcom ABI shims for in-game replay playback controls.
%include "macros/patch.inc"
%include "macros/datatypes.inc"

cextern NativeReplay_ModeValue
cextern NativeReplay_IsAtEnd

cextern pb_restoring_snap
cextern pb_saving_snap

cextern pb_on_pre_render
cextern pb_on_render
cextern pb_on_gscreen_input
cextern pb_on_wndproc
cextern pb_on_main_loop_top
cextern pb_on_main_loop_timer
cextern pb_on_sync_delay_end

cglobal pb_watcom_call_back
cglobal pb_watcom_keyboard_check
cglobal pb_watcom_render_sync_delay
cglobal pb_watcom_flag_redraw
cglobal pb_watcom_override_mouse_normal
cglobal pb_watcom_set_tactical_pos
cglobal pb_watcom_save_game
cglobal pb_watcom_load_game
cglobal pb_watcom_hidpage_lock
cglobal pb_watcom_hidpage_unlock


; 0. GScreenClass::Render pre-Draw_It: restore clean background under bar before dirty tiles redraw
hack 0x004CB133, 0x004CB138
    pushfd
    pushad
    call pb_on_pre_render
    popad
    popfd
    mov edx, [ebx]
    mov esi, [ebx+4]
    jmp hackend

; 1. GScreenClass::Render: draw playback bar over tactical/sidebar, under WWMouse cursor
hack 0x004CB140, 0x004CB146
    pushfd
    pushad
    call pb_on_render
    popad
    popfd
    mov edx, [0x00680900]
    jmp hackend

; 2. GScreenClass::Input: intercept bar hover/drag before Map.AI(&key, mouseX, mouseY)
hack 0x004CB093, 0x004CB099
    pushfd
    pushad
    push dword [ebp-0x0C]
    push ebx
    push edi
    call pb_on_gscreen_input
    add esp, 12
    test eax, eax
    jnz .bar_owns_input
    popad
    popfd
    mov ecx, [ebp-0x0C]
    mov eax, [esi+4]
    jmp hackend
.bar_owns_input:
    popad
    popfd
    jmp 0x004CB0AA

; 3. WndProc: intercept bar mouse clicks/drags and replay hotkeys before Message_Handler
hack 0x005B37D6, 0x005B37DC
    pushfd
    pushad
    push dword [ebp+0x14]
    push dword [ebp+0x10]
    push esi
    push dword [ebp+0x08]
    call pb_on_wndproc
    add esp, 16
    test eax, eax
    jnz .swallow_msg
    popad
    popfd
    mov ecx, [ebp+0x14]
    mov edx, [ebp+0x08]
    jmp hackend
.swallow_msg:
    popad
    popfd
    xor eax, eax
    jmp 0x005B390B

; 4. Main_Loop prologue (clear of WWDEBUG 0x004A7C8B): capture frame-boundary keyframes
hack 0x004A7C7C, 0x004A7C82
    sub esp, 0xB4
    pushfd
    pushad
    call pb_on_main_loop_top
    popad
    popfd
    jmp hackend

; 5. Main_Loop timer/render check: speed scaling and fast-seek render skip
hack 0x004A7E7B, 0x004A7E82
    pushfd
    pushad
    call pb_on_main_loop_timer
    test eax, eax
    jnz .skip_render_main_loop
    popad
    popfd
    cmp byte [0x00680834], 0
    jmp hackend
.skip_render_main_loop:
    popad
    popfd
    jmp 0x004A7ECD

; 6. Sync_Delay end: pause loop, snapshot restore, forward seek completion, EOF stop
hack 0x004A7C60, 0x004A7C65
    call 0x004A735C
    pushfd
    pushad
    call pb_on_sync_delay_end
    test eax, eax
    jnz .fast_seek_exit
    popad
    popfd
    jmp hackend
.fast_seek_exit:
    popad
    popfd
    jmp 0x004A7C6A

; 7. Queue_Playback EOF: keep playback window open at end of replay so pause loop can engage
hack 0x0052BFBD, 0x0052BFC3
    cmp dword [NativeReplay_ModeValue], 2
    jne .orig_eof
    pushfd
    pushad
    call NativeReplay_IsAtEnd
    test eax, eax
    jnz .keep_open
    popad
    popfd
.orig_eof:
    mov [0x00669924], ecx
    jmp hackend
.keep_open:
    popad
    popfd
    jmp hackend

; 8. Put_All: include multiplayer values when saving keyframe snapshots
hack 0x005379DB, 0x005379E1
    cmp dword [pb_saving_snap], 0
    jne 0x005379E1
    cmp dword [ebp-0x24], 0
    je 0x005379E8
    jmp hackend

; 9. Load_Game: load multiplayer values when restoring keyframe snapshots
hack 0x00538704, 0x0053870E
    cmp dword [pb_restoring_snap], 0
    jne .force_mp_load
    test ecx, ecx
    je 0x00538713
.force_mp_load:
    lea eax, [ebp-0x104]
    jmp hackend

; 10. Load_Game: skip BlackPalette fade when restoring keyframe snapshots
hack 0x00538D56, 0x00538D5B
    cmp dword [pb_restoring_snap], 0
    jne 0x00538D82
    mov ebx, 0x004A765C
    jmp hackend

; 11. Load_Game: skip Queue_Song when restoring keyframe snapshots
hack 0x00538ED0, 0x00538ED7
    cmp dword [pb_restoring_snap], 0
    jne 0x00538EF5
    cmp byte [0x00668009], 0xFF
    jmp hackend

[section .text]
align 16, db 0xCC

pb_watcom_call_back:
    push ebx
    push esi
    push edi
    call 0x004A735C
    pop edi
    pop esi
    pop ebx
    ret

pb_watcom_keyboard_check:
    push ebx
    push esi
    push edi
    call 0x004A765C
    pop edi
    pop esi
    pop ebx
    ret

pb_watcom_render_sync_delay:
    push ebx
    push esi
    push edi
    cmp byte [0x004A7C1C], 0xE8
    jne .fallback_render
    mov eax, [0x004A7C1D]
    add eax, 0x004A7C21
    call eax
    pop edi
    pop esi
    pop ebx
    ret
.fallback_render:
    mov eax, 0x00668250
    call 0x004CB110
    pop edi
    pop esi
    pop ebx
    ret

pb_watcom_flag_redraw:
    push ebx
    push esi
    push edi
    mov edx, [esp+16]
    mov eax, 0x00668250
    call 0x004CAFF4
    pop edi
    pop esi
    pop ebx
    ret

pb_watcom_override_mouse_normal:
    push ebx
    push esi
    push edi
    mov eax, 0x00668250
    xor edx, edx
    xor ebx, ebx
    call 0x0050316C
    pop edi
    pop esi
    pop ebx
    ret

pb_watcom_set_tactical_pos:
    push ebx
    push esi
    push edi
    mov edx, [esp+16]
    mov eax, 0x00668250
    mov ecx, [eax+4]
    call [ecx+0x8C]
    pop edi
    pop esi
    pop ebx
    ret

pb_watcom_save_game:
    push ebx
    push esi
    push edi
    mov eax, [esp+16]
    mov edx, [esp+20]
    xor ebx, ebx
    call 0x005379FC
    pop edi
    pop esi
    pop ebx
    ret

pb_watcom_load_game:
    push ebx
    push esi
    push edi
    mov eax, [esp+16]
    call 0x00537D10
    pop edi
    pop esi
    pop ebx
    ret

pb_watcom_hidpage_lock:
    push ebx
    push esi
    push edi
    mov eax, [0x006807E8]
    test eax, eax
    jz .lock_fail
    call 0x005C101A
    test eax, eax
    jz .lock_fail
    mov edx, [0x006807E8]
    cmp edx, 0x006807CC
    je .lock_ok
    mov ecx, [0x006807D4]
    mov eax, [0x006807D0]
    push ecx
    mov ebx, [0x006807DC]
    push eax
    mov ecx, [0x006807E0]
    mov eax, 0x006807CC
    call 0x005C094F
.lock_ok:
    mov eax, 1
    pop edi
    pop esi
    pop ebx
    ret
.lock_fail:
    xor eax, eax
    pop edi
    pop esi
    pop ebx
    ret

pb_watcom_hidpage_unlock:
    push ebx
    push esi
    push edi
    mov eax, [0x006807E8]
    test eax, eax
    jz .unlock_done
    call 0x005C1191
    test eax, eax
    jz .unlock_done
    mov ecx, [0x006807E8]
    cmp ecx, 0x006807CC
    je .unlock_done
    cmp dword [0x006807EC], 0
    je .unlock_done
    mov edx, [ecx+0x24]
    test edx, edx
    jnz .unlock_done
    mov [0x006807CC], edx
.unlock_done:
    pop edi
    pop esi
    pop ebx
    ret
