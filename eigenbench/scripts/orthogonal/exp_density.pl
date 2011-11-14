#!/usr/bin/perl

#----------------------------------------
# Read Default Parameters
#----------------------------------------
do "./default.pl";
$LOOPS = $LOOPS/2;



#----------------------------------
# Setup output directory
#----------------------------------
$OUTDIR = "output_density";
if (!(-d $OUTDIR)) {
    mkdir $OUTDIR or die $!;
}


#------------------------------------
# Create Input file and Execute
#------------------------------------
$var_name = "DEN";
@var_vals = (10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0); #  Kii

$A1 = 512;
$A2 = 31*1024;
$A3 = 512;
$R1 = 8;
$R2 = 82;
$W1 = 2;
$W2 = 8;

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
    $R3i = $var_vals[$j];
    $Ki = 10;
    $Ko = 1;
    $TOTAL = $LEN;  # $LEN = 100
    $R3o = $TOTAL - $R3i * ($LEN/$Ki);
    print PARAMFILE "A1 $A1 \n";
    print PARAMFILE "A2 $A2 \n";
    print PARAMFILE "A3 $A3 \n";
    print PARAMFILE "R1 $R1 \n";
    print PARAMFILE "R2 $R2 \n";
    print PARAMFILE "W1 $W1 \n";
    print PARAMFILE "W2 $W2 \n";
    print PARAMFILE "R3o $R3o \n";
    print PARAMFILE "R3i $R3i \n";
    print PARAMFILE "Ki $Ki \n";
    print PARAMFILE "Ko $Ko \n";
    print PARAMFILE "LOOPS $L\n";

    close(PARAMFILE);
    
    $NAME = sprintf("%03d", $R3o); 
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

