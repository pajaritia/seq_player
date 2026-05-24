# seq_player
Playstation 1 sound format player (*.seq *.vh *.vb)

## Building instructions
- Clone to your working directory this repo https://github.com/ABelliqueux/nolibgs_hello_worlds
- Follow the instructions in the link to set it up and ensure everything is working.
- (Unsure if this step is required) Replace the PSYQ sdk include folder with the one from this other repo https://github.com/johnbaumann/psyq_include_what_you_use/tree/master
- Important step: Add the required file formats to the common.mk file in nolibgs_hello_worlds directory, paste the following lines at the end of the file (under the other PS1 file format entries)

```
# convert SEQ files to bin  
%.o: %.seq
    $(call OBJCOPYME)

# convert VH files to bin
%.o: %.vh
    $(call OBJCOPYME)

# convert VB files to bin
%.o: %.vb
    $(call OBJCOPYME)
```
- Clone this repo and place the folder in the working directory (seq_player directory is in the same directory as all the individual nolibgs_hello_worlds examples)
- If file structure has changed, delete fileconfig.h to create updated file
- Run make in the seq_player directory
- Note: If the program does not compile due to a FntPrint error, add an ellipsis (...) to the FntPrint definition in [libgpu.h](https://github.com/johnbaumann/psyq_include_what_you_use/blob/5cbf9f68d10490949b43b52846dae8a6383d5c55/include/libgpu.h#L724) on the psyq/include directory. Line 724 should look like this ```extern int FntPrint(...);```
## Usage
- Place the .seq files in the SEQ directory. (default max 5 files, can be increased)
- Place the .vh and .vb files in the respective SOUNDBANK/VH and SOUNDBANK/VB directories. (default max 5 files, more and bigger files make upload to console via serial take considerable time)
- Ensure that the .vh and .vb files have matching filenames.
- Build the ps-exe.
- Upload to a console via serial with nops or run ps-exe with Duckstation.

## Serial MIDI implementation:
- Pressing Start in VAB Mode will open a serial connection that can decode MIDI messages.
- Baud Rate for the serial connection is hardcoded, can be configured in line 247 of seq_player.c
- How to use:
  Requirements:
	- A Playstation console with a serial connection.
	- A way to run the ps-exe in the console
	- Something that outputs MIDI messages via a serial connection:
- How I tested this (description of my setup):
	- A SCPH-7501 console connected via the serial port to a RP2040 Zero
 	- The RP2040 Zero is connected via USB to a Windows PC, running version 4.2 of this firmware: https://github.com/Noltari/pico-uart-bridge
	-I am using `nops /fast /exe seq_player.exe COMport` to upload and run the ps-exe on the console.
	-A USB MIDI controller connected to the same Windows PC, sending the MIDI messages to the COMport via serial using this program: https://github.com/ezequielabregu/EA-serialmidi-bridge
- List of supported MIDI messages:
	- Note ON + Velocity
	- Note OFF + Velocity
	- Pitch Bend
	- Program Volume (CC 7)
	- Program Pan (CC 10)
	- NRPN messages
		```
  		ATTRIBUTE 			Data1 (CC99) 	Data2 (CC98)	Data3 (CC06)
		Attack Rate		Tone Number		4				0~127
		Attack Exp				"			5				0~127
		Delay Rate				"			6				0~127
		Sustain Level			"			7				0~127
		Sustain Rate			"			8				0~127
		Sustain Exp			"			9				0~127
		Release Rate			"			10				0~127
		Release Exp			"			11				0~127
		Sustain Sign			"			12				0~64=+ 65~127=–
			13, 14 are vibrato and portamento messages, not implemented in the PsyQ SDK :(
		Reverb type 			16			15				0~9
		Reverb depth 			16			16				0~127
		Echo feedback 			16			17				0~127
		Echo delay time 		16			18				0~127
		Delay time				16			19				0~127```

## Functionality
- Plays .seq files with a selected soundbank, can change soundbank parameters during playback. Some parameters like reverb type will require playback to restart.
- Can play single notes using data from a soundbank.
- In both modes the Program Editor can be selected to edit program settings, selecting a tone will open the Tone Editor to edit Tone settings.
- ADSR values can be edited.
- Changes are volatile in the RAM, reloading the soundbank may undo any changes. (This may no longer occur, uncertain)

## Optional features
- A background image can be provided by placing a 16bpp, 320x240 pixel resolution .TIM image in the IMG directory. (/IMG/image.tim)
- If an image is detected the program will load it into VRAM
- Pressing SELECT in the initial screen, the SEQ playback screen or the VAB playback screen will toggle between 3 background modes (no image, image + program text, image only)

## Video
https://www.youtube.com/watch?v=wyz4xGdSDhg
