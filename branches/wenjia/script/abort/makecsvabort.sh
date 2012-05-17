#/bin/bash

benches=( "" tree1 tree2 tree3)

for i in `seq 1 3`
do
    bench=${benches[$i]}
    
    for alg in 'Cohorts3' 'CTokenTurbo' 'TML' 'NOrec' 'OrecEager' 'PipelineTurbo'
    do
        filename="$bench"."$alg".csv

        if [ -e $filename ]
        then
            echo $filename already exists... remove it
            rm $filename
        fi
                
        echo Generating file "$filename"...

        for thread in 1 2 3 4 5 6 7 8 10 12
        do
            echo -n "$filename", "$thread", "" >> $filename
            
            ratios=0
            ratioaverage=0
            for iter in `seq 1 $thread`
            do
                rw=`cat NONE."$alg"."$bench"."$thread".1.32.edat | grep Aborts | cut -d" " -f5 | cut -d";" -f1|head -$iter|tail -1`
                ro=`cat NONE."$alg"."$bench"."$thread".1.32.edat | grep Aborts | cut -d" " -f8 | cut -d";" -f1|head -$iter|tail -1`
                ab=`cat NONE."$alg"."$bench"."$thread".1.32.edat | grep Aborts | cut -d" " -f10 | cut -d";" -f1|head -$iter|tail -1`

                total=`echo $rw+$ro+$ab | bc -l`
                ratio=`echo $ab/$total | bc -l`
                ratios=`echo $ratios+$ratio |bc -l`
            done
            
            ratioaverage=`echo $ratios/$thread |bc -l`
            #echo $ratioaverage
            line=$ratioaverage"\n"
            printf "$line" >>$filename

        done
                
    done
done




