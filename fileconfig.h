// Auto-generated file - do not edit manually
// Generated from Makefile based on detected files

#ifndef FILECONFIG_H
#define FILECONFIG_H

// File counts
#define MAX_SEQ_FILES 2
#define MAX_VH_FILES 1

// Extern declarations for SEQ files
extern u_char _binary_SEQ_MOUSE_seq_start[];
extern u_char _binary_SEQ_scale_seq_start[];

// Extern declarations for VH files
extern u_char _binary_SOUNDBANK_VH_piano_vh_start[];

// Extern declarations for VB files
extern u_char _binary_SOUNDBANK_VB_piano_vb_start[];

// SEQ file initialization array
#define SEQ_FILES_INIT { \
    {"MOUSE.seq", _binary_SEQ_MOUSE_seq_start, 0, 0}, \
    {"scale.seq", _binary_SEQ_scale_seq_start, 0, 0}, \
}

// VH file initialization array
#define VH_FILES_INIT { \
    {"piano.vh", _binary_SOUNDBANK_VH_piano_vh_start, 0, 1}, \
}

// VB file initialization array
#define VB_FILES_INIT { \
    _binary_SOUNDBANK_VB_piano_vb_start, \
}

#endif // FILECONFIG_H
