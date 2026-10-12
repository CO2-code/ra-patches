%include "macros/patch.inc"
%include "macros/datatypes.inc"

gstring recording_filename, "ratest.rarec"

%if 1
@CLEAR 0x004A7EDD, 0x90, 0x004A7EE2 ; don't record mouse and other stuff
@CLEAR 0x004A53E7, 0x90, 0x004A53EC ; don't hide mouse
@CLEAR 0x004F874F, 0x90, 0x004F8760 ; don't save seed
@CLEAR 0x004F87FB, 0x90, 0x004F880C ; don't load seed
;@CLEAR 0x, 0x90, 0x ; 

@CLEAR 0x0052BE3E, 0x90, 0x0052BE47 ; don't exit on mouse movement
@LJMP 0x0052BE3E, 0x0052BE73 ; don't exit on mouse movement
; 004A7E72 ???
%endif
