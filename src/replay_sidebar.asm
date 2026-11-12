; Replay spectator sidebar: use the spectator presentation during playback.
;
; Vanilla SidebarClass::Activate (0x0054DA70) short-circuits on the playback
; bit before touching any state:
;
;   0x0054DA88  F6 05 51 01 68 00 02   test ds:recording_mode, 2
;   0x0054DA8F  74 33                  jz   0x0054DAC4   ; normal -> live dispatch
;   0x0054DA91  E9 F1 01 00 00         jmp  0x0054DC87   ; playback -> return untouched
;
; (recording_mode = 0x00680151, bit 0x02 = playback, sym.asm)
;
; Consequence during a replay: every activation call returns early -
; Select_Game's Activate(1) at match start (0x004F54ED), Init_IO, Fill_In_Data,
; SidebarClass::Add, TabClass::Set_Active, the hotkey toggle - so
; [MouseClass::Map + 0x15F6] (absolute 0x669846) bit 0 never turns on. That bit
; is the master switch for the whole right-hand column:
;
;   SidebarClass::Draw_It    strips / buttons / background - and the fork's
;                            DrawLiveStats hook (0x0054D8A5) sits inside it
;   PowerClass::Draw_It      0x0052768B
;   RadarClass::Draw_It      0x0052DC0A
;   RadarClass::Draw_Names   0x005325F2 (names / colors / score / credits,
;                            incl. the fork's credit hooks 0x005326A2, 0x00532855)
;   RadarClass::Draw_House_Info 0x00532276
;   Radar_Pixel / Radar_Anim / RadarClass::AI / Click_In_Radar
;
; Fix: both modes take the live argument dispatch at 0x0054DAC4 (edx = 1 force
; ON, -1 toggle, 0 force OFF), which also runs the ON-transition (bit1 redraw
; mark, StripClass::Activate x2, linking of Background / RadarButton /
; PowerButton / repair / sell / zoom).
;
; Live games are behaviorally unchanged: the vanilla jz already fell straight
; to 0x0054DAC4, the removed jmp arm only ever executed under playback, and the
; dispatch's first instruction (test edx,edx) recomputes the flags the old
; `test` had set, so no incoming flags are consumed. The nine orphaned bytes
; 0x0054DA8D-0x0054DA95 are never executed and no branch targets them
; (0x0054DA96, the toggle entry from 0x0054DAD4, stays intact).
;
; Note: src/replay_observer.asm also repairs the radar blip map at the entry to
; RadarClass::Draw_It, but that object is intentionally NOT linked into the
; current build (removed from OBJS in commit 471276c - its per-frame raw writes
; to Map+0x15F6 / Map+0xCB0 were judged too fragile). With the Activate guard
; removed above, the radar panel follows the normal live engine path instead:
; RadarClass::AI / Radar_Activate run exactly as they do in a live game.
;
; Activating the sidebar is necessary, but the replay POV is deliberately still
; a normal player house: it owns real factories and production queues. Therefore
; its StripClass objects have cameo entries that a true live spectator's empty
; house would not have. Do not alter that state, PlayerPtr, or the replay roster.
; Instead, each of SidebarClass::Draw_It's two calls to StripClass::Draw_It is
; wrapped below. During playback only, the wrapper temporarily supplies a zero
; cameo count to the native strip renderer and restores the real count before
; returning to SidebarClass::Draw_It. The native empty-strip frame, arrows,
; repair/upgrade/zoom buttons, power panel, radar, and stats hooks stay intact.
;
; Kill switch: set ENABLE_REPLAY_SIDEBAR to 0 to drop back to vanilla behavior.

%include "macros/patch.inc"
%include "macros/hack.inc"
%include "macros/extern.inc"
%include "macros/datatypes.inc"

cextern recording_mode

%define ENABLE_REPLAY_SIDEBAR 1

%if ENABLE_REPLAY_SIDEBAR
; Remove the playback-only short-circuit inside SidebarClass::Activate so both
; modes take the live argument dispatch. This is the fix described in the file
; header; without it every Activate() call during playback returns untouched and
; the right-hand column can never turn on, regardless of which engine site
; requests it (Select_Game, Init_IO, Fill_In_Data, Add, Set_Active, hotkey).
;
;   0x0054DA88  F6 05 51 01 68 00 02   test ds:recording_mode, 2
;   0x0054DA8F  74 33                  jz   0x0054DAC4
;   0x0054DA91  E9 F1 01 00 00         jmp  0x0054DC87   ; playback -> return
;
; The 5-byte LJMP overwrites the front of the test instruction; the nine
; remaining bytes 0x0054DA8D-0x0054DA95 are orphaned (nothing branches into
; them - verified by xrefs), and 0x0054DA96, the edx=-1 toggle entry from
; 0x0054DAD4, stays intact. The dispatch's first instruction (test edx, edx)
; recomputes the flags the old test had set, so no incoming flags are consumed.
;
; Live and recording sessions are byte-for-byte unchanged in behavior: the
; vanilla jz already fell straight to 0x0054DAC4 whenever playback bit 1 was
; clear, and the removed jmp arm only ever executed during playback.
@LJMP 0x0054DA88, 0x0054DAC4

; Keep a manually hidden sidebar hidden during playback. SidebarClass::Add
; (0x0054D61C) force-reopens the column whenever a new strip entry appears
; (Update_Buildables, Super_Weapon_Handler, Goodie_Check, Per_Cell_Process,
; trigger actions) - the "sidebar comes back by itself after hiding it" report.
;
; Overwritten (exactly 5 bytes):
;   0x0054D656  E8 15 04 00 00   call SidebarClass::Activate
; Continuation: 0x0054D65B (mov ah, [ecx+15F6h]).
;
; Entry state verified: 0x0054D64F mov edx, 1 (arg), 0x0054D654 mov eax, ecx
; (this); StripClass::Add has already committed the cameo and returned
; non-zero at 0x0054D64B before the patch point. During playback
; (recording_mode bit 2) only this call is skipped - the bit-1 redraw request
; at 0x0054D664, the vtable redraw call at 0x0054D671 and mov eax,1 at
; 0x0054D674 all continue unchanged, so cameo insertion, redraw and the
; return value are preserved. Live and recording sessions re-emit the
; original call; test/jnz touch no registers, so eax/ecx/edx arrive exactly
; as vanilla and live behavior is byte-for-byte identical. Load-time
; activation (Select_Game, Fill_In_Data, Init_*) and the hide/show hotkey
; (direct Activate(-1)) are separate call sites and stay untouched.
@HACK 0x0054D656, _Replay_Add_No_Force_Open
    test byte [recording_mode], 0x02
    jnz 0x0054D65B
    call 0x0054DA70
    jmp 0x0054D65B
@ENDHACK

; The replay-only strip wrappers below preserve the real cameo data without
; forcing the live sidebar activation state machine.

; SidebarClass::Draw_It calls StripClass::Draw_It exactly twice:
;
;   0x0054D88F  eax = Sidebar + 0x103E  (building strip)
;   0x0054D8A0  eax = Sidebar + 0x131A  (unit strip)
;
; StripClass::Draw_It receives `this` in eax and force-redraw in edx. Its
; 32-bit cameo-entry count is [this+0x35]. Preserve the caller's redraw policy;
; the strip still draws only when the native code requested it, but it renders
; its normal empty-slot presentation rather than the replay player's cameos.
;
; The saved strip pointer is restored through edx only after the original call.
; The first continuation reloads edx immediately; the second only tests ebx, so
; this matches the registers observable by the surrounding SidebarClass code.
@HACK 0x0054D88F, _Replay_Sidebar_Draw_Empty_Building_Strip
    test byte [recording_mode], 0x02
    jz .Draw_Original

    push eax                            ; StripClass *this
    push dword [eax+0x35]               ; preserve its real cameo-entry count
    mov dword [eax+0x35], 0
    call 0x0054E6FC                     ; SidebarClass::StripClass::Draw_It
    pop ecx                             ; saved cameo-entry count
    pop edx                             ; saved StripClass *this
    mov [edx+0x35], ecx                 ; restore simulation/UI state exactly
    jmp 0x0054D894

.Draw_Original:
    call 0x0054E6FC                     ; vanilla call, unchanged outside playback
    jmp 0x0054D894
@ENDHACK

@HACK 0x0054D8A0, _Replay_Sidebar_Draw_Empty_Unit_Strip
    test byte [recording_mode], 0x02
    jz .Draw_Original

    push eax                            ; StripClass *this
    push dword [eax+0x35]               ; preserve its real cameo-entry count
    mov dword [eax+0x35], 0
    call 0x0054E6FC                     ; SidebarClass::StripClass::Draw_It
    pop ecx                             ; saved cameo-entry count
    pop edx                             ; saved StripClass *this
    mov [edx+0x35], ecx                 ; restore simulation/UI state exactly
    jmp 0x0054D8A5

.Draw_Original:
    call 0x0054E6FC                     ; vanilla call, unchanged outside playback
    jmp 0x0054D8A5
@ENDHACK
%endif

