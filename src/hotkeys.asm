%include "macros/patch.inc"
%include "macros/hack.inc"
%include "macros/string.inc"
%include "macros/datatypes.inc"
%include "ini.inc"
%include "macro.inc"

extern keysidebartoggle
extern keymapsnapshot
extern _MouseClass_Map
extern _SessionClass__Session
extern Create_Map_Snapshot
extern spawner_is_active
cextern MessageListClass__EditState
cextern QuickSave
cextern QuickLoad

%define SessionClass__Session 0x0067F2B4
%define KeyResign 0x006681C0

global ResignKeyPressed

gshort KeyQuickSave, 0x50 ; P
gshort KeyQuickLoad, 0x4C ; L
gshort KeyDebug, 0x49 ; I
gshort KeyReplayControls, 0x70 ; F1 (handled in replay-panel.c)
gshort KeyReplayPause, 0x71 ; F2 (handled in replay-panel.c)

[section .data]
ResignKeyPressed: dd 0

@HACK 0x004A603E, _UnhardCode_Keyboard_Key1
	jmp 0x004A6056
@ENDHACK

@HACK 0x004A606E, _UnhardCode_Keyboard_Key2
	jmp 0x004A6089
@ENDHACK

@HACK 0x004A60A1, _UnhardCode_Keyboard_Key3
	jmp 0x004A60BC
@ENDHACK

@HACK 0x004A60D4, _UnhardCode_Keyboard_Key4
	jmp 0x004A60EF
@ENDHACK

@HACK 0x004A6107, _UnhardCode_Keyboard_Key5
	jmp 0x004A6122
@ENDHACK

@HACK 0x004A613A, _UnhardCode_Keyboard_Key6
	jmp 0x004A6155
@ENDHACK

@HACK 0x004A616D, _UnhardCode_Keyboard_Key7
	jmp 0x004A6188
@ENDHACK

@HACK 0x004A61A0, _UnhardCode_Keyboard_Key8
	jmp 0x004A61BB
@ENDHACK

@HACK 0x004A61D3, _UnhardCode_Keyboard_Key9
	jmp 0x004A61EE
@ENDHACK

@HACK 0x004A6206, _UnhardCode_Keyboard_Key0
	jmp 0x004A6221
@ENDHACK

@HACK 0x004A5753, _Keyboard_Process_Home_Key_Overwrite
	cmp word ax, [keysidebartoggle]
	jz .Toggle_Sidebar
    
	cmp word ax, [keymapsnapshot]
	jz .Map_Snapshot
    
	cmp word ax, [KeyResign]
	jz .Resign_Key

    cmp word ax, [KeyQuickSave]
    jz .QuickSave
    
    cmp word ax, [KeyQuickLoad]
    jz .QuickLoad
    
    cmp word ax, [KeyDebug]
    jz .Debug

.Out:
	cmp ax, [0x006681B4] ; KeyNext
	jnz 0x004A57DF
	jmp 0x004A5760
    
    
.QuickSave:
    call QuickSave
    jmp .Out
    
.QuickLoad:
    call QuickLoad
    jmp .Out
    
.Debug:
    pushad
    call 0x005C21E0 ; Show_Mouse(void)
    popad
    jmp .Out

.Toggle_Sidebar:
	push eax
	
;	mov eax, 0 ; Crash	
	mov eax, _MouseClass_Map
	mov edx, 0FFFFFFFFh
	call 0x0054DA70 ;  SidebarClass::Activate(int)
	
	
	mov edx, 1
	mov eax, 0x00668250 ; MouseClass Map
	call 0x004CAFF4 ; GScreenClass::Flag_To_Redraw(int)
	
	mov eax, 0x00668250 ; MouseClass Map
	call 0x004CB110 ; GScreenClass::Render()
	
	pop eax
	jmp .Out

.Resign_Key:
	push eax

	cmp byte [_SessionClass__Session],0
	jz .Out
	mov dword [ResignKeyPressed], 1
	call 0x00528DCC ; Queue_Options(void)
	
	pop eax
	jmp .Out
	
.Map_Snapshot:
%ifdef WWDEBUG
	call Create_Map_Snapshot
%endif
	jmp .Out
@ENDHACK
	
@HACK 0x0054D916, _Patch_Out_Erroneous_Sidebar_Activate_CALL
	jmp 0x0054D91B
@ENDHACK

@HACK 0x004C9F46, _RedrawOptionsMenu_Add_Surrender_Dialog_Flag_Check
	cmp dword [ResignKeyPressed], 0
	jnz 0x004CA9C9

.Out:
	test eax, eax
	jle 0x004CA15E
	jmp 0x004C9F4E
@ENDHACK
	
@HACK	0x004CAA29, _RedrawOptionsMenu_Add_Surrender_Dialog_Flag_Check2
	cmp dword [ResignKeyPressed], 0
	jz 0x004CA7A5 
		
	mov dword [ResignKeyPressed], 0
	lea esp, [ebp-14h]
	pop edi
	pop esi
	pop edx
	pop ecx
	pop ebx
	pop ebp
	retn
@ENDHACK