#!/usr/bin/perl

#----------------------------------------
# Read Default Parameters
#----------------------------------------
do "./default.pl";

#----------------------------------
# Setup output directory
#----------------------------------
$OUTDIR = "output_locality";
if (!(-d $OUTDIR)) {
    mkdir $OUTDIR or die $!;
}


#------------------------------------
# Create Input file and Execute
#------------------------------------
$var_name = "LCT";
#@var_vals = (0, 128, 256, 384, 512, 640, 768, 896, 1024); #  x / 1024
@var_vals = (1024);

for($j = 0; $j<=$#var_vals; $j=$j+1)
{
    
    open (PARAMFILE, ">$OUTDIR/param.txt") or die $!;
    print PARAMFILE "#------------------------\n";
    print PARAMFILE "# Default Parameter Values \n";
    print PARAMFILE "#------------------------\n";
    for($i = 0; $i <=$#opt_names; $i= $i+1)
    {
        print PARAMFILE "$opt_names[$i] $def_vals[$i] \n";
    }
    print PARAMFILE "#------------------------\n";
    print PARAMFILE "# Explored Parameters\n";
    print PARAMFILE "#------------------------\n";

    $L = $LOOPS / $N;
    $LCT = $var_vals[$j];
    print PARAMFILE "LCT $LCT \n";
    print PARAMFILE "LOOPS $L\n";

    close(PARAMFILE);

    $NAME = sprintf("%04d", $LCT/1024*1000); 
    print("running $var_name: $NAME");

    #------------------------------------------
    # Run Parallel Execution
    #------------------------------------------
    system ("$EXEDIR/eigenbench-unp $OUTDIR/param.txt > $OUTDIR/unp_${var_name}_$NAME");
    system ("$EXEDIR/eigenbench-tl2 $OUTDIR/param.txt > $OUTDIR/tl2_${var_name}_$NAME");
    system ("$EXEDIR/eigenbench-swisstm $OUTDIR/param.txt > $OUTDIR/sws_${var_name}_$NAME");

    #------------------------------------------
    # Run Sequential Execution
    #------------------------------------------
    system ("echo #------------------------ >> $OUTDIR/param.txt");
    system ("echo # Single Thread Execution >> $OUTDIR/param.txt");
    system ("echo #------------------------ >> $OUTDIR/param.txt");
    system ("echo N 1 >> $OUTDIR/param.txt");
    system ("echo LOOPS $LOOPS >> $OUTDIR/param.txt");

    system ("$EXEDIR/eigenbench-unp -p $OUTDIR/param.txt > $OUTDIR/seq_${var_name}_$NAME");
}

