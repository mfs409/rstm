#!/usr/bin/perl

#----------------------------------------
# Read Default Parameters
#----------------------------------------
do "./default.pl";

#----------------------------------
# Setup output directory
#----------------------------------
$OUTDIR = "output_len";
if (!(-d $OUTDIR)) {
    mkdir $OUTDIR or die $!;
}


#------------------------------------
# Create Input file and Execute
#------------------------------------
$var_name = "LEN";
@var_vals = (10, 20, 30, 40, 80, 120, 160, 200, 240, 280, 320, 360, 400, 440, 480, 520);
#@var_vals = (10);
$P = 0.1;
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

    $LEN = $var_vals[$j];
    $W2 = $LEN * $P;
    $R2 = $LEN - $W2;
    if   ($LEN >= 300) {$LOOPS2 = $LOOPS/4;}
    elsif ($LEN >= 80) {$LOOPS2 = $LOOPS;}
    else {$LOOPS2 = $LOOPS * 4;}

    $L = $LOOPS2 / $N;
    print PARAMFILE "R2 $R2 \n";
    print PARAMFILE "W2 $W2 \n";
    print PARAMFILE "LOOPS $L\n";

    close(PARAMFILE);

    print("running $var_name: $LEN");

    #------------------------------------------
    # Run Parallel Execution
    #------------------------------------------
    $NAME = sprintf("%04d", $LEN); 
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
    system ("echo LOOPS $LOOPS2 >> $OUTDIR/param.txt");

    system ("$EXEDIR/eigenbench-unp -p $OUTDIR/param.txt > $OUTDIR/seq_${var_name}_$NAME");
}

