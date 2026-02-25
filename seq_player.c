// PS1 SEQ/VH/VB Player
// Controller-driven UI for playing sequence files with soundbanks
//
// Based on nolibgs_hello_worlds SDK
// Uses libsnd for sequence/VAB playback

#include <sys/types.h>
#include <stdio.h>
#include <libgte.h>
#include <libetc.h>
#include <libgpu.h>
#include <libsnd.h>
#include <libspu.h>

// Include auto-generated file configuration
#include "fileconfig.h"

#define VMODE 0                 // Video Mode : 0 : NTSC, 1: PAL
#define SCREENXRES 320
#define SCREENYRES 240

DISPENV disp[2];
DRAWENV draw[2];
short db = 0;

// UI States
typedef enum {
    STATE_SEQ_SELECT,
    STATE_VH_SELECT,
    STATE_PLAYBACK,
    STATE_VAB_VH_SELECT,
    STATE_VAB_PLAYBACK,
    STATE_PROGRAM_EDIT,
    STATE_TONE_EDIT
} UIState;

// Playback menu items (SEQ mode)
typedef enum {
    MENU_PLAY,
    MENU_PAUSE,
    MENU_STOP,
    MENU_TEMPO,
    MENU_REV_TYPE,
    MENU_REV_DEPTH,
    MENU_REV_DELAY,
    MENU_REV_FEEDBACK,
    MENU_PROGRAM_EDIT,
    MENU_ITEM_COUNT
} PlaybackMenuItem;

// VAB playback menu items
typedef enum {
    VAB_MENU_NOTE,
    VAB_MENU_PROGRAM,
    VAB_MENU_REV_TYPE,
    VAB_MENU_REV_DEPTH,
    VAB_MENU_REV_DELAY,
    VAB_MENU_REV_FEEDBACK,
    VAB_MENU_PROGRAM_EDIT,
    VAB_MENU_ITEM_COUNT
} VabPlaybackMenuItem;

// Program edit menu items
typedef enum {
    PROG_MENU_PROGRAM_SEL,
    PROG_MENU_NUM_TONES,
    PROG_MENU_PROG_VOL,
    PROG_MENU_PROG_PAN,
    PROG_MENU_MASTER_VOL,
    PROG_MENU_MASTER_PAN,
    PROG_MENU_ITEM_COUNT
} ProgramEditMenuItem;

// Tone edit menu items
typedef enum {
    TONE_MENU_PROG,      // Program selector (navigable)
    TONE_MENU_TONE_SEL,
    TONE_MENU_NOTE_SEL,  // VAB mode only
    TONE_MENU_PRIOR,
    TONE_MENU_MODE,
    TONE_MENU_VOL,
    TONE_MENU_PAN,
    TONE_MENU_CENTER,
    TONE_MENU_SHIFT,
    TONE_MENU_MIN,
    TONE_MENU_MAX,
    TONE_MENU_PBMIN,
    TONE_MENU_PBMAX,
    TONE_MENU_ADSR1,
    TONE_MENU_ADSR2,
    TONE_MENU_ITEM_COUNT
} ToneEditMenuItem;

// File entry structure
typedef struct {
    char name[32];
    u_char* data;
    u_int size;
    u_char type; // 0=seq, 1=vh
} FileEntry;

// Audio file structure
typedef struct {
    u_char* seq_data;
    u_char* vh_data;
    u_char* vb_data;
    u_int seq_size;
    u_int vh_size;
    u_int vb_size;
    char seq_name[32];
    char vh_name[32];
    short vab_id;
    short seq_id;
    u_short num_programs;
    u_short num_tones;
} AudioFile;

// File counts and extern declarations are now in fileconfig.h
// MAX_SEQ_FILES and MAX_VH_FILES are defined there
// All extern declarations are generated at build time

// File lists - initialized from generated macros
FileEntry seq_files[MAX_SEQ_FILES] = SEQ_FILES_INIT;

FileEntry vh_files[MAX_VH_FILES] = VH_FILES_INIT;

u_char* vb_files[MAX_VH_FILES] = VB_FILES_INIT;

// SEQ attribute table (required by libsnd)
char seq_table[SS_SEQ_TABSIZ * MAX_SEQ_FILES * MAX_VH_FILES];

// Global state
UIState current_state = STATE_SEQ_SELECT;
int selected_seq = 0;
int selected_vh = 0;
int cursor = 0;
int menu_cursor = 0;  // Cursor position in playback menu
AudioFile current_audio;
int is_playing = 0;
int is_paused = 0;
int pad, oldpad;
long current_tempo = 120;  // Track current tempo (default 120 BPM)
int tempo_changed = 0;  // Track if tempo has been modified from original
short reverb_depth_left = 64;
short reverb_depth_right = 64;
short reverb_delay = 0;
short reverb_feedback = 0;
short reverb_type = 0;

// VAB mode specific variables
int vab_mode = 0;  // 0 = SEQ mode, 1 = VAB mode
short current_note = 60;  // Middle C (MIDI note 60)
short current_program = 0;  // Program (instrument) number
short current_voice = -1;  // Voice channel ID (-1 = not playing)
int note_playing = 0;  // Is note currently playing

// Program/Tone editing variables
int edit_program = 0;  // Currently selected program for editing
int edit_tone = 0;  // Currently selected tone for editing
UIState return_state = STATE_PLAYBACK;  // State to return to from editor
ProgAtr current_prog_atr;  // Current program attributes
ProgAtr original_prog_atr;  // Original program attributes (for change detection)
VagAtr current_vag_atr;  // Current tone attributes
VagAtr original_vag_atr;  // Original tone attributes (for change detection)
u_char vab_master_vol = 127;  // VAB master volume
u_char vab_master_pan = 64;  // VAB master pan
u_char original_master_vol = 127;  // Original master volume
u_char original_master_pan = 64;  // Original master pan

// ADSR hex editing
int adsr_editing = 0;  // 0=not editing, 1=editing ADSR1, 2=editing ADSR2
int adsr_digit_pos = 0;  // Current digit position (0-3)
u_short adsr_temp_value = 0;  // Temporary value while editing
u_short adsr_original_value = 0;  // Original value (for cancel)

// Hold detection (at 60Hz, 0.5 seconds = 30 frames)
#define HOLD_THRESHOLD 30
#define HOLD_REPEAT_RATE 3  // Repeat every 3 frames when held
int hold_counter_left = 0;
int hold_counter_right = 0;
int hold_active_left = 0;
int hold_active_right = 0;
int hold_counter_up = 0;
int hold_counter_down = 0;
int hold_active_up = 0;
int hold_active_down = 0;

// Function prototypes
void initGraph(void);
void display(void);
void initSound(void);
void processInput(void);
void drawUI(void);
void drawSeqSelect(void);
void drawVhSelect(void);
void drawPlayback(void);
void loadAudioFiles(void);
void playSequence(void);
void pauseSequence(void);
void stopSequence(void);
void adjustMenuValue(int direction, int amount);
void toggleMinMax(void);
const char* getReverbTypeName(short type);
void drawVabVhSelect(void);
void drawVabPlayback(void);
void playNote(void);
void stopNote(void);
void adjustVabMenuValue(int direction, int amount);
void toggleVabMinMax(void);
void enterProgramEdit(void);
void exitProgramEdit(void);
void loadProgramData(void);
void loadToneData(void);
void saveProgramData(void);
void saveToneData(void);
void adjustProgramEditValue(int direction, int amount);
void toggleProgramEditMinMax(void);
void adjustToneEditValue(int direction, int amount);
void toggleToneEditMinMax(void);
void drawProgramEdit(void);
void drawToneEdit(void);
int isProgramValueChanged(int menu_item);
int isToneValueChanged(int menu_item);

void initGraph(void)
{
    ResetGraph(0);
    SetDefDispEnv(&disp[0], 0, 0, SCREENXRES, SCREENYRES);
    SetDefDispEnv(&disp[1], 0, SCREENYRES, SCREENXRES, SCREENYRES);
    SetDefDrawEnv(&draw[0], 0, SCREENYRES, SCREENXRES, SCREENYRES);
    SetDefDrawEnv(&draw[1], 0, 0, SCREENXRES, SCREENYRES);
    
    if (VMODE)
    {
        SetVideoMode(MODE_PAL);
        disp[0].screen.y += 8;
        disp[1].screen.y += 8;
    }
    
    SetDispMask(1);
    setRGB0(&draw[0], 60, 20, 40);
    setRGB0(&draw[1], 60, 20, 40);
    draw[0].isbg = 1;
    draw[1].isbg = 1;
    
    PutDispEnv(&disp[db]);
    PutDrawEnv(&draw[db]);
    
    FntLoad(960, 0);
    FntOpen(8, 8, 304, 224, 0, 2048);
}

void display(void)
{
    DrawSync(0);
    VSync(0);
    PutDispEnv(&disp[db]);
    PutDrawEnv(&draw[db]);
    db = !db;
}

void initSound(void)
{
    // Initialize sound library
    SsInit();
    
    // Set up SEQ attribute table
    SsSetTableSize(seq_table, MAX_SEQ_FILES, MAX_VH_FILES);
    
    // Set tick mode to SS_TICK240 for correct tempo (240Hz timing)
    // SS_TICK60 causes sequences to play too fast
    SsSetTickMode(SS_TICK240);
    
    // Set master volume
    SsSetMVol(127, 127);
    
    // Start sound system
    SsStart();

	
}

void loadAudioFiles(void)
{
    int i;
    VabHdr* vab_hdr;
    
    // Get VH file info from headers
    for (i = 0; i < MAX_VH_FILES; i++) {
        vab_hdr = (VabHdr*)vh_files[i].data;
        vh_files[i].size = vab_hdr->fsize;
    }
    
    // Initialize current audio structure
    current_audio.vab_id = -1;
    current_audio.seq_id = -1;
}

void playSequence(void)
{
    if (is_playing) {
        stopSequence();
    }
    
    // Reset tempo to default
    current_tempo = 120;
    tempo_changed = 0;  // Mark as unchanged
    
    // Open VAB header (VH)
    current_audio.vab_id = SsVabOpenHead(current_audio.vh_data, -1);
    if (current_audio.vab_id < 0) {
        FntPrint("Failed to open VAB header!\n");
        return;
    }
    
    // Transfer VAB body (VB) to SPU
    if (SsVabTransBody(current_audio.vb_data, current_audio.vab_id) != current_audio.vab_id) {
        FntPrint("Failed to transfer VAB body!\n");
        SsVabClose(current_audio.vab_id);
        current_audio.vab_id = -1;
        return;
    }
    
    // Wait for transfer to complete
    SsVabTransCompleted(SS_WAIT_COMPLETED);
    
    // Open sequence (cast to unsigned long* as required by API)
    current_audio.seq_id = SsSeqOpen((unsigned long*)current_audio.seq_data, current_audio.vab_id);
    if (current_audio.seq_id < 0) {
        FntPrint("Failed to open sequence!\n");
        SsVabClose(current_audio.vab_id);
        current_audio.vab_id = -1;
        return;
    }
    
    // Set sequence volume
    SsSeqSetVol(current_audio.seq_id, 127, 127);

	//Enable reverb
	SsUtSetReverbType(reverb_type);
	SsUtReverbOn();

	
    
    // Play sequence (infinite loop)
    SsSeqPlay(current_audio.seq_id, SSPLAY_PLAY, SSPLAY_INFINITY);

	//Set to default values
	SsUtSetReverbDepth (reverb_depth_left, reverb_depth_right);

    is_playing = 1;
}

void stopSequence(void)
{
    if (is_playing && current_audio.seq_id >= 0) {
        SsSeqStop(current_audio.seq_id);
        SsSeqClose(current_audio.seq_id);
        is_playing = 0;
    }
    
    if (current_audio.vab_id >= 0) {
        SsVabClose(current_audio.vab_id);
        current_audio.vab_id = -1;
    }
    
    current_audio.seq_id = -1;
    current_tempo = 120;  // Reset tempo to default
    tempo_changed = 0;  // Mark as unchanged
    is_paused = 0;  // Reset pause state
}

void pauseSequence(void)
{
    if (is_playing && current_audio.seq_id >= 0 && !is_paused) {
        SsSeqPause(current_audio.seq_id);
        is_paused = 1;
    } else if (is_playing && current_audio.seq_id >= 0 && is_paused) {
        SsSeqReplay(current_audio.seq_id);
        is_paused = 0;
    }
}

const char* getReverbTypeName(short type)
{
    switch (type) {
        case 0: return "OFF";
        case 1: return "ROOM";
        case 2: return "STUDIO_A";
        case 3: return "STUDIO_B";
        case 4: return "STUDIO_C";
        case 5: return "HALL";
        case 6: return "SPACE";
        case 7: return "ECHO";
        case 8: return "DELAY";
        case 9: return "PIPE";
        default: return "UNKNOWN";
    }
}

void adjustMenuValue(int direction, int amount)
{
    // direction: -1 for decrease, 1 for increase
    // amount: how much to adjust by
    
    switch (menu_cursor) {
        case MENU_TEMPO:
            current_tempo += (direction * amount);
            if (current_tempo < 30) current_tempo = 30;
            if (current_tempo > 240) current_tempo = 240;
            tempo_changed = 1;  // Mark as changed
            if (is_playing && current_audio.seq_id >= 0) {
                if (direction > 0) {
                    SsSeqSetAccelerando(current_audio.seq_id, current_tempo, 120);
                } else {
                    SsSeqSetRitardando(current_audio.seq_id, current_tempo, 120);
                }
            }
            break;
            
        case MENU_REV_TYPE:
            reverb_type += direction;
            if (reverb_type < 0) reverb_type = 0;
            if (reverb_type > 9) reverb_type = 9;
            SsUtSetReverbType(reverb_type);
            // Reapply reverb depth when changing type
            SsUtSetReverbDepth(reverb_depth_left, reverb_depth_right);
            break;
            
        case MENU_REV_DEPTH:
            reverb_depth_left += (direction * amount);
            reverb_depth_right += (direction * amount);
            if (reverb_depth_left < 0) {
                reverb_depth_left = 0;
                reverb_depth_right = 0;
            }
            if (reverb_depth_left > 127) {
                reverb_depth_left = 127;
                reverb_depth_right = 127;
            }
            SsUtSetReverbDepth(reverb_depth_left, reverb_depth_right);
            break;
            
        case MENU_REV_DELAY:
            reverb_delay += (direction * amount);
            if (reverb_delay < 0) reverb_delay = 0;
            if (reverb_delay > 127) reverb_delay = 127;
            SsUtSetReverbDelay(reverb_delay);
            break;
            
        case MENU_REV_FEEDBACK:
            reverb_feedback += (direction * amount);
            if (reverb_feedback < 0) reverb_feedback = 0;
            if (reverb_feedback > 127) reverb_feedback = 127;
            SsUtSetReverbFeedback(reverb_feedback);
            break;
    }
}

void toggleMinMax(void)
{
    switch (menu_cursor) {
        case MENU_TEMPO:
            current_tempo = (current_tempo == 240) ? 30 : 240;
            tempo_changed = 1;  // Mark as changed
            if (is_playing && current_audio.seq_id >= 0) {
                if (current_tempo == 240) {
                    SsSeqSetAccelerando(current_audio.seq_id, current_tempo, 120);
                } else {
                    SsSeqSetRitardando(current_audio.seq_id, current_tempo, 120);
                }
            }
            break;
            
        case MENU_REV_TYPE:
            reverb_type = (reverb_type == 9) ? 0 : 9;
            SsUtSetReverbType(reverb_type);
            break;
            
        case MENU_REV_DEPTH:
            if (reverb_depth_left == 127) {
                reverb_depth_left = 0;
                reverb_depth_right = 0;
            } else {
                reverb_depth_left = 127;
                reverb_depth_right = 127;
            }
            SsUtSetReverbDepth(reverb_depth_left, reverb_depth_right);
            break;
            
        case MENU_REV_DELAY:
            reverb_delay = (reverb_delay == 127) ? 0 : 127;
            SsUtSetReverbDelay(reverb_delay);
            break;
            
        case MENU_REV_FEEDBACK:
            reverb_feedback = (reverb_feedback == 127) ? 0 : 127;
            SsUtSetReverbFeedback(reverb_feedback);
            break;
    }
}

void playNote(void)
{
    short program_to_use;
    short tone_to_use;
    
    // Use edit_program if in editor states, otherwise use current_program
    if (current_state == STATE_PROGRAM_EDIT || current_state == STATE_TONE_EDIT) {
        program_to_use = edit_program;
        // In Tone Editor, use the currently selected tone
        if (current_state == STATE_TONE_EDIT) {
            tone_to_use = edit_tone;
        } else {
            tone_to_use = 0;
        }
    } else {
        program_to_use = current_program;
        tone_to_use = 0;
    }
    
    // Stop any currently playing note
    if (note_playing && current_voice >= 0) {
        stopNote();
    }
    
    // Play the note using the appropriate program, tone, and note value
    // SsUtKeyOn(vab_id, program, tone, note, fine, vol_left, vol_right)
    current_voice = SsUtKeyOn(current_audio.vab_id, program_to_use, tone_to_use, current_note, 0, 127, 127);
    
    if (current_voice >= 0) {
        note_playing = 1;
    }
}

void stopNote(void)
{
    short program_to_use;
    short tone_to_use;
    
    // Use edit_program if in editor states, otherwise use current_program
    if (current_state == STATE_PROGRAM_EDIT || current_state == STATE_TONE_EDIT) {
        program_to_use = edit_program;
        // In Tone Editor, use the currently selected tone
        if (current_state == STATE_TONE_EDIT) {
            tone_to_use = edit_tone;
        } else {
            tone_to_use = 0;
        }
    } else {
        program_to_use = current_program;
        tone_to_use = 0;
    }
    
    if (note_playing && current_voice >= 0) {
        // SsUtKeyOff(voice_channel, vab_id, program, tone, note)
        SsUtKeyOff(current_voice, current_audio.vab_id, program_to_use, tone_to_use, current_note);
        note_playing = 0;
        current_voice = -1;
    }
}

void adjustVabMenuValue(int direction, int amount)
{
    // direction: -1 for decrease, 1 for increase
    // amount: how much to adjust by
    int was_playing = note_playing;
    
    switch (menu_cursor) {
        case VAB_MENU_NOTE:
            current_note += (direction * amount);
            if (current_note < 0) current_note = 0;
            if (current_note > 127) current_note = 127;
            // If note is playing, restart with new note
            if (was_playing) {
                playNote();
            }
            break;
            
        case VAB_MENU_PROGRAM:
            current_program += direction;
            if (current_program < 0) current_program = 0;
            if (current_program >= current_audio.num_programs) {
                current_program = current_audio.num_programs - 1;
            }
            // If note is playing, retrigger with new program
            if (was_playing) {
                playNote();
            }
            break;
            
        case VAB_MENU_REV_TYPE:
            reverb_type += direction;
            if (reverb_type < 0) reverb_type = 0;
            if (reverb_type > 9) reverb_type = 9;
            SsUtSetReverbType(reverb_type);
            // Reapply reverb depth when changing type
            SsUtSetReverbDepth(reverb_depth_left, reverb_depth_right);
            break;
            
        case VAB_MENU_REV_DEPTH:
            reverb_depth_left += (direction * amount);
            reverb_depth_right += (direction * amount);
            if (reverb_depth_left < 0) {
                reverb_depth_left = 0;
                reverb_depth_right = 0;
            }
            if (reverb_depth_left > 127) {
                reverb_depth_left = 127;
                reverb_depth_right = 127;
            }
            SsUtSetReverbDepth(reverb_depth_left, reverb_depth_right);
            break;
            
        case VAB_MENU_REV_DELAY:
            reverb_delay += (direction * amount);
            if (reverb_delay < 0) reverb_delay = 0;
            if (reverb_delay > 127) reverb_delay = 127;
            SsUtSetReverbDelay(reverb_delay);
            break;
            
        case VAB_MENU_REV_FEEDBACK:
            reverb_feedback += (direction * amount);
            if (reverb_feedback < 0) reverb_feedback = 0;
            if (reverb_feedback > 127) reverb_feedback = 127;
            SsUtSetReverbFeedback(reverb_feedback);
            break;
    }
}

void toggleVabMinMax(void)
{
    switch (menu_cursor) {
        case VAB_MENU_NOTE:
            current_note = (current_note == 127) ? 0 : 127;
            break;
            
        case VAB_MENU_PROGRAM:
            current_program = (current_program == current_audio.num_programs - 1) ? 0 : current_audio.num_programs - 1;
            break;
            
        case VAB_MENU_REV_TYPE:
            reverb_type = (reverb_type == 9) ? 0 : 9;
            SsUtSetReverbType(reverb_type);
            break;
            
        case VAB_MENU_REV_DEPTH:
            if (reverb_depth_left == 127) {
                reverb_depth_left = 0;
                reverb_depth_right = 0;
            } else {
                reverb_depth_left = 127;
                reverb_depth_right = 127;
            }
            SsUtSetReverbDepth(reverb_depth_left, reverb_depth_right);
            break;
            
        case VAB_MENU_REV_DELAY:
            reverb_delay = (reverb_delay == 127) ? 0 : 127;
            SsUtSetReverbDelay(reverb_delay);
            break;
            
        case VAB_MENU_REV_FEEDBACK:
            reverb_feedback = (reverb_feedback == 127) ? 0 : 127;
            SsUtSetReverbFeedback(reverb_feedback);
            break;
    }
}

void enterProgramEdit(void)
{
    // Save return state
    return_state = current_state;
    
    // Load VAB header to get master vol/pan
    VabHdr* vab_hdr = (VabHdr*)current_audio.vh_data;
    vab_master_vol = vab_hdr->mvol;
    vab_master_pan = vab_hdr->pan;  // VabHdr has 'pan', not 'mpan'
    original_master_vol = vab_master_vol;
    original_master_pan = vab_master_pan;
    
    // Start with program 0
    edit_program = current_program;
    edit_tone = 0;
    
    // Load program data
    loadProgramData();
    
    current_state = STATE_PROGRAM_EDIT;
    menu_cursor = 0;
}

void loadProgramData(void)
{
    // Load program attributes
    if (SsUtGetProgAtr(current_audio.vab_id, edit_program, &current_prog_atr) == 0) {
        // Save original for change detection
        original_prog_atr = current_prog_atr;
    }
}

void loadToneData(void)
{
    // Load tone attributes
    if (SsUtGetVagAtr(current_audio.vab_id, edit_program, edit_tone, &current_vag_atr) == 0) {
        // Save original for change detection
        original_vag_atr = current_vag_atr;
        
        // Check if min and max are the same (single note mapping)
        if (current_vag_atr.min == current_vag_atr.max && vab_mode) {
            // Set current note to the min/max value
            current_note = current_vag_atr.min;
        }
    }
}

void saveProgramData(void)
{
    // Save program attributes
    SsUtSetProgAtr(current_audio.vab_id, edit_program, &current_prog_atr);
    
    // Update VAB header master vol/pan if changed
    if (vab_master_vol != original_master_vol || vab_master_pan != original_master_pan) {
        VabHdr* vab_hdr = (VabHdr*)current_audio.vh_data;
        vab_hdr->mvol = vab_master_vol;
        vab_hdr->pan = vab_master_pan;  // VabHdr has 'pan', not 'mpan'
    }
}

void saveToneData(void)
{
    // Save tone attributes
    SsUtSetVagAtr(current_audio.vab_id, edit_program, edit_tone, &current_vag_atr);
}

int isProgramValueChanged(int menu_item)
{
    switch (menu_item) {
        case PROG_MENU_PROG_VOL:
            return current_prog_atr.mvol != original_prog_atr.mvol;
        case PROG_MENU_PROG_PAN:
            return current_prog_atr.mpan != original_prog_atr.mpan;
        case PROG_MENU_MASTER_VOL:
            return vab_master_vol != original_master_vol;
        case PROG_MENU_MASTER_PAN:
            return vab_master_pan != original_master_pan;
        default:
            return 0;
    }
}

int isToneValueChanged(int menu_item)
{
    switch (menu_item) {
        case TONE_MENU_PRIOR:
            return current_vag_atr.prior != original_vag_atr.prior;
        case TONE_MENU_MODE:
            return current_vag_atr.mode != original_vag_atr.mode;
        case TONE_MENU_VOL:
            return current_vag_atr.vol != original_vag_atr.vol;
        case TONE_MENU_PAN:
            return current_vag_atr.pan != original_vag_atr.pan;
        case TONE_MENU_CENTER:
            return current_vag_atr.center != original_vag_atr.center;
        case TONE_MENU_SHIFT:
            return current_vag_atr.shift != original_vag_atr.shift;
        case TONE_MENU_MIN:
            return current_vag_atr.min != original_vag_atr.min;
        case TONE_MENU_MAX:
            return current_vag_atr.max != original_vag_atr.max;
        case TONE_MENU_PBMIN:
            return current_vag_atr.pbmin != original_vag_atr.pbmin;
        case TONE_MENU_PBMAX:
            return current_vag_atr.pbmax != original_vag_atr.pbmax;
        case TONE_MENU_ADSR1:
            return current_vag_atr.adsr1 != original_vag_atr.adsr1;
        case TONE_MENU_ADSR2:
            return current_vag_atr.adsr2 != original_vag_atr.adsr2;
        default:
            return 0;
    }
}

void adjustProgramEditValue(int direction, int amount)
{
    switch (menu_cursor) {
        case PROG_MENU_PROGRAM_SEL:
            edit_program += direction;
            if (edit_program < 0) edit_program = 0;
            if (edit_program >= current_audio.num_programs) {
                edit_program = current_audio.num_programs - 1;
            }
            loadProgramData();
            saveProgramData();  // Apply previous changes
            break;
            
        case PROG_MENU_PROG_VOL:
            current_prog_atr.mvol += (direction * amount);
            if (current_prog_atr.mvol > 127) current_prog_atr.mvol = 127;
            saveProgramData();
            break;
            
        case PROG_MENU_PROG_PAN:
            current_prog_atr.mpan += (direction * amount);
            if (current_prog_atr.mpan > 127) current_prog_atr.mpan = 127;
            saveProgramData();
            break;
            
        case PROG_MENU_MASTER_VOL:
            vab_master_vol += (direction * amount);
            if (vab_master_vol > 127) vab_master_vol = 127;
            saveProgramData();
            break;
            
        case PROG_MENU_MASTER_PAN:
            vab_master_pan += (direction * amount);
            if (vab_master_pan > 127) vab_master_pan = 127;
            saveProgramData();
            break;
    }
}

void toggleProgramEditMinMax(void)
{
    switch (menu_cursor) {
        case PROG_MENU_PROGRAM_SEL:
            edit_program = (edit_program == current_audio.num_programs - 1) ? 0 : current_audio.num_programs - 1;
            loadProgramData();
            saveProgramData();
            break;
            
        case PROG_MENU_PROG_VOL:
            current_prog_atr.mvol = (current_prog_atr.mvol == 127) ? 0 : 127;
            saveProgramData();
            break;
            
        case PROG_MENU_PROG_PAN:
            current_prog_atr.mpan = (current_prog_atr.mpan == 127) ? 0 : 127;
            saveProgramData();
            break;
            
        case PROG_MENU_MASTER_VOL:
            vab_master_vol = (vab_master_vol == 127) ? 0 : 127;
            saveProgramData();
            break;
            
        case PROG_MENU_MASTER_PAN:
            vab_master_pan = (vab_master_pan == 127) ? 0 : 127;
            saveProgramData();
            break;
    }
}

void adjustToneEditValue(int direction, int amount)
{
    int was_playing = note_playing;
    int temp_val;  // Use int for calculations to prevent wrap-around
    
    switch (menu_cursor) {
        case TONE_MENU_PROG:
            // Save current tone data before changing program
            saveToneData();
            
            edit_program += direction;
            if (edit_program < 0) edit_program = 0;
            if (edit_program >= current_audio.num_programs) {
                edit_program = current_audio.num_programs - 1;
            }
            
            // Load new program data and reset to tone 0
            loadProgramData();
            edit_tone = 0;
            loadToneData();
            break;
            
        case TONE_MENU_TONE_SEL:
            edit_tone += direction;
            if (edit_tone < 0) edit_tone = 0;
            if (edit_tone >= current_prog_atr.tones) {
                edit_tone = current_prog_atr.tones - 1;
            }
            loadToneData();
            saveToneData();  // Apply previous changes
            break;
            
        case TONE_MENU_NOTE_SEL:
            if (vab_mode) {
                current_note += (direction * amount);
                if (current_note < 0) current_note = 0;
                if (current_note > 127) current_note = 127;
                // Retrigger note if playing
                if (was_playing) playNote();
            }
            break;
            
        case TONE_MENU_PRIOR:
            temp_val = (int)current_vag_atr.prior + (direction * amount);
            if (temp_val < 0) temp_val = 0;
            if (temp_val > 15) temp_val = 15;
            current_vag_atr.prior = (u_char)temp_val;
            saveToneData();
            break;
            
        case TONE_MENU_MODE:
            // Toggle between 0 (normal) and 4 (reverb)
            if (direction != 0) {
                current_vag_atr.mode = (current_vag_atr.mode == 0) ? 4 : 0;
            }
            saveToneData();
            break;
            
        case TONE_MENU_VOL:
            temp_val = (int)current_vag_atr.vol + (direction * amount);
            if (temp_val < 0) temp_val = 0;
            if (temp_val > 127) temp_val = 127;
            current_vag_atr.vol = (u_char)temp_val;
            saveToneData();
            break;
            
        case TONE_MENU_PAN:
            temp_val = (int)current_vag_atr.pan + (direction * amount);
            if (temp_val < 0) temp_val = 0;
            if (temp_val > 127) temp_val = 127;
            current_vag_atr.pan = (u_char)temp_val;
            saveToneData();
            break;
            
        case TONE_MENU_CENTER:
            temp_val = (int)current_vag_atr.center + (direction * amount);
            if (temp_val < 0) temp_val = 0;
            if (temp_val > 127) temp_val = 127;
            current_vag_atr.center = (u_char)temp_val;
            saveToneData();
            break;
            
        case TONE_MENU_SHIFT:
            temp_val = (int)current_vag_atr.shift + (direction * amount);
            if (temp_val < 0) temp_val = 0;
            if (temp_val > 127) temp_val = 127;
            current_vag_atr.shift = (u_char)temp_val;
            saveToneData();
            break;
            
        case TONE_MENU_MIN:
            temp_val = (int)current_vag_atr.min + (direction * amount);
            if (temp_val < 0) temp_val = 0;
            if (temp_val > 127) temp_val = 127;
            current_vag_atr.min = (u_char)temp_val;
            saveToneData();
            break;
            
        case TONE_MENU_MAX:
            temp_val = (int)current_vag_atr.max + (direction * amount);
            if (temp_val < 0) temp_val = 0;
            if (temp_val > 127) temp_val = 127;
            current_vag_atr.max = (u_char)temp_val;
            saveToneData();
            break;
            
        case TONE_MENU_PBMIN:
            temp_val = (int)current_vag_atr.pbmin + (direction * amount);
            if (temp_val < 0) temp_val = 0;
            if (temp_val > 127) temp_val = 127;
            current_vag_atr.pbmin = (u_char)temp_val;
            saveToneData();
            break;
            
        case TONE_MENU_PBMAX:
            temp_val = (int)current_vag_atr.pbmax + (direction * amount);
            if (temp_val < 0) temp_val = 0;
            if (temp_val > 127) temp_val = 127;
            current_vag_atr.pbmax = (u_char)temp_val;
            saveToneData();
            break;
            
        case TONE_MENU_ADSR1:
        case TONE_MENU_ADSR2:
            // ADSR values are edited with X button, not directional controls
            break;
    }
}

void toggleToneEditMinMax(void)
{
    switch (menu_cursor) {
        case TONE_MENU_PROG:
            edit_program = (edit_program == current_audio.num_programs - 1) ? 0 : current_audio.num_programs - 1;
            loadProgramData();
            edit_tone = 0;
            loadToneData();
            saveToneData();
            break;
            
        case TONE_MENU_TONE_SEL:
            edit_tone = (edit_tone == current_prog_atr.tones - 1) ? 0 : current_prog_atr.tones - 1;
            loadToneData();
            saveToneData();
            break;
            
        case TONE_MENU_NOTE_SEL:
            if (vab_mode) {
                current_note = (current_note == 127) ? 0 : 127;
            }
            break;
            
        case TONE_MENU_PRIOR:
            current_vag_atr.prior = (current_vag_atr.prior == 15) ? 0 : 15;
            saveToneData();
            break;
            
        case TONE_MENU_MODE:
            current_vag_atr.mode = (current_vag_atr.mode == 0) ? 4 : 0;
            saveToneData();
            break;
            
        case TONE_MENU_VOL:
            current_vag_atr.vol = (current_vag_atr.vol == 127) ? 0 : 127;
            saveToneData();
            break;
            
        case TONE_MENU_PAN:
            current_vag_atr.pan = (current_vag_atr.pan == 127) ? 0 : 127;
            saveToneData();
            break;
            
        case TONE_MENU_CENTER:
            current_vag_atr.center = (current_vag_atr.center == 127) ? 0 : 127;
            saveToneData();
            break;
            
        case TONE_MENU_SHIFT:
            current_vag_atr.shift = (current_vag_atr.shift == 127) ? 0 : 127;
            saveToneData();
            break;
            
        case TONE_MENU_MIN:
            current_vag_atr.min = (current_vag_atr.min == 127) ? 0 : 127;
            saveToneData();
            break;
            
        case TONE_MENU_MAX:
            current_vag_atr.max = (current_vag_atr.max == 127) ? 0 : 127;
            saveToneData();
            break;
            
        case TONE_MENU_PBMIN:
            current_vag_atr.pbmin = (current_vag_atr.pbmin == 127) ? 0 : 127;
            saveToneData();
            break;
            
        case TONE_MENU_PBMAX:
            current_vag_atr.pbmax = (current_vag_atr.pbmax == 127) ? 0 : 127;
            saveToneData();
            break;
            
        case TONE_MENU_ADSR1:
        case TONE_MENU_ADSR2:
            // ADSR values edited with X button
            break;
    }
}

void processInput(void)
{
    pad = PadRead(0);
    
    // Global controls that work in all states
    
    // Select button - Enter program edit (only on initial press, not while held)
    if (pad & PADselect && !(oldpad & PADselect)) {
        if (current_audio.vab_id >= 0 && 
            (current_state == STATE_PLAYBACK || current_state == STATE_VAB_PLAYBACK)) {
            enterProgramEdit();
        }
    }
    
    // Check if Select is being held (layer modifier active)
    // When Select is held, normal inputs are blocked - only Select+button combos work
    int select_layer_active = (pad & PADselect) && (oldpad & PADselect);
    
    // Triangle - Play/Stop in SEQ mode (wrapped), Play note in VAB mode (always works)
    if (vab_mode) {
        // VAB mode - Triangle for note playback ALWAYS works (even with Select held)
        // This prevents stuck notes when Select is pressed during playback
        // Actual playback handling is done per-state
    } else {
        // SEQ mode - Triangle toggles play/stop (disabled when Select held)
        if (!select_layer_active) {
            if (pad & PADRup && !(oldpad & PADRup)) {
                if (current_state == STATE_PLAYBACK || current_state == STATE_PROGRAM_EDIT || current_state == STATE_TONE_EDIT) {
                    if (is_playing) {
                        stopSequence();
                    } else {
                        playSequence();
                    }
                }
            }
        }
    }
    
    // Start button - Pause in SEQ mode (disabled if Select held)
    if (!select_layer_active && !vab_mode && (current_state == STATE_PLAYBACK || current_state == STATE_PROGRAM_EDIT || current_state == STATE_TONE_EDIT)) {
        if (pad & PADstart && !(oldpad & PADstart)) {
            pauseSequence();
        }
    }
    
    // L2/R2 - Change note in VAB mode (disabled if Select held)
    if (!select_layer_active && vab_mode && (current_state == STATE_VAB_PLAYBACK || current_state == STATE_PROGRAM_EDIT || current_state == STATE_TONE_EDIT)) {
        if (pad & PADL2 && !(oldpad & PADL2)) {
            current_note--;
            if (current_note < 0) current_note = 0;
            if (note_playing) playNote();  // Restart if playing
        }
        if (pad & PADR2 && !(oldpad & PADR2)) {
            current_note++;
            if (current_note > 127) current_note = 127;
            if (note_playing) playNote();  // Restart if playing
        }
    }
    
    switch (current_state) {
        case STATE_SEQ_SELECT:
            if (pad & PADLup && !(oldpad & PADLup)) {
                cursor--;
                if (cursor < 0) cursor = MAX_SEQ_FILES - 1;
            }
            if (pad & PADLdown && !(oldpad & PADLdown)) {
                cursor++;
                if (cursor >= MAX_SEQ_FILES) cursor = 0;
            }
            if (pad & PADRdown && !(oldpad & PADRdown)) { // X button - SEQ mode
                selected_seq = cursor;
                vab_mode = 0;
                current_state = STATE_VH_SELECT;
                cursor = 0;
            }
            if (pad & PADRleft && !(oldpad & PADRleft)) { // Square button - VAB mode
                vab_mode = 1;
                current_state = STATE_VAB_VH_SELECT;
                cursor = 0;
            }
            break;
            
        case STATE_VH_SELECT:
            if (pad & PADLup && !(oldpad & PADLup)) {
                cursor--;
                if (cursor < 0) cursor = MAX_VH_FILES - 1;
            }
            if (pad & PADLdown && !(oldpad & PADLdown)) {
                cursor++;
                if (cursor >= MAX_VH_FILES) cursor = 0;
            }
            if (pad & PADRdown && !(oldpad & PADRdown)) { // X button
                selected_vh = cursor;
                
                // Set up audio file structure
                current_audio.seq_data = seq_files[selected_seq].data;
                current_audio.seq_size = seq_files[selected_seq].size;
                sprintf(current_audio.seq_name, "%s", seq_files[selected_seq].name);
                
                current_audio.vh_data = vh_files[selected_vh].data;
                current_audio.vb_data = vb_files[selected_vh];
                current_audio.vh_size = vh_files[selected_vh].size;
                sprintf(current_audio.vh_name, "%s", vh_files[selected_vh].name);
                
                // Read VAB header info (using library's VabHdr struct)
                VabHdr* vab_hdr = (VabHdr*)current_audio.vh_data;
                current_audio.num_programs = vab_hdr->ps;  // Note: 'ps' not 'programs'
                current_audio.num_tones = vab_hdr->ts;     // Note: 'ts' not 'tones'
                
                current_state = STATE_PLAYBACK;
            }
            if (pad & PADRright && !(oldpad & PADRright)) { // Circle button
                current_state = STATE_SEQ_SELECT;
                cursor = selected_seq;
            }
            break;
            
        case STATE_PLAYBACK:
            // If Select is held, skip all normal inputs (layer modifier)
            if (!select_layer_active) {
                // Circle - Back to VH select
                if (pad & PADRright && !(oldpad & PADRright)) {
                    stopSequence();
                    current_state = STATE_VH_SELECT;
                    cursor = selected_vh;
                    menu_cursor = 0;
                }
                
                
                // D-Pad Up/Down - Navigate menu with hold detection
            if (pad & PADLup) {
                hold_counter_up++;
                if (!(oldpad & PADLup)) {
                    menu_cursor--;
                    if (menu_cursor < 0) menu_cursor = MENU_ITEM_COUNT - 1;
                    hold_counter_up = 0;
                    hold_active_up = 0;
                } else if (hold_counter_up >= HOLD_THRESHOLD && !hold_active_up) {
                    hold_active_up = 1;
                } else if (hold_active_up && (hold_counter_up % HOLD_REPEAT_RATE == 0)) {
                    menu_cursor--;
                    if (menu_cursor < 0) menu_cursor = MENU_ITEM_COUNT - 1;
                }
            } else {
                hold_counter_up = 0;
                hold_active_up = 0;
            }
            
            if (pad & PADLdown) {
                hold_counter_down++;
                if (!(oldpad & PADLdown)) {
                    menu_cursor++;
                    if (menu_cursor >= MENU_ITEM_COUNT) menu_cursor = 0;
                    hold_counter_down = 0;
                    hold_active_down = 0;
                } else if (hold_counter_down >= HOLD_THRESHOLD && !hold_active_down) {
                    hold_active_down = 1;
                } else if (hold_active_down && (hold_counter_down % HOLD_REPEAT_RATE == 0)) {
                    menu_cursor++;
                    if (menu_cursor >= MENU_ITEM_COUNT) menu_cursor = 0;
                }
            } else {
                hold_counter_down = 0;
                hold_active_down = 0;
            }
            
            // X button - Execute menu action
            if (pad & PADRdown && !(oldpad & PADRdown)) {
                switch (menu_cursor) {
                    case MENU_PLAY:
                        playSequence();
                        break;
                    case MENU_PAUSE:
                        pauseSequence();
                        break;
                    case MENU_STOP:
                        stopSequence();
                        break;
                    case MENU_TEMPO:
                        // If tempo is unchanged, set to default 120
                        if (!tempo_changed) {
                            current_tempo = 120;
                            tempo_changed = 1;
                            if (is_playing && current_audio.seq_id >= 0) {
                                SsSeqSetAccelerando(current_audio.seq_id, current_tempo, 120);
                            }
                        }
                        break;
                    case MENU_PROGRAM_EDIT:
                        enterProgramEdit();
                        break;
                }
            }
            
            // Square - Toggle min/max for current value
            if (pad & PADRleft && !(oldpad & PADRleft)) {
                if (menu_cursor >= MENU_TEMPO) {  // Only for value items
                    toggleMinMax();
                }
            }
            
            // L1 - Decrease by 10
            if (pad & PADL1 && !(oldpad & PADL1)) {
                if (menu_cursor >= MENU_TEMPO) {
                    adjustMenuValue(-1, 10);
                }
            }
            
            // R1 - Increase by 10
            if (pad & PADR1 && !(oldpad & PADR1)) {
                if (menu_cursor >= MENU_TEMPO) {
                    adjustMenuValue(1, 10);
                }
            }
            
            // D-Pad Left/Right with hold detection - Adjust by 1
            if (menu_cursor >= MENU_TEMPO) {  // Only for value items
                // Left button
                if (pad & PADLleft) {
                    hold_counter_left++;
                    
                    // Initial press
                    if (!(oldpad & PADLleft)) {
                        adjustMenuValue(-1, 1);
                        hold_counter_left = 0;
                        hold_active_left = 0;
                    }
                    // Hold threshold reached
                    else if (hold_counter_left >= HOLD_THRESHOLD && !hold_active_left) {
                        hold_active_left = 1;
                    }
                    // Continuous adjustment while held
                    else if (hold_active_left && (hold_counter_left % HOLD_REPEAT_RATE == 0)) {
                        adjustMenuValue(-1, 1);
                    }
                } else {
                    hold_counter_left = 0;
                    hold_active_left = 0;
                }
                
                // Right button
                if (pad & PADLright) {
                    hold_counter_right++;
                    
                    // Initial press
                    if (!(oldpad & PADLright)) {
                        adjustMenuValue(1, 1);
                        hold_counter_right = 0;
                        hold_active_right = 0;
                    }
                    // Hold threshold reached
                    else if (hold_counter_right >= HOLD_THRESHOLD && !hold_active_right) {
                        hold_active_right = 1;
                    }
                    // Continuous adjustment while held
                    else if (hold_active_right && (hold_counter_right % HOLD_REPEAT_RATE == 0)) {
                        adjustMenuValue(1, 1);
                    }
                } else {
                    hold_counter_right = 0;
                    hold_active_right = 0;
                }
            }
            // End of normal inputs - closing select_layer_active check
            }
            
            break;
            
        case STATE_VAB_VH_SELECT:
            if (pad & PADLup && !(oldpad & PADLup)) {
                cursor--;
                if (cursor < 0) cursor = MAX_VH_FILES - 1;
            }
            if (pad & PADLdown && !(oldpad & PADLdown)) {
                cursor++;
                if (cursor >= MAX_VH_FILES) cursor = 0;
            }
            if (pad & PADRdown && !(oldpad & PADRdown)) { // X button
                selected_vh = cursor;
                
                // Set up audio file structure for VAB mode
                current_audio.vh_data = vh_files[selected_vh].data;
                current_audio.vb_data = vb_files[selected_vh];
                current_audio.vh_size = vh_files[selected_vh].size;
                sprintf(current_audio.vh_name, "%s", vh_files[selected_vh].name);
                
                // Read VAB header info
                VabHdr* vab_hdr = (VabHdr*)current_audio.vh_data;
                current_audio.num_programs = vab_hdr->ps;
                current_audio.num_tones = vab_hdr->ts;
                
                // Open VAB for VAB mode
                current_audio.vab_id = SsVabOpenHead(current_audio.vh_data, -1);
                if (current_audio.vab_id < 0) {
                    FntPrint("Failed to open VAB header!\n");
                    break;
                }
                
                // Transfer VAB body
                if (SsVabTransBody(current_audio.vb_data, current_audio.vab_id) != current_audio.vab_id) {
                    FntPrint("Failed to transfer VAB body!\n");
                    SsVabClose(current_audio.vab_id);
                    current_audio.vab_id = -1;
                    break;
                }
                SsVabTransCompleted(SS_WAIT_COMPLETED);
                
                // Enable reverb
                SsUtSetReverbType(reverb_type);
                SsUtReverbOn();
                SsUtSetReverbDepth(reverb_depth_left, reverb_depth_right);
                
                // Reset VAB mode parameters
                current_note = 60;  // Middle C
                current_program = 0;
                note_playing = 0;
                current_voice = -1;
                menu_cursor = 0;
                
                current_state = STATE_VAB_PLAYBACK;
            }
            if (pad & PADRright && !(oldpad & PADRright)) { // Circle button - Back to SEQ select
                current_state = STATE_SEQ_SELECT;
                cursor = 0;
                vab_mode = 0;
            }
            break;
            
        case STATE_VAB_PLAYBACK:
            // Triangle - Play/sustain note ALWAYS works (even when Select held)
            // This prevents stuck notes
            if (pad & PADRup) {
                if (!(oldpad & PADRup)) {
                    // Just pressed - play note
                    playNote();
                }
                // Holding - note sustains automatically
            } else {
                if (oldpad & PADRup) {
                    // Just released - stop note
                    stopNote();
                }
            }
            
            // If Select is held, skip all other normal inputs (layer modifier)
            if (!select_layer_active) {
                // Circle - Back to VH select
                if (pad & PADRright && !(oldpad & PADRright)) {
                    stopNote();
                    if (current_audio.vab_id >= 0) {
                        SsVabClose(current_audio.vab_id);
                        current_audio.vab_id = -1;
                    }
                    current_state = STATE_VAB_VH_SELECT;
                    cursor = selected_vh;
                    menu_cursor = 0;
                }
                
                // D-Pad Up/Down - Navigate menu with hold detection
            if (pad & PADLup) {
                hold_counter_up++;
                if (!(oldpad & PADLup)) {
                    menu_cursor--;
                    if (menu_cursor < 0) menu_cursor = VAB_MENU_ITEM_COUNT - 1;
                    hold_counter_up = 0;
                    hold_active_up = 0;
                } else if (hold_counter_up >= HOLD_THRESHOLD && !hold_active_up) {
                    hold_active_up = 1;
                } else if (hold_active_up && (hold_counter_up % HOLD_REPEAT_RATE == 0)) {
                    menu_cursor--;
                    if (menu_cursor < 0) menu_cursor = VAB_MENU_ITEM_COUNT - 1;
                }
            } else {
                hold_counter_up = 0;
                hold_active_up = 0;
            }
            
            if (pad & PADLdown) {
                hold_counter_down++;
                if (!(oldpad & PADLdown)) {
                    menu_cursor++;
                    if (menu_cursor >= VAB_MENU_ITEM_COUNT) menu_cursor = 0;
                    hold_counter_down = 0;
                    hold_active_down = 0;
                } else if (hold_counter_down >= HOLD_THRESHOLD && !hold_active_down) {
                    hold_active_down = 1;
                } else if (hold_active_down && (hold_counter_down % HOLD_REPEAT_RATE == 0)) {
                    menu_cursor++;
                    if (menu_cursor >= VAB_MENU_ITEM_COUNT) menu_cursor = 0;
                }
            } else {
                hold_counter_down = 0;
                hold_active_down = 0;
            }
            
            // X button - Enter program edit if on PROGRAM_EDIT menu item
            if (pad & PADRdown && !(oldpad & PADRdown)) {
                if (menu_cursor == VAB_MENU_PROGRAM_EDIT) {
                    enterProgramEdit();
                }
            }
            
            // Square - Toggle min/max for current value
            if (pad & PADRleft && !(oldpad & PADRleft)) {
                toggleVabMinMax();
            }
            
            // L1 - Decrease by 10
            if (pad & PADL1 && !(oldpad & PADL1)) {
                adjustVabMenuValue(-1, 10);
            }
            
            // R1 - Increase by 10
            if (pad & PADR1 && !(oldpad & PADR1)) {
                adjustVabMenuValue(1, 10);
            }
            
            // D-Pad Left/Right with hold detection - Adjust by 1
            // Left button
            if (pad & PADLleft) {
                hold_counter_left++;
                
                // Initial press
                if (!(oldpad & PADLleft)) {
                    adjustVabMenuValue(-1, 1);
                    hold_counter_left = 0;
                    hold_active_left = 0;
                }
                // Hold threshold reached
                else if (hold_counter_left >= HOLD_THRESHOLD && !hold_active_left) {
                    hold_active_left = 1;
                }
                // Continuous adjustment while held
                else if (hold_active_left && (hold_counter_left % HOLD_REPEAT_RATE == 0)) {
                    adjustVabMenuValue(-1, 1);
                }
            } else {
                hold_counter_left = 0;
                hold_active_left = 0;
            }
            
            // Right button
            if (pad & PADLright) {
                hold_counter_right++;
                
                // Initial press
                if (!(oldpad & PADLright)) {
                    adjustVabMenuValue(1, 1);
                    hold_counter_right = 0;
                    hold_active_right = 0;
                }
                // Hold threshold reached
                else if (hold_counter_right >= HOLD_THRESHOLD && !hold_active_right) {
                    hold_active_right = 1;
                }
                // Continuous adjustment while held
                else if (hold_active_right && (hold_counter_right % HOLD_REPEAT_RATE == 0)) {
                    adjustVabMenuValue(1, 1);
                }
            } else {
                hold_counter_right = 0;
                hold_active_right = 0;
            }
            // End of normal inputs - closing select_layer_active check
            }
            
            break;
            
        case STATE_PROGRAM_EDIT:
            // If Select is held, skip all normal inputs (layer modifier)
            if (!select_layer_active) {
                // Circle - Back to playback
                if (pad & PADRright && !(oldpad & PADRright)) {
                    current_state = return_state;
                    menu_cursor = 0;
                }
                
                // D-Pad Up/Down - Navigate menu with hold detection
            if (pad & PADLup) {
                hold_counter_up++;
                if (!(oldpad & PADLup)) {
                    menu_cursor--;
                    if (menu_cursor < 0) menu_cursor = PROG_MENU_ITEM_COUNT - 1;
                    hold_counter_up = 0;
                    hold_active_up = 0;
                } else if (hold_counter_up >= HOLD_THRESHOLD && !hold_active_up) {
                    hold_active_up = 1;
                } else if (hold_active_up && (hold_counter_up % HOLD_REPEAT_RATE == 0)) {
                    menu_cursor--;
                    if (menu_cursor < 0) menu_cursor = PROG_MENU_ITEM_COUNT - 1;
                }
            } else {
                hold_counter_up = 0;
                hold_active_up = 0;
            }
            
            if (pad & PADLdown) {
                hold_counter_down++;
                if (!(oldpad & PADLdown)) {
                    menu_cursor++;
                    if (menu_cursor >= PROG_MENU_ITEM_COUNT) menu_cursor = 0;
                    hold_counter_down = 0;
                    hold_active_down = 0;
                } else if (hold_counter_down >= HOLD_THRESHOLD && !hold_active_down) {
                    hold_active_down = 1;
                } else if (hold_active_down && (hold_counter_down % HOLD_REPEAT_RATE == 0)) {
                    menu_cursor++;
                    if (menu_cursor >= PROG_MENU_ITEM_COUNT) menu_cursor = 0;
                }
            } else {
                hold_counter_down = 0;
                hold_active_down = 0;
            }
            
            // X button - Enter tone edit if on NUM_TONES
            if (pad & PADRdown && !(oldpad & PADRdown)) {
                if (menu_cursor == PROG_MENU_NUM_TONES) {
                    edit_tone = 0;
                    loadToneData();
                    current_state = STATE_TONE_EDIT;
                    menu_cursor = 0;
                }
            }
            
            // Square - Toggle min/max
            if (pad & PADRleft && !(oldpad & PADRleft)) {
                toggleProgramEditMinMax();
            }
            
            // L1 - Decrease by 10
            if (pad & PADL1 && !(oldpad & PADL1)) {
                adjustProgramEditValue(-1, 10);
            }
            
            // R1 - Increase by 10
            if (pad & PADR1 && !(oldpad & PADR1)) {
                adjustProgramEditValue(1, 10);
            }
            
            // D-Pad Left/Right with hold detection
            if (pad & PADLleft) {
                hold_counter_left++;
                if (!(oldpad & PADLleft)) {
                    adjustProgramEditValue(-1, 1);
                    hold_counter_left = 0;
                    hold_active_left = 0;
                } else if (hold_counter_left >= HOLD_THRESHOLD && !hold_active_left) {
                    hold_active_left = 1;
                } else if (hold_active_left && (hold_counter_left % HOLD_REPEAT_RATE == 0)) {
                    adjustProgramEditValue(-1, 1);
                }
            } else {
                hold_counter_left = 0;
                hold_active_left = 0;
            }
            
            if (pad & PADLright) {
                hold_counter_right++;
                if (!(oldpad & PADLright)) {
                    adjustProgramEditValue(1, 1);
                    hold_counter_right = 0;
                    hold_active_right = 0;
                } else if (hold_counter_right >= HOLD_THRESHOLD && !hold_active_right) {
                    hold_active_right = 1;
                } else if (hold_active_right && (hold_counter_right % HOLD_REPEAT_RATE == 0)) {
                    adjustProgramEditValue(1, 1);
                }
            } else {
                hold_counter_right = 0;
                hold_active_right = 0;
            }
            // End of normal inputs - closing select_layer_active check
            }
            
            // Triangle - Play note in VAB mode ALWAYS works (even when Select held)
            if (vab_mode) {
                if (pad & PADRup) {
                    if (!(oldpad & PADRup)) {
                        playNote();
                    }
                } else {
                    if (oldpad & PADRup) {
                        stopNote();
                    }
                }
            }
            
            break;
            
        case STATE_TONE_EDIT:
            // Check if we're in ADSR editing mode
            if (adsr_editing > 0) {
                // ADSR HEX EDITING MODE
                // Skip all inputs if Select is held (layer modifier)
                if (!select_layer_active) {
                    // Circle - Cancel editing
                if (pad & PADRright && !(oldpad & PADRright)) {
                    // Restore original value
                    if (adsr_editing == 1) {
                        current_vag_atr.adsr1 = adsr_original_value;
                    } else {
                        current_vag_atr.adsr2 = adsr_original_value;
                    }
                    adsr_editing = 0;
                    adsr_digit_pos = 0;
                }
                
                // X - Confirm editing
                if (pad & PADRdown && !(oldpad & PADRdown)) {
                    // Save the edited value
                    if (adsr_editing == 1) {
                        current_vag_atr.adsr1 = adsr_temp_value;
                    } else {
                        current_vag_atr.adsr2 = adsr_temp_value;
                    }
                    saveToneData();
                    adsr_editing = 0;
                    adsr_digit_pos = 0;
                }
                
                // Left/Right - Navigate digits
                if (pad & PADLleft && !(oldpad & PADLleft)) {
                    adsr_digit_pos--;
                    if (adsr_digit_pos < 0) adsr_digit_pos = 3;
                }
                if (pad & PADLright && !(oldpad & PADLright)) {
                    adsr_digit_pos++;
                    if (adsr_digit_pos > 3) adsr_digit_pos = 0;
                }
                
                // Up/Down - Adjust digit value with hold detection
                if (pad & PADLup) {
                    hold_counter_up++;
                    if (!(oldpad & PADLup)) {
                        // Increase digit value
                        int shift = (3 - adsr_digit_pos) * 4;
                        int digit = (adsr_temp_value >> shift) & 0xF;
                        digit = (digit + 1) & 0xF;  // Wrap at 0xF
                        adsr_temp_value = (adsr_temp_value & ~(0xF << shift)) | (digit << shift);
                        hold_counter_up = 0;
                        hold_active_up = 0;
                    } else if (hold_counter_up >= HOLD_THRESHOLD && !hold_active_up) {
                        hold_active_up = 1;
                    } else if (hold_active_up && (hold_counter_up % HOLD_REPEAT_RATE == 0)) {
                        int shift = (3 - adsr_digit_pos) * 4;
                        int digit = (adsr_temp_value >> shift) & 0xF;
                        digit = (digit + 1) & 0xF;
                        adsr_temp_value = (adsr_temp_value & ~(0xF << shift)) | (digit << shift);
                    }
                } else {
                    hold_counter_up = 0;
                    hold_active_up = 0;
                }
                
                if (pad & PADLdown) {
                    hold_counter_down++;
                    if (!(oldpad & PADLdown)) {
                        // Decrease digit value
                        int shift = (3 - adsr_digit_pos) * 4;
                        int digit = (adsr_temp_value >> shift) & 0xF;
                        digit = (digit - 1) & 0xF;  // Wrap at 0xF
                        adsr_temp_value = (adsr_temp_value & ~(0xF << shift)) | (digit << shift);
                        hold_counter_down = 0;
                        hold_active_down = 0;
                    } else if (hold_counter_down >= HOLD_THRESHOLD && !hold_active_down) {
                        hold_active_down = 1;
                    } else if (hold_active_down && (hold_counter_down % HOLD_REPEAT_RATE == 0)) {
                        int shift = (3 - adsr_digit_pos) * 4;
                        int digit = (adsr_temp_value >> shift) & 0xF;
                        digit = (digit - 1) & 0xF;
                        adsr_temp_value = (adsr_temp_value & ~(0xF << shift)) | (digit << shift);
                    }
                } else {
                    hold_counter_down = 0;
                    hold_active_down = 0;
                }
                // End of ADSR editing mode - closing select_layer_active check
                }
            } else {
                // NORMAL NAVIGATION MODE
                
                // Select+L1/R1 combos work when Select is held (NOT wrapped)
                if (pad & PADselect) {
                    if (pad & PADL1 && !(oldpad & PADL1)) {
                        // Previous program
                        saveToneData();
                        edit_program--;
                        if (edit_program < 0) edit_program = 0;
                        loadProgramData();
                        edit_tone = 0;
                        loadToneData();
                    }
                    if (pad & PADR1 && !(oldpad & PADR1)) {
                        // Next program
                        saveToneData();
                        edit_program++;
                        if (edit_program >= current_audio.num_programs) {
                            edit_program = current_audio.num_programs - 1;
                        }
                        loadProgramData();
                        edit_tone = 0;
                        loadToneData();
                    }
                }
                
                // All other normal inputs (wrapped - disabled when Select held)
                if (!select_layer_active) {
                    // Circle - Back to program edit
                if (pad & PADRright && !(oldpad & PADRright)) {
                    current_state = STATE_PROGRAM_EDIT;
                    menu_cursor = PROG_MENU_NUM_TONES;
                }
                
                // D-Pad Up/Down - Navigate menu with hold detection (skip NOTE_SEL if not VAB mode)
                if (pad & PADLup) {
                    hold_counter_up++;
                    if (!(oldpad & PADLup)) {
                        do {
                            menu_cursor--;
                            if (menu_cursor < 0) menu_cursor = TONE_MENU_ITEM_COUNT - 1;
                        } while (menu_cursor == TONE_MENU_NOTE_SEL && !vab_mode);
                        hold_counter_up = 0;
                        hold_active_up = 0;
                    } else if (hold_counter_up >= HOLD_THRESHOLD && !hold_active_up) {
                        hold_active_up = 1;
                    } else if (hold_active_up && (hold_counter_up % HOLD_REPEAT_RATE == 0)) {
                        do {
                            menu_cursor--;
                            if (menu_cursor < 0) menu_cursor = TONE_MENU_ITEM_COUNT - 1;
                        } while (menu_cursor == TONE_MENU_NOTE_SEL && !vab_mode);
                    }
                } else {
                    hold_counter_up = 0;
                    hold_active_up = 0;
                }
                
                if (pad & PADLdown) {
                    hold_counter_down++;
                    if (!(oldpad & PADLdown)) {
                        do {
                            menu_cursor++;
                            if (menu_cursor >= TONE_MENU_ITEM_COUNT) menu_cursor = 0;
                        } while (menu_cursor == TONE_MENU_NOTE_SEL && !vab_mode);
                        hold_counter_down = 0;
                        hold_active_down = 0;
                    } else if (hold_counter_down >= HOLD_THRESHOLD && !hold_active_down) {
                        hold_active_down = 1;
                    } else if (hold_active_down && (hold_counter_down % HOLD_REPEAT_RATE == 0)) {
                        do {
                            menu_cursor++;
                            if (menu_cursor >= TONE_MENU_ITEM_COUNT) menu_cursor = 0;
                        } while (menu_cursor == TONE_MENU_NOTE_SEL && !vab_mode);
                    }
                } else {
                    hold_counter_down = 0;
                    hold_active_down = 0;
                }
                
                // X button - Enter ADSR edit mode if on ADSR1 or ADSR2
                if (pad & PADRdown && !(oldpad & PADRdown)) {
                    if (menu_cursor == TONE_MENU_ADSR1) {
                        adsr_editing = 1;
                        adsr_digit_pos = 0;
                        adsr_temp_value = current_vag_atr.adsr1;
                        adsr_original_value = current_vag_atr.adsr1;
                    } else if (menu_cursor == TONE_MENU_ADSR2) {
                        adsr_editing = 2;
                        adsr_digit_pos = 0;
                        adsr_temp_value = current_vag_atr.adsr2;
                        adsr_original_value = current_vag_atr.adsr2;
                    }
                }
                
                // Square - Toggle min/max
                if (pad & PADRleft && !(oldpad & PADRleft)) {
                    toggleToneEditMinMax();
                }
                
                // L1 - Decrease by 10 (skip for ADSR)
                if (pad & PADL1 && !(oldpad & PADL1)) {
                    if (menu_cursor != TONE_MENU_ADSR1 && menu_cursor != TONE_MENU_ADSR2) {
                        adjustToneEditValue(-1, 10);
                    }
                }
                
                // R1 - Increase by 10 (skip for ADSR)
                if (pad & PADR1 && !(oldpad & PADR1)) {
                    if (menu_cursor != TONE_MENU_ADSR1 && menu_cursor != TONE_MENU_ADSR2) {
                        adjustToneEditValue(1, 10);
                    }
                }
                
                // D-Pad Left/Right with hold detection (skip for ADSR)
                if (menu_cursor != TONE_MENU_ADSR1 && menu_cursor != TONE_MENU_ADSR2) {
                    if (pad & PADLleft) {
                        hold_counter_left++;
                        if (!(oldpad & PADLleft)) {
                            adjustToneEditValue(-1, 1);
                            hold_counter_left = 0;
                            hold_active_left = 0;
                        } else if (hold_counter_left >= HOLD_THRESHOLD && !hold_active_left) {
                            hold_active_left = 1;
                        } else if (hold_active_left && (hold_counter_left % HOLD_REPEAT_RATE == 0)) {
                            adjustToneEditValue(-1, 1);
                        }
                    } else {
                        hold_counter_left = 0;
                        hold_active_left = 0;
                    }
                    
                    if (pad & PADLright) {
                        hold_counter_right++;
                        if (!(oldpad & PADLright)) {
                            adjustToneEditValue(1, 1);
                            hold_counter_right = 0;
                            hold_active_right = 0;
                        } else if (hold_counter_right >= HOLD_THRESHOLD && !hold_active_right) {
                            hold_active_right = 1;
                        } else if (hold_active_right && (hold_counter_right % HOLD_REPEAT_RATE == 0)) {
                            adjustToneEditValue(1, 1);
                        }
                    } else {
                        hold_counter_right = 0;
                        hold_active_right = 0;
                    }
                }
                // End of normal inputs - closing select_layer_active check
                }
                
                // Triangle - Play note in VAB mode ALWAYS works (even when Select held)
                if (vab_mode) {
                    if (pad & PADRup) {
                        if (!(oldpad & PADRup)) {
                            playNote();
                        }
                    } else {
                        if (oldpad & PADRup) {
                            stopNote();
                        }
                    }
                }
            }
            
            break;
    }
    
    oldpad = pad;
}

void drawSeqSelect(void)
{
    int i;
    
	FntPrint("\n");
    FntPrint("=== SELECT SEQUENCE FILE ===\n\n");
    FntPrint("Available SEQ files:\n\n");
    
    for (i = 0; i < MAX_SEQ_FILES; i++) {
        if (i == cursor) {
            FntPrint("> %s\n", seq_files[i].name);
        } else {
            FntPrint("  %s\n", seq_files[i].name);
        }
    }
    
    FntPrint("\n");
    FntPrint("X: Select (SEQ Mode)\n");
    FntPrint("Square: SOUNDBANK Mode\n");
}

void drawVhSelect(void)
{
    int i;
    
	FntPrint("\n");
    FntPrint("=== SELECT SOUNDBANK ===\n\n");
    FntPrint("Selected SEQ: %s\n\n", seq_files[selected_seq].name);
    FntPrint("Available VH files:\n\n");
    
    for (i = 0; i < MAX_VH_FILES; i++) {
        if (i == cursor) {
            FntPrint("> %s\n", vh_files[i].name);
        } else {
            FntPrint("  %s\n", vh_files[i].name);
        }
    }
    
    FntPrint("\n");
    FntPrint("X: Select\n");
    FntPrint("Circle: Back\n");
}

void drawPlayback(void)
{
    const char* status_text;
    
    FntPrint("\n");
    FntPrint("=== PLAYBACK ===\n\n");
    FntPrint("SEQ: %s\n", current_audio.seq_name);
    FntPrint("VH: %s\n", current_audio.vh_name);
    FntPrint("Progs: %d Tones: %d\n\n", current_audio.num_programs, current_audio.num_tones);
    
    // Determine status
    if (is_playing && is_paused) {
        status_text = "PAUSED";
    } else if (is_playing) {
        status_text = "PLAYING";
    } else {
        status_text = "STOPPED";
    }
    FntPrint("Status: %s\n\n", status_text);
    
    // Menu items
    FntPrint("=== MENU ===\n");
    
    // PLAY
    if (menu_cursor == MENU_PLAY) {
        FntPrint("> PLAY\n");
    } else {
        FntPrint("  PLAY\n");
    }
    
    // PAUSE
    if (menu_cursor == MENU_PAUSE) {
        FntPrint("> PAUSE\n");
    } else {
        FntPrint("  PAUSE\n");
    }
    
    // STOP
    if (menu_cursor == MENU_STOP) {
        FntPrint("> STOP\n");
    } else {
        FntPrint("  STOP\n");
    }
    
    // TEMPO
    if (menu_cursor == MENU_TEMPO) {
        if (tempo_changed) {
            FntPrint("> TEMPO: %d\n", current_tempo);
        } else {
            FntPrint("> TEMPO: unchanged (X)\n");
        }
    } else {
        if (tempo_changed) {
            FntPrint("  TEMPO: %d\n", current_tempo);
        } else {
            FntPrint("  TEMPO: unchanged\n");
        }
    }
    
    // REVERB TYPE
    if (menu_cursor == MENU_REV_TYPE) {
        FntPrint("> REV TYPE: %s\n", getReverbTypeName(reverb_type));
    } else {
        FntPrint("  REV TYPE: %s\n", getReverbTypeName(reverb_type));
    }
    
    // REVERB DEPTH
    if (menu_cursor == MENU_REV_DEPTH) {
        FntPrint("> REV DEPTH: %d\n", reverb_depth_left);
    } else {
        FntPrint("  REV DEPTH: %d\n", reverb_depth_left);
    }
    
    // REVERB DELAY
    if (menu_cursor == MENU_REV_DELAY) {
        FntPrint("> REV DELAY: %d\n", reverb_delay);
    } else {
        FntPrint("  REV DELAY: %d\n", reverb_delay);
    }
    
    // REVERB FEEDBACK
    if (menu_cursor == MENU_REV_FEEDBACK) {
        FntPrint("> REV FEEDBACK: %d\n", reverb_feedback);
    } else {
        FntPrint("  REV FEEDBACK: %d\n", reverb_feedback);
    }
    
    // PROGRAM EDIT
    if (menu_cursor == MENU_PROGRAM_EDIT) {
        FntPrint("> PROGRAM EDIT\n");
    } else {
        FntPrint("  PROGRAM EDIT\n");
    }
    
    FntPrint("\n=== CONTROLS ===\n");
    FntPrint("X: Select\n");
    FntPrint("Triangle: Toggle Play\n");
    FntPrint("Start: Pause\n");
    FntPrint("L/R:-1/+1 L1/R1:-10+10\n");
    FntPrint("Square: Min/Max\n");
    FntPrint("Circle: Back\n");
}

void drawVabVhSelect(void)
{
    int i;
    
    FntPrint("\n");
    FntPrint("=== SOUNDBANK MODE ===\n\n");
    FntPrint("Available VH files:\n\n");
    
    for (i = 0; i < MAX_VH_FILES; i++) {
        if (i == cursor) {
            FntPrint("> %s\n", vh_files[i].name);
        } else {
            FntPrint("  %s\n", vh_files[i].name);
        }
    }
    
    FntPrint("\n");
    FntPrint("X: Select\n");
    FntPrint("Circle: Back to Mode Select\n");
}

void drawVabPlayback(void)
{
    const char* note_names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = (current_note / 12) - 1;
    const char* note_name = note_names[current_note % 12];
    
    FntPrint("\n");
    FntPrint("=== VAB PLAYER ===\n\n");
    FntPrint("VH: %s\n", current_audio.vh_name);
    FntPrint("Programs: %d\n", current_audio.num_programs);
    FntPrint("Tones: %d\n\n", current_audio.num_tones);
    
    if (note_playing) {
        FntPrint("Status: PLAYING\n\n");
    } else {
        FntPrint("Status: STOPPED\n\n");
    }
    
    // Menu items
    FntPrint("=== MENU ===\n");
    
    // NOTE
    if (menu_cursor == VAB_MENU_NOTE) {
        FntPrint("> NOTE: %d (%s%d)\n", current_note, note_name, octave);
    } else {
        FntPrint("  NOTE: %d (%s%d)\n", current_note, note_name, octave);
    }
    
    // PROGRAM
    if (menu_cursor == VAB_MENU_PROGRAM) {
        FntPrint("> PROGRAM: %d\n", current_program);
    } else {
        FntPrint("  PROGRAM: %d\n", current_program);
    }
    
    // REVERB TYPE
    if (menu_cursor == VAB_MENU_REV_TYPE) {
        FntPrint("> REV TYPE: %s\n", getReverbTypeName(reverb_type));
    } else {
        FntPrint("  REV TYPE: %s\n", getReverbTypeName(reverb_type));
    }
    
    // REVERB DEPTH
    if (menu_cursor == VAB_MENU_REV_DEPTH) {
        FntPrint("> REV DEPTH: %d\n", reverb_depth_left);
    } else {
        FntPrint("  REV DEPTH: %d\n", reverb_depth_left);
    }
    
    // REVERB DELAY
    if (menu_cursor == VAB_MENU_REV_DELAY) {
        FntPrint("> REV DELAY: %d\n", reverb_delay);
    } else {
        FntPrint("  REV DELAY: %d\n", reverb_delay);
    }
    
    // REVERB FEEDBACK
    if (menu_cursor == VAB_MENU_REV_FEEDBACK) {
        FntPrint("> REV FEEDBACK: %d\n", reverb_feedback);
    } else {
        FntPrint("  REV FEEDBACK: %d\n", reverb_feedback);
    }
    
    // PROGRAM EDIT
    if (menu_cursor == VAB_MENU_PROGRAM_EDIT) {
        FntPrint("> PROGRAM EDIT\n");
    } else {
        FntPrint("  PROGRAM EDIT\n");
    }
    
    FntPrint("\n=== CONTROLS ===\n");
    FntPrint("Triangle: Play Note\n");
    FntPrint("L2/R2: Note +/-\n");
    FntPrint("L/R:-1/+1 L1/R1:-10+10\n");
    FntPrint("Square: Min/Max\n");
    FntPrint("Circle: Back\n");
}

void drawProgramEdit(void)
{
    VabHdr* vab_hdr = (VabHdr*)current_audio.vh_data;
    
    FntPrint("\n");
    FntPrint("=== PROGRAM EDITOR ===  ");
	
	// Display an indicator when SEQ or note is playing on submenus
	if (note_playing) {
        FntPrint("*NOTE ON*");
    }

	if (is_playing && is_paused) {
        FntPrint("*PAUSED*");
    } else if (is_playing) {
        FntPrint("*PLAYING*");
    }

    FntPrint("\n\nVH: %s\n", current_audio.vh_name);
    FntPrint("Programs: %d\n", vab_hdr->ps);
    FntPrint("Tones: %d\n", vab_hdr->ts);
    FntPrint("VAGs: %d\n\n", vab_hdr->vs);
    
    FntPrint("=== MENU ===\n");
    
    // PROGRAM SELECTOR
    if (menu_cursor == PROG_MENU_PROGRAM_SEL) {
        FntPrint("> PROGRAM: %d\n", edit_program);
    } else {
        FntPrint("  PROGRAM: %d\n", edit_program);
    }
    
    // NUM TONES (press X to edit)
    if (menu_cursor == PROG_MENU_NUM_TONES) {
        FntPrint("> TONES: %d (X:Edit)\n", current_prog_atr.tones);
    } else {
        FntPrint("  TONES: %d\n", current_prog_atr.tones);
    }
    
    // PROGRAM VOLUME
    if (menu_cursor == PROG_MENU_PROG_VOL) {
        FntPrint("> PROG VOL: %d%s\n", current_prog_atr.mvol, 
                 isProgramValueChanged(PROG_MENU_PROG_VOL) ? " !" : "");
    } else {
        FntPrint("  PROG VOL: %d%s\n", current_prog_atr.mvol,
                 isProgramValueChanged(PROG_MENU_PROG_VOL) ? " !" : "");
    }
    
    // PROGRAM PAN
    if (menu_cursor == PROG_MENU_PROG_PAN) {
        FntPrint("> PROG PAN: %d%s\n", current_prog_atr.mpan,
                 isProgramValueChanged(PROG_MENU_PROG_PAN) ? " !" : "");
    } else {
        FntPrint("  PROG PAN: %d%s\n", current_prog_atr.mpan,
                 isProgramValueChanged(PROG_MENU_PROG_PAN) ? " !" : "");
    }
    
    // MASTER VOLUME
    if (menu_cursor == PROG_MENU_MASTER_VOL) {
        FntPrint("> MASTER VOL: %d%s\n", vab_master_vol,
                 isProgramValueChanged(PROG_MENU_MASTER_VOL) ? " !" : "");
    } else {
        FntPrint("  MASTER VOL: %d%s\n", vab_master_vol,
                 isProgramValueChanged(PROG_MENU_MASTER_VOL) ? " !" : "");
    }
    
    // MASTER PAN
    if (menu_cursor == PROG_MENU_MASTER_PAN) {
        FntPrint("> MASTER PAN: %d%s\n", vab_master_pan,
                 isProgramValueChanged(PROG_MENU_MASTER_PAN) ? " !" : "");
    } else {
        FntPrint("  MASTER PAN: %d%s\n", vab_master_pan,
                 isProgramValueChanged(PROG_MENU_MASTER_PAN) ? " !" : "");
    }
    
    FntPrint("\n=== CONTROLS ===\n");
    if (vab_mode) {
        FntPrint("Triangle: Play\n");
        FntPrint("L2/R2: Note +/-\n");
    } else {
        FntPrint("Triangle: Play/Stop\n");
        FntPrint("Start: Pause\n");
    }
    FntPrint("Circle: Back\n");
}

void drawToneEdit(void)
{
    const char* note_names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave, center_octave;
    const char* note_name;
    const char* center_note_name;
    
    FntPrint("\n");
    FntPrint("=== TONE EDITOR ===  ");

	// Display an indicator when SEQ or note is playing on submenus
	if (note_playing) {
        FntPrint("*NOTE ON*");
    }

	if (is_playing && is_paused) {
        FntPrint("*PAUSED*");
    } else if (is_playing) {
        FntPrint("*PLAYING*");
    }

    FntPrint("\n\nProg: %d  VAG: %d\n\n", current_vag_atr.prog, current_vag_atr.vag);
    
    FntPrint("=== MENU ===\n");
    
    // PROGRAM (navigable)
    if (menu_cursor == TONE_MENU_PROG) {
        FntPrint("> PROGRAM: %d\n", edit_program);
    } else {
        FntPrint("  PROGRAM: %d\n", edit_program);
    }
    
    // TONE SELECTOR
    if (menu_cursor == TONE_MENU_TONE_SEL) {
        FntPrint("> TONE: %d/%d\n", edit_tone, current_prog_atr.tones - 1);
    } else {
        FntPrint("  TONE: %d/%d\n", edit_tone, current_prog_atr.tones - 1);
    }
    
    // NOTE SELECTOR (VAB mode only)
    if (vab_mode) {
        octave = (current_note / 12) - 1;
        note_name = note_names[current_note % 12];
        if (menu_cursor == TONE_MENU_NOTE_SEL) {
            FntPrint("> NOTE: %d (%s%d)\n", current_note, note_name, octave);
        } else {
            FntPrint("  NOTE: %d (%s%d)\n", current_note, note_name, octave);
        }
    }
    
    // PRIORITY
    if (menu_cursor == TONE_MENU_PRIOR) {
        FntPrint("> PRIORITY: %d%s\n", current_vag_atr.prior,
                 isToneValueChanged(TONE_MENU_PRIOR) ? " !" : "");
    } else {
        FntPrint("  PRIORITY: %d%s\n", current_vag_atr.prior,
                 isToneValueChanged(TONE_MENU_PRIOR) ? " !" : "");
    }
    
    // MODE
    if (menu_cursor == TONE_MENU_MODE) {
        FntPrint("> MODE: %s%s\n", current_vag_atr.mode == 0 ? "NORMAL" : "REVERB",
                 isToneValueChanged(TONE_MENU_MODE) ? " !" : "");
    } else {
        FntPrint("  MODE: %s%s\n", current_vag_atr.mode == 0 ? "NORMAL" : "REVERB",
                 isToneValueChanged(TONE_MENU_MODE) ? " !" : "");
    }
    
    // VOLUME
    if (menu_cursor == TONE_MENU_VOL) {
        FntPrint("> VOL: %d%s\n", current_vag_atr.vol,
                 isToneValueChanged(TONE_MENU_VOL) ? " !" : "");
    } else {
        FntPrint("  VOL: %d%s\n", current_vag_atr.vol,
                 isToneValueChanged(TONE_MENU_VOL) ? " !" : "");
    }
    
    // PAN
    if (menu_cursor == TONE_MENU_PAN) {
        FntPrint("> PAN: %d%s\n", current_vag_atr.pan,
                 isToneValueChanged(TONE_MENU_PAN) ? " !" : "");
    } else {
        FntPrint("  PAN: %d%s\n", current_vag_atr.pan,
                 isToneValueChanged(TONE_MENU_PAN) ? " !" : "");
    }
    
    // CENTER NOTE
    center_octave = (current_vag_atr.center / 12) - 1;
    center_note_name = note_names[current_vag_atr.center % 12];
    if (menu_cursor == TONE_MENU_CENTER) {
        FntPrint("> CENTER: %d(%s%d)%s\n", current_vag_atr.center, center_note_name, center_octave,
                 isToneValueChanged(TONE_MENU_CENTER) ? " !" : "");
    } else {
        FntPrint("  CENTER: %d(%s%d)%s\n", current_vag_atr.center, center_note_name, center_octave,
                 isToneValueChanged(TONE_MENU_CENTER) ? " !" : "");
    }
    
    // SHIFT
    if (menu_cursor == TONE_MENU_SHIFT) {
        FntPrint("> SHIFT: %d%s\n", current_vag_atr.shift,
                 isToneValueChanged(TONE_MENU_SHIFT) ? " !" : "");
    } else {
        FntPrint("  SHIFT: %d%s\n", current_vag_atr.shift,
                 isToneValueChanged(TONE_MENU_SHIFT) ? " !" : "");
    }
    
    // MIN/MAX
    if (menu_cursor == TONE_MENU_MIN) {
        FntPrint("> MIN: %d%s\n", current_vag_atr.min,
                 isToneValueChanged(TONE_MENU_MIN) ? " !" : "");
    } else {
        FntPrint("  MIN: %d%s\n", current_vag_atr.min,
                 isToneValueChanged(TONE_MENU_MIN) ? " !" : "");
    }
    
    if (menu_cursor == TONE_MENU_MAX) {
        FntPrint("> MAX: %d%s\n", current_vag_atr.max,
                 isToneValueChanged(TONE_MENU_MAX) ? " !" : "");
    } else {
        FntPrint("  MAX: %d%s\n", current_vag_atr.max,
                 isToneValueChanged(TONE_MENU_MAX) ? " !" : "");
    }
    
    // PITCH BEND
    if (menu_cursor == TONE_MENU_PBMIN) {
        FntPrint("> PBMIN: %d%s\n", current_vag_atr.pbmin,
                 isToneValueChanged(TONE_MENU_PBMIN) ? " !" : "");
    } else {
        FntPrint("  PBMIN: %d%s\n", current_vag_atr.pbmin,
                 isToneValueChanged(TONE_MENU_PBMIN) ? " !" : "");
    }
    
    if (menu_cursor == TONE_MENU_PBMAX) {
        FntPrint("> PBMAX: %d%s\n", current_vag_atr.pbmax,
                 isToneValueChanged(TONE_MENU_PBMAX) ? " !" : "");
    } else {
        FntPrint("  PBMAX: %d%s\n", current_vag_atr.pbmax,
                 isToneValueChanged(TONE_MENU_PBMAX) ? " !" : "");
    }
    
    // ADSR1 - Show with highlighted digit if editing
    if (menu_cursor == TONE_MENU_ADSR1) {
        if (adsr_editing == 1) {
            // Editing mode - show with highlighted digit
            FntPrint("> ADSR1: 0x");
            int i;
            for (i = 0; i < 4; i++) {
                int digit = (adsr_temp_value >> ((3 - i) * 4)) & 0xF;
                if (i == adsr_digit_pos) {
                    FntPrint("[%X]", digit);
                } else {
                    FntPrint("%X", digit);
                }
            }
            FntPrint("%s\n", isToneValueChanged(TONE_MENU_ADSR1) ? " !" : "");
        } else {
            FntPrint("> ADSR1: 0x%04X (X)%s\n", current_vag_atr.adsr1,
                     isToneValueChanged(TONE_MENU_ADSR1) ? " !" : "");
        }
    } else {
        FntPrint("  ADSR1: 0x%04X%s\n", current_vag_atr.adsr1,
                 isToneValueChanged(TONE_MENU_ADSR1) ? " !" : "");
    }
    
    // ADSR2 - Show with highlighted digit if editing
    if (menu_cursor == TONE_MENU_ADSR2) {
        if (adsr_editing == 2) {
            // Editing mode - show with highlighted digit
            FntPrint("> ADSR2: 0x");
            int i;
            for (i = 0; i < 4; i++) {
                int digit = (adsr_temp_value >> ((3 - i) * 4)) & 0xF;
                if (i == adsr_digit_pos) {
                    FntPrint("[%X]", digit);
                } else {
                    FntPrint("%X", digit);
                }
            }
            FntPrint("%s\n", isToneValueChanged(TONE_MENU_ADSR2) ? " !" : "");
        } else {
            FntPrint("> ADSR2: 0x%04X (X)%s\n", current_vag_atr.adsr2,
                     isToneValueChanged(TONE_MENU_ADSR2) ? " !" : "");
        }
    } else {
        FntPrint("  ADSR2: 0x%04X%s\n", current_vag_atr.adsr2,
                 isToneValueChanged(TONE_MENU_ADSR2) ? " !" : "");
    }
    
    FntPrint("\n=== CONTROLS ===\n");
    if (adsr_editing > 0) {
        FntPrint("X: Confirm\n");
        FntPrint("Circle: Cancel\n");
    } else {
        if (vab_mode) {
            FntPrint("Triangle: Play\n");
            FntPrint("L2/R2: Note +/-\n");
        } else {
            FntPrint("Triangle: Play/Stop\n");
            FntPrint("Start: Pause\n");
        }
        FntPrint("X: Edit ADSR\n");
        FntPrint("Circle: Back\n");
    }
}

void drawUI(void)
{
    switch (current_state) {
        case STATE_SEQ_SELECT:
            drawSeqSelect();
            break;
        case STATE_VH_SELECT:
            drawVhSelect();
            break;
        case STATE_PLAYBACK:
            drawPlayback();
            break;
        case STATE_VAB_VH_SELECT:
            drawVabVhSelect();
            break;
        case STATE_VAB_PLAYBACK:
            drawVabPlayback();
            break;
        case STATE_PROGRAM_EDIT:
            drawProgramEdit();
            break;
        case STATE_TONE_EDIT:
            drawToneEdit();
            break;
    }
}

int main(void)
{
    // Initialize
    initGraph();
    initSound();
    PadInit(0);
    
    // Load file information
    loadAudioFiles();
    
    // Main loop
    while (1)
    {
        processInput();
        drawUI();
        
        FntFlush(-1);
        display();
        
        //ONLY USE THIS COMMAND IF NOTICK IS ENABLED
		//otherwise everything plays fast

        //SsSeqCalledTbyT();
    }
    
    return 0;
}