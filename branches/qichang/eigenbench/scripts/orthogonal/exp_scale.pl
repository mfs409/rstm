#!/usr/bin/perl

#----------------------------------------
# Read Default Parameters
#----------------------------------------
do "./default.pl";

#----------------------------------
# Setup output directory
#----------------------------------
$OUTDIR = "output_scale";
if (!(-d $OUTDIR)) {
    mkdir $OUTDIR or die $!;
}
$LOOPS = $LOOPS/ 8;


#------------------------------------
# Create Input file and Execute
#------------------------------------
$var_name = "N";
@var_vals = (1,2,4,8);
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

    $N = $var_vals[$j];
    $L = $LOOPS  ;
    print PARAMFILE "$var_name $var_vals[$j] \n";
    print PARAMFILE "LOOPS $L\n";

    close(PARAMFILE);

    print("running $var_name: $N\n");

    #------------------------------------------
    # Run Parallel Execution
    #------------------------------------------
    system ("$EXEDIR/eigenbench-unp $OUTDIR/param.txt > $OUTDIR/unp_${var_name}_$N");
    system ("$EXEDIR/eigenbench-tl2 $OUTDIR/param.txt > $OUTDIR/tl2_${var_name}_$N");
    system ("$EXEDIR/eigenbench-swisstm $OUTDIR/param.txt > $OUTDIR/sws_${var_name}_$N");

    #------------------------------------------
    # Run Sequential Execution
    #------------------------------------------
    $L = $L * $N;
    system ("echo #------------------------ >> $OUTDIR/param.txt");
    system ("echo # Single Thread Execution >> $OUTDIR/param.txt");
    system ("echo #------------------------ >> $OUTDIR/param.txt");
    system ("echo N 1 >> $OUTDIR/param.txt");
    system ("echo LOOPS $L >> $OUTDIR/param.txt");

    system ("$EXEDIR/eigenbench-unp -p $OUTDIR/param.txt > $OUTDIR/seq_${var_name}_$N");

}

