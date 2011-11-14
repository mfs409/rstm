#!/usr/bin/perl

#----------------------------------------
# Read Default Parameters
#----------------------------------------
do "./default.pl";

#----------------------------------
# Setup output directory
#----------------------------------
$OUTDIR = "output_pollution";
if (!(-d $OUTDIR)) {
    mkdir $OUTDIR or die $!;
}


#------------------------------------
# Create Input file and Execute
#------------------------------------
$var_name = "POL";
@var_vals = (0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0);
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

    $P = $var_vals[$j];
    $W2 = $LEN * $P;
    $R2 = $LEN - $W2;

    $L = $LOOPS / $N;
    print PARAMFILE "R2 $R2 \n";
    print PARAMFILE "W2 $W2 \n";
    print PARAMFILE "LOOPS $L\n";

    close(PARAMFILE);

    $NAME = sprintf("%03d", $P*100); 
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

