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
- Run make in the seq_player directory
- Note: If the program does not compile due to a FntPrint error, add an ellipsis (...) to the FntPrint definition in [libgpu.h](https://github.com/johnbaumann/psyq_include_what_you_use/blob/5cbf9f68d10490949b43b52846dae8a6383d5c55/include/libgpu.h#L724) on the psyq/include directory. Line 724 should look like this ```extern int FntPrint(...);```
## Usage
- Place the .seq files in the SEQ directory. (default max 5 files, can be increased)
- Place the .vh and .vb files in the respective SOUNDBANK/VH and SOUNDBANK/VB directories. (default max 5 files, more and bigger files make upload to console via serial take considerable time)
- Ensure that the .vh and .vb files have matching filenames.
- Build the ps-exe.
- Upload to a console via serial with nops or run ps-exe with Duckstation.

## Functionality
- Plays .seq files with a selected soundbank, can change soundbank parameters during playback. Some parameters like reverb type will require playback to restart.
- Can play single notes using data from a soundbank.
- In both modes the Program Editor can be selected to edit program settings, selecting a tone will open the Tone Editor to edit Tone settings.
- ADSR numeric hex value can be edited.
- Changes are volatile in the RAM, reloading the soundbank may undo any changes.
## Video
https://www.youtube.com/watch?v=wyz4xGdSDhg
