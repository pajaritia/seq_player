TARGET = seq_player

# Check for uppercase extension files and rename them to lowercase
UPPERCASE_SEQ := $(wildcard ./SEQ/*.SEQ)
UPPERCASE_VH := $(wildcard ./SOUNDBANK/VH/*.VH)
UPPERCASE_VB := $(wildcard ./SOUNDBANK/VB/*.VB)

# Rename uppercase extensions to lowercase if found
ifneq ($(UPPERCASE_SEQ),)
$(info Found uppercase .SEQ files, renaming to lowercase...)
$(foreach file,$(UPPERCASE_SEQ),$(shell mv "$(file)" "$(dir $(file))$(basename $(notdir $(file))).seq"))
endif

ifneq ($(UPPERCASE_VH),)
$(info Found uppercase .VH files, renaming to lowercase...)
$(foreach file,$(UPPERCASE_VH),$(shell mv "$(file)" "$(dir $(file))$(basename $(notdir $(file))).vh"))
endif

ifneq ($(UPPERCASE_VB),)
$(info Found uppercase .VB files, renaming to lowercase...)
$(foreach file,$(UPPERCASE_VB),$(shell mv "$(file)" "$(dir $(file))$(basename $(notdir $(file))).vb"))
endif

# Automatically detect SEQ and VH files (limit to 5 each)
SEQ_FILES := $(wildcard ./SEQ/*.seq)
VH_FILES := $(wildcard ./SOUNDBANK/VH/*.vh)

# Limit to maximum 5 files each
SEQ_FILES := $(wordlist 1,5,$(SEQ_FILES))
VH_FILES := $(wordlist 1,5,$(VH_FILES))

# Check minimum requirement (at least 1 of each)
ifeq ($(words $(SEQ_FILES)),0)
$(error No .seq files found in ./SEQ/ directory. At least 1 is required.)
endif
ifeq ($(words $(VH_FILES)),0)
$(error No .vh files found in ./SOUNDBANK/VH/ directory. At least 1 is required.)
endif

# Generate corresponding VB file paths
VB_FILES := $(patsubst ./SOUNDBANK/VH/%.vh,./SOUNDBANK/VB/%.vb,$(VH_FILES))

# Verify VB files exist
$(foreach vb,$(VB_FILES),$(if $(wildcard $(vb)),,$(error Missing VB file: $(vb))))

# Build SRCS list
SRCS = seq_player.c $(SEQ_FILES) $(VH_FILES) $(VB_FILES)

# Debug: show what will be built
$(info SRCS: $(SRCS))
$(info Will build OBJS from these SRCS)

# Generate fileconfig.h if it doesn't exist (during Makefile parsing)
ifeq ($(wildcard fileconfig.h),)
$(info Generating fileconfig.h for first time...)
$(shell echo "// Auto-generated file - do not edit manually" > fileconfig.h)
$(shell echo "// Generated from Makefile based on detected files" >> fileconfig.h)
$(shell echo "" >> fileconfig.h)
$(shell echo "#ifndef FILECONFIG_H" >> fileconfig.h)
$(shell echo "#define FILECONFIG_H" >> fileconfig.h)
$(shell echo "" >> fileconfig.h)
$(shell echo "// File counts" >> fileconfig.h)
$(shell echo "#define MAX_SEQ_FILES $(words $(SEQ_FILES))" >> fileconfig.h)
$(shell echo "#define MAX_VH_FILES $(words $(VH_FILES))" >> fileconfig.h)
$(shell echo "" >> fileconfig.h)
$(shell echo "// Extern declarations for SEQ files" >> fileconfig.h)
$(foreach seq,$(SEQ_FILES),$(shell echo 'extern u_char _binary_SEQ_$(basename $(notdir $(seq)))_seq_start[];' >> fileconfig.h))
$(shell echo "" >> fileconfig.h)
$(shell echo "// Extern declarations for VH files" >> fileconfig.h)
$(foreach vh,$(VH_FILES),$(shell echo 'extern u_char _binary_SOUNDBANK_VH_$(basename $(notdir $(vh)))_vh_start[];' >> fileconfig.h))
$(shell echo "" >> fileconfig.h)
$(shell echo "// Extern declarations for VB files" >> fileconfig.h)
$(foreach vb,$(VB_FILES),$(shell echo 'extern u_char _binary_SOUNDBANK_VB_$(basename $(notdir $(vb)))_vb_start[];' >> fileconfig.h))
$(shell echo "" >> fileconfig.h)
$(shell echo "// SEQ file initialization array" >> fileconfig.h)
$(shell echo "#define SEQ_FILES_INIT { \\" >> fileconfig.h)
$(foreach seq,$(SEQ_FILES),$(shell echo "    {\"$(notdir $(seq))\", _binary_SEQ_$(basename $(notdir $(seq)))_seq_start, 0, 0}, \\" >> fileconfig.h))
$(shell echo "}" >> fileconfig.h)
$(shell echo "" >> fileconfig.h)
$(shell echo "// VH file initialization array" >> fileconfig.h)
$(shell echo "#define VH_FILES_INIT { \\" >> fileconfig.h)
$(foreach vh,$(VH_FILES),$(shell echo "    {\"$(notdir $(vh))\", _binary_SOUNDBANK_VH_$(basename $(notdir $(vh)))_vh_start, 0, 1}, \\" >> fileconfig.h))
$(shell echo "}" >> fileconfig.h)
$(shell echo "" >> fileconfig.h)
$(shell echo "// VB file initialization array" >> fileconfig.h)
$(shell echo "#define VB_FILES_INIT { \\" >> fileconfig.h)
$(foreach vb,$(VB_FILES),$(shell echo "    _binary_SOUNDBANK_VB_$(basename $(notdir $(vb)))_vb_start, \\" >> fileconfig.h))
$(shell echo "}" >> fileconfig.h)
$(shell echo "" >> fileconfig.h)
$(shell echo "#endif // FILECONFIG_H" >> fileconfig.h)
$(info Detected $(words $(SEQ_FILES)) SEQ files and $(words $(VH_FILES)) VH/VB pairs)
endif

# Ensure fileconfig.h exists before compilation
seq_player.o: fileconfig.h
seq_player.dep: fileconfig.h

# Target to regenerate fileconfig.h when Makefile changes
fileconfig.h: Makefile
	@echo "Regenerating fileconfig.h..."
	@echo "// Auto-generated file - do not edit manually" > $@
	@echo "// Generated from Makefile based on detected files" >> $@
	@echo "" >> $@
	@echo "#ifndef FILECONFIG_H" >> $@
	@echo "#define FILECONFIG_H" >> $@
	@echo "" >> $@
	@echo "// File counts" >> $@
	@echo "#define MAX_SEQ_FILES $(words $(SEQ_FILES))" >> $@
	@echo "#define MAX_VH_FILES $(words $(VH_FILES))" >> $@
	@echo "" >> $@
	@echo "// Extern declarations for SEQ files" >> $@
	@$(foreach seq,$(SEQ_FILES),echo 'extern u_char _binary_SEQ_$(basename $(notdir $(seq)))_seq_start[];' >> $@;)
	@echo "" >> $@
	@echo "// Extern declarations for VH files" >> $@
	@$(foreach vh,$(VH_FILES),echo 'extern u_char _binary_SOUNDBANK_VH_$(basename $(notdir $(vh)))_vh_start[];' >> $@;)
	@echo "" >> $@
	@echo "// Extern declarations for VB files" >> $@
	@$(foreach vb,$(VB_FILES),echo 'extern u_char _binary_SOUNDBANK_VB_$(basename $(notdir $(vb)))_vb_start[];' >> $@;)
	@echo "" >> $@
	@echo "// SEQ file initialization array" >> $@
	@echo "#define SEQ_FILES_INIT { \\" >> $@
	@$(foreach seq,$(SEQ_FILES),echo "    {\"$(notdir $(seq))\", _binary_SEQ_$(basename $(notdir $(seq)))_seq_start, 0, 0}, \\" >> $@;)
	@echo "}" >> $@
	@echo "" >> $@
	@echo "// VH file initialization array" >> $@
	@echo "#define VH_FILES_INIT { \\" >> $@
	@$(foreach vh,$(VH_FILES),echo "    {\"$(notdir $(vh))\", _binary_SOUNDBANK_VH_$(basename $(notdir $(vh)))_vh_start, 0, 1}, \\" >> $@;)
	@echo "}" >> $@
	@echo "" >> $@
	@echo "// VB file initialization array" >> $@
	@echo "#define VB_FILES_INIT { \\" >> $@
	@$(foreach vb,$(VB_FILES),echo "    _binary_SOUNDBANK_VB_$(basename $(notdir $(vb)))_vb_start, \\" >> $@;)
	@echo "}" >> $@
	@echo "" >> $@
	@echo "#endif // FILECONFIG_H" >> $@
	@echo "Detected $(words $(SEQ_FILES)) SEQ files and $(words $(VH_FILES)) VH/VB pairs"

# Ensure the default goal is 'all' from common.mk
.DEFAULT_GOAL := all

# Make fileconfig.h a prerequisite of all
all: fileconfig.h

include ../common.mk	