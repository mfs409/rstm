#!/usr/bin/env gnuplot
set terminal postscript eps monochrome 'Helvetica' 20
set style data linespoints
set ylabel 'Throughput' font 'Helvetica,20'
set xlabel 'Threads' font 'Helvetica,20'
set pointsize 2
set key top left
set ytic auto
set yrange[0:*]
set xrange[0.8:12.2]
set xtics 1

# glyphs (the 'pt' parameter)
# 1 is plus
# 2 is x
# 3 is star
# 4 is empty square
# 5 is filled square
# 6 is empty circle
# 7 is filled circle
# 8 is empty up triangle
# 9 is filled up triangle
# 10 is empty down triangle
# 11 is full down triangle
# 12 is empty diamond
# 13 is full diamond
# 14 is empty pentagon
# 15 is full pentagon

set output 'tree1.eps'
plot \
    'tree1.CohortsENQ.csv'           u 2:($3) pt 4 lw 1.5 t 'CohortsENQ',   \
    'tree1.CToken.csv'               u 2:($3) pt 3 lw 1.5 t 'CToken',   \
    'tree1.CohortsEN2Q.csv'          u 2:($3) pt 6 lw 1.5 t 'CohortsEN2Q',   \
    'tree1.CTokenELA.csv'            u 2:($3) pt 7 lw 1.5 t 'CTokenELA', \
    'tree1.Cohorts3.csv'              u 2:($3) pt 5 lw 1.5 t 'Cohorts3', \
    'tree1.Fastlane1.csv'             u 2:($3) pt 2 lw 1.5 t 'Fastlane1', \
    'tree1.PTM.csv'                   u 2:($3) pt 13 lw 1.5 t 'PTM', \
    'tree1.PipelineTurbo.csv'         u 2:($3) pt 12 lw 1.5 t 'PipelineTurbo', \
    'tree1.Fastlane2.csv'                   u 2:($3) pt 9 lw 1.5 t 'Fastlane2'
set output 'tree2.eps'
plot \
    'tree2.CohortsENQ.csv'              u 2:($3) pt 4 lw 1.5 t 'CohortsENQ',   \
    'tree2.CToken.csv'             u 2:($3) pt 3 lw 1.5 t 'CToken',   \
    'tree2.CohortsEN2Q.csv'                   u 2:($3) pt 6 lw 1.5 t 'CohortsEN2Q',   \
    'tree2.CTokenELA.csv'                 u 2:($3) pt 7 lw 1.5 t 'CTokenELA', \
    'tree2.Cohorts3.csv'                   u 2:($3) pt 5 lw 1.5 t 'Cohorts3', \
    'tree2.Fastlane1.csv'               u 2:($3) pt 2 lw 1.5 t 'Fastlane1', \
    'tree2.PTM.csv'               u 2:($3) pt 13 lw 1.5 t 'PTM', \
    'tree2.PipelineTurbo.csv'               u 2:($3) pt 12 lw 1.5 t 'PipelineTurbo', \
    'tree2.Fastlane2.csv'                   u 2:($3) pt 9 lw 1.5 t 'Fastlane2'
    
set output 'tree3.eps'
plot \
    'tree3.CohortsENQ.csv'              u 2:($3) pt 4 lw 1.5 t 'CohortsENQ',   \
    'tree3.CToken.csv'             u 2:($3) pt 3 lw 1.5 t 'CToken',   \
    'tree3.CohortsEN2Q.csv'                   u 2:($3) pt 6 lw 1.5 t 'CohortsEN2Q',   \
    'tree3.CTokenELA.csv'                 u 2:($3) pt 7 lw 1.5 t 'CTokenELA',\
    'tree3.Cohorts3.csv'                   u 2:($3) pt 5 lw 1.5 t 'Cohorts3',\
    'tree3.Fastlane1.csv'               u 2:($3) pt 2 lw 1.5 t 'Fastlane1',\
    'tree3.PTM.csv'               u 2:($3) pt 13 lw 1.5 t 'PTM',\
    'tree3.PipelineTurbo.csv'               u 2:($3) pt 12 lw 1.5 t 'PipelineTurbo',\
    'tree3.Fastlane2.csv'                   u 2:($3) pt 9 lw 1.5 t 'Fastlane2'
    
