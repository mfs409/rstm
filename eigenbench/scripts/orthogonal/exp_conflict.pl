#!/usr/bin/perl

#----------------------------------------
# Read Default Parameters
#----------------------------------------
do "./default.pl";
$LOOPS = (800000*8)/2;



#----------------------------------
# Setup output directory
#----------------------------------
$OUTDIR = "output_conflict";
if (!(-d $OUTDIR)) {
    mkdir $OUTDIR or die $!;
}


#------------------------------------
# Create Input file and Execute
#------------------------------------
$var_name = "A1";
@var_vals = (1024, 1280, 2048, 3182, 4096, 5120, 6144, 7168, 8092, 10140, 12288, 16384, 20480, 24576);

$R1 = 45;
$R2 = 45;
$W1 = 5;
$W2 = 5;

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
    $A1 = $var_vals[$j];
    $TOTAL = 32*1024;
    $A2 = $TOTAL - $A1;

    print PARAMFILE "A1 $A1 \n";
    print PARAMFILE "A2 $A2 \n";
    print PARAMFILE "R1 $R1 \n";
    print PARAMFILE "R2 $R2 \n";
    print PARAMFILE "W1 $W1 \n";
    print PARAMFILE "W2 $W2 \n";
    print PARAMFILE "LOOPS $L\n";

    close(PARAMFILE);
    
    $NAME = sprintf("%05d", $A1); 
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

