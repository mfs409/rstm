#!/bin/bash
# number of trials to run
TRIALS=5   #we can change it to a smaller value if it takes too long

# STM_CONFIG values
ALGS="CGL LLT NOrec TML TMLLazy OrecLazy OrecEager Fastlane1 Fastlane2 FastlaneSwitch CToken CTokenELA CTokenTurbo CTokenTurboELA Cohorts3 CohortsENQ CohortsEN2Q Pipeline PipelineTurbo CTokenQ PTM"

# obj folder
OBJ="obj.lib_gcc_linux_ia32_opt"

# STM_PMU values
PMUS="NONE"
#"PAPI_BR_CN PAPI_BR_INS PAPI_BR_MSP PAPI_BR_NTK PAPI_BR_PRC PAPI_BR_TKN PAPI_BR_UCN PAPI_L1_DCM PAPI_L1_ICA PAPI_L1_ICH PAPI_L1_ICM PAPI_L1_ICR PAPI_L1_LDM PAPI_L1_STM PAPI_L1_TCM PAPI_L2_DCA PAPI_L2_DCH PAPI_L2_DCM PAPI_L2_DCR PAPI_L2_DCW PAPI_L2_ICA PAPI_L2_ICH PAPI_L2_ICM PAPI_L2_ICR PAPI_L2_LDM PAPI_L2_STM PAPI_L2_TCA PAPI_L2_TCH PAPI_L2_TCM PAPI_L2_TCR PAPI_L2_TCW PAPI_L3_DCA PAPI_L3_DCR PAPI_L3_DCW PAPI_L3_ICA PAPI_L3_ICR PAPI_L3_LDM PAPI_L3_TCA PAPI_L3_TCM PAPI_L3_TCR PAPI_L3_TCW PAPI_LD_INS PAPI_LST_INS PAPI_RES_STL PAPI_SR_INS PAPI_TLB_DM PAPI_TLB_IM PAPI_TLB_TL PAPI_TOT_CYC PAPI_TOT_IIS PAPI_TOT_INS"

# thread levels to test
THREADS="1 2 3 4 5 6 7 8 10 12"

# need 64 for non-ByteEager/ByteLazy

########################################
# no changes should be needed below this point

# uname gives either Linux or SunOS
LDP=""
if [ `uname` == "SunOS" ]; then
    echo "Running on Solaris... using libmtmalloc in the LD_PRELOAD"
    LDP="libmtmalloc.so"
elif [ `uname` == "Linux" ]; then
    echo "Running on Linux... using libhoard in the LD_PRELOAD... this won't work for 64-bit code"
    LDP="/home/wenjia/rstm/rstm/stamp-0.9.10/libhoard_32_linux.so"
fi

# Config Options for each benchmark
BAY_CONFIG="-v32 -r4096 -n10 -p40 -i2 -e8 -s1"
GEN_CONFIG="-g16384 -s64 -n16777216"
INT_CONFIG="-a10 -l128 -n262144 -s1"
KLO_CONFIG="-m40 -n40 -t0.00001 -i inputs/random-n65536-d32-c16.txt"
KHI_CONFIG="-m15 -n15 -t0.00001 -i inputs/random-n65536-d32-c16.txt"
LAB_CONFIG="-i inputs/random-x512-y512-z7-n512.txt"
SSC_CONFIG="-s20 -i1.0 -u1.0 -l3 -p3"
VLO_CONFIG="-n2 -q90 -u98 -r1048576 -t4194304"
VHI_CONFIG="-n4 -q60 -u90 -r1048576 -t4194304"
YAD_CONFIG="-a15 -i inputs/ttimeu100000.2"

# now create four arrays... remember that arrays are zero-indexed
# array 1:  our mnemonic for this experiment
names=( "" bay gen int klo khi lab ssc vlo vhi yad )
# array 2:  the name of the 'threads' parameter
tparam=( "" t t t p p t t c c t )
# array 3:  the name of the executable to run
exes=( "" ./bayes/$OBJ/bayes  ./genome/$OBJ/genome  ./intruder/$OBJ/intruder  ./kmeans/$OBJ/kmeans ./kmeans/$OBJ/kmeans  ./labyrinth_ext1/$OBJ/labyrinth  ./ssca2/$OBJ/ssca2  ./vacation/$OBJ/vacation  ./vacation/$OBJ/vacation  ./yada/$OBJ/yada )
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
EXPERIMENTS="2 3 4 5 6 7 8 9"

for bits in 32
  do
  #if [ -d bins$bits ]; then
        # how many trials?
      for iter in `seq $TRIALS`
        do
            # which STM algorithms should we test?
        for mode in $ALGS
          do
                # which thread levels?
          for thr in $THREADS
            do
            for exp in $EXPERIMENTS
              do
                for pmu in $PMUS
                do
                    if [ -e $pmu.$mode.${names[$exp]}.$thr.$iter.$bits.edat ]
                    then
                        echo $pmu.$mode.${names[$exp]}.$thr.$iter.$bits.edat already exists... skipping
                    else
                        echo generating $pmu.$mode.${names[$exp]}.$thr.$iter.$bits.edat `date`
                        STM_PMU=$pmu STM_CONFIG=$mode LD_PRELOAD=$LDP ${exes[$exp]} ${config[$exp]} -${tparam[$exp]}$thr > tmp
                        #STM_PMU=$pmu STM_CONFIG=$mode LD_PRELOAD=$LDP bins$bits/${exes[$exp]} ${config[$exp]} -${tparam[$exp]}$thr > tmp
                        mv tmp  $pmu.$mode.${names[$exp]}.$thr.$iter.$bits.edat
                        echo done.
                    fi
              done
            done
          done
        done
      done
#  else
#     echo bins$bits does not exist... skipping
# fi
done

