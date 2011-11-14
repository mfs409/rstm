#!/usr/bin/perl

#----------------------------------
# Setup output directory
#----------------------------------
$NAME = "ssca2";
$OUTDIR = "output_$NAME";
if (!(-d $OUTDIR)) {
    mkdir $OUTDIR or die $!;
}
$EXEDIR = "../../src";

#------------------------------------
# Create Param file and Execute
#------------------------------------
for($N=1;$N<=8;$N=$N*2)
{
    #------------------------------------------
    # Run Parallel Execution
    #------------------------------------------
    system("cp ./$NAME.param.base $OUTDIR/$NAME.param") ;
    system("echo N $N >> $OUTDIR/$NAME.param"); 


    system ("$EXEDIR/eigenbench-unp -p $OUTDIR/$NAME.param > $OUTDIR/unp_$N");
    system ("$EXEDIR/eigenbench-tl2 -p $OUTDIR/$NAME.param > $OUTDIR/tl2_$N");
    system ("$EXEDIR/eigenbench-swisstm -p $OUTDIR/$NAME.param > $OUTDIR/sws_$N");

}

    # Sequential = unproected single thread
    system("cp $OUTDIR/unp_1 $OUTDIR/seq"); 

