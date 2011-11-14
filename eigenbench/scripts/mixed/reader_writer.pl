#! /usr/bin/perl

  #---------------------------------
  # Parameters
  #---------------------------------
  $TOTAL_EXECUTION = 2097152;
  $MAX_N = 8;
  $A1 = 64*1024;
  $A2_ALL = 2*1024*1024;
  $A3 = 64*1024;
  $CF = 0.2;        # 20% will goes to A1

  #-----------------------------
  # Long Reader, Short Reader, Long Writer, Short Writer
  #-----------------------------
  @NICKS = ("Long Reader", "Short Reader", "Long Writer", "Short Writer");
  @RS   = ( 248,     9,    10,   2);
  @WS   = (   2,     1,   140,   8);
  @FREQ = ( 0.1,   0.6,  0.05, 0.25);

  #----------------------------------
  # Setup output directory
  #----------------------------------
  $NAME = "reader-writer";
  $EXEDIR = "../../src";
  $OUTDIR = "./output_$NAME";
  if (!(-d $OUTDIR)) 
  {
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
  print PARAMFILE "#------------------------\n";
  print PARAMFILE "A1 $A1 \n";
  print PARAMFILE "A2 $A2_ALL # Should be overrided \n";
  print PARAMFILE "A3 $A3 \n";
  print PARAMFILE "M  4\n";
  print PARAMFILE "\n";

  for($i=0;$i<4;$i++)
  {
    $COUNT = sprintf("%d", $FREQ[$i] * $TOTAL_EXECUTION);
    $R = $RS[$i];
    $W = $WS[$i];
    $LOOPS = 1;
    $RW = $R + $W;

    if ($RW <= 10) {$LOOPS = 8;}
    elsif ($RW < 100) {$LOOPS = 2;}

    $W1 =  sprintf("%d",$W*$CF);
    if (($W1 < 1) and ($POL > 0) and ($W > 1)) {$W1 = 1;}
    $W2 = $W - $W1;

    $R1 =  sprintf("%d",$R*$CF);
    if (($R1 < 1) and ($POL > 0) and ($R >= 1)) {$R1 = 1;}
    $R2 = $R - $R1;

    print PARAMFILE "#------------------------\n";
    print PARAMFILE "# $NICKS[$i]\n";
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

