#!/usr/bin/perl
#------------------------------------------
# Default Parameters
#------------------------------------------
$LOOPS = (1000000*8);
$A2 = 32*1024;          # 32 * 1024 * sizeof(long)
$N = 8;
$P = 0.1;
$LEN = 100;
$W2 = $LEN * $P;        # 10
$R2 = $LEN - $W2;       # 90
@opt_names = ("N", "LOOPS", "A1", "A2",   "A3", "R1", "W1", "R2", "W2", 
        "R3i", "W3i", "R3o", "W3o", "NOPi", "NOPo", "Ki", "Ko", "LCT", "PERSIST");
@def_vals = ($N,   $LOOPS,   0,   $A2,      0,   0,    0,   $R2,   $W2,
         0,     0,     0,     0,     0,      0,      0,    0,     0,    0);

$EXEDIR = "../../src/";
