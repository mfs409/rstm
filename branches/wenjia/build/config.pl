#!/usr/bin/perl

# Note: this is a pretty dirty program.  It doesn't check input at all!

use strict;
use warnings;

my @LINES = (); # these are the lines of the .mk file
my $FNAME = ""; # the name of the .mk file
my $PLATFORM = ""; # the build folder

# handle the flags related to the compiler.  Right now, the only choices are
# gcc and gcctm
sub compiler {
    my $option=shift;
    if ($option =~ "gcctm") {
        push(@LINES, "# COMPILER = GCCTM"); 
        $PLATFORM .= "gcctm_"; # include gcctm in the platform description
        push(@LINES, "CC = gcc"); # use gcc for c and asm code
        push(@LINES, "CXX = g++"); # use g++ for c++ code
        push(@LINES, "CXXFLAGS += -Wall -Wextra #-Werror"); # why not?
        push(@LINES, "CXXFLAGS += -ggdb"); # turn on debug symbols
        push(@LINES, "LDFLAGS  += -lrt -lpthread"); # ensure we'll link correctly
        push(@LINES, "CXXFLAGS += -fgnu-tm"); # turn on gcctm suuport
        push(@LINES, "LDFLAGS  += -fgnu-tm"); # turn on gcctm support
        push(@LINES, "CXXFLAGS += -DSTM_CC_GCC"); # indicate compiler
        push(@LINES, "CXXFLAGS += -DSTM_API_GCCTM"); # we're using the C++ TM API
        push(@LINES, "CXXFLAGS += -DSTM_WS_BYTELOGLOG"); # needed for gcctm
        push(@LINES, "LIB_OVERRIDE = true"); # not sure we still need this...
    }
    elsif ($option =~ "gcc") {
        push(@LINES, "# COMPILER = GCC"); 
        $PLATFORM .= "gcc_"; # include gcc in the platform description
        push(@LINES, "CC = gcc"); # use gcc for c and asm code
        push(@LINES, "CXX = g++"); # use g++ for c++ code
        push(@LINES, "CXXFLAGS += -Wall -Wextra #-Werror"); # why not?
        push(@LINES, "CXXFLAGS += -ggdb"); # turn on debug symbols
        push(@LINES, "LDFLAGS  += -lrt -lpthread"); # ensure we'll link correctly
        push(@LINES, "CXXFLAGS += -DSTM_CC_GCC"); # indicate compiler
        push(@LINES, "CXXFLAGS += -DSTM_API_LIB"); # we're not using the C++ TM API
        push(@LINES, "CXXFLAGS += -DSTM_WS_WORDLOG"); # default is word writeset
    }
    else { die $option . "is invalid" }
}

# handle the flags related to the OS.  Right now, we support Linux and Solaris
sub os {
    my $option = shift;
    if ($option =~ "linux") {
        push(@LINES, "# OS = LINUX"); 
        $PLATFORM .= "linux_";
        push(@LINES, "ASFLAGS  += -DSTM_OS_LINUX");
        push(@LINES, "CXXFLAGS += -DSTM_OS_LINUX");
    }
    elsif ($option =~ "solaris") {
        push(@LINES, "# OS = SOLARIS"); 
        $PLATFORM .= "solaris_";
        push(@LINES, "ASFLAGS  += -DSTM_OS_SOLARIS");
        push(@LINES, "CXXFLAGS += -DSTM_OS_SOLARIS");
        push(@LINES, "LDFLAGS  += -lmtmalloc");
    }
    else { die $option . "is invalid" }
    push(@LINES, "CXXFLAGS += -DSTM_TLS_GCC"); # If we re-add MacOS support, this will change
}

# handle flags related to CPU
sub cpu {
    my $option = shift;
    if ($option =~ "ia32") {
        push(@LINES, "# CPU = IA32"); 
        $PLATFORM .= "ia32_";
        push(@LINES, "ASFLAGS += -DSTM_CPU_X86");
        push(@LINES, "CXXFLAGS += -DSTM_CPU_X86");
        push(@LINES, "ASFLAGS += -m32 -DSTM_BITS_32");
        push(@LINES, "CFLAGS += -m32");
        push(@LINES, "LDFLAGS += -m32");
        push(@LINES, "CXXFLAGS += -m32 -DSTM_BITS_32");
        push(@LINES, "CXXFLAGS += -march=native -mtune=native -msse2 -mfpmath=sse -DSTM_USE_SSE");
    }
    elsif ($option =~ "amd64") {
        push(@LINES, "# CPU = AMD64"); 
        $PLATFORM .= "amd64_";
        push(@LINES, "ASFLAGS += -DSTM_CPU_X86");
        push(@LINES, "CXXFLAGS += -DSTM_CPU_X86");
        push(@LINES, "ASFLAGS += -m64 -DSTM_BITS_64");
        push(@LINES, "CFLAGS += -m64");
        push(@LINES, "LDFLAGS += -m64");
        push(@LINES, "CXXFLAGS += -m64 -DSTM_BITS_64");
        push(@LINES, "CXXFLAGS += -march=native -mtune=native -msse2 -mfpmath=sse -DSTM_USE_SSE");
    }
    elsif ($option =~ "sparc32") {
        push(@LINES, "# CPU = SPARC32"); 
        $PLATFORM .= "sparc32_";
        push(@LINES, "ASFLAGS += -DSTM_CPU_SPARC");
        push(@LINES, "CXXFLAGS += -DSTM_CPU_SPARC");
        push(@LINES, "ASFLAGS += -m32 -DSTM_BITS_32");
        push(@LINES, "CFLAGS += -m32");
        push(@LINES, "LDFLAGS += -m32");
        push(@LINES, "CXXFLAGS += -m32 -DSTM_BITS_32");
        push(@LINES, "CXXFLAGS += -mcpu=native -mtune=native");
    }
    elsif ($option =~ "sparc64") {
        push(@LINES, "# CPU = SPARC64"); 
        $PLATFORM .= "sparc64_";
        push(@LINES, "ASFLAGS += -DSTM_CPU_SPARC");
        push(@LINES, "CXXFLAGS += -DSTM_CPU_SPARC");
        push(@LINES, "ASFLAGS += -m64 -DSTM_BITS_64");
        push(@LINES, "CFLAGS += -m64");
        push(@LINES, "LDFLAGS += -m64");
        push(@LINES, "CXXFLAGS += -m64 -DSTM_BITS_64");
        push(@LINES, "CXXFLAGS += -mcpu=native -mtune=native");
    }
    elsif ($option =~ "armv7") {
        push(@LINES, "# CPU = ARMV7"); 
        $PLATFORM .= "armv7_";
        push(@LINES, "ASFLAGS += -m32 -DSTM_BITS_32");
        push(@LINES, "CFLAGS += -m32");
        push(@LINES, "LDFLAGS += -m32");
        push(@LINES, "CXXFLAGS += -DSTM_BITS_32");
        push(@LINES, "CXXFLAGS += -DSTM_CPU_ARMV7");
        push(@LINES, "ASFLAGS += -DSTM_CPU_ARMV7");
        push(@LINES, "CXXFLAGS += -march=armv7-a");
    }
    else { die $option . "is invalid" }
}

# handle flags related to optimization level
sub optimization {
    my $option = shift;
    if ($option =~ "opt") {
        push(@LINES, "# OPTIMIZATION = O3"); 
        $PLATFORM .= "opt_";
        push(@LINES, "CXXFLAGS += -O3");
    }
    elsif ($option =~ "dbg") {
        push(@LINES, "# OPTIMIZATION = O0"); 
        $PLATFORM .= "dbg_";
        push(@LINES, "CXXFLAGS += -O0");
    }
    else { die $option . "is invalid" }
}

# handle flags related to adaptivity
sub adapt {
    my $option = shift;
    if ($option =~ "none") {
        push(@LINES, "# ADAPT = NONE"); 
        $PLATFORM .= "noadapt_";
        push(@LINES, "CXXFLAGS += -DSTM_PROFILETMTRIGGER_NONE");
    }
    elsif ($option =~ "pathology") {
        push(@LINES, "# ADAPT = PATHOLOGY"); 
        $PLATFORM .= "pathologyadapt_";
        push(@LINES, "CXXFLAGS += -DSTM_PROFILETMTRIGGER_PATHOLOGY");
    }
    elsif ($option =~ "all") {
        push(@LINES, "# ADAPT = ALL"); 
        $PLATFORM .= "alladapt_";
        push(@LINES, "CXXFLAGS += -DSTM_PROFILETMTRIGGER_ALL");
    }
    else { die $option . "is invalid" }
}

sub checkpt {
    my $option = shift;
    if ($option =~ "asm") {
        push(@LINES, "# CHECKPOINT = ASM"); 
        $PLATFORM .= "chkptasm_";
        push(@LINES, "CHECKPOINT = asm");
        push(@LINES, "CXXFLAGS += -DSTM_CHECKPOINT_ASM");
    }
    elsif ($option =~ "sjlj") {
        push(@LINES, "# CHECKPOINT = SJLJ"); 
        $PLATFORM .= "chkptsjlj_";
        push(@LINES, "CXXFLAGS += -DSTM_CHECKPOINT_SJLJ");
    }
    else { die $option . "is invalid" }
}

sub descriptor {
    my $option = shift;
    if ($option =~ "tls") {
        push(@LINES, "# DESCRIPTOR = TLS"); 
        $PLATFORM .= "descriptls_";
        push(@LINES, "CXXFLAGS += -DSTM_API_NOTLSPARAM");
    }
    elsif ($option =~ "extraparameter") {
        push(@LINES, "# DESCRIPTOR = EXTRA PARAMETER"); 
        $PLATFORM .= "descripextra_";
        push(@LINES, "CXXFLAGS += -DSTM_API_TLSPARAM");
    }
    else { die $option . "is invalid" }
}

sub pmu {
    my $option = shift;
    if ($option =~ "on") {
        push(@LINES, "# PMU = ON"); 
        $PLATFORM .= "pmuon_";
        push(@LINES, "PAPI_DIR=/path/to/papi-4.4.0-32/");
        print ("Note: be sure to edit the output file to specify PAPI_DIR correctly!\n");
        push(@LINES, "CXXFLAGS += -DSTM_USE_PMU");
        push(@LINES, "LDFLAGS += -L$(PAPI_DIR)/lib");
        push(@LINES, "LDFLAGS += -lpapi");
        push(@LINES, "CXXFLAGS += -I$(PAPI_DIR)/include");
    }
    elsif ($option =~ "off") {
        push(@LINES, "# PMU = OFF"); 
        $PLATFORM .= "pmuoff_";
        push(@LINES, "CXXFLAGS += -DSTM_NO_PMU");
    }
    else { die $option . "is invalid" }
}

sub toxic {
    my $option = shift;
    if ($option =~ "on") {
        push(@LINES, "# TOXIC = ON"); 
        $PLATFORM .= "toxicon_";
        push(@LINES, "CXXFLAGS += -DSTM_COUNTCONSEC_YES");
    }
    elsif ($option =~ "off") {
        push(@LINES, "# TOXIC = OFF"); 
        $PLATFORM .= "toxicoff_";
        push(@LINES, "CXXFLAGS += -DSTM_COUNTCONSEC_NO");
    }
    else { die $option . "is invalid" }
}

sub instrumentation {
    my $option = shift;
    if ($option =~ "fgadapt") {
        push(@LINES, "# INSTRUMENTATION = FINE GRAINED ADAPTIVITY"); 
        $PLATFORM .= "fgadapt_";
        push(@LINES, "CXXFLAGS += -DSTM_INST_FINEGRAINADAPT");
    }
    elsif ($option =~ "cgadapt") {
        push(@LINES, "# INSTRUMENTATION = COARSE GRAINED ADAPTIVITY");  
        $PLATFORM .= "cgadapt_";
        push(@LINES, "CXXFLAGS += -DSTM_INST_COARSEGRAINADAPT");
    }
    elsif ($option =~ "switch") {
        push(@LINES, "# INSTRUMENTATION = SWITCH-BASED ADAPTIVITY");  
        $PLATFORM .= "switchadapt_";
        push(@LINES, "CXXFLAGS += -DSTM_INST_SWITCHADAPT");
    }
    else {
        push(@LINES, "# INSTRUMENTATION = ONESHOT ($option)"); 
        $PLATFORM .= "oneshot${option}_";
        push(@LINES, "CXXFLAGS += -DSTM_INST_ONESHOT");
        push(@LINES, "CXXFLAGS += -DSTM_ONESHOT_ALG_$option");
    }
}

# [mfs] I found this option somewhere, but I don't know what it is for, so I
# didn't set it up.
#
# #CXXFLAGS += -DSTM_USE_WORD_LOGGING_VALUELIST

#
# hack: this is a global variable used by query
#
my @options = ();

#
# get user input
#
sub query {
    print "\n" . shift() . ":\n";
    for my $o (@options) { print "  " . $o . "\n" }
    print "\nType your choice here :> ";
    my $answer = <STDIN>;
    chomp ($answer);
    return $answer;
}

#
# here are our nasty queries
#
@options = qw(gcc gcctm);
compiler(&query("Choose a compiler"));

@options = qw(linux solaris);
os(&query("Choose an os"));

@options = qw(ia32 amd64 sparc32 sparc64 armv7);
cpu(&query("Choose a cpu"));

@options = qw(opt dbg);
optimization(&query("Choose an optimization level"));

@options = qw(none pathology all);
adapt(&query("Choose an adaptivity level"));

@options = qw(sjlj asm);
checkpt(&query("Choose a checkpoint mechanism"));

@options = qw(tls extraparameter);
descriptor(&query("Choose a descriptor access mechanism"));

@options = qw(off on);
pmu(&query("Do you want PMU support"));

toxic(&query("Do you want to count Toxic Transactions"));


### [mfs] I think the rest of these shall be subsumed
@options = ("fgadapt - fine-grained adaptivity: per-thread function pointers",
            "cgadapt - coarse adaptivity: function pointers, but not per-thread",
            "switch  - No function pointers: use a big switch statement for adapting",
            "<algname> - Enter the name of the only algorithm to ever use");
instrumentation(&query("How would you like to configure instrumentation?"));

# get name of output file, dump output
print "\nEnter the name of the output file :> ";
$FNAME = <STDIN>;
open(MKFILE, ">$FNAME");
print MKFILE "PLATFORM = " . $PLATFORM . "\n";
for my $l (@LINES) { print MKFILE $l . "\n"; }
