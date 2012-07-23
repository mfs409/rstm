#
#  Copyright (C) 2011
#  University of Rochester Department of Computer Science
#    and
#  Lehigh University Department of Computer Science and Engineering
# 
# License: Modified BSD
#          Please see the file LICENSE.RSTM for licensing information

#
# The top-level RSTM makefile simply forwards to a makefile that is specific
# to a particular build platform.  We use the MAKEFILES environment variable
# to achieve this end without significant makefile wizardry.
#

#
# Location of helper build files
#
MKFOLDER = build
include $(MKFOLDER)/info.mk

#
# simply typing 'make' dumps a message, rather than trying to guess a default
# platform
#
default: info

#
# Perform a build in the lib/ folder, and then a build in the bench/ folder,
# using the MAKEFILES envar to specify the platform.
#
# $(MAKE) will error unless the platform's corresponding definitions are in
# $(MKFOLDER)
#
%: $(MKFOLDER)/%.mk
	@cd libstm && $(MAKE) $@
	@cd bench && $(MAKE) $@
clean:
	@rm -rf libstm/obj.*
	@rm -rf bench/obj.* 
