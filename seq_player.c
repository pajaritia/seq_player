// PS1 SEQ/VH/VB Player
// Controller-driven UI for playing sequence files with soundbanks
// Based on nolibgs_hello_worlds SDK
// Uses libsnd for sequence/VAB playback
// Uses the PicoDev PIO cartridge UART for MIDI input
// Optional libcd CD-ROM browser to load SEQ/VH/VB files

#include <sys/types.h>
#include <stdio.h>
#include <libetc.h>
#include <libgpu.h>
#include <libsnd.h>
#include <libspu.h>
#ifdef ENABLE_CD_BROWSER
#include <libcd.h>
#endif

// Include auto-generated file configuration
#include "fileconfig.h"

#define VMODE 0                 // Video Mode : 0 : NTSC, 1: PAL
#define SCREENXRES 320
#define SCREENYRES 240

// Debug menu is configured in the Makefile via CPPFLAGS += -DENABLE_DEBUG_MENU
// Comment that line to remove debug from build

#ifndef offsetof
#define offsetof(t, f) ((unsigned long)&(((t*)0)->f))
#endif

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
    STATE_TONE_EDIT,
    STATE_ADSR_EDIT,
    STATE_VISUALIZER,
#ifdef ENABLE_CD_BROWSER
    STATE_CD_BROWSER,
#endif
#ifdef ENABLE_DEBUG_MENU
    STATE_DEBUG
#endif
} UIState;

// SEQ mode menu items
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
    MENU_VISUALIZER,
    MENU_ITEM_COUNT
} PlaybackMenuItem;

// VAB mode menu items
typedef enum {
    VAB_MENU_NOTE,
    VAB_MENU_PROGRAM,
    VAB_MENU_REV_TYPE,
    VAB_MENU_REV_DEPTH,
    VAB_MENU_REV_DELAY,
    VAB_MENU_REV_FEEDBACK,
    VAB_MENU_PROGRAM_EDIT,
    VAB_MENU_VISUALIZER,
    VAB_MENU_ITEM_COUNT
} VabPlaybackMenuItem;

// Program editor menu items
typedef enum {
    PROG_MENU_PROGRAM_SEL,
    PROG_MENU_NUM_TONES,
    PROG_MENU_PROG_VOL,
    PROG_MENU_PROG_PAN,
    PROG_MENU_MASTER_VOL,
    PROG_MENU_MASTER_PAN,
    PROG_MENU_ITEM_COUNT
} ProgramEditMenuItem;

// Tone editor menu items
// TONE_MENU_PRIOR..TONE_MENU_PBMAX must stay contiguous and in the same order as the tone_fields[] table below
typedef enum {
    TONE_MENU_PROG,
    TONE_MENU_TONE_SEL,
    TONE_MENU_NOTE_SEL,
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

// Number of fields handled by the tone_fields[] table
#define TONE_FIELD_COUNT (TONE_MENU_PBMAX - TONE_MENU_PRIOR + 1)

// ADSR editor menu items
// ADSR_MENU_ATTACK_RATE..ADSR_MENU_RELEASE_EXP index adsr_fields[]
typedef enum {
    ADSR_MENU_ATTACK_RATE,
    ADSR_MENU_ATTACK_EXP,
    ADSR_MENU_DECAY_RATE,
    ADSR_MENU_SUSTAIN_LEVEL,
    ADSR_MENU_SUSTAIN_RATE,
    ADSR_MENU_SUSTAIN_SIGNED,
    ADSR_MENU_SUSTAIN_EXP,
    ADSR_MENU_RELEASE_RATE,
    ADSR_MENU_RELEASE_EXP,
    ADSR_MENU_SAVE,
    ADSR_MENU_CANCEL,
    ADSR_MENU_ITEM_COUNT
} AdsrEditMenuItem;

// ADSR parameters structure
typedef struct {
    // ADSR1 parameters
    int attack;              // 0-127
    int attackExponential;   // 0 or 1
    int decay;               // 0-15
    int sustainLevel;        // 0-15
    
    // ADSR2 parameters
    int sustain;             // 0-127 (sustain rate)
    int sustainSigned;       // 0 or 1
    int sustainExponential;  // 0 or 1
    int release;             // 0-31
    int releaseExponential;  // 0 or 1
} AdsrParams;

// Table-driven tone fields (u_char members of VagAtr)
// Indexed by (menu item - TONE_MENU_PRIOR)
typedef struct {
    const char* label;   // Shown in the tone editor
    u_char      off;     // Byte offset inside VagAtr
    u_char      max;     // Upper limit (MODE is a special case: 0=NORMAL, 4=REVERB)
} ToneField;

static const ToneField tone_fields[TONE_FIELD_COUNT] = {
    { "PRIORITY", offsetof(VagAtr, prior),  15  },
    { "MODE",     offsetof(VagAtr, mode),   4   },
    { "VOL",      offsetof(VagAtr, vol),    127 },
    { "PAN",      offsetof(VagAtr, pan),    127 },
    { "CENTER",   offsetof(VagAtr, center), 127 },
    { "SHIFT",    offsetof(VagAtr, shift),  127 },
    { "MIN",      offsetof(VagAtr, min),    127 },
    { "MAX",      offsetof(VagAtr, max),    127 },
    { "PBMIN",    offsetof(VagAtr, pbmin),  127 },
    { "PBMAX",    offsetof(VagAtr, pbmax),  127 }
};

// Table-driven ADSR fields (int members of AdsrParams)
// Indexed by ADSR menu item. max == 1 for toggles
typedef struct {
    const char* label;
    u_char      off;     // Byte offset inside AdsrParams
    u_char      max;
} AdsrField;

static const AdsrField adsr_fields[ADSR_MENU_SAVE] = {
    { "ATTACK RATE",   offsetof(AdsrParams, attack),             127 },
    { "ATTACK EXP",    offsetof(AdsrParams, attackExponential),  1   },
    { "DECAY RATE",    offsetof(AdsrParams, decay),              15  },
    { "SUSTAIN LEVEL", offsetof(AdsrParams, sustainLevel),       15  },
    { "SUSTAIN RATE",  offsetof(AdsrParams, sustain),            127 },
    { "SUSTAIN SIGN",  offsetof(AdsrParams, sustainSigned),      1   },
    { "SUSTAIN EXP",   offsetof(AdsrParams, sustainExponential), 1   },
    { "RELEASE RATE",  offsetof(AdsrParams, release),            31  },
    { "RELEASE EXP",   offsetof(AdsrParams, releaseExponential), 1   }
};

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

// File definitions are now in fileconfig.h
// MAX_SEQ_FILES and MAX_VH_FILES are defined there

// File lists - initialized from generated macros
// Extra slots for files loaded from the CD-ROM browser
#ifdef ENABLE_CD_BROWSER
#define CD_MAX_LOADED 16
#else
#define CD_MAX_LOADED 0
#endif

// The built-in files come first, CD-loaded files are appended
FileEntry seq_files[MAX_SEQ_FILES + CD_MAX_LOADED] = SEQ_FILES_INIT;

FileEntry vh_files[MAX_VH_FILES + CD_MAX_LOADED] = VH_FILES_INIT;

u_char* vb_files[MAX_VH_FILES + CD_MAX_LOADED] = VB_FILES_INIT;

// Total number of entries
int seq_count = MAX_SEQ_FILES;
int vh_count  = MAX_VH_FILES;

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

// Background image support (16-bit TIM, 320x240 split into two primitives)
#if HAS_BACKGROUND_IMAGE
int bg_state = 0;       // 0=default, 1=image+text, 2=image only
TIM_IMAGE bg_tim;       // TIM image structure
u_short bg_tpage_left;  // Texture page for left 256 pixels
u_short bg_tpage_right; // Texture page for right 64 pixels (if 320-wide)
u_short bg_clut;        // CLUT ID (for 8bpp, unused for 16bpp)
int bg_is_wide;         // 1 if image is wider than 256 pixels (needs dual draw)
#endif

short reverb_depth_left = 64;
short reverb_depth_right = 64;
short reverb_delay = 0;
short reverb_feedback = 0;
short reverb_type = 0;

// VAB mode specific variables
int vab_mode = 0;  // 0 = SEQ mode, 1 = VAB mode
short current_note = 60;  // Middle C (MIDI note 60)
short current_program = 0;  // Program number
short current_voice = -1;  // Voice channel ID for controller (Triangle) note (-1 = not playing)
int note_playing = 0;  // Is controller (Triangle) note playing

// MIDI polyphony pool, one slot per simultaneously held MIDI note
// Kept separate from the controller path so Triangle and MIDI work
#define MIDI_MAX_VOICES  24   // Polyphony limit; SPU has 24 hardware voices

typedef struct {
    short note;      // MIDI note number; -1 = slot is free
    short voice_id;  // SPU voice ID returned by SsUtKeyOn
    short program;   // Program used when the note was triggered (for KeyOff)
    short tone;      // Tone used when the note was triggered (for KeyOff)
} MidiVoice;

MidiVoice midi_voices[MIDI_MAX_VOICES];
int midi_poly_count = 0;  // Number of currently active MIDI voices

#ifdef ENABLE_DEBUG_MENU
// Display last voice triggered by playNote() or midiNoteOn()
static int debug_voice = -1;
// Save previous state
static UIState pre_debug_state = STATE_SEQ_SELECT;
#endif

// Visualizer per-voice volume cache
// vis_voice_map_vol : SpuGetVoiceVolume peak mapped 0-100, latched once at key-on
// vis_voice_was_on  : previous-frame key status
static int  vis_voice_map_vol[24];
static char vis_voice_was_on[24];

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

// ADSR parameter editing
int adsr_editor_active = 0;  // Which ADSR being edited: 1=ADSR1, 2=ADSR2
AdsrParams current_adsr;  // Current ADSR parameters
AdsrParams original_adsr;  // Original ADSR parameters (for change detection and cancel)


#define HOLD_THRESHOLD 30  // Hold detection (at 60Hz, 0.5 seconds = 30 frames)
#define HOLD_REPEAT_RATE 3  // Repeat every 3 frames when held
int hold_counter_left = 0;
int hold_counter_right = 0;
int hold_active_left = 0;
int hold_active_right = 0;
int hold_counter_up = 0;
int hold_counter_down = 0;
int hold_active_up = 0;
int hold_active_down = 0;

// Reworking of stuff:
// Input and draw helper macros
// LIST_ROWS: file list rows shown at once before the list scrolls
#define LIST_ROWS       10
// PRESSED(m): Frame at which button was pressed
#define PRESSED(m)      (((pad) & (m)) && !((oldpad) & (m)))
// CUR(i): Selection cursor glyph. '>' when i is selected, ' ' when not selected
#define CUR(i)          ((menu_cursor == (i)) ? '>' : ' ')
// SEL(i, s): string s when selected, "" when not selected
#define SEL(i, s)       ((menu_cursor == (i)) ? (s) : "")
// CHG(fn, i): " !" flag to report modified values
#define CHG(fn, i)      (fn(i) ? " !" : "")

// PicoDev PIO cartridge defs. UART channel (A1=1)
#define PIO_BASE             0x1F000000
#define PIO_UART_DATA        (*(volatile u_char*)(PIO_BASE + 2))
#define PIO_UART_STATUS      (*(volatile u_char*)(PIO_BASE + 3))
#define PIO_ST_UART_RXAVAIL  0x10   // bit 4: a byte is waiting
#define PIO_ST_VALID_MASK    0xCC   // bits 7,6,3,2
#define PIO_ST_VALID_VALUE   0x88   // configured=1, suspend=0 (firmware constants)
#define MIDI_MAX_BYTES_PER_FRAME 128

static int cartMidiAvailable(void) { return (PIO_UART_STATUS & PIO_ST_UART_RXAVAIL) != 0; }
static u_char cartMidiRead(void)   { return PIO_UART_DATA; }

// Midi message duration in frames
#define MIDI_DISPLAY_FRAMES  180

// MIDI running-status parser (main-loop only, no volatile needed)
static u_char midi_run_status = 0;
static u_char midi_pbuf[3];
static int    midi_pbuf_pos   = 0;
static int    midi_pbuf_exp   = 0;

// Display state
int  serial_midi_enabled = 0;
char midi_last_msg[48]   = "";
int  midi_msg_timer      = 0;

// Current MIDI pitch bend value (0-127, 64 = center/no bend)
// Stored so new notes triggered while bent play with bend already applied
short midi_pitch_bend = 64;

// NRPN accumulator, CC99(MSB)+CC98(LSB) before CC06 fires
// 0xFF = not yet received
static u_char midi_nrpn_msb = 0xFF;
static u_char midi_nrpn_lsb = 0xFF;


// Function prototypes
void initGraph(void);
void display(void);
void initSound(void);
void processInput(void);
#if HAS_BACKGROUND_IMAGE
void drawBackground(void);
#endif
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

// ADSR editor functions
void decodeADSR(u_short adsr1, u_short adsr2, AdsrParams* params);
void encodeADSR(AdsrParams* params, u_short* adsr1, u_short* adsr2);
void enterAdsrEdit(int which_adsr);
void exitAdsrEdit(int save);
void adjustAdsrValue(int direction, int amount);
void toggleAdsrValue(void);
void drawAdsrEdit(void);
void drawVisualizer(void);
int isAdsrValueChanged(int menu_item);

#ifdef ENABLE_DEBUG_MENU
void drawDebug(void);
#endif

// New shared helpers for VAB handling, input, table-driven editing
static void closeVab(void);
static int  openVab(void);
static void selectVhFile(int idx);
static void addClamped(u_char* p, int delta, int max);
static void toneEditProgramStep(int delta);
static void adjustToneField(int item, int direction, int amount);
static void toggleToneField(int item);
static int* adsrFieldPtr(AdsrParams* p, int item);
static void applyAdsrPreview(void);
static int  repeatFire(int mask, int* cnt, int* active);
static void navList(int count);
static void navMenu(int count, int skip_item);
static void editInput(void (*adjust)(int, int), void (*toggle)(void));
static void handleTriangleNote(void);
static int  clampi(int v, int lo, int hi);
static void setReverbType(int v);
static void setReverbDepth(int v);
static void setReverbDelay(int v);
static void setReverbFeedback(int v);
static void adjustReverbParam(int param, int direction, int amount);
static void toggleReverbParam(int param);
static void drawReverbRows(int first_item);
static int  inEditorState(void);
static void activeProgTone(short* prog, short* tone);
static void releaseMidiSlot(int i);
static void listWindow(int count, int cur, int rows, int* top, int* end);
static void drawFileList(const FileEntry* files, int count);
#ifdef ENABLE_CD_BROWSER
void drawCdBrowser(void);
#endif

// Serial MIDI functions
static int  midiMsgLength(u_char status);
void startSerialMidi(void);
void stopSerialMidi(void);
void processMidi(void);
void initMidiVoices(void);
void midiNoteOn(u_char note, u_char velocity);
void midiNoteOff(u_char note);
void midiAllNotesOff(void);

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
    setRGB0(&draw[0], 111, 0, 66);
    setRGB0(&draw[1], 111, 0, 66);
    draw[0].isbg = 1;
    draw[1].isbg = 1;
    
    PutDispEnv(&disp[db]);
    PutDrawEnv(&draw[db]);
    
    FntLoad(960, 0);
    FntOpen(8, 8, 304, 224, 0, 2048);

// Load TIM background image (supports up to 320x240 16-bit)
#if HAS_BACKGROUND_IMAGE
    OpenTIM((u_long*)BG_IMAGE_DATA);
    ReadTIM(&bg_tim);
    
    // Load pixel data to VRAM
    LoadImage(bg_tim.prect, (u_long*)bg_tim.paddr);
    DrawSync(0);
    
    // Load CLUT if present (8bpp has CLUT, bit 3 set)
    if (bg_tim.mode & 0x8) {
        LoadImage(bg_tim.crect, (u_long*)bg_tim.caddr);
        DrawSync(0);
        bg_clut = GetClut(bg_tim.crect->x, bg_tim.crect->y);
    }
    
    // Determine if image is wide (> 256 pixels)
    // For 16-bit: prect->w is pixel width
    // For 8-bit: prect->w * 2 is pixel width
    int pixel_width = bg_tim.prect->w;
    if ((bg_tim.mode & 0x3) == 1) {  // 8-bit mode
        pixel_width *= 2;
    }
    
    bg_is_wide = (pixel_width > 256) ? 1 : 0;
    
    // Get texture page ID for left portion (first 256 pixels)
    bg_tpage_left = GetTPage(bg_tim.mode & 0x3, 0, 
                             bg_tim.prect->x, bg_tim.prect->y);
    
    // If wide, get texture page for right portion
    if (bg_is_wide) {
        // For 16-bit: second page starts at X + 256
        // For 8-bit: second page starts at X + 128 (VRAM words)
        int offset = ((bg_tim.mode & 0x3) == 2) ? 256 : 128;
        bg_tpage_right = GetTPage(bg_tim.mode & 0x3, 0,
                                  bg_tim.prect->x + offset, bg_tim.prect->y);
    }
#endif
}

void display(void)
{
#if HAS_BACKGROUND_IMAGE
    // Disable background clear when showing image, enable when not
    if (bg_state > 0) {
        draw[db].isbg = 0;  // Disable solid color clear
    } else {
        draw[db].isbg = 1;  // Enable solid color clear
    }
#endif
    
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
    
    // SS_TICK60 causes sequences to play too fast, SS_TICK240 seems to have tempo issues
	// SFG recommended an arbitrary tickrate of 97
    SsSetTickMode(97);
    
    // Set master volume
    SsSetMVol(127, 127);
    
    // Start sound system
    SsStart();
    
    // Initialise MIDI polyphony pool
    initMidiVoices();
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

// VAB open/close helpers
static void closeVab(void)
{
    if (current_audio.vab_id >= 0) {
        SsVabClose(current_audio.vab_id);
        current_audio.vab_id = -1;
    }
}

// Open the VAB for the currently selected VH/VB pair and transfer the body to SPU ram
// Does nothing (returns 1) if a VAB is already loaded, so edits made in the program editor are preserved.
// Returns 1 on success, 0 if failed
static int openVab(void)
{
    if (current_audio.vab_id >= 0) return 1;
    if (current_audio.vh_data == NULL) return 0;

    // Open header
    current_audio.vab_id = SsVabOpenHead(current_audio.vh_data, -1);
    if (current_audio.vab_id < 0) {
        FntPrint("Failed to open VAB header!\n");
        return 0;
    }

    // Transfer body
    if (SsVabTransBody(current_audio.vb_data, current_audio.vab_id) != current_audio.vab_id) {
        FntPrint("Failed to transfer VAB body!\n");
        SsVabClose(current_audio.vab_id);
        current_audio.vab_id = -1;
        return 0;
    }

    // Wait for transfer to complete
    SsVabTransCompleted(SS_WAIT_COMPLETED);
    return 1;
}

// Point current_audio to VAB index and read header counts
static void selectVhFile(int idx)
{
    VabHdr* vab_hdr;

    current_audio.vh_data = vh_files[idx].data;
    current_audio.vb_data = vb_files[idx];
    current_audio.vh_size = vh_files[idx].size;
    sprintf(current_audio.vh_name, "%s", vh_files[idx].name);

    // Read VAB header (VabHdr struct)
    vab_hdr = (VabHdr*)current_audio.vh_data;
    current_audio.num_programs = vab_hdr->ps;  // Note: 'ps' not 'programs'
    current_audio.num_tones = vab_hdr->ts;     // Note: 'ts' not 'tones'
}

void playSequence(void)
{
    if (is_playing) {
        stopSequence();
    }
    
    // Reset tempo to default
    current_tempo = 120;
    tempo_changed = 0;  // Mark as unchanged
    
    // Load the VAB if not already loaded. Reusing an already loaded VAB preserves the changes made in program editor
    if (!openVab()) return;
    
    // Open SEQ (cast to unsigned long* as required by API)
    current_audio.seq_id = SsSeqOpen((unsigned long*)current_audio.seq_data, current_audio.vab_id);
    if (current_audio.seq_id < 0) {
        FntPrint("Failed to open sequence!\n");
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
    
    // Keep VAB loaded so program editing works when stopped
    // VAB will be closed when changing files or in cleanup
    
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

// Reverb param helpers
// The two playback menus list the reverb rows in the same order, menu items are mapped to a parameter via offset from the first reverb row
typedef enum {
    REV_PARAM_TYPE,
    REV_PARAM_DEPTH,
    REV_PARAM_DELAY,
    REV_PARAM_FEEDBACK
} ReverbParam;

// Array size becomes -1 (and compiler fails) if reverb rows and param order is not correct
typedef char reverb_menu_order_check[
    ((MENU_REV_DEPTH     - MENU_REV_TYPE) == REV_PARAM_DEPTH    &&
     (MENU_REV_DELAY     - MENU_REV_TYPE) == REV_PARAM_DELAY    &&
     (MENU_REV_FEEDBACK  - MENU_REV_TYPE) == REV_PARAM_FEEDBACK &&
     (VAB_MENU_REV_DEPTH    - VAB_MENU_REV_TYPE) == REV_PARAM_DEPTH    &&
     (VAB_MENU_REV_DELAY    - VAB_MENU_REV_TYPE) == REV_PARAM_DELAY    &&
     (VAB_MENU_REV_FEEDBACK - VAB_MENU_REV_TYPE) == REV_PARAM_FEEDBACK) ? 1 : -1];

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Each setter clamps, stores and applies one reverb value. Also used by the NRPN handler in processMidi()
static void setReverbType(int v)
{
    reverb_type = (short)clampi(v, 0, 9);
    SsUtSetReverbType(reverb_type);
    // Reapply reverb depth when changing type
    SsUtSetReverbDepth(reverb_depth_left, reverb_depth_right);
}

static void setReverbDepth(int v)
{
    reverb_depth_left = reverb_depth_right = (short)clampi(v, 0, 127);
    SsUtSetReverbDepth(reverb_depth_left, reverb_depth_right);
}

static void setReverbDelay(int v)
{
    reverb_delay = (short)clampi(v, 0, 127);
    SsUtSetReverbDelay(reverb_delay);
}

static void setReverbFeedback(int v)
{
    reverb_feedback = (short)clampi(v, 0, 127);
    SsUtSetReverbFeedback(reverb_feedback);
}

// direction: -1/+1, amount: step size (TYPE always steps by 1)
static void adjustReverbParam(int param, int direction, int amount)
{
    switch (param) {
        case REV_PARAM_TYPE:     setReverbType(reverb_type + direction);                   break;
        case REV_PARAM_DEPTH:    setReverbDepth(reverb_depth_left + direction * amount);   break;
        case REV_PARAM_DELAY:    setReverbDelay(reverb_delay + direction * amount);        break;
        case REV_PARAM_FEEDBACK: setReverbFeedback(reverb_feedback + direction * amount);  break;
    }
}

// Toggle min/max for reverb params
static void toggleReverbParam(int param)
{
    switch (param) {
        case REV_PARAM_TYPE:     setReverbType(reverb_type == 9 ? 0 : 9);                   break;
        case REV_PARAM_DEPTH:    setReverbDepth(reverb_depth_left == 127 ? 0 : 127);        break;
        case REV_PARAM_DELAY:    setReverbDelay(reverb_delay == 127 ? 0 : 127);             break;
        case REV_PARAM_FEEDBACK: setReverbFeedback(reverb_feedback == 127 ? 0 : 127);       break;
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
					//SsSeqSetAccelerando(seq_access_num, tempo, v_time);
                    SsSeqSetAccelerando(current_audio.seq_id, current_tempo, 120);
                } else {
                    SsSeqSetRitardando(current_audio.seq_id, current_tempo, 120);
                }
            }
            break;
            
        default:
            if (menu_cursor >= MENU_REV_TYPE && menu_cursor <= MENU_REV_FEEDBACK) {
                adjustReverbParam(menu_cursor - MENU_REV_TYPE, direction, amount);
            }
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
            
        default:
            if (menu_cursor >= MENU_REV_TYPE && menu_cursor <= MENU_REV_FEEDBACK) {
                toggleReverbParam(menu_cursor - MENU_REV_TYPE);
            }
            break;
    }
}

// True while any editor is being displayed
static int inEditorState(void)
{
    return current_state == STATE_PROGRAM_EDIT ||
           current_state == STATE_TONE_EDIT    ||
           current_state == STATE_ADSR_EDIT;
}

// Program/tone that notes should be played with: active edit (tone only in the Tone and ADSR editors) or otherwise current_program
static void activeProgTone(short* prog, short* tone)
{
    if (inEditorState()) {
        *prog = edit_program;
        *tone = (current_state == STATE_TONE_EDIT ||
                 current_state == STATE_ADSR_EDIT) ? edit_tone : 0;
    } else {
        *prog = current_program;
        *tone = 0;
    }
}

void playNote(void)
{
    short program_to_use;
    short tone_to_use;
    
    // Use active tone/program if in editor, otherwise use current_program
    activeProgTone(&program_to_use, &tone_to_use);
    
    // Stop any currently playing note
    if (note_playing && current_voice >= 0) {
        stopNote();
    }
    
    // Play the note using the appropriate program, tone, and note value
    // SsUtKeyOn(vab_id, program, tone, note, fine, vol_left, vol_right)
    current_voice = SsUtKeyOn(current_audio.vab_id, program_to_use, tone_to_use, current_note, 0, 127, 127);

    if (current_voice >= 0) {
        note_playing = 1;
#ifdef ENABLE_DEBUG_MENU
        debug_voice = current_voice;
#endif
    }
}

void stopNote(void)
{
    short program_to_use;
    short tone_to_use;
    
    // Use active tone/program if in editor, otherwise use current_program
    activeProgTone(&program_to_use, &tone_to_use);
    
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
            
        default:
            if (menu_cursor >= VAB_MENU_REV_TYPE && menu_cursor <= VAB_MENU_REV_FEEDBACK) {
                adjustReverbParam(menu_cursor - VAB_MENU_REV_TYPE, direction, amount);
            }
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
            
        default:
            if (menu_cursor >= VAB_MENU_REV_TYPE && menu_cursor <= VAB_MENU_REV_FEEDBACK) {
                toggleReverbParam(menu_cursor - VAB_MENU_REV_TYPE);
            }
            break;
    }
}

void enterProgramEdit(void)
{
    // Save return state
    return_state = current_state;
    
    // Ensure VAB is loaded
	// if not already loaded, load it now
    openVab();
    
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

// Table-driven editing helpers
// *p += delta, clamped to 0..max (uses int so u_char can't wrap)
static void addClamped(u_char* p, int delta, int max)
{
    *p = (u_char)clampi((int)*p + delta, 0, max);
}

// Pointer to the u_char program-editor value of each menu item (NULL if none)
static u_char* programFieldPtr(int menu_item)
{
    switch (menu_item) {
        case PROG_MENU_PROG_VOL:    return &current_prog_atr.mvol;
        case PROG_MENU_PROG_PAN:    return &current_prog_atr.mpan;
        case PROG_MENU_MASTER_VOL:  return &vab_master_vol;
        case PROG_MENU_MASTER_PAN:  return &vab_master_pan;
        default:                    return NULL;
    }
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
    int idx;

    // ADSR words are u_short, everything else is in tone_fields[]
    if (menu_item == TONE_MENU_ADSR1)
        return current_vag_atr.adsr1 != original_vag_atr.adsr1;
    if (menu_item == TONE_MENU_ADSR2)
        return current_vag_atr.adsr2 != original_vag_atr.adsr2;

    idx = menu_item - TONE_MENU_PRIOR;
    if (idx < 0 || idx >= TONE_FIELD_COUNT) return 0;

    return *((u_char*)&current_vag_atr  + tone_fields[idx].off) !=
           *((u_char*)&original_vag_atr + tone_fields[idx].off);
}

void adjustProgramEditValue(int direction, int amount)
{
    u_char* p;

    if (menu_cursor == PROG_MENU_PROGRAM_SEL) {
        edit_program += direction;
        if (edit_program < 0) edit_program = 0;
        if (edit_program >= current_audio.num_programs) {
            edit_program = current_audio.num_programs - 1;
        }
        loadProgramData();
        saveProgramData();  // Apply previous changes
        return;
    }

    p = programFieldPtr(menu_cursor);
    if (p) {
        addClamped(p, direction * amount, 127);
        saveProgramData();
    }
}

void toggleProgramEditMinMax(void)
{
    u_char* p;

    if (menu_cursor == PROG_MENU_PROGRAM_SEL) {
        edit_program = (edit_program == current_audio.num_programs - 1) ? 0 : current_audio.num_programs - 1;
        loadProgramData();
        saveProgramData();
        return;
    }

    p = programFieldPtr(menu_cursor);
    if (p) {
        *p = (*p == 127) ? 0 : 127;
        saveProgramData();
    }
}

// Select prev/next program
// Saves the current tone, then resets to tone 0 of the newly selected program
static void toneEditProgramStep(int delta)
{
    saveToneData();

    edit_program += delta;
    if (edit_program < 0) edit_program = 0;
    if (edit_program >= current_audio.num_programs) {
        edit_program = current_audio.num_programs - 1;
    }

    loadProgramData();
    edit_tone = 0;
    loadToneData();
}

// Adjust the table-driven tone fields (TONE_MENU_PRIOR..TONE_MENU_PBMAX)
static void adjustToneField(int item, int direction, int amount)
{
    const ToneField* f = &tone_fields[item - TONE_MENU_PRIOR];
    u_char* p = (u_char*)&current_vag_atr + f->off;

    if (item == TONE_MENU_MODE) {
        // Toggle between 0 and 4 for Reverb
        if (direction != 0) {
            *p = (*p == 0) ? 4 : 0;
        }
    } else {
        addClamped(p, direction * amount, f->max);
    }
    saveToneData();
}

// Toggle min/max
static void toggleToneField(int item)
{
    const ToneField* f = &tone_fields[item - TONE_MENU_PRIOR];
    u_char* p = (u_char*)&current_vag_atr + f->off;

    if (item == TONE_MENU_MODE) {
        *p = (*p == 0) ? 4 : 0;
    } else {
        *p = (*p == f->max) ? 0 : f->max;
    }
    saveToneData();
}

void adjustToneEditValue(int direction, int amount)
{
    int was_playing = note_playing;
    
    switch (menu_cursor) {
        case TONE_MENU_PROG:
            toneEditProgramStep(direction);
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
            
        case TONE_MENU_ADSR1:
        case TONE_MENU_ADSR2:
            // ADSR values are edited with X button, not directional controls
            break;

        default:
            if (menu_cursor >= TONE_MENU_PRIOR && menu_cursor <= TONE_MENU_PBMAX) {
                adjustToneField(menu_cursor, direction, amount);
            }
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
            
        case TONE_MENU_ADSR1:
        case TONE_MENU_ADSR2:
            // ADSR values edited with X button
            break;

        default:
            if (menu_cursor >= TONE_MENU_PRIOR && menu_cursor <= TONE_MENU_PBMAX) {
                toggleToneField(menu_cursor);
            }
            break;
    }
}

// ADSR Editor Functions

void decodeADSR(u_short adsr1, u_short adsr2, AdsrParams* params)
{
    // Decode ADSR1 (16 bits)
    // Bit 15: exponential flag
    // Bits 14-8: attack stored (7 bits) = (127 - attack)
    // Bits 7-4: decay stored (4 bits) = (15 - decay)
    // Bits 3-0: sustain level (4 bits)
    
    params->attackExponential = (adsr1 & 0x8000) != 0;
    int stored_attack = (adsr1 >> 8) & 0x7F;
    params->attack = 127 - stored_attack;
    int stored_decay = (adsr1 >> 4) & 0x0F;
    params->decay = 15 - stored_decay;
    params->sustainLevel = adsr1 & 0x0F;
    
    // Decode ADSR2 (16 bits)
    // Bit 15: sustain exponential flag
    // Bit 14: signed flag
    // Bits 13-6: sustain rate stored (7 bits) = (127 - sustain)
    // Bit 5: release exponential flag
    // Bits 4-0: release stored (5 bits) = (31 - release)
    
    params->sustainExponential = (adsr2 & 0x8000) != 0;
    params->sustainSigned = (adsr2 & 0x4000) != 0;
    int stored_sustain = (adsr2 >> 6) & 0x7F;
    params->sustain = 127 - stored_sustain;
    params->releaseExponential = (adsr2 & 0x0020) != 0;
    int stored_release = adsr2 & 0x1F;
    params->release = 31 - stored_release;
}

void encodeADSR(AdsrParams* params, u_short* adsr1, u_short* adsr2)
{
    // Encode ADSR1
    int stored_attack = (127 - params->attack) & 0x7F;
    int stored_decay = (15 - params->decay) & 0x0F;
    int sustain_level = params->sustainLevel & 0x0F;
    
    *adsr1 = (params->attackExponential ? 0x8000 : 0) |
             (stored_attack << 8) |
             (stored_decay << 4) |
             sustain_level;
    
    // Encode ADSR2
    int stored_sustain = (127 - params->sustain) & 0x7F;
    int stored_release = (31 - params->release) & 0x1F;
    
    *adsr2 = (params->sustainExponential ? 0x8000 : 0) |
             (params->sustainSigned ? 0x4000 : 0) |
             (stored_sustain << 6) |
             (params->releaseExponential ? 0x0020 : 0) |
             stored_release;
}

void enterAdsrEdit(int which_adsr)
{
    adsr_editor_active = which_adsr;
    
    // Decode current ADSR values
    decodeADSR(current_vag_atr.adsr1, current_vag_atr.adsr2, &current_adsr);
    
    // Save original for comparison and cancel
    original_adsr = current_adsr;
    
    // Enter ADSR edit state
    current_state = STATE_ADSR_EDIT;
    menu_cursor = 0;
}

void exitAdsrEdit(int save)
{
    if (save) {
        // Encode and save the parameters
        encodeADSR(&current_adsr, &current_vag_atr.adsr1, &current_vag_atr.adsr2);
        saveToneData();
    } else {
        // Restore original values
        current_adsr = original_adsr;
    }
    
    // Return to tone edit
    current_state = STATE_TONE_EDIT;
    
    // Set cursor to the ADSR that was being edited
    if (adsr_editor_active == 1) {
        menu_cursor = TONE_MENU_ADSR1;
    } else {
        menu_cursor = TONE_MENU_ADSR2;
    }
}

// Pointer to the int member in AdsrParams for each ADSR menu item (0..ADSR_MENU_SAVE-1)
static int* adsrFieldPtr(AdsrParams* p, int item)
{
    return (int*)((u_char*)p + adsr_fields[item].off);
}

// Encode current_adsr and apply to tone immediately for preview
static void applyAdsrPreview(void)
{
    encodeADSR(&current_adsr, &current_vag_atr.adsr1, &current_vag_atr.adsr2);
    saveToneData();
}

void adjustAdsrValue(int direction, int amount)
{
    const AdsrField* f;
    int* p;
    int temp_val;

    // SAVE / CANCEL have no adjust
    if (menu_cursor < 0 || menu_cursor >= ADSR_MENU_SAVE) return;

    f = &adsr_fields[menu_cursor];
    p = adsrFieldPtr(&current_adsr, menu_cursor);

    if (f->max == 1) {
        // ON/OFF flag
        *p = !*p;
    } else {
        temp_val = *p + (direction * amount);
        if (temp_val < 0) temp_val = 0;
        if (temp_val > f->max) temp_val = f->max;
        *p = temp_val;
    }

    // Apply changes immediately
    applyAdsrPreview();
}

void toggleAdsrValue(void)
{
    const AdsrField* f;
    int* p;

    if (menu_cursor < 0 || menu_cursor >= ADSR_MENU_SAVE) return;

    f = &adsr_fields[menu_cursor];
    p = adsrFieldPtr(&current_adsr, menu_cursor);

    if (f->max == 1) {
        *p = !*p;
    } else {
        *p = (*p == f->max) ? 0 : f->max;
    }

    // Apply changes immediately
    applyAdsrPreview();
}

int isAdsrValueChanged(int menu_item)
{
    if (menu_item < 0 || menu_item >= ADSR_MENU_SAVE) return 0;
    return *adsrFieldPtr(&current_adsr, menu_item) !=
           *adsrFieldPtr(&original_adsr, menu_item);
}

// Serial MIDI experiment :)
// Returns the expected byte length for a given MIDI status byte
// System real-time messages (0xF8-0xFF) are not relevant here
static int midiMsgLength(u_char status)
{
    u_char type = status & 0xF0;
    // 2-byte messages: Program Change, Channel Pressure
    if (type == 0xC0 || type == 0xD0) return 2;
    // 3-byte messages: Note Off, Note On, Poly AT, CC, Pitch Bend
    if (type == 0x80 || type == 0x90 ||
        type == 0xA0 || type == 0xB0 || type == 0xE0) return 3;
    // SysEx / System messages: treat as 1-byte (skip/ignore)
    return 1;
}

// Polyphony Voice Pool
// Reset every slot to "free", called once from initSound()
void initMidiVoices(void)
{
    int i;
    for (i = 0; i < MIDI_MAX_VOICES; i++) {
        midi_voices[i].note     = -1;
        midi_voices[i].voice_id = -1;
        midi_voices[i].program  = 0;
        midi_voices[i].tone     = 0;
    }
    midi_poly_count = 0;

    // Reset visualizer volume cache
    for (i = 0; i < 24; i++) {
        vis_voice_map_vol[i] = 0;
        vis_voice_was_on[i]  = SPU_OFF;
    }
}

// Key off the voice in pool slot i and remove the slot, shifting later slots down
static void releaseMidiSlot(int i)
{
    int j;
    SsUtKeyOff(midi_voices[i].voice_id,
               current_audio.vab_id,
               midi_voices[i].program,
               midi_voices[i].tone,
               midi_voices[i].note);
    for (j = i; j < MIDI_MAX_VOICES - 1; j++) {
        midi_voices[j] = midi_voices[j + 1];
    }
    midi_voices[MIDI_MAX_VOICES - 1].note     = -1;
    midi_voices[MIDI_MAX_VOICES - 1].voice_id = -1;
    if (midi_poly_count > 0) midi_poly_count--;
}

// Trigger a note from a MIDI NoteOn message
// velocity is mapped 1-127 → 1-127 straight onto vol_left / vol_right
// If all MIDI_MAX_VOICES slots are already occupied the oldest voice
// (slot 0) is stolen, all remaining slots shift down by one
void midiNoteOn(u_char note, u_char velocity)
{
    int i;
    short program_to_use;
    short tone_to_use;
    short new_voice;

    // Determine program/tone to use, respects editor state same as playNote
    activeProgTone(&program_to_use, &tone_to_use);

    // If this exact note is already playing retrigger it
    for (i = 0; i < MIDI_MAX_VOICES; i++) {
        if (midi_voices[i].note == (short)note) {
            releaseMidiSlot(i);
            break;
        }
    }

    // If all slots are full, steal the oldest (slot 0)
    if (midi_poly_count >= MIDI_MAX_VOICES) {
        releaseMidiSlot(0);
    }

    // Fire the new note with MIDI velocity as SPU volume
    // SsUtKeyOn(vab_id, program, tone, note, fine, vol_left, vol_right)
    new_voice = SsUtKeyOn(current_audio.vab_id,
                          program_to_use, tone_to_use,
                          (short)note, 0,
                          (short)velocity, (short)velocity);

    if (new_voice >= 0) {
        // Store in the next free slot (guaranteed free after the steal above)
        midi_voices[midi_poly_count].note     = (short)note;
        midi_voices[midi_poly_count].voice_id = new_voice;
        midi_voices[midi_poly_count].program  = program_to_use;
        midi_voices[midi_poly_count].tone     = tone_to_use;
        midi_poly_count++;

        // Apply any pitch bend that arrived before this note was triggered
        // SsUtPitchBend(voice, vabId, prog, note, pbend)
		// 64 = center (no bend)
        if (midi_pitch_bend != 64) {
            SsUtPitchBend(new_voice, current_audio.vab_id,
                          program_to_use, (short)note, midi_pitch_bend);
        }

        // Keep current_note in sync so the UI displays the last triggered note
        current_note = (short)note;
        // note_playing reflects whether ANY voice is active (MIDI or controller)
        note_playing = 1;
#ifdef ENABLE_DEBUG_MENU
        debug_voice = new_voice;
#endif
    }
}

// Release a specific MIDI note by number
// Searches the pool; does nothing if the note is not found
void midiNoteOff(u_char note)
{
    int i;
    for (i = 0; i < MIDI_MAX_VOICES; i++) {
        if (midi_voices[i].note == (short)note) {
            releaseMidiSlot(i);
            break;
        }
    }
    // Update note_playing, remain true if the controller note or other MIDI voices are active
    if (midi_poly_count == 0 && !note_playing) {
        note_playing = 0;
    } else if (midi_poly_count == 0 && current_voice < 0) {
        note_playing = 0;
    }
}

// Silence every active MIDI voice, called when Serial MIDI is stopped
void midiAllNotesOff(void)
{
    int i;
    for (i = 0; i < MIDI_MAX_VOICES; i++) {
        if (midi_voices[i].note >= 0 && midi_voices[i].voice_id >= 0) {
            SsUtKeyOff(midi_voices[i].voice_id,
                       current_audio.vab_id,
                       midi_voices[i].program,
                       midi_voices[i].tone,
                       midi_voices[i].note);
        }
        midi_voices[i].note     = -1;
        midi_voices[i].voice_id = -1;
    }
    midi_poly_count = 0;
    // Only clear note_playing if the controller note is also silent
    if (current_voice < 0) {
        note_playing = 0;
    }
}

// Enable MIDI input from the PicoDev cartridge UART, drains stale bytes first
// If serial MIDI is already running, calling this again toggles it off
void startSerialMidi(void)
{
    int guard;
    u_char st;

    if (serial_midi_enabled) { stopSerialMidi(); return; }

    // Sanity check: the cart always reports configured=1, suspend=0
    st = PIO_UART_STATUS;
    if ((st & PIO_ST_VALID_MASK) != PIO_ST_VALID_VALUE) {
        sprintf(midi_last_msg, "No PIO cart? ST=%02X", st);
        midi_msg_timer = MIDI_DISPLAY_FRAMES;
        return;
    }

    // The cart buffers up to 4 KB while MIDI is off
    // Clear the buffer so stale bytes don't fire on enable.
    for (guard = 0; guard < 6000 && cartMidiAvailable(); guard++)
        (void)PIO_UART_DATA;

    midi_run_status = 0;
    midi_pbuf_pos   = 0;
    midi_pbuf_exp   = 0;

    serial_midi_enabled = 1;
    sprintf(midi_last_msg, "PIO UART ready");
    midi_msg_timer = MIDI_DISPLAY_FRAMES;
}

void stopSerialMidi(void)
{
    if (!serial_midi_enabled) return;
    midiAllNotesOff();
    serial_midi_enabled = 0;
    midi_pitch_bend = 64;
    midi_msg_timer = 0;
    midi_last_msg[0] = '\0';
}

// Apply a single NRPN ADSR attribute (nrpn_lsb 4-12) to one tone
// Decode existing ADSR, modify the named field, re-encode and save via SsUtSetVagAtr
// keeps the ADSR/Tone editor in sync
static void applyNrpnAdsr(short prog, int tone_num, u_char attr, u_char value)
{
    VagAtr va;
    AdsrParams params;
    int temp_val;

    if (SsUtGetVagAtr(current_audio.vab_id, prog, (short)tone_num, &va) != 0)
        return;

    decodeADSR(va.adsr1, va.adsr2, &params);

    switch (attr) {
        case 4:  // Attack rate   0-127
            temp_val = (int)value;
            if (temp_val > 127) temp_val = 127;
            params.attack = temp_val;
            break;
        case 5:  // Attack exp    0=off, >0=on
            params.attackExponential = (value > 0) ? 1 : 0;
            break;
        case 6:  // Decay rate    0-15
            temp_val = (int)value;
            if (temp_val > 15) temp_val = 15;
            params.decay = temp_val;
            break;
        case 7:  // Sustain level 0-15
            temp_val = (int)value;
            if (temp_val > 15) temp_val = 15;
            params.sustainLevel = temp_val;
            break;
        case 8:  // Sustain rate  0-127
            temp_val = (int)value;
            if (temp_val > 127) temp_val = 127;
            params.sustain = temp_val;
            break;
        case 9:  // Sustain exp   0=off, >0=on 
            params.sustainExponential = (value > 0) ? 1 : 0;
            break;
        case 10: // Release rate  0-31
            temp_val = (int)value;
            if (temp_val > 31) temp_val = 31;
            params.release = temp_val;
            break;
        case 11: // Release exp   0=off, >0=on
            params.releaseExponential = (value > 0) ? 1 : 0;
            break;
        case 12: // Sustain sign  0-64=+  65-127=-
            params.sustainSigned = (value > 64) ? 1 : 0;
            break;
        default:
            return;
    }

    encodeADSR(&params, &va.adsr1, &va.adsr2);
    SsUtSetVagAtr(current_audio.vab_id, prog, (short)tone_num, &va);

    // Keep the Tone and ADSR editor structs in sync if this tone is open
    if ((current_state == STATE_TONE_EDIT || current_state == STATE_ADSR_EDIT) &&
        edit_program == (int)prog && edit_tone == tone_num) {
        current_vag_atr.adsr1 = va.adsr1;
        current_vag_atr.adsr2 = va.adsr2;
        if (current_state == STATE_ADSR_EDIT) {
            decodeADSR(va.adsr1, va.adsr2, &current_adsr);
        }
    }
}

// Process all bytes currently waiting in the cartridge UART
// Call once per frame from processInput() before handling pad input
void processMidi(void)
{
    if (!serial_midi_enabled) return;

    // Drain the cartridge UART per frame bound
	int budget = MIDI_MAX_BYTES_PER_FRAME;
	while (budget-- > 0 && cartMidiAvailable()) {
        u_char b = cartMidiRead();

        if (b & 0x80) {
            // Status byte
            // Ignore real-time messages (clock 0xF8, active sense 0xFE, etc)
            if (b == 0xF8 || b == 0xFE || b == 0xFF) continue;
            // Ignore other system messages (SysEx start, song position, etc)
            if (b >= 0xF0) { midi_pbuf_pos = 0; continue; }

            // Regular channel message, start a new message
            midi_run_status  = b;
            midi_pbuf[0]     = b;
            midi_pbuf_pos    = 1;
            midi_pbuf_exp    = midiMsgLength(b);
        } else {
            // Data byte
            // If no current message in progress, apply running status
            if (midi_pbuf_pos == 0 && midi_run_status != 0) {
                midi_pbuf[0]  = midi_run_status;
                midi_pbuf_pos = 1;
                midi_pbuf_exp = midiMsgLength(midi_run_status);
            }
            if (midi_pbuf_pos > 0 && midi_pbuf_pos < 3) {
                midi_pbuf[midi_pbuf_pos++] = b;
            }
        }

        // Complete message?
        if (midi_pbuf_pos > 0 && midi_pbuf_pos >= midi_pbuf_exp) {
            u_char msg_type = midi_pbuf[0] & 0xF0;
            u_char channel  = (midi_pbuf[0] & 0x0F) + 1;   // 1-based for display
            u_char d1 = (midi_pbuf_exp > 1) ? midi_pbuf[1] : 0;
            u_char d2 = (midi_pbuf_exp > 2) ? midi_pbuf[2] : 0;

            switch (msg_type) {

                case 0x90:  // Note On
                    if (d2 > 0) {
                        // Velocity > 0: genuine Note On
                        midiNoteOn(d1, d2);
                        sprintf(midi_last_msg, "NoteOn  N%3d V%3d CH%2d", d1, d2, channel);
                    } else {
                        // Velocity == 0 is treated as Note Off
                        midiNoteOff(d1);
                        sprintf(midi_last_msg, "NoteOff N%3d       CH%2d", d1, channel);
                    }
                    midi_msg_timer = MIDI_DISPLAY_FRAMES;
                    break;

                case 0x80:  // Note Off
                    midiNoteOff(d1);
                    sprintf(midi_last_msg, "NoteOff N%3d V%3d CH%2d", d1, d2, channel);
                    midi_msg_timer = MIDI_DISPLAY_FRAMES;
                    break;

                case 0xC0: {  // Program Change
                    short new_prog = (short)d1;
                    if (new_prog < (short)current_audio.num_programs) {
                        current_program = new_prog;
                        // Sync editor program selector when in edit screens
                        if (inEditorState()) {
                            edit_program = new_prog;
                            loadProgramData();
                        }
                        sprintf(midi_last_msg, "ProgChg P%3d       CH%2d", d1, channel);
                    } else {
                        // Out-of-range program: reject and show warning
                        sprintf(midi_last_msg, "ProgChg P%3d >max! CH%2d", d1, channel);
                    }
                    midi_msg_timer = MIDI_DISPLAY_FRAMES;
                    break;
                }

                case 0xB0: {  // Control Change

                    // --------------------------------------------------
                    // NRPN accumulation (CC99+CC98+CC06)
                    // Reverb NRPNs work even without a sequence loaded
                    // --------------------------------------------------
                    if (d1 == 99) {              // NRPN MSB
                        midi_nrpn_msb = d2;
                        midi_nrpn_lsb = 0xFF;    // invalidate LSB on new MSB
                        break;
                    }
                    if (d1 == 98) {              // NRPN LSB
                        midi_nrpn_lsb = d2;
                        break;
                    }
                    if (d1 == 6 &&               // Data Entry
                        midi_nrpn_msb != 0xFF && midi_nrpn_lsb != 0xFF) {

                        u_char n_tone = midi_nrpn_msb;   // CC99 value
                        u_char n_attr = midi_nrpn_lsb;   // CC98 value

                        if (n_tone == 16 && n_attr >= 15 && n_attr <= 19) {
                            // Reverb and echo settings
                            switch (n_attr) {
                                case 15: // Reverb type 0-9
                                    setReverbType((int)d2);
                                    sprintf(midi_last_msg, "NRPN RevType:%s",
                                            getReverbTypeName(reverb_type));
                                    break;
                                case 16: // Reverb depth 0-127
                                    setReverbDepth((int)d2);
                                    sprintf(midi_last_msg, "NRPN RevDepth:%3d", d2);
                                    break;
                                case 17: // Echo feedback 0-127
                                    setReverbFeedback((int)d2);
                                    sprintf(midi_last_msg, "NRPN Feedback:%3d", d2);
                                    break;
                                case 18: // Echo delay time 0-127
                                case 19: // Delay time 0-127
                                    setReverbDelay((int)d2);
                                    sprintf(midi_last_msg, "NRPN Delay:   %3d", d2);
                                    break;
                            }
                            midi_msg_timer = MIDI_DISPLAY_FRAMES;

                        } else if (n_attr >= 4 && n_attr <= 12 &&
                                   current_audio.vab_id >= 0) {

                            // ADSR parameter for a specific tone (0-15) or all tones (16)
                            short cc_prog = (inEditorState())
                                            ? (short)edit_program
                                            : (short)current_program;

                            if (n_tone == 16) {
                                ProgAtr pa;
                                int t, ntones = 0;
                                if (SsUtGetProgAtr(current_audio.vab_id, cc_prog, &pa) == 0)
                                    ntones = pa.tones;
                                for (t = 0; t < ntones; t++)
                                    applyNrpnAdsr(cc_prog, t, n_attr, d2);
                            } else if (n_tone <= 15) {
                                applyNrpnAdsr(cc_prog, (int)n_tone, n_attr, d2);
                            }
                            sprintf(midi_last_msg, "NRPN T%2d A%2d V%3d",
                                    n_tone, n_attr, d2);
                            midi_msg_timer = MIDI_DISPLAY_FRAMES;
                        }
                        break;
                    }

                    // CC7 volume and CC10 pan, only when a VAB is loaded
                    if (current_audio.vab_id < 0) break;

                    // Target Editor program if in Editor
                    short cc_prog = (inEditorState())
                                    ? (short)edit_program
                                    : (short)current_program;

                    if (d1 == 7 || d1 == 10) {
                        // Read the current attributes
                        ProgAtr pa;
                        if (SsUtGetProgAtr(current_audio.vab_id, cc_prog, &pa) == 0) {
                            if (d1 == 7) {      // CC 7: Channel Volume
                                pa.mvol = (u_char)d2;
                                SsUtSetProgAtr(current_audio.vab_id, cc_prog, &pa);
                                // Keep editor struct in sync so UI reflects the change
                                if (inEditorState()) {
                                    current_prog_atr.mvol = pa.mvol;
                                }
                                sprintf(midi_last_msg, "CC Vol  P%3d V%3d CH%2d",
                                        cc_prog, d2, channel);
                            } else {            // CC 10: Pan
                                pa.mpan = (u_char)d2;
                                SsUtSetProgAtr(current_audio.vab_id, cc_prog, &pa);
                                if (inEditorState()) {
                                    current_prog_atr.mpan = pa.mpan;
                                }
                                sprintf(midi_last_msg, "CC Pan  P%3d V%3d CH%2d",
                                        cc_prog, d2, channel);
                            }
                            midi_msg_timer = MIDI_DISPLAY_FRAMES;
                        }
                    }
                    // All other CC numbers are silently ignored
                    break;
                }

                case 0xE0: {  // Pitch Bend
                    // MIDI pitch bend is a 14-bit value: (d2 << 7) | d1
                    // Center = 8192 (0x2000).  The MSB (d2) is a 7-bit value 0-127 with 64 as center
                    // maps directly to SsUtPitchBend's pbend parameter
                    midi_pitch_bend = (short)d2;  // 0-127, 64 = no bend

                    // Apply to every active MIDI voice
                    {
                        int vi;
                        for (vi = 0; vi < midi_poly_count; vi++) {
                            if (midi_voices[vi].voice_id >= 0) {
                                SsUtPitchBend(midi_voices[vi].voice_id,
                                              current_audio.vab_id,
                                              midi_voices[vi].program,
                                              midi_voices[vi].note,
                                              midi_pitch_bend);
                            }
                        }
                    }
                    sprintf(midi_last_msg, "PitchBnd %3d       CH%2d", midi_pitch_bend, channel);
                    midi_msg_timer = MIDI_DISPLAY_FRAMES;
                    break;
                }

                default:
                    // All other message types silently discarded
                    break;
            }

            // Reset position for next message (running status stays set)
            midi_pbuf_pos = 0;
        }
    }

    // Count down the on-screen message display timer
    if (midi_msg_timer > 0) midi_msg_timer--;
}

// Input helpers

// Hold-repeat logic for one button. Call once per frame per button with that button's own counters.
// Returns 1 on the initial press, then again every HOLD_REPEAT_RATE frames once the button has been held for HOLD_THRESHOLD frames.
static int repeatFire(int mask, int* cnt, int* active)
{
    if (pad & mask) {
        (*cnt)++;
        if (!(oldpad & mask)) {
            // Initial press
            *cnt = 0;
            *active = 0;
            return 1;
        } else if (*cnt >= HOLD_THRESHOLD && !*active) {
            // Hold threshold reached
            *active = 1;
        } else if (*active && (*cnt % HOLD_REPEAT_RATE == 0)) {
            // Continuous repeat while held
            return 1;
        }
    } else {
        *cnt = 0;
        *active = 0;
    }
    return 0;
}

// D-Pad Up/Down on a file list (uses 'cursor')
static void navList(int count)
{
    if (PRESSED(PADLup)) {
        cursor--;
        if (cursor < 0) cursor = count - 1;
    }
    if (PRESSED(PADLdown)) {
        cursor++;
        if (cursor >= count) cursor = 0;
    }
}

// D-Pad Up/Down on a menu with hold-repeat (uses 'menu_cursor')
// skip_item: menu item the cursor must never rest on, or -1 for none
static void navMenu(int count, int skip_item)
{
    if (repeatFire(PADLup, &hold_counter_up, &hold_active_up)) {
        do {
            menu_cursor--;
            if (menu_cursor < 0) menu_cursor = count - 1;
        } while (menu_cursor == skip_item);
    }
    if (repeatFire(PADLdown, &hold_counter_down, &hold_active_down)) {
        do {
            menu_cursor++;
            if (menu_cursor >= count) menu_cursor = 0;
        } while (menu_cursor == skip_item);
    }
}

// Value editing controls for editor screens
// Square = min/max toggle, L1/R1 = -10/+10, D-Pad Left/Right = -1/+1 with hold-repeat
static void editInput(void (*adjust)(int, int), void (*toggle)(void))
{
    if (PRESSED(PADRleft)) toggle();
    if (PRESSED(PADL1))    adjust(-1, 10);
    if (PRESSED(PADR1))    adjust(1, 10);

    if (repeatFire(PADLleft,  &hold_counter_left,  &hold_active_left))  adjust(-1, 1);
    if (repeatFire(PADLright, &hold_counter_right, &hold_active_right)) adjust(1, 1);
}

// Triangle plays a note while held in VAB mode, always works even when Select is held to avoid notes getting stuck ON
static void handleTriangleNote(void)
{
    if (pad & PADRup) {
        if (!(oldpad & PADRup)) {
            playNote();
        }
    } else if (oldpad & PADRup) {
        stopNote();
    }
}

#ifdef ENABLE_CD_BROWSER
// CD-ROM browser
// Reads the /SEQ and /VAB folders in the disc root, lists their files and loads the selected file into a static RAM pool.
// Loaded files are appended to seq_files[] / vh_files[] / vb_files[] so the normal SEQ and VH select screens list them like the built-in ones.
// /SEQ/*.SEQ        loaded as SEQ files
// /VAB/NAME.VH+VB   loading either half loads the pair, both files must exist

#ifndef CD_POOL_SIZE
#define CD_POOL_SIZE      (512 * 1024)   // RAM reserved for files loaded from CD
#endif
#define CD_MAX_ENTRIES    32             // files listed per folder
#define CD_NAME_LEN       24             // longest file name kept (incl. NUL)
#define CD_SECTOR         2048
#define CD_DIRBUF_SECTORS 4              // directory sectors read per folder
#define CD_LIST_ROWS      8              // rows shown at once in the browser

typedef struct {
    char   name[CD_NAME_LEN];
    u_long lba;
    u_long size;
} CdEntry;

typedef struct {
    CdEntry entry[CD_MAX_ENTRIES];
    int     count;
} CdFolder;

typedef enum { CD_ACT_NONE, CD_ACT_SCAN, CD_ACT_LOAD } CdAction;

static const char* const cd_folder_names[2] = { "SEQ", "VAB" };
static CdFolder cd_folder[2];
static int      cd_folder_sel   = 0;     // 0 = SEQ, 1 = VAB
static int      cd_cursor       = 0;
static int      cd_saved_cursor = 0;     // SEQ list cursor to restore on exit
static int      cd_inited       = 0;
static CdAction cd_action       = CD_ACT_NONE;
static char     cd_status[40]   = "";
static u_int    cd_pool_used    = 0;
static u_long   cd_pool[CD_POOL_SIZE / 4];
static u_long   cd_dirbuf[(CD_DIRBUF_SECTORS * CD_SECTOR) / 4];

static u_long cdRead32(const u_char* p)
{
    return (u_long)p[0] | ((u_long)p[1] << 8) | ((u_long)p[2] << 16) | ((u_long)p[3] << 24);
}

static int cdLower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

// Case-insensitive string compare, 1 if equal
static int cdNameEq(const char* a, const char* b)
{
    while (*a && *b) {
        if (cdLower(*a) != cdLower(*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

// Read nsec sectors starting at ISO9660 sector lba into buf with 3 retry attempts
static int cdReadSectors(u_long lba, u_int nsec, void* buf)
{
    CdlLOC loc;
    int tries;

    for (tries = 0; tries < 3; tries++) {
        CdIntToPos((int)lba, &loc);
        CdControlB(CdlSetloc, (u_char*)&loc, 0);
        if (CdRead((int)nsec, (u_long*)buf, CdlModeSpeed) && CdReadSync(0, 0) == 0) {
            return 1;
        }
    }
    return 0;
}

// Init libcd the first time the browser needs it
static int cdEnsureInit(void)
{
    if (!cd_inited) {
        if (!CdInit()) return 0;
        cd_inited = 1;
    }
    return 1;
}

// Read (up to CD_DIRBUF_SECTORS of) a directory into cd_dirbuf
// Returns the number of bytes read, 0 on failure
static u_int cdReadDir(u_long lba, u_long size)
{
    u_int nsec = (u_int)(size / CD_SECTOR) + ((size % CD_SECTOR) ? 1 : 0);
    if (nsec == 0) nsec = 1;
    if (nsec > CD_DIRBUF_SECTORS) nsec = CD_DIRBUF_SECTORS;
    if (!cdReadSectors(lba, nsec, cd_dirbuf)) return 0;
    return nsec * CD_SECTOR;
}

// Step through ISO9660 directory records in buf. Returns the next record or NULL at the end.
// Records never cross a sector boundary; a zero length byte means skip to the next sector
static const u_char* cdNextRecord(const u_char* buf, u_int bytes, u_int* off)
{
    while (*off < bytes) {
        const u_char* r = buf + *off;
        u_int len = r[0];
        if (len == 0) {
            *off = (*off | (CD_SECTOR - 1)) + 1;
            continue;
        }
        if (len < 34 || *off + len > bytes) return NULL;
        *off += len;
        return r;
    }
    return NULL;
}

// Copy the name of directory record r into out without the ";1" version and without a trailing '.'
// Returns 0 for "." / ".." or names that are too long
static int cdRecordName(const u_char* r, char* out)
{
    u_int nlen = r[32];
    const u_char* n = r + 33;
    u_int i;

    if (nlen == 1 && (n[0] == 0 || n[0] == 1)) return 0;
    for (i = 0; i < nlen && n[i] != ';'; i++) {
        if (i >= CD_NAME_LEN - 1) return 0;
        out[i] = (char)n[i];
    }
    if (i > 0 && out[i - 1] == '.') i--;
    out[i] = '\0';
    return i > 0;
}

// Read cdrom: find /SEQ and /VAB in the root directory and list their files
static int cdScan(void)
{
    u_char* buf = (u_char*)cd_dirbuf;
    const u_char* r;
    u_long dir_lba[2]  = { 0, 0 };
    u_long dir_size[2] = { 0, 0 };
    int    found[2]    = { 0, 0 };
    char   name[CD_NAME_LEN];
    u_long root_lba, root_size;
    u_int  bytes, off;
    int    f;

    cd_folder[0].count = 0;
    cd_folder[1].count = 0;

    if (!cdEnsureInit()) {
        sprintf(cd_status, "CD INIT FAILED");
        return 0;
    }

    // Primary volume descriptor at sector 16 holds the root dir record
    if (!cdReadSectors(16, 1, cd_dirbuf) ||
        buf[0] != 1 || buf[1] != 'C' || buf[2] != 'D' ||
        buf[3] != '0' || buf[4] != '0' || buf[5] != '1') {
        sprintf(cd_status, "NO DISC / NOT ISO9660");
        return 0;
    }
    root_lba  = cdRead32(buf + 156 + 2);
    root_size = cdRead32(buf + 156 + 10);

    bytes = cdReadDir(root_lba, root_size);
    if (!bytes) {
        sprintf(cd_status, "ROOT DIR READ ERROR");
        return 0;
    }

    // Find the SEQ and VAB dirs in the root
    off = 0;
    while ((r = cdNextRecord(buf, bytes, &off)) != NULL) {
        if ((r[25] & 2) && cdRecordName(r, name)) {
            for (f = 0; f < 2; f++) {
                if (cdNameEq(name, cd_folder_names[f])) {
                    dir_lba[f]  = cdRead32(r + 2);
                    dir_size[f] = cdRead32(r + 10);
                    found[f]    = 1;
                }
            }
        }
    }

    // List the files of each folder that exists
    for (f = 0; f < 2; f++) {
        if (!found[f]) continue;
        bytes = cdReadDir(dir_lba[f], dir_size[f]);
        if (!bytes) continue;

        off = 0;
        while ((r = cdNextRecord(buf, bytes, &off)) != NULL) {
            if (!(r[25] & 2) && cdRecordName(r, name) &&
                cd_folder[f].count < CD_MAX_ENTRIES) {
                CdEntry* e = &cd_folder[f].entry[cd_folder[f].count++];
                sprintf(e->name, "%s", name);
                e->lba  = cdRead32(r + 2);
                e->size = cdRead32(r + 10);
            }
        }
    }

    if (!found[0] && !found[1]) {
        sprintf(cd_status, "NO SEQ OR VAB FOLDER");
    } else {
        sprintf(cd_status, "SEQ:%d  VAB:%d FILES", cd_folder[0].count, cd_folder[1].count);
    }
    return 1;
}

// Index in files whose name matches, or -1
static int cdFindLoaded(const FileEntry* files, int first, int count, const char* name)
{
    int i;
    for (i = first; i < count; i++) {
        if (cdNameEq(files[i].name, name)) return i;
    }
    return -1;
}

// Entry in folder f with the given name, or NULL
static const CdEntry* cdFindEntry(const CdFolder* f, const char* name)
{
    int i;
    for (i = 0; i < f->count; i++) {
        if (cdNameEq(f->entry[i].name, name)) return &f->entry[i];
    }
    return NULL;
}

// Split "NAME.EXT" into base (copied to base) and extension pointer
// Returns NULL if the name has no extension
static const char* cdSplitName(const char* name, char* base)
{
    const char* dot = NULL;
    const char* p;
    int n;

    for (p = name; *p; p++) {
        if (*p == '.') dot = p;
    }
    if (!dot) return NULL;
    n = (int)(dot - name);
    if (n >= CD_NAME_LEN) n = CD_NAME_LEN - 1;
    for (p = name; p < name + n; p++) base[p - name] = *p;
    base[n] = '\0';
    return dot + 1;
}

// Check if folder is already loaded into RAM
static int cdEntryLoaded(int folder, const CdEntry* e)
{
    char base[CD_NAME_LEN];
    char key[40];
    const char* ext;

    if (folder == 0) {
        sprintf(key, "CD:%s", e->name);
        return cdFindLoaded(seq_files, MAX_SEQ_FILES, seq_count, key) >= 0;
    }
    ext = cdSplitName(e->name, base);
    if (!ext || !(cdNameEq(ext, "VH") || cdNameEq(ext, "VB"))) return 0;
    sprintf(key, "CD:%s.VH", base);
    return cdFindLoaded(vh_files, MAX_VH_FILES, vh_count, key) >= 0;
}

// Read an entry into the next free part of the RAM pool.
// Returns the buffer, or NULL with cd_status set
static u_char* cdLoadEntry(const CdEntry* e)
{
    u_int sectors = (u_int)(e->size / CD_SECTOR) + ((e->size % CD_SECTOR) ? 1 : 0);
    u_int bytes   = sectors * CD_SECTOR;
    u_char* dst;

    if (sectors == 0) {
        sprintf(cd_status, "EMPTY FILE");
        return NULL;
    }
    if (bytes > CD_POOL_SIZE - cd_pool_used) {
        sprintf(cd_status, "NO RAM: NEED %dK FREE", (int)(bytes / 1024));
        return NULL;
    }
    dst = (u_char*)cd_pool + cd_pool_used;
    if (!cdReadSectors(e->lba, sectors, dst)) {
        sprintf(cd_status, "READ ERROR");
        return NULL;
    }
    cd_pool_used += bytes;
    return dst;
}

static void cdLoadSeq(const CdEntry* e)
{
    char name[32];
    u_char* d;
    u_int mark = cd_pool_used;

    sprintf(name, "CD:%s", e->name);
    if (cdFindLoaded(seq_files, MAX_SEQ_FILES, seq_count, name) >= 0) {
        sprintf(cd_status, "ALREADY LOADED");
        return;
    }
    if (seq_count >= MAX_SEQ_FILES + CD_MAX_LOADED) {
        sprintf(cd_status, "SEQ LIST FULL");
        return;
    }
    d = cdLoadEntry(e);
    if (!d) return;

    // SEQ files start with "pQES"
    if (!(d[0] == 'p' && d[1] == 'Q' && d[2] == 'E' && d[3] == 'S')) {
        cd_pool_used = mark;
        sprintf(cd_status, "NOT A SEQ FILE");
        return;
    }

    sprintf(seq_files[seq_count].name, "%s", name);
    seq_files[seq_count].data = d;
    seq_files[seq_count].size = e->size;
    seq_files[seq_count].type = 0;
    seq_count++;
    sprintf(cd_status, "LOADED %dK", (int)((e->size + 1023) / 1024));
}

// Load the selected entry VH + VB pair
static void cdLoadVab(const CdEntry* sel)
{
    char base[CD_NAME_LEN];
    char want[CD_NAME_LEN + 4];
    char name[32];
    const char* ext;
    const CdEntry *vh, *vb;
    u_char *vhp, *vbp;
    u_int mark = cd_pool_used;
    VabHdr* hdr;

    ext = cdSplitName(sel->name, base);
    if (!ext || !(cdNameEq(ext, "VH") || cdNameEq(ext, "VB"))) {
        sprintf(cd_status, "NOT A VH/VB FILE");
        return;
    }

    sprintf(want, "%s.VH", base);
    vh = cdFindEntry(&cd_folder[1], want);
    sprintf(want, "%s.VB", base);
    vb = cdFindEntry(&cd_folder[1], want);
    if (!vh) { sprintf(cd_status, "MISSING %s.VH", base); return; }
    if (!vb) { sprintf(cd_status, "MISSING %s.VB", base); return; }

    sprintf(name, "CD:%s", vh->name);
    if (cdFindLoaded(vh_files, MAX_VH_FILES, vh_count, name) >= 0) {
        sprintf(cd_status, "ALREADY LOADED");
        return;
    }
    if (vh_count >= MAX_VH_FILES + CD_MAX_LOADED) {
        sprintf(cd_status, "VH LIST FULL");
        return;
    }

    vhp = cdLoadEntry(vh);
    if (!vhp) return;
    vbp = cdLoadEntry(vb);
    if (!vbp) {
        cd_pool_used = mark;     // give back the VH too
        return;
    }

    // Check VAB header pVAB
    if (!(vhp[0] == 'p' && vhp[1] == 'B' && vhp[2] == 'A' && vhp[3] == 'V')) {
        cd_pool_used = mark;
        sprintf(cd_status, "NOT A VH FILE");
        return;
    }

    hdr = (VabHdr*)vhp;
    sprintf(vh_files[vh_count].name, "%s", name);
    vh_files[vh_count].data = vhp;
    vh_files[vh_count].size = hdr->fsize;
    vh_files[vh_count].type = 1;
    vb_files[vh_count]      = vbp;
    vh_count++;
    sprintf(cd_status, "LOADED %dK+%dK", (int)((vh->size + 1023) / 1024), (int)((vb->size + 1023) / 1024));
}

// Load selection
static void cdLoadSelected(void)
{
    const CdFolder* f = &cd_folder[cd_folder_sel];

    if (cd_cursor < 0 || cd_cursor >= f->count) return;
    if (cd_folder_sel == 0) {
        cdLoadSeq(&f->entry[cd_cursor]);
    } else {
        cdLoadVab(&f->entry[cd_cursor]);
    }
}

// Forget every file loaded from CD and free the pool
// Only reachable from the browser, which is only entered from the SEQ list where no VAB or sequence is using that memory
static void cdUnloadAll(void)
{
    if (is_playing) stopSequence();
    closeVab();
    cd_pool_used = 0;
    seq_count = MAX_SEQ_FILES;
    vh_count  = MAX_VH_FILES;
    sprintf(cd_status, "UNLOADED ALL CD FILES");
}

static void enterCdBrowser(void)
{
    cd_saved_cursor = cursor;
    cd_cursor       = 0;
    cd_folder_sel   = 0;
    sprintf(cd_status, "READING DISC...");
    cd_action       = CD_ACT_SCAN;     // runs next frame, after the message is drawn
    current_state   = STATE_CD_BROWSER;
}

static void cdBrowserInput(void)
{
    const CdFolder* f;

    // Slow actions (disc reads) are queued by the previous frame so that the "working" message has been drawn before the read blocks
    if (cd_action != CD_ACT_NONE) {
        CdAction act = cd_action;
        cd_action = CD_ACT_NONE;
        if (act == CD_ACT_SCAN) {
            cdScan();
            cd_cursor = 0;
        } else {
            cdLoadSelected();
        }
        return;
    }

    f = &cd_folder[cd_folder_sel];

    // D-Pad Up/Down with hold-repeat
    if (repeatFire(PADLup, &hold_counter_up, &hold_active_up) && f->count > 0) {
        cd_cursor--;
        if (cd_cursor < 0) cd_cursor = f->count - 1;
    }
    if (repeatFire(PADLdown, &hold_counter_down, &hold_active_down) && f->count > 0) {
        cd_cursor++;
        if (cd_cursor >= f->count) cd_cursor = 0;
    }

    // L1/R1 or D-Pad Left/Right switch between the SEQ and VAB folder
    if (PRESSED(PADL1) || PRESSED(PADR1) || PRESSED(PADLleft) || PRESSED(PADLright)) {
        cd_folder_sel ^= 1;
        cd_cursor = 0;
    }

    // X - load the selected file
    if (PRESSED(PADRdown) && f->count > 0) {
        sprintf(cd_status, "LOADING...");
        cd_action = CD_ACT_LOAD;
    }

    // Square - unload everything that came from the CD
    if (PRESSED(PADRleft)) {
        cdUnloadAll();
    }

    // Start - rescan the disc
    if (PRESSED(PADstart)) {
        sprintf(cd_status, "READING DISC...");
        cd_action = CD_ACT_SCAN;
    }

    // Circle - back to the SEQ list
    if (PRESSED(PADRright)) {
        current_state = STATE_SEQ_SELECT;
        cursor = (cd_saved_cursor < seq_count) ? cd_saved_cursor : 0;
    }
}

#endif // ENABLE_CD_BROWSER

// Input Processing
void processInput(void)
{
    // Process incoming Serial MIDI bytes before handling pad input
    // MIDI-triggered notes are applied every frame regardless of UI state
    processMidi();

    pad = PadRead(0);

#ifdef ENABLE_DEBUG_MENU
    // Select+Start toggles the debug menu. Ssaves and restores the previous state
#define DEBUG_TOGGLE_COMBO (PADselect | PADstart)
    if ((pad    & DEBUG_TOGGLE_COMBO) == DEBUG_TOGGLE_COMBO &&
        (oldpad & DEBUG_TOGGLE_COMBO) != DEBUG_TOGGLE_COMBO) {
        if (current_state == STATE_DEBUG) {
            current_state = pre_debug_state;
        } else {
            pre_debug_state = current_state;
            current_state   = STATE_DEBUG;
        }
        oldpad = pad;
        return;
    }
    if (current_state == STATE_DEBUG) {
        // Circle to return to previous state
        if (PRESSED(PADRright)) {
            current_state = pre_debug_state;
            oldpad = pad;
            return;
        }
     	// Triangle: interact with audio without leaving the debug screen
        if (vab_mode) {
            handleTriangleNote();
        } else {
            if (PRESSED(PADRup)) {
                if (is_playing) {
                    stopSequence();
                } else {
                    playSequence();
                }
            }
        }
        oldpad = pad;
        return;
    }
#undef DEBUG_TOGGLE_COMBO
#endif

    // Global controls that work in all states
    // Select button behavior in playback states
    if (PRESSED(PADselect)) {
        if (current_state == STATE_PLAYBACK || current_state == STATE_VAB_PLAYBACK) {
#if HAS_BACKGROUND_IMAGE
            // If background image present, Select toggles background
            bg_state = (bg_state + 1) % 3;  // Cycle 0->1->2->0
#else
            // No background image, Select enters program edit
            enterProgramEdit();
#endif
        }
    }
    
    // Check if Select is being held (layer modifier active)
    // When Select is held, normal inputs are blocked, only Select+button combos work
    int select_layer_active = (pad & PADselect) && (oldpad & PADselect);
    
    // Triangle - Play/Stop in SEQ mode (disabled when Select held)
    // In VAB mode Triangle note playback is handled per-state
    if (!vab_mode && !select_layer_active && PRESSED(PADRup)) {
        if (current_state == STATE_PLAYBACK || current_state == STATE_PROGRAM_EDIT || current_state == STATE_TONE_EDIT) {
            if (is_playing) {
                stopSequence();
            } else {
                playSequence();
            }
        }
    }
    
    // Start button:
    //   SEQ mode  → Pause/Unpause sequence (as before)
    //   VAB mode  → Toggle Serial MIDI on/off
    //   (disabled when Select is held)
    if (!select_layer_active && PRESSED(PADstart)) {
        if (!vab_mode &&
            (current_state == STATE_PLAYBACK      ||
             current_state == STATE_PROGRAM_EDIT  ||
             current_state == STATE_TONE_EDIT)) {
            // SEQ mode: pause / unpause
            pauseSequence();
        } else if (vab_mode &&
                   (current_state == STATE_VAB_PLAYBACK  ||
                    inEditorState())) {
            // VAB mode: toggle Serial MIDI (startSerialMidi handles toggle-off)
            if (serial_midi_enabled) {
                stopSerialMidi();
            } else {
                startSerialMidi();
            }
        }
    }
    
    // L2/R2 - Change note in VAB mode (disabled if Select held)
    if (!select_layer_active && vab_mode && (current_state == STATE_VAB_PLAYBACK || current_state == STATE_PROGRAM_EDIT || current_state == STATE_TONE_EDIT)) {
        if (PRESSED(PADL2)) {
            current_note--;
            if (current_note < 0) current_note = 0;
            if (note_playing) playNote();  // Restart if playing
        }
        if (PRESSED(PADR2)) {
            current_note++;
            if (current_note > 127) current_note = 127;
            if (note_playing) playNote();  // Restart if playing
        }
    }
    
    switch (current_state) {
        case STATE_SEQ_SELECT:
            navList(seq_count);
            if (PRESSED(PADRdown)) { // X button - SEQ mode
                selected_seq = cursor;
                vab_mode = 0;
                current_state = STATE_VH_SELECT;
                cursor = 0;
            }
            if (PRESSED(PADRleft)) { // Square button - VAB mode
                vab_mode = 1;
                current_state = STATE_VAB_VH_SELECT;
                cursor = 0;
            }
#ifdef ENABLE_CD_BROWSER
            if (PRESSED(PADRup)) { // Triangle button - CD-ROM browser
                enterCdBrowser();
            }
#endif
#if HAS_BACKGROUND_IMAGE
            if (PRESSED(PADselect)) { // Select - Toggle background
                bg_state = (bg_state + 1) % 3;  // Cycle 0->1->2->0
            }
#endif
            break;
            
        case STATE_VH_SELECT:
            navList(vh_count);
            if (PRESSED(PADRdown)) { // X button
                selected_vh = cursor;
                
                // Close previously loaded VAB if any (changing to new file)
                closeVab();
                
                // Set up audio file structure
                current_audio.seq_data = seq_files[selected_seq].data;
                current_audio.seq_size = seq_files[selected_seq].size;
                sprintf(current_audio.seq_name, "%s", seq_files[selected_seq].name);
                
                selectVhFile(selected_vh);
                
                current_state = STATE_PLAYBACK;
            }
            if (PRESSED(PADRright)) { // Circle button
                // Going back to SEQ select - close VAB as user may select different SEQ
                closeVab();
                current_state = STATE_SEQ_SELECT;
                cursor = selected_seq;
            }
            break;
            
        case STATE_PLAYBACK:
            // If Select is held, skip all normal inputs (layer modifier)
            if (!select_layer_active) {
                // Circle - Back to VH select
                if (PRESSED(PADRright)) {
                    stopSequence();
                    current_state = STATE_VH_SELECT;
                    cursor = selected_vh;
                    menu_cursor = 0;
                }
                
                // D-Pad Up/Down - Navigate menu with hold detection
                navMenu(MENU_ITEM_COUNT, -1);
                
                // X button - Execute menu action
                if (PRESSED(PADRdown)) {
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
                        case MENU_VISUALIZER:
                            return_state = STATE_PLAYBACK;
                            current_state = STATE_VISUALIZER;
                            break;
                    }
                }
                
                // Square/L1/R1/D-Pad Left-Right - only for value items
                if (menu_cursor >= MENU_TEMPO) {
                    editInput(adjustMenuValue, toggleMinMax);
                }
            }
            
            break;
            
        case STATE_VAB_VH_SELECT:
            navList(vh_count);
            if (PRESSED(PADRdown)) { // X button
                selected_vh = cursor;
                
                // Close previously loaded VAB if any (changing to new file)
                closeVab();
                
                // Set up audio file structure for VAB mode
                selectVhFile(selected_vh);
                
                // Open VAB and transfer body to SPU memory
                if (!openVab()) break;
                
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
                initMidiVoices();  // Clear polyphony pool for fresh session
                
                current_state = STATE_VAB_PLAYBACK;
            }
            if (PRESSED(PADRright)) { // Circle button - Back to SEQ select
                // Safety: stop Serial MIDI if somehow still active
                stopSerialMidi();
                // Close VAB if any (going back to SEQ select)
                closeVab();
                current_state = STATE_SEQ_SELECT;
                cursor = 0;
                vab_mode = 0;
            }
            break;
            
        case STATE_VAB_PLAYBACK:
            // Triangle - Play/sustain note ALWAYS works (even when Select held)
            // This prevents stuck notes
            handleTriangleNote();
            
            // If Select is held, skip all other normal inputs (layer modifier)
            if (!select_layer_active) {
                // Circle - Back to VH select; also closes Serial MIDI connection
                if (PRESSED(PADRright)) {
                    stopNote();
                    stopSerialMidi();  // Close serial connection on leaving VAB mode
                    closeVab();
                    current_state = STATE_VAB_VH_SELECT;
                    cursor = selected_vh;
                    menu_cursor = 0;
                }
                
                // D-Pad Up/Down - Navigate menu with hold detection
                navMenu(VAB_MENU_ITEM_COUNT, -1);
                
                // X button - Enter program edit / visualizer
                if (PRESSED(PADRdown)) {
                    if (menu_cursor == VAB_MENU_PROGRAM_EDIT) {
                        enterProgramEdit();
                    } else if (menu_cursor == VAB_MENU_VISUALIZER) {
                        return_state = STATE_VAB_PLAYBACK;
                        current_state = STATE_VISUALIZER;
                    }
                }
                
                // Square/L1/R1/D-Pad Left-Right
                editInput(adjustVabMenuValue, toggleVabMinMax);
            }
            
            break;
            
        case STATE_PROGRAM_EDIT:
            // If Select is held, skip all normal inputs (layer modifier)
            if (!select_layer_active) {
                // Circle - Back to playback
                if (PRESSED(PADRright)) {
                    current_state = return_state;
                    menu_cursor = 0;
                }
                
                // D-Pad Up/Down - Navigate menu with hold detection
                navMenu(PROG_MENU_ITEM_COUNT, -1);
                
                // X button - Enter tone edit if on NUM_TONES
                if (PRESSED(PADRdown)) {
                    if (menu_cursor == PROG_MENU_NUM_TONES) {
                        edit_tone = 0;
                        loadToneData();
                        current_state = STATE_TONE_EDIT;
                        menu_cursor = 0;
                    }
                }
                
                // Square/L1/R1/D-Pad Left-Right
                editInput(adjustProgramEditValue, toggleProgramEditMinMax);
            }
            
            // Triangle - Play note in VAB mode ALWAYS works (even when Select held)
            if (vab_mode) {
                handleTriangleNote();
            }
            
            break;
            
        case STATE_TONE_EDIT:
            // Select+L1/R1 combos work when Select is held (NOT wrapped)
            if (pad & PADselect) {
                if (PRESSED(PADL1)) {
                    toneEditProgramStep(-1);  // Previous program
                }
                if (PRESSED(PADR1)) {
                    toneEditProgramStep(1);   // Next program
                }
            }
            
            // All other normal inputs (wrapped, disabled when Select is held)
            if (!select_layer_active) {
                // Circle - Back to program edit
                if (PRESSED(PADRright)) {
                    current_state = STATE_PROGRAM_EDIT;
                    menu_cursor = PROG_MENU_NUM_TONES;
                }
                
                // D-Pad Up/Down - Navigate menu with hold detection (skip NOTE_SEL if not VAB mode)
                navMenu(TONE_MENU_ITEM_COUNT, vab_mode ? -1 : TONE_MENU_NOTE_SEL);
                
                // X button - Enter ADSR parameter editor if on ADSR1 or ADSR2
                if (PRESSED(PADRdown)) {
                    if (menu_cursor == TONE_MENU_ADSR1) {
                        enterAdsrEdit(1);  // Edit ADSR1
                    } else if (menu_cursor == TONE_MENU_ADSR2) {
                        enterAdsrEdit(2);  // Edit ADSR2
                    }
                }
                
                // Square/L1/R1/D-Pad Left-Right (ADSR rows are edited with X instead)
                if (menu_cursor != TONE_MENU_ADSR1 && menu_cursor != TONE_MENU_ADSR2) {
                    editInput(adjustToneEditValue, toggleToneEditMinMax);
                }
            }
            
            // Triangle - Play note in VAB mode ALWAYS works (not disabled by select held)
            if (vab_mode) {
                handleTriangleNote();
            }
            
            break;
            
        case STATE_ADSR_EDIT:
            // If Select is held, skip all normal inputs (layer modifier)
            if (!select_layer_active) {
                // Circle - Cancel and exit
                if (PRESSED(PADRright)) {
                    exitAdsrEdit(0);  // Don't save
                }
                
                // D-Pad Up/Down - Navigate menu with hold detection
                navMenu(ADSR_MENU_ITEM_COUNT, -1);
                
                // X button - Execute action (Save or Cancel)
                if (PRESSED(PADRdown)) {
                    if (menu_cursor == ADSR_MENU_SAVE) {
                        exitAdsrEdit(1);  // Save
                    } else if (menu_cursor == ADSR_MENU_CANCEL) {
                        exitAdsrEdit(0);  // Don't save
                    }
                }
                
                // Square/L1/R1/D-Pad Left-Right (no-ops on SAVE/CANCEL)
                editInput(adjustAdsrValue, toggleAdsrValue);
            }
            
            // Triangle - Play note in VAB mode ALWAYS works
            if (vab_mode) {
                handleTriangleNote();
            }
            
            break;
            
#ifdef ENABLE_CD_BROWSER
        case STATE_CD_BROWSER:
            cdBrowserInput();
            break;
#endif

        case STATE_VISUALIZER:
            // Circle: return to previous state
            if (PRESSED(PADRright)) {
                current_state = return_state;
            }
            // Triangle in VAB mode still works for note preview
            if (vab_mode) {
                handleTriangleNote();
            }
            break;
    }
    
    oldpad = pad;
}

// Rows of a file list
static void listWindow(int count, int cur, int rows, int* top, int* end)
{
    int t = cur - rows / 2;
    if (t > count - rows) t = count - rows;
    if (t < 0) t = 0;
    *top = t;
    *end = (t + rows < count) ? t + rows : count;
}

// File list used by the SEQ and VH select screens (uses 'cursor')
// Lists with more than LIST_ROWS files scroll, "..." marks hidden entries
static void drawFileList(const FileEntry* files, int count)
{
    int i, top, end;

    listWindow(count, cursor, LIST_ROWS, &top, &end);
    if (top > 0) FntPrint("  ...\n");
    for (i = top; i < end; i++) {
        FntPrint("%c %s\n", (i == cursor) ? '>' : ' ', files[i].name);
    }
    if (end < count) FntPrint("  ...\n");
}

#ifdef ENABLE_CD_BROWSER
void drawCdBrowser(void)
{
    const CdFolder* f = &cd_folder[cd_folder_sel];
    int i, top, end;

    FntPrint("\n");
    FntPrint("=== CD-ROM BROWSER ===\n\n");
    FntPrint("%s  %s\n\n", cd_folder_sel == 0 ? "[SEQ]" : " SEQ ",
                           cd_folder_sel == 1 ? "[VAB]" : " VAB ");

    FntPrint("-------------------\n");
    if (f->count == 0) {
        FntPrint("  (NO FILES)\n");
    } else {
        listWindow(f->count, cd_cursor, CD_LIST_ROWS, &top, &end);
        if (top > 0) FntPrint("  ...\n");
        for (i = top; i < end; i++) {
            FntPrint("%c %s %dK%s\n", (i == cd_cursor) ? '>' : ' ',
                     f->entry[i].name, (int)((f->entry[i].size + 1023) / 1024),
                     cdEntryLoaded(cd_folder_sel, &f->entry[i]) ? " [LOADED]" : "");
        }
        if (end < f->count) FntPrint("  ...\n");
    }
    FntPrint("-------------------\n");
    FntPrint("%s\n", cd_status);
    FntPrint("RAM: %d/%dK\n", (int)(cd_pool_used / 1024), (int)(CD_POOL_SIZE / 1024));

    FntPrint("\n");
    FntPrint("(?)CONTROLS\n");
    FntPrint("X: LOAD FILE\n");
    FntPrint("L1/R1: SEQ/VAB FOLDER\n");
    FntPrint("SQUARE: UNLOAD ALL\n");
    FntPrint("START: RESCAN DISC\n");
    FntPrint("CIRCLE: BACK\n");
}
#endif // ENABLE_CD_BROWSER

//Initial screen, program boots to SEQ file selection
void drawSeqSelect(void)
{
	FntPrint("\n");
    FntPrint("=== SEQ PLAYER ===\n\n");
    FntPrint("AVAILABLE SEQ FILES:\n\n");
    
	FntPrint("-------------------\n");
    drawFileList(seq_files, seq_count);
    FntPrint("-------------------\n");

    FntPrint("\n");
	FntPrint("(?)CONTROLS\n");
    FntPrint("X: OPEN FILE IN SEQ MODE\n");
    FntPrint("SQUARE: SOUNDBANK MODE\n");
	FntPrint("[SKIPS SEQ FILE SELECTION]");
#ifdef ENABLE_CD_BROWSER
    FntPrint("\nTRIANGLE: CD-ROM BROWSER");
#endif
}

void drawVhSelect(void)
{
	FntPrint("\n");
    FntPrint("=== SEQ PLAYER ===\n\n");
    FntPrint("SELECTED SEQ: %s\n\n", seq_files[selected_seq].name);
	FntPrint("AVAILABLE VH FILES:\n\n");

    FntPrint("-------------------\n");
    drawFileList(vh_files, vh_count);
    FntPrint("-------------------\n");

    FntPrint("\n");
	FntPrint("(?)CONTROLS\n");
    FntPrint("X: SELECT VH FILE\n");
    FntPrint("CIRCLE: BACK TO MODE SELECT\n");
}

// The four reverb rows shared by both playback screens
// first_item is the menu index of the REV TYPE row (rows follow in ReverbParam order)
static void drawReverbRows(int first_item)
{
    FntPrint("%c REV TYPE: %s\n",     CUR(first_item),     getReverbTypeName(reverb_type));
    FntPrint("%c REV DEPTH: %d\n",    CUR(first_item + 1), reverb_depth_left);
    FntPrint("%c REV DELAY: %d\n",    CUR(first_item + 2), reverb_delay);
    FntPrint("%c REV FEEDBACK: %d\n", CUR(first_item + 3), reverb_feedback);
}

void drawPlayback(void)
{
    const char* status_text;
    
    FntPrint("\n");
    FntPrint("=== SEQ PLAYER ===\n\n");
    FntPrint("SEQ: %s\n", current_audio.seq_name);
    FntPrint("VH: %s @%08X sz:%04X\n", current_audio.vh_name, (unsigned int)current_audio.vh_data, (unsigned int)current_audio.vh_size);
    FntPrint("PROGS: %d TONES: %d\n\n", current_audio.num_programs, current_audio.num_tones);
    
    // Determine status
    if (is_playing && is_paused) {
        status_text = "PAUSED";
    } else if (is_playing) {
        status_text = "PLAYING";
    } else {
        status_text = "STOPPED";
    }
    FntPrint("STATUS: %s\n\n", status_text);
    
    // Menu items
    FntPrint("-------------------\n");
    
    FntPrint("%c PLAY\n",  CUR(MENU_PLAY));
    FntPrint("%c PAUSE\n", CUR(MENU_PAUSE));
    FntPrint("%c STOP\n",  CUR(MENU_STOP));
    
    // TEMPO
    if (tempo_changed) {
        FntPrint("%c TEMPO: %d\n", CUR(MENU_TEMPO), current_tempo);
    } else {
        FntPrint("%c TEMPO: UNCHANGED%s\n", CUR(MENU_TEMPO), SEL(MENU_TEMPO, " (X)"));
    }
    
    drawReverbRows(MENU_REV_TYPE);
    FntPrint("%c PROGRAM EDIT\n",    CUR(MENU_PROGRAM_EDIT));
    FntPrint("%c VISUALIZER\n",      CUR(MENU_VISUALIZER));
	FntPrint("-------------------\n");
    
	FntPrint("\n");
    FntPrint("(?)CONTROLS\n");
    FntPrint("X: SELECT\n");
    FntPrint("TRIANGLE: PLAY/STOP\n");
    FntPrint("START: TOGGLE PAUSE\n");
    FntPrint("L/R:-1/+1 L1/R1:-10+10\n");
    FntPrint("SQUARE: MIN/MAX\n");
    FntPrint("CIRCLE: BACK\n");
}

void drawVabVhSelect(void)
{
    FntPrint("\n");
    FntPrint("=== VAB PLAYER ===\n\n");
    FntPrint("AVAILABLE VH FILES:\n\n");
    
	FntPrint("-------------------\n");
    drawFileList(vh_files, vh_count);
    FntPrint("-------------------\n");

    FntPrint("\n");
	FntPrint("(?)CONTROLS\n");
    FntPrint("X: OPEN FILE IN SOUNDBANK MODE\n");
    FntPrint("CIRCLE: BACK TO MODE SELECT\n");
}

void drawVabPlayback(void)
{
    const char* note_names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = (current_note / 12) - 1;
    const char* note_name = note_names[current_note % 12];
    
    FntPrint("\n");
    FntPrint("=== VAB PLAYER ===\n\n");
    FntPrint("VH: %s @%08X sz:%04X\n", current_audio.vh_name, (unsigned int)current_audio.vh_data, (unsigned int)current_audio.vh_size);
    FntPrint("PROGRAMS: %d\n", current_audio.num_programs);
    FntPrint("TONES: %d\n\n", current_audio.num_tones);
    
    if (note_playing) {
        // Show controller note and/or MIDI poly count
        if (serial_midi_enabled && midi_poly_count > 0) {
            FntPrint("STATUS: MIDI %d voice%s\n",
                     midi_poly_count, midi_poly_count == 1 ? "" : "s");
        } else {
            FntPrint("STATUS: PLAYING\n");
        }
    } else {
        FntPrint("STATUS: STOPPED\n");
    }
    
    // Serial MIDI status line
    if (serial_midi_enabled) {
        if (midi_msg_timer > 0) {
            // Show last received MIDI message while timer is active
            FntPrint("MIDI ON: %s\n\n", midi_last_msg);
        } else {
            FntPrint("MIDI ON: LISTENING...\n\n");
        }
    } else {
        FntPrint("MIDI OFF  (START TO ENABLE)\n\n");
    }
    
    // Menu items
    FntPrint("-------------------\n");
    
    FntPrint("%c NOTE: %d (%s%d)\n",   CUR(VAB_MENU_NOTE), current_note, note_name, octave);
    FntPrint("%c PROGRAM: %d\n",        CUR(VAB_MENU_PROGRAM),      current_program);
    drawReverbRows(VAB_MENU_REV_TYPE);
    FntPrint("%c PROGRAM EDIT\n",       CUR(VAB_MENU_PROGRAM_EDIT));
    FntPrint("%c VISUALIZER\n",         CUR(VAB_MENU_VISUALIZER));
    FntPrint("-------------------\n");

	FntPrint("\n");
    FntPrint("(?)CONTROLS\n");
    FntPrint("TRIANGLE: PLAY NOTE\n");
    FntPrint("START: MIDI ON/OFF\n");
    FntPrint("L2/R2: NOTE +/-\n");
    FntPrint("L/R:-1/+1 L1/R1:-10+10\n");
    FntPrint("SQUARE: MIN/MAX\n");
    FntPrint("CIRCLE: BACK\n");
}

void drawProgramEdit(void)
{
    VabHdr* vab_hdr = (VabHdr*)current_audio.vh_data;
    
    FntPrint("\n");
    FntPrint("=== PROGRAM EDITOR ===  ");
	
	// Display playback/MIDI status indicators in the header
	if (note_playing) {
        FntPrint("*NOTE ON*");
    }
	if (is_playing && is_paused) {
        FntPrint("*PAUSED*");
    } else if (is_playing) {
        FntPrint("*PLAYING*");
    }
    FntPrint("\n");

    // Show last MIDI message if one arrived recently
    if (serial_midi_enabled && vab_mode && midi_msg_timer > 0) {
        FntPrint("RX: %s\n", midi_last_msg);
    } else {
        FntPrint("\n");
    }

    FntPrint("VH: %s\n", current_audio.vh_name);
    FntPrint("PROGRAMS: %d\n", vab_hdr->ps);
    FntPrint("TONES: %d\n", vab_hdr->ts);
    FntPrint("VAGs: %d\n\n", vab_hdr->vs);
    
	// Menu items
    FntPrint("-------------------\n");
    
    FntPrint("%c PROGRAM: %d\n", CUR(PROG_MENU_PROGRAM_SEL), edit_program);
    
    // NUM TONES (press X to edit)
    FntPrint("%c TONES: %d%s\n", CUR(PROG_MENU_NUM_TONES), current_prog_atr.tones,
             SEL(PROG_MENU_NUM_TONES, " (X:EDIT)"));
    
    FntPrint("%c PROG VOL: %d%s\n", CUR(PROG_MENU_PROG_VOL), current_prog_atr.mvol,
             CHG(isProgramValueChanged, PROG_MENU_PROG_VOL));
    FntPrint("%c PROG PAN: %d%s\n", CUR(PROG_MENU_PROG_PAN), current_prog_atr.mpan,
             CHG(isProgramValueChanged, PROG_MENU_PROG_PAN));
    FntPrint("%c MASTER VOL: %d%s\n", CUR(PROG_MENU_MASTER_VOL), vab_master_vol,
             CHG(isProgramValueChanged, PROG_MENU_MASTER_VOL));
    FntPrint("%c MASTER PAN: %d%s\n", CUR(PROG_MENU_MASTER_PAN), vab_master_pan,
             CHG(isProgramValueChanged, PROG_MENU_MASTER_PAN));
    FntPrint("-------------------\n");

    FntPrint("\n");
    FntPrint("(?)CONTROLS\n");
    if (vab_mode) {
        FntPrint("TRIANGLE: PLAY\n");
        FntPrint("L2/R2: NOTE +/-\n");
        FntPrint("START: MIDI ON/OFF\n");
    } else {
        FntPrint("TRIANGLE: PLAY/STOP\n");
        FntPrint("START: PAUSE\n");
    }
    FntPrint("CIRCLE: BACK\n");
}

void drawToneEdit(void)
{
    const char* note_names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int i, item;
    int octave;
    u_char val;
    const char* note_name;
    
    FntPrint("\n");
    FntPrint("=== TONE EDITOR ===  ");

	// Display status indicators in the header
	if (note_playing) {
        FntPrint("*NOTE ON*");
    }
	if (is_playing && is_paused) {
        FntPrint("*PAUSED*");
    } else if (is_playing) {
        FntPrint("*PLAYING*");
    }
    FntPrint("\n");

    // Show last MIDI message
    if (serial_midi_enabled && vab_mode && midi_msg_timer > 0) {
        FntPrint("RX: %s\n", midi_last_msg);
    } else {
        FntPrint("\n");
    }
    
    FntPrint("-------------------\n");
    
    // PROGRAM
    FntPrint("%c PROGRAM: %d\n", CUR(TONE_MENU_PROG), edit_program);
    
    // TONE SELECTOR (VAG number only shown while the row is selected)
    FntPrint("%c TONE: %d/%d", CUR(TONE_MENU_TONE_SEL), edit_tone, current_prog_atr.tones - 1);
    if (menu_cursor == TONE_MENU_TONE_SEL) {
        FntPrint(" VAG: %d", current_vag_atr.vag);
    }
    FntPrint("\n");
    
    // NOTE SELECTOR (VAB mode only)
    if (vab_mode) {
        octave = (current_note / 12) - 1;
        note_name = note_names[current_note % 12];
        FntPrint("%c NOTE: %d (%s%d)\n", CUR(TONE_MENU_NOTE_SEL), current_note, note_name, octave);
    }
    
    // PRIORITY, MODE, VOL, PAN, CENTER, SHIFT, MIN, MAX, PBMIN, PBMAX
    for (i = 0; i < TONE_FIELD_COUNT; i++) {
        item = TONE_MENU_PRIOR + i;
        val  = *((u_char*)&current_vag_atr + tone_fields[i].off);

        if (item == TONE_MENU_MODE) {
            FntPrint("%c %s: %s%s\n", CUR(item), tone_fields[i].label,
                     val == 0 ? "NORMAL" : "REVERB",
                     CHG(isToneValueChanged, item));
        } else if (item == TONE_MENU_CENTER) {
            // Center note also shows its note name
            FntPrint("%c %s: %d(%s%d)%s\n", CUR(item), tone_fields[i].label,
                     val, note_names[val % 12], (val / 12) - 1,
                     CHG(isToneValueChanged, item));
        } else {
            FntPrint("%c %s: %d%s\n", CUR(item), tone_fields[i].label, val,
                     CHG(isToneValueChanged, item));
        }
    }
    
    // ADSR1 / ADSR2 - Press X to enter parameter editor
    FntPrint("%c ADSR1: 0x%04X%s%s\n", CUR(TONE_MENU_ADSR1), current_vag_atr.adsr1,
             SEL(TONE_MENU_ADSR1, " (X:EDIT)"), CHG(isToneValueChanged, TONE_MENU_ADSR1));
    FntPrint("%c ADSR2: 0x%04X%s%s\n", CUR(TONE_MENU_ADSR2), current_vag_atr.adsr2,
             SEL(TONE_MENU_ADSR2, " (X:EDIT)"), CHG(isToneValueChanged, TONE_MENU_ADSR2));
    FntPrint("-------------------\n");

    FntPrint("\n");
    FntPrint("(?)CONTROLS\n");
	if (vab_mode) {
		FntPrint("L/R:-1/+1 L1/R1:-10+10 SQUARE: MIN/MAX");
		FntPrint("TRIANGLE: PLAY L2/R2: NOTE+/-\n");
		FntPrint("START: MIDI ON/OFF\n");
		FntPrint("SEL+L1/R1: PROGRAM CHANGE\n");
	} else {
		FntPrint("L/R:-1/+1 L1/R1:-10+10\n");
    	FntPrint("SQUARE: MIN/MAX\n");
		FntPrint("TRIANGLE: PLAY/STOP\n");
		FntPrint("START: PAUSE\n");
	}
	FntPrint("CIRCLE: BACK\n");
}

void drawAdsrEdit(void)
{
    int i, v;
    u_short temp_adsr1, temp_adsr2;

    FntPrint("\n");
    FntPrint("=== ADSR EDITOR ===");

	// Display playback status indicator
	if (note_playing) {
        FntPrint("*NOTE ON*");
    }
	if (is_playing && is_paused) {
        FntPrint("*PAUSED*");
    } else if (is_playing) {
        FntPrint("*PLAYING*");
    }
    FntPrint("\n");

    // Show MIDI indicator when active in VAB mode
    if (serial_midi_enabled && vab_mode && midi_msg_timer > 0) {
        FntPrint("RX: %s\n", midi_last_msg);
    } else {
        FntPrint("\n");
    }
    
    // Display current hex values
    encodeADSR(&current_adsr, &temp_adsr1, &temp_adsr2);
    FntPrint("ADSR1: 0x%04X\n", temp_adsr1);
    FntPrint("ADSR2: 0x%04X\n\n", temp_adsr2);
    
    FntPrint("PARAMETERS:\n");

	FntPrint("\n");
	FntPrint("-------------------\n");
    
    // Attack/decay/sustain/release rows (numbers, or ON/OFF for toggle rows)
    for (i = 0; i < ADSR_MENU_SAVE; i++) {
        v = *adsrFieldPtr(&current_adsr, i);
        if (adsr_fields[i].max == 1) {
            FntPrint("%c %s: %s%s\n", CUR(i), adsr_fields[i].label,
                     v ? "ON" : "OFF", CHG(isAdsrValueChanged, i));
        } else {
            FntPrint("%c %s: %d%s\n", CUR(i), adsr_fields[i].label,
                     v, CHG(isAdsrValueChanged, i));
        }
    }
    
    FntPrint("\n");
    
    FntPrint("%c SAVE\n",   CUR(ADSR_MENU_SAVE));
    FntPrint("%c CANCEL\n", CUR(ADSR_MENU_CANCEL));
	FntPrint("-------------------\n");
    
    FntPrint("\n");
    FntPrint("(?)CONTROLS\n");
    if (vab_mode) {
        FntPrint("TRIANGLE: PLAY\n");
        FntPrint("L2/R2: NOTE +/-\n");
        FntPrint("START: MIDI ON/OFF\n");
    }
    FntPrint("CIRCLE: CANCEL\n");
}

#if HAS_BACKGROUND_IMAGE
// In drawBackground() - Fix UV coordinates for 320x240:
void drawBackground(void)
{
    if (bg_state == 0) return;
    
    // Determine pixel dimensions
    int pixel_width = bg_tim.prect->w;
    int pixel_height = bg_tim.prect->h;
    
    if ((bg_tim.mode & 0x3) == 1) {  // 8-bit mode: VRAM width * 2 = pixel width
        pixel_width *= 2;
    }
    
    if (!bg_is_wide) {
        // Standard single primitive for images <= 256 pixels wide
        POLY_FT4 bg_poly;
        
        SetPolyFT4(&bg_poly);
        setRGB0(&bg_poly, 128, 128, 128);
        
        // Screen coordinates: stretch to fullscreen
        setXY4(&bg_poly, 
               0, 0,
               SCREENXRES, 0,
               0, SCREENYRES,
               SCREENXRES, SCREENYRES);
        
        // UV coordinates: full texture
        int uvw = (pixel_width > 255) ? 255 : pixel_width;
        int uvh = (pixel_height > 255) ? 255 : pixel_height;
        
        setUV4(&bg_poly,
               0, 0,
               uvw, 0,
               0, uvh,
               uvw, uvh);
        
        bg_poly.tpage = bg_tpage_left;
        if (bg_tim.mode & 0x8) {  // Has CLUT
            bg_poly.clut = bg_clut;
        }
        
        DrawPrim(&bg_poly);
    }
    else {
        // Wide image (> 256 pixels): draw two primitives side-by-side
        POLY_FT4 bg_poly_left, bg_poly_right;
        
        // LEFT PRIMITIVE: First 256 pixels (0-255)
        SetPolyFT4(&bg_poly_left);
        setRGB0(&bg_poly_left, 128, 128, 128);
        
        // Screen coordinates: left portion (0-256)
        setXY4(&bg_poly_left,
               0, 0,
               256, 0,
               0, SCREENYRES,
               256, SCREENYRES);
        
        // UV coordinates: full 256 pixels from left texture page
        int uvh = (pixel_height > 255) ? 255 : pixel_height;
        
        setUV4(&bg_poly_left,
               0, 0,
               255, 0,
               0, uvh,
               255, uvh);
        
        bg_poly_left.tpage = bg_tpage_left;
        if (bg_tim.mode & 0x8) {
            bg_poly_left.clut = bg_clut;
        }
        
        DrawPrim(&bg_poly_left);
        
        // RIGHT PRIMITIVE: Remaining pixels (256-319 = 64 pixels)
        SetPolyFT4(&bg_poly_right);
        setRGB0(&bg_poly_right, 128, 128, 128);
        
        // Screen coordinates: right portion (256-320)
        setXY4(&bg_poly_right,
               256, 0,
               SCREENXRES, 0,
               256, SCREENYRES,
               SCREENXRES, SCREENYRES);
        
        // UV coordinates: remaining pixels from right texture page
        // For 320-wide image, this is 64 pixels (0-63 in the second page)
        int uvw_right = pixel_width - 256;
        if (uvw_right > 255) uvw_right = 255;  // Safety clamp
        
        setUV4(&bg_poly_right,
               0, 0,
               uvw_right, 0,
               0, uvh,
               uvw_right, uvh);
        
        bg_poly_right.tpage = bg_tpage_right;
        if (bg_tim.mode & 0x8) {
            bg_poly_right.clut = bg_clut;
        }
        
        DrawPrim(&bg_poly_right);
    }
}
#endif // HAS_BACKGROUND_IMAGE

// ====================
// SPU Visualizer
// ====================

// Format a MIDI note number into a fixed 3-char string (e.g. "C#4", "C 4", "---")
// buf must be at least 4 bytes
static void formatVoiceNote(int midi_note, char* buf)
{
    static const char* note_names[12] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };
    const char* nn = note_names[midi_note % 12];
    int oct = (midi_note / 12) - 1;
    char oct_c;
    if      (oct < 0) oct_c = '-';
    else if (oct > 9) oct_c = '+';
    else              oct_c = (char)('0' + oct);

    if (nn[1] != '\0') {       // sharp: 2-char name
        buf[0] = nn[0]; buf[1] = nn[1]; buf[2] = oct_c;
    } else {                   // natural: 1-char name, pad with space
        buf[0] = nn[0]; buf[1] = ' ';   buf[2] = oct_c;
    }
    buf[3] = '\0';
}

// In seq mode we don't know the center of the tone that's playing a given note using a program from the VAB
// Quick solution is to display only the difference from center
static void formatNoteOffset(int offset, char* buf)
{
    int abs_off = offset < 0 ? -offset : offset;
    char sign   = offset < 0 ? '-' : '+';
    if (abs_off > 99) {
        buf[0] = sign; buf[1] = sign; buf[2] = sign;
    } else if (abs_off >= 10) {
        buf[0] = sign;
        buf[1] = (char)('0' + abs_off / 10);
        buf[2] = (char)('0' + abs_off % 10);
    } else {
        buf[0] = ' ';
        buf[1] = sign;
        buf[2] = (char)('0' + abs_off);
    }
    buf[3] = '\0';
}

//   Bar fill = Display Vol / 10  (0-100 mapped to 0-10 segments)
static void buildBar(int voice, char key_status_now, char* bar10)
{
    int i, fill;

    // latch SpuGetVoiceVolume when voice first becomes active
    if (key_status_now != SPU_OFF && vis_voice_was_on[voice] == SPU_OFF) {
        short volL, volR;
        short absL, absR, peak;
        int   mapped;
        SpuGetVoiceVolume(voice, &volL, &volR);
        absL   = (volL < 0) ? (short)(-volL) : volL;
        absR   = (volR < 0) ? (short)(-volR) : volR;
        peak   = (absL > absR) ? absL : absR;
        // Map 0-16383 -> 0-100 (Voice Map Vol)
        mapped = ((int)peak * 100) / 16383;
        if (mapped > 100) mapped = 100;
        if (mapped < 0)   mapped = 0;
        vis_voice_map_vol[voice] = mapped;
    }
    vis_voice_was_on[voice] = key_status_now;

    if (key_status_now == SPU_OFF) {
        fill = 0;
    } else {
        short envx = 0;
        int   envx_mapped, display_vol;
        // Read live ENVX value
        SpuGetVoiceEnvelope(voice, &envx);
        if (envx < 0) envx = 0;
        // Map ENVX 0-32767 -> 0-100 (Voice Map ENVX Vol)
        envx_mapped = ((int)envx * 100) / 32767;
        if (envx_mapped > 100) envx_mapped = 100;
        // Voice Display Vol = Voice Map ENVX * Voice Map Vol / 100
        display_vol = (envx_mapped * vis_voice_map_vol[voice]) / 100;
        // Map 0-100 -> 0-10 bar segments
        fill = display_vol / 10;
        if (fill > 10) fill = 10;
    }

    for (i = 0; i < 10; i++)
        bar10[i] = (i < fill) ? '#' : '.';
    bar10[10] = '\0';
}

// Fill in the key/note/bar/reverb strings for one SPU voice
static void visVoiceInfo(int v, const char* key_status, const short* note_map,
                         char* note, char* bar, char* key, const char** rev)
{
    u_short seq_note_raw;

    switch (key_status[v]) {
        case SPU_ON:
        case SPU_ON_ENV_OFF: *key = 'S'; break;
        case SPU_OFF_ENV_ON: *key = 'R'; break;
        default:             *key = '-'; break;
    }

    if (key_status[v] != SPU_OFF) {
        if (note_map[v] >= 0) {
            formatVoiceNote((int)note_map[v], note);
        } else if (!vab_mode) {
            SpuGetVoiceNote(v, &seq_note_raw);
            formatNoteOffset((int)((seq_note_raw >> 8) & 0xFF) - 0xC0, note);
        } else {
            note[0] = '-'; note[1] = '-'; note[2] = '-'; note[3] = '\0';
        }
    } else {
        note[0] = '-'; note[1] = '-'; note[2] = '-'; note[3] = '\0';
    }

    *rev = (reverb_type > 0 && key_status[v] != SPU_OFF) ? "REV" : "---";
    buildBar(v, key_status[v], bar);
}

void drawVisualizer(void)
{
    char key_status[24];
    int i;
    char note_l[4], note_r[4];
    char bar_l[11], bar_r[11];
    char key_l, key_r;
    const char *rev_l, *rev_r;

    // Build a voice-index/MIDI-note lookup SsUtKeyOn
    short voice_note_map[24];
    for (i = 0; i < 24; i++) voice_note_map[i] = -1;

    // Controller note (Triangle button)
    if (note_playing && current_voice >= 0 && current_voice < 24) {
        voice_note_map[current_voice] = current_note;
    }
    // MIDI polyphony pool
    for (i = 0; i < midi_poly_count; i++) {
        if (midi_voices[i].voice_id >= 0 && midi_voices[i].voice_id < 24) {
            voice_note_map[midi_voices[i].voice_id] = midi_voices[i].note;
        }
    }

    SpuGetAllKeysStatus(key_status);

    FntPrint("\n=== SPU VOICES ===\n\n");

    for (i = 0; i < 24; i += 2) {
        visVoiceInfo(i,     key_status, voice_note_map, note_l, bar_l, &key_l, &rev_l);
        visVoiceInfo(i + 1, key_status, voice_note_map, note_r, bar_r, &key_r, &rev_r);

        // ---- Print voice info line ----
        // Format: "V00 C#4 KEY:S REV / V01 --- KEY:- ---"
        FntPrint("V%02d %s KEY:%c %s / V%02d %s KEY:%c %s\n",
                 i,     note_l, key_l, rev_l,
                 i + 1, note_r, key_r, rev_r);

        // ---- Print volume bar line ----
        // Format: "V00[##########]VOL/V01[..........]VOL"
        FntPrint("   [%s]   /   [%s]   \n",
                 bar_l, bar_r);
    }

    // MIDI status line
    if (serial_midi_enabled) {
        FntPrint("\n");
        if (midi_msg_timer > 0) {
            FntPrint("MIDI ON(%d): %s\n", midi_poly_count, midi_last_msg);
        } else {
            FntPrint("MIDI ON  %d voice%s  LISTENING...\n",
                     midi_poly_count, midi_poly_count == 1 ? "" : "s");
        }
    }

    FntPrint("            CIRCLE: BACK\n");
}

// ====================
// Debug Menu
// ====================
// ENABLE_DEBUG_MENU must be #defined (top of file) to compile
// Toggle with Select+Start
// To display different data: edit drawDebug() below
// debug_voice is the SPU voice index of the last triggered note
#ifdef ENABLE_DEBUG_MENU

// Direct read of the hardware ENVX register (live ADSR amplitude, 0x0000-0x7FFF)
// Each voice occupies 0x10 bytes starting at SPU_VOICE_BASE
#define SPU_VOICE_BASE    0x1F801C00UL
#define SPU_VOICE_STRIDE  0x10
#define SPU_VOICE_ENV_OFF 0x0C
static unsigned short spuReadEnvelope(int voice)
{
    volatile unsigned short *reg =
        (volatile unsigned short *)(SPU_VOICE_BASE + (unsigned long)voice * SPU_VOICE_STRIDE + SPU_VOICE_ENV_OFF);
    return *reg;
}

void drawDebug(void)
{
    SpuVoiceAttr   attr;
    unsigned short live_env;
    int            midi_n, cent;
    char           nstr[4];

    FntPrint("\n");

    if (debug_voice < 0) {
        FntPrint("=== DEBUG: NO VOICE ===\n\n");
        FntPrint("No voice triggered yet.\n");
        if (vab_mode) {
            FntPrint("TRI:PLAY  CIR:BACK  SEL+STA+L1+R1:BACK\n");
        } else {
            FntPrint("TRI:PLAY/STP  CIR:BACK  SEL+STA+L1+R1:BACK\n");
        }
        return;
    }

    // Fetch all live attributes for the tracked voice
    // mask = 0 reads every field per the SDK docs
    attr.voice = (u_long)(1UL << debug_voice);
    attr.mask  = 0;
    SpuGetVoiceAttr(&attr);

    // Hardware ENVX register
    live_env = spuReadEnvelope(debug_voice);

    FntPrint("=== DEBUG: VOICE %02d ===\n\n", debug_voice);

    // Volume channels
    FntPrint("VOL  L:%6d R:%6d\n", attr.volume.left,  attr.volume.right);
    FntPrint("VMOD L:%6d R:%6d\n", attr.volmode.left, attr.volmode.right);
    FntPrint("VOLX L:%6d R:%6d\n", attr.volumex.left, attr.volumex.right);

    // Pitch and envelope level
    FntPrint("PITCH:0x%04X  ENVX:%5d\n", attr.pitch, attr.envx);
    FntPrint("ENV(hw):0x%04X\n", live_env);

    // Upper 8 bits = MIDI note, lower 8 bits = fine cents
    midi_n = (attr.note >> 8) & 0xFF;
    cent   =  attr.note       & 0xFF;
    formatVoiceNote(midi_n, nstr);
    FntPrint("NOTE: 0x%04X (%s +%d)\n", attr.note, nstr, cent);
    FntPrint("SNOT: 0x%04X\n", attr.sample_note);

    // Waveform addresses
	// note: I guess this could be used to track the vags and know which tone is used to play a note
	// to display the correct note in the SEQ player visualizer
    FntPrint("ADDR: 0x%08X\n", (unsigned int)attr.addr);
    FntPrint("LADR: 0x%08X\n", (unsigned int)attr.loop_addr);

    // Envelope mode flags (0=lin-inc-norm, 1=lin-inc-rev, 2=exp-inc, 3=exp-dec)
    FntPrint("AMOD:%ld SMOD:%ld RMOD:%ld\n", attr.a_mode, attr.s_mode, attr.r_mode);

    // ADSR rate and level fields
    FntPrint("AR:%3u DR:%3u SR:%3u\n", attr.ar, attr.dr, attr.sr);
    FntPrint("RR:%3u SL:%3u\n",        attr.rr, attr.sl);

    // Raw packed ADSR words (matches VagAtr adsr1/adsr2 layout)
    FntPrint("ADSR1:0x%04X ADSR2:0x%04X\n", attr.adsr1, attr.adsr2);

    if (vab_mode) {
        FntPrint("\nTRI:PLAY(HOLD)  CIR:BACK  SEL+STA+L1+R1:BACK\n");
    } else {
        FntPrint("\nTRI:PLAY/STP  CIR:BACK  SEL+STA+L1+R1:BACK\n");
    }
}

#endif // ENABLE_DEBUG_MENU

void drawUI(void)
{
#if HAS_BACKGROUND_IMAGE
    // Don't draw UI text when in full image mode (bg_state == 2)
    if (bg_state == 2) {
        return;
    }
#endif
    
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
        case STATE_ADSR_EDIT:
            drawAdsrEdit();
            break;
        case STATE_VISUALIZER:
            drawVisualizer();
            break;
#ifdef ENABLE_CD_BROWSER
        case STATE_CD_BROWSER:
            drawCdBrowser();
            break;
#endif
#ifdef ENABLE_DEBUG_MENU
        case STATE_DEBUG:
            drawDebug();
            break;
#endif
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
        
#if HAS_BACKGROUND_IMAGE
        drawBackground();  // Draw background image first if enabled
#endif
				
        drawUI();
				
#if HAS_BACKGROUND_IMAGE
        // Only flush font buffer when not in full image mode
        if (bg_state != 2) {
            FntFlush(-1);
        }
#else
        FntFlush(-1);
#endif
        
        display();
        
        //ONLY USE THIS COMMAND IF NOTICK IS ENABLED
		//otherwise everything plays fast

        //SsSeqCalledTbyT();
    }
    
    return 0;
}
