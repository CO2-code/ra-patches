%include "macros/patch.inc"
%include "macros/hack.inc"
%include "macros/string.inc"
%include "macros/datatypes.inc"
%ifdef REPLAY_NATIVE
cextern NativeReplay_RecordChat
%endif

@HACK 0x004A731C, _Message_Input_Display_Message_After_Sending_To_Players 
	pushad
	%ifdef REPLAY_NATIVE
	push dword [0x006680C4]
	movzx eax, byte [0x00680201]
	push eax
	push dword 0x00680189
	push dword 0x0068017D
	call NativeReplay_RecordChat
	add esp, 16
	%endif
	; Calculate duration the message is displayed for
	xor ebx, ebx
	mov bx, [0x006667C7] ; ds:int RulesClass.MessageDelay
	lea eax, [ebx*8]
	sub eax, ebx
	shl eax, 5
	add eax, ebx
	shl eax, 2
	add eax, 80h
	shr eax, 8
	push eax ; Message display duration
	push 0x4046 ; TextPrintType?
	mov al, byte [0x0067F313]
	movsx eax, al
	push eax ; PlayerColorType arg
	mov ecx, 0x00680189
	mov bl, byte [0x00680201]
	movsx ebx, bl
	mov edx, 0x0068017D ; offset byte_0068017D, text to display
	mov eax, 0x0067F5A8; offset MessageListClass_67F5A8
	call 0x0050542C    ; TextLabelClass * MessageListClass::Add_Message(char *, int, char *, PlayerColorType, TextPrintType, int)
.Ret:
	popad
	mov esi, 0x00680189 ; (offset dword_00680186+3)
	jmp 0x004A7321
@ENDHACK