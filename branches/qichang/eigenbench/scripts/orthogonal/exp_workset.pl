#!/usr/bin/perl

#----------------------------------------
# Read Default Parameters
#----------------------------------------
do "./default.pl";

#----------------------------------
# Setup output directory
#----------------------------------
$OUTDIR = "output_workset";
if (!(-d $OUTDIR)) {
    mkdir $OUTDIR or die $!;
}


#------------------------------------
# Create Input file and Execute
#------------------------------------
$var_name = "A2";
@var_vals = (1*1024,2*1024,4*1024,8*1024,16*1024,32*1024,64*1024,128*1024,256*1024,512*1024,1024*1024,2048*1024,
        4*1024*1024, 8*1024*1024, 16*1024*1024);
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

    $A2 = $var_vals[$j];

    if   ($A2 >= (2*1024*1024)) {$LOOPS2 = $LOOPS/8;}
    else {$LOOPS2 = $LOOPS;}

    $L = $LOOPS2 / $N;

    print PARAMFILE "$var_name $var_vals[$j] \n";
    print PARAMFILE "LOOPS $L\n";
    print PARAMFILE "A2 $A2\n";

    close(PARAMFILE);


    #------------------------------------------
    # Run Parallel Execution
    #------------------------------------------
    $A2NAME = sprintf("%08Xh", $A2); 
    print("running $var_name: $A2NAME");
    system ("$EXEDIR/eigenbench-unp $OUTDIR/param.txt > $OUTDIR/unp_${var_name}_$A2NAME");
    system ("$EXEDIR/eigenbench-tl2 $OUTDIR/param.txt > $OUTDIR/tl2_${var_name}_$A2NAME");
    system ("$EXEDIR/eigenbench-swisstm $OUTDIR/param.txt > $OUTDIR/sws_${var_name}_$A2NAME");

    #------------------------------------------
    # Run Sequential Execution
    #------------------------------------------
    system ("echo #------------------------ >> $OUTDIR/param.txt");
    system ("echo # Single Thread Execution >> $OUTDIR/param.txt");
    system ("echo #------------------------ >> $OUTDIR/param.txt");
    system ("echo N 1 >> $OUTDIR/param.txt");
    system ("echo LOOPS $LOOPS2 >> $OUTDIR/param.txt");

    system ("$EXEDIR/eigenbench-unp -p $OUTDIR/param.txt > $OUTDIR/seq_${var_name}_$A2NAME");

}

