#!/usr/bin/env gnuplot
set terminal postscript eps monochrome 'Helvetica' 20
set style data linespoints
set ylabel 'Abort Rate' font 'Helvetica,20'
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
    'tree1.Cohorts3.csv'           u 2:($3) pt 4 lw 1.5 t 'Cohorts3',   \
    'tree1.CTokenTurbo.csv'               u 2:($3) pt 3 lw 1.5 t 'CTokenTurbo',   \
    'tree1.TML.csv'          u 2:($3) pt 6 lw 1.5 t 'TML',   \
    'tree1.OrecEager.csv'            u 2:($3) pt 7 lw 1.5 t 'OrecEager', \
    'tree1.Pipeline.csv'              u 2:($3) pt 5 lw 1.5 t 'Pipeline', \
    'tree1.Fastlane1.csv'             u 2:($3) pt 2 lw 1.5 t 'Fastlane1', \
    'tree1.NOrec.csv'                   u 2:($3) pt 13 lw 1.5 t 'NOrec', \
    'tree1.PipelineTurbo.csv'         u 2:($3) pt 12 lw 1.5 t 'PipelineTurbo', \
    'tree1.Fastlane2.csv'                   u 2:($3) pt 9 lw 1.5 t 'Fastlane2'
set output 'tree2.eps'
plot \
    'tree2.Cohorts3.csv'              u 2:($3) pt 4 lw 1.5 t 'Cohorts3',   \
    'tree2.CTokenTurbo.csv'             u 2:($3) pt 3 lw 1.5 t 'CTokenTurbo',   \
    'tree2.TML.csv'                   u 2:($3) pt 6 lw 1.5 t 'TML',   \
    'tree2.OrecEager.csv'                 u 2:($3) pt 7 lw 1.5 t 'OrecEager', \
    'tree2.Pipeline.csv'                   u 2:($3) pt 5 lw 1.5 t 'Pipeline', \
    'tree2.Fastlane1.csv'               u 2:($3) pt 2 lw 1.5 t 'Fastlane1', \
    'tree2.NOrec.csv'               u 2:($3) pt 13 lw 1.5 t 'NOrec', \
    'tree2.PipelineTurbo.csv'               u 2:($3) pt 12 lw 1.5 t 'PipelineTurbo', \
    'tree2.Fastlane2.csv'                   u 2:($3) pt 9 lw 1.5 t 'Fastlane2'
    
set output 'tree3.eps'
plot \
    'tree3.Cohorts3.csv'              u 2:($3) pt 4 lw 1.5 t 'Cohorts3',   \
    'tree3.CTokenTurbo.csv'             u 2:($3) pt 3 lw 1.5 t 'CTokenTurbo',   \
    'tree3.TML.csv'                   u 2:($3) pt 6 lw 1.5 t 'TML',   \
    'tree3.OrecEager.csv'                 u 2:($3) pt 7 lw 1.5 t 'OrecEager',\
    'tree3.Pipeline.csv'                   u 2:($3) pt 5 lw 1.5 t 'Pipeline',\
    'tree3.Fastlane1.csv'               u 2:($3) pt 2 lw 1.5 t 'Fastlane1',\
    'tree3.NOrec.csv'               u 2:($3) pt 13 lw 1.5 t 'NOrec',\
    'tree3.PipelineTurbo.csv'               u 2:($3) pt 12 lw 1.5 t 'PipelineTurbo',\
    'tree3.Fastlane2.csv'                   u 2:($3) pt 9 lw 1.5 t 'Fastlane2'
    
