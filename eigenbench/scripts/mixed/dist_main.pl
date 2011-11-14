#--------------------------------------------
# NEED following global variable
# $NAME
# $EXE_DIR
# @FREQ  
# @LEN
# $CONFLICT, $WORKSET, $DIRTY (optional: -1, 0, 1)
#--------------------------------------------

sub generate_distribute {

  #---------------------------------
  # Default Parameters
  #---------------------------------
  @WORKSETS = (64*1024, 2*1024*1024, 8*1024*1024);
  @POLS   =   (0.01, 0.1, 0.5);
  @CONFS  =   (0.1, 0.3, 0.8);

  $TOTAL_EXECUTION = 2097152;
  $MAX_N = 8;
  $A1 = 64*1024;
  $A2_ALL = $WORKSETS[$WORKSET+1];
  $POL = $POLS[$DIRTY+1];  # pollution (% of wr)
  $CF  = $CONFS[$CONFLCIT+1]; # % of acccess to A1
  $A3 = 64*1024;

  #----------------------------------
  # Setup output directory
  #----------------------------------
  $EXEDIR = "../../src";
  $OUTDIR = "./output_$NAME";
  if (!(-d $OUTDIR)) {
    print("creating $OUTDIR\n");
    mkdir $OUTDIR or die $!;
  }

  #----------------------------------
  # Create parameter file
  #----------------------------------
  open (PARAMFILE, ">$OUTDIR/param.txt") or die $!;
  $BIN = @LEN;

  print PARAMFILE "#------------------------\n";
  print PARAMFILE "# Default Parameter Values \n";
  print PARAMFILE "# Pollution: $POL\n";
  print PARAMFILE "#------------------------\n";
  print PARAMFILE "A1 $A1 \n";
  print PARAMFILE "A2 $A2_ALL # Should be overrided \n";
  print PARAMFILE "A3 $A3 \n";
  print PARAMFILE "M  $BIN\n";
  print PARAMFILE "\n";

  for($i=0;$i<$BIN;$i++)
  {
    $COUNT = sprintf("%d", $FREQ[$i] * $TOTAL_EXECUTION);
    $RW = $LEN[$i];
    $LOOPS = 1;

    if ($RW <= 10) {$LOOPS = 8;}
    elsif ($RW < 100) {$LOOPS = 2;}

    $W = sprintf("%d",$RW*$POL);
    if (($W < 1) and ($POL > 0) and ($RW >= 1)) {$W = 1;}
    $R = $RW -$W;

    $W1 =  sprintf("%d",$W*$CF);
    if (($W1 < 1) and ($POL > 0) and ($W >= 1)) {$W1 = 1;}
    $W2 = $W - $W1;

    $R1 =  sprintf("%d",$R*$CF);
    if (($R1 < 1) and ($POL > 0) and ($R >= 1)) {$R1 = 1;}
    $R2 = $R - $R1;

#$RW_A1 = sprintf("%d",$RW * $CF); 
#if (($RW_A1 < 1) and ($RW > 1)) {$RW_A1 = 1;}
#    $W1 = sprintf("%d", $RW_A1 * $POL);
#    if (($W1 < 1) and ($RW_A1 > 1)) {$W1 = 1;}
#    $R1 = $RW_A1 - $W1;
#    $RW_A2 = $RW - $RW_A1;
#    $W2 = sprintf("%d", $RW_A2 * $POL);
#    if (($W2 < 1) and ($RW_A2 > 1)) {$W2 = 1;}
#    $R2 = $RW_A2 - $W2;

    print PARAMFILE "#------------------------\n";
    print PARAMFILE "# SET $d\n";
    print PARAMFILE "# LENGTH $RW\n";
    print PARAMFILE "# FREQUENCY $FREQ[$i]\n";
    print PARAMFILE "#------------------------\n";
    print PARAMFILE "*M     $i $COUNT\n";
    print PARAMFILE "*LOOPS $i $LOOPS\n";
    print PARAMFILE "*R1  $i $R1\n";
    print PARAMFILE "*W1  $i $W1\n";
    print PARAMFILE "*R2  $i $R2\n";
    print PARAMFILE "*W2  $i $W2\n";
    print PARAMFILE "\n";
  }
  print PARAMFILE "#------------------------\n";
  print PARAMFILE "#Overriding Ns\n";
  print PARAMFILE "#------------------------\n";
  close(PARAMFILE);


  for($i=1;$i<=$MAX_N;$i=$i*2)
  {
    $A2 = sprintf("%d", $A2_ALL / $i);
    system ("echo \"#---\" >> $OUTDIR/param.txt");
    system ("echo N $i  >> $OUTDIR/param.txt");
    system ("echo A2 $A2  >> $OUTDIR/param.txt");
    print ("Running $i Threads\n");

    system ("$EXEDIR/eigenbench-unp $OUTDIR/param.txt > $OUTDIR/unp_$i");
    system ("$EXEDIR/eigenbench-tl2 $OUTDIR/param.txt > $OUTDIR/tl2_$i");
    system ("$EXEDIR/eigenbench-swisstm $OUTDIR/param.txt > $OUTDIR/sws_$i");
  }

  system("cp $OUTDIR/unp_1 $OUTDIR/seq");
}

1; # need to end with a true value
