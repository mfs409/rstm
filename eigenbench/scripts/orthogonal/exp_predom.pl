#!/usr/bin/perl

#----------------------------------------
# Read Default Parameters
#----------------------------------------
do "./default.pl";



#----------------------------------
# Setup output directory
#----------------------------------
$OUTDIR = "output_predom";
if (!(-d $OUTDIR)) {
    mkdir $OUTDIR or die $!;
}


#------------------------------------
# Create Input file and Execute
#------------------------------------
$var_name = "PREDOM";
@var_vals = (10,   20,  50, 100, 200, 500, 1000, 2000); #  R3o
@Kos =      ( 1,    1,   2,   1,   1,   1,    1,    1);

$A2 = 16*1024;
$A3 = 16*1024;

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
    $R3o = $var_vals[$j];
    $Ko = $Kos[$j];
    print PARAMFILE "A2 $A2 \n";
    print PARAMFILE "A3 $A3 \n";
    print PARAMFILE "R3o $R3o \n";
    print PARAMFILE "Ko $Ko \n";
    print PARAMFILE "LOOPS $L\n";

    close(PARAMFILE);

    $NAME = sprintf("%04d", (100 / (100 + $R3o / $Ko))*1000); 
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

