%include "macros/patch.inc"
%include "macros/hack.inc"
%include "macros/string.inc"
%include "macros/datatypes.inc"
%include "ini.inc"
%include "macro.inc"
%include "ra95.inc"
%include "patch.inc"

%define EXT_IsSpectator				0x17B0

extern spawner_is_active
extern SpectatorsArray
cextern recording_mode

gbyte SpectatorCount, 0


hack 0x00568F2E, 0x00568F38
    mov edx, dword[HouseClass__PlayerPtr]
    cmp dword[edx+EXT_IsSpectator], 1
    jz .Spectator
    test byte[recording_mode], 2
    jnz .Spectator

    ; original code
    cmp dword[ebp-0x60], 0
    je 0x0056900B
    jmp hackend

.Spectator:
    jmp hackend

    
hack 0x005625D7, 0x005625DF
    mov edx, dword[HouseClass__PlayerPtr]
    cmp dword[edx+EXT_IsSpectator], 1
    jz .Spectator
    test byte[recording_mode], 2
    jnz .Spectator

    ; original code
    test dword[esi+0x8b], eax
    je 0x0056260F
    jmp hackend

.Spectator:
    jmp hackend


hack 0x0045551C, 0x00455528 ; spy mode - show what is being built
    mov edx, dword[HouseClass__PlayerPtr]
    cmp dword[edx+EXT_IsSpectator], 1
    jz .Spectator
    test byte[recording_mode], 2
    jnz .Spectator

    ; original code
    test dword[esi+0x8B], eax ; building spied check
    je 0x0045560E
    jmp hackend

.Spectator:
    jmp hackend

    
hack 0x0054D8A5, 0x0054D8B0 ; SidebarClass::Draw_It(int) - Draw live stats
    pushad
    mov eax, dword[HouseClass__PlayerPtr]
    cmp dword[eax+EXT_IsSpectator], 1
    jz .Draw_Stats
    test byte[recording_mode], 2
    jz .out
.Draw_Stats:
    call DrawLiveStats

.out:
    popad
    
    test ebx, ebx
    jne 0x0054D8B2
    test byte[esi+0x15F6], 02
    jmp hackend


@HACK 0x00532855, _RadarClass__Draw_Names__Draw_Credits_Count_For_Specator
	push eax
	mov dword eax, [0x00669958] ; PlayerPtr
	cmp dword [eax+EXT_IsSpectator], 1
	jz .Is_Spectator
	test byte[recording_mode], 2
	jnz .Is_Spectator
	pop eax
	add eax, edi

.Ret:
	cmp cl, 14h
	jmp 0x0053285A

.Draw_Credits_Count:
	mov eax, ebx
	call 0x004D5E00 ; long const HouseClass::Available_Money(void)const  proc near
	jmp .Ret

.Is_Spectator:
	pop eax
	jmp .Draw_Credits_Count
@ENDHACK	

@HACK 0x005326A2, _RadarClass__Draw_Names__Draw_Credits_Text_For_Specator
;	push eax
	mov dword eax, [0x00669958] ; PlayerPtr
	cmp dword [eax+EXT_IsSpectator], 1
	jz .Draw_Credits_Text
	test byte[recording_mode], 2
	jnz .Draw_Credits_Text
	push 12Ah
.Ret:
;	pop eax
	jmp 0x005326A7
	
.Draw_Credits_Text:
	push 0xDF
	jmp .Ret
@ENDHACK

@HACK 0x0045EF20, _BuildingClass__Read_INI_Skip_Dead_Houses
	call 0x004CD0E4 ; HouseTypeClass::From_Name(char *)
	mov bl, al
	mov edx, 0x005E8F5D ; ","
	mov bh, al
	cmp al, 0FFh
	jz 0x0045EF2E ; Code is different for buildings than for other stuff like infantry
	
	pushad
	
	call 0x004D2CB0    ; HouseClass * HouseClass::As_Pointer(HousesType)
    test byte [eax+0x43], 1
    jnz .Next_Iteration
	
	popad	
	jmp 0x0045EF2E
	
.Next_Iteration:
	popad
	jmp 0x0045EED8
@ENDHACK

@HACK 0x004F095B, _InfantryClass__Read_INI_Skip_Dead_Houses
	call 0x004CD0E4 ; HouseTypeClass::From_Name(char *)
	mov bh, al
	cmp al, 0FFh
	jz 0x004F0913
	
	pushad
	
	call 0x004D2CB0 ; HouseClass * HouseClass::As_Pointer(HousesType)
    test byte [eax+0x43], 1
    jnz .Next_Iteration
	
	popad	
	jmp 0x004F0966
.Next_Iteration:
	popad
	jmp 0x004F0913
@ENDHACK

@HACK 0x0058CAD3, _VesselClass__Read_INI_Skip_Dead_Houses
	call 0x004CD0E4 ; HouseTypeClass::From_Name(char *)
	mov bh, al
	cmp al, 0FFh
	jz 0x0058CA8B

	pushad
	
	call 0x004D2CB0    ; HouseClass * HouseClass::As_Pointer(HousesType)
    test byte [eax+0x43], 1
    jnz .Next_Iteration
	
	popad	
	jmp 0x0058CADE

.Next_Iteration:
	popad
	jmp 0x0058CA8B
@ENDHACK

@HACK 0x00581153, _UnitClass__Read_INI_Skip_Dead_Houses
	call 0x004CD0E4 ; HouseTypeClass::From_Name(char *)
	mov bh, al
	cmp al, 0FFh
	jz 0x0058110B
	pushad
	
	call 0x004D2CB0    ; HouseClass * HouseClass::As_Pointer(HousesType)
    test byte [eax+0x43], 1
    jnz .Next_Iteration
	
	popad
	jmp 0x0058115E

.Next_Iteration:
	popad
	jmp 0x0058110B
@ENDHACK

@HACK 0x004D8CB4, _HouseClass__Init_Data_Spectator_Stuff
    pushad
    mov byte [eax+178Fh], dl
    cmp byte [spawner_is_active], 0
    jz .Ret
    mov ebx, eax
    call 0x004D2C48 ;  const HouseClass::operator HousesType(void)
    cmp byte [SpectatorsArray+eax], 0
    jz .Ret
    
    inc byte[SpectatorCount]
    mov eax, ebx
    or byte [eax+0x43], 1 ; Make house dead
	mov dword [eax+EXT_IsSpectator], 1
;    mov eax, 0x00668250
;    mov edx, 1
;    call 0x0052D790
;    mov eax, 0x00668250
;    mov edx, 3
;    call 0x0052D790
    
.Ret:
    popad
    jmp 0x004D8CBA
@ENDHACK

@HACK 0x0053E4FB, _Create_Units_Skip_Dead_Houses
    cmp byte [spawner_is_active], 0
    jz .Ret
    test byte [eax+0x43], 1
    jnz .Spectator

.Ret:
    cmp dword [ebp-0x8C], 0
    jmp 0x0053E502
	
.Spectator:
	jmp 0x0053E4D6
@ENDHACK

@HACK 0x0053DFD7, _Assign_Houses_Set_Up_Player_Pointer
    mov dword [0x00669958], edi
    test byte [recording_mode], 2
    jnz .Replay
    cmp byte [spawner_is_active], 0
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
@ENDHACK

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

hack 0x004D4D5C, 0x004D4D64
    test byte [recording_mode], 2
    jnz .Replay_GPS
    mov cl, byte [ecx+0x44]
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