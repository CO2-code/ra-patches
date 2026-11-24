; Hooks for the replay playback panel (src/replay-panel.c).
%include "macros/patch.inc"
%include "macros/hack.inc"
%include "macros/extern.inc"

cextern ReplayPanel_Draw
cextern Buffer_Fill_Rect
cextern GraphicsViewPortClass_HidPage

%define GraphicBuffer_HiddenPage    0x00680700
%define GraphicBufferClass__Lock    0x005C101A
%define GraphicBufferClass__Unlock  0x005C1191

; GScreenClass::Render: after the map, sidebar, buttons and message list were drawn to
; the hidden page and right before Blit_Display, so the panel is always on top.
; Replaces: mov edx,[ebx+4] / mov eax,ebx
hack 0x004CB1A3, 0x004CB1A8
    pushad
    call ReplayPanel_Draw
    popad
    mov edx, [ebx+4]
    mov eax, ebx
    jmp hackend

; void ReplayPanel_FillRect(int x1, int y1, int x2, int y2, int color)  (cdecl, x2/y2 inclusive)
; Same sequence the game's inline GraphicViewPortClass::Fill_Rect uses: Lock, Buffer_Fill_Rect, Unlock.
[section .text]
cglobal ReplayPanel_FillRect
ReplayPanel_FillRect:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov eax, GraphicBuffer_HiddenPage
    call GraphicBufferClass__Lock
    test eax, eax
    jz .done

    push dword [ebp+24]     ; color
    push dword [ebp+20]     ; y2
    push dword [ebp+16]     ; x2
    push dword [ebp+12]     ; y1
    push dword [ebp+8]      ; x1
    push dword GraphicsViewPortClass_HidPage
    call Buffer_Fill_Rect
    add esp, 24

    mov eax, GraphicBuffer_HiddenPage
    call GraphicBufferClass__Unlock
.done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
