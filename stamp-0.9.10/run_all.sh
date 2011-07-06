#!/bin/bash
# number of trials to run
TRIALS=1   #we can chang it to a smaller value if it takes too long

# STM Algorithms to test
ALGS="NOrec OrecEager"

# thread levels to test
THREADS="1 2 4 8"

########################################
# no changes should be needed below this point

# uname gives either Linux or SunOS
LDP=""
if [ `uname` == "SunOS" ]; then
    echo "Running on Solaris... using libmtmalloc in the LD_PRELOAD"
    LDP="libmtmalloc.so"
elif [ `uname` == "Linux" ]; then
    echo "Running on Linux... using libhoard in the LD_PRELOAD... this won't work for 32-bit code"
    LDP="libhoard.so"
fi

# Config Options for each benchmark
BAY_CONFIG="-v32 -r4096 -n10 -p40 -i2 -e8 -s1"
GEN_CONFIG="-g16384 -s64 -n16777216"
INT_CONFIG="-a10 -l128 -n262144 -s1"
KLO_CONFIG="-m40 -n40 -t0.00001 -i ../stamp_inputs/random-n65536-d32-c16.txt"
KHI_CONFIG="-m15 -n15 -t0.00001 -i ../stamp_inputs/random-n65536-d32-c16.txt"
LAB_CONFIG="-i ../stamp_inputs/random-x512-y512-z7-n512.txt"
SSC_CONFIG="-s20 -i1.0 -u1.0 -l3 -p3"
VLO_CONFIG="-n2 -q90 -u98 -r1048576 -t4194304"
VHI_CONFIG="-n4 -q60 -u90 -r1048576 -t4194304"
YAD_CONFIG="-a15 -i ../stamp_inputs/ttimeu100000.2"

# now create four arrays... remember that arrays are zero-indexed
# array 1:  our mnemonic for this experiment
names=( "" bay gen int klo khi lab ssc vlo vhi yad )
# array 2:  the name of the 'threads' parameter
tparam=( "" t t t p p t t c c t )
# array 3:  the name of the executable to run
exes=( "" bayes genome intruder kmeans kmeans labyrinth ssca2 vacation vacation yada )
# the config strings are tricky, so do them this way
config[1]=$BAY_CONFIG
config[2]=$GEN_CONFIG
config[3]=$INT_CONFIG
config[4]=$KLO_CONFIG
config[5]=$KHI_CONFIG
config[6]=$LAB_CONFIG
config[7]=$SSC_CONFIG
config[8]=$VLO_CONFIG
config[9]=$VHI_CONFIG
config[10]=$YAD_CONFIG

# now specify the number of experiments
EXPERIMENTS=10

for bits in 32 64
  do
  if [ -d bins$bits ]; then
        # how many trials?
      for iter in `seq $TRIALS`
        do
            # which STM algorithms should we test?
        for mode in $ALGS
          do
                # which thread levels?
          for thr in $THREADS
            do
            for exp in `seq $EXPERIMENTS`
              do
              if [ -e $mode.${names[$exp]}.$thr.$iter.$bits.edat ]
                  then
                  echo $mode.${names[$exp]}.$thr.$iter.$bits.edat already exists... skipping
              else
                  echo generating $mode.${names[$exp]}.$thr.$iter.$bits.edat
                  STM_CONFIG=$mode LD_PRELOAD=$LDP bins$bits/${exes[$exp]} ${config[$exp]} -${tparam[$exp]}$thr > tmp
                  mv tmp  $mode.${names[$exp]}.$thr.$iter.$bits.edat
                  echo done.
              fi
            done
          done
        done
      done
  else
      echo bins$bits does not exist... skipping
  fi
done

