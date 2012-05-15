#!/usr/bin/env gnuplot
set terminal postscript eps monochrome 'Helvetica' 20
set style data linespoints
set ylabel 'Time (seconds)' font 'Helvetica,20'
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

set output 'gen.eps'
plot \
    'gen.OrecLazy.csv'              u 2:($3) pt 4 lw 1.5 t 'OrecLazy',   \
    'gen.OrecEager.csv'             u 2:($3) pt 3 lw 1.5 t 'OrecEager',   \
    'gen.TML.csv'                   u 2:($3) pt 6 lw 1.5 t 'TML',   \
    'gen.NOrec.csv'                 u 2:($3) pt 7 lw 1.5 t 'NOrec', \
    'gen.LLT.csv'                   u 2:($3) pt 1 lw 1.5 t 'LLT', \
    'gen.TMLLazy.csv'               u 2:($3) pt 2 lw 1.5 t 'TMLLazy', \
    'gen.CGL.csv'                   u 2:($3) pt 9 lw 1.5 t 'CGL'
set output 'int.eps'
plot \
    'int.OrecLazy.csv'              u 2:($3) pt 4 lw 1.5 t 'OrecLazy',   \
    'int.OrecEager.csv'             u 2:($3) pt 3 lw 1.5 t 'OrecEager',   \
    'int.TML.csv'                   u 2:($3) pt 6 lw 1.5 t 'TML',   \
    'int.NOrec.csv'                 u 2:($3) pt 7 lw 1.5 t 'NOrec', \
    'int.LLT.csv'                   u 2:($3) pt 1 lw 1.5 t 'LLT', \
    'int.TMLLazy.csv'               u 2:($3) pt 2 lw 1.5 t 'TMLLazy', \
    'int.CGL.csv'                   u 2:($3) pt 9 lw 1.5 t 'CGL'
    
set output 'khi.eps'
plot \
    'khi.OrecLazy.csv'              u 2:($3) pt 4 lw 1.5 t 'OrecLazy',   \
    'khi.OrecEager.csv'             u 2:($3) pt 3 lw 1.5 t 'OrecEager',   \
    'khi.TML.csv'                   u 2:($3) pt 6 lw 1.5 t 'TML',   \
    'khi.NOrec.csv'                 u 2:($3) pt 7 lw 1.5 t 'NOrec',\
    'khi.LLT.csv'                   u 2:($3) pt 1 lw 1.5 t 'LLT',\
    'khi.TMLLazy.csv'               u 2:($3) pt 2 lw 1.5 t 'TMLLazy',\
    'khi.CGL.csv'                   u 2:($3) pt 9 lw 1.5 t 'CGL'
    
set output 'klo.eps'
plot \
    'klo.OrecLazy.csv'              u 2:($3) pt 4 lw 1.5 t 'OrecLazy',   \
    'klo.OrecEager.csv'             u 2:($3) pt 3 lw 1.5 t 'OrecEager',   \
    'klo.TML.csv'                   u 2:($3) pt 6 lw 1.5 t 'TML',   \
    'klo.NOrec.csv'                 u 2:($3) pt 7 lw 1.5 t 'NOrec',\
    'klo.LLT.csv'                   u 2:($3) pt 1 lw 1.5 t 'LLT',\
    'klo.TMLLazy.csv'               u 2:($3) pt 2 lw 1.5 t 'TMLLazy',\
    'klo.CGL.csv'                   u 2:($3) pt 9 lw 1.5 t 'CGL'
    
set output 'ssc.eps'
plot \
    'ssc.OrecLazy.csv'              u 2:($3) pt 4 lw 1.5 t 'OrecLazy',   \
    'ssc.OrecEager.csv'             u 2:($3) pt 3 lw 1.5 t 'OrecEager',   \
    'ssc.TML.csv'                   u 2:($3) pt 6 lw 1.5 t 'TML',   \
    'ssc.NOrec.csv'                 u 2:($3) pt 7 lw 1.5 t 'NOrec',\
    'ssc.LLT.csv'                   u 2:($3) pt 1 lw 1.5 t 'LLT',\
    'ssc.TMLLazy.csv'               u 2:($3) pt 2 lw 1.5 t 'TMLLazy',\
    'ssc.CGL.csv'                   u 2:($3) pt 9 lw 1.5 t 'CGL'
    
set output 'vhi.eps'
plot \
    'vhi.OrecLazy.csv'              u 2:($3) pt 4 lw 1.5 t 'OrecLazy',   \
    'vhi.OrecEager.csv'             u 2:($3) pt 3 lw 1.5 t 'OrecEager',   \
    'vhi.TML.csv'                   u 2:($3) pt 6 lw 1.5 t 'TML',   \
    'vhi.NOrec.csv'                 u 2:($3) pt 7 lw 1.5 t 'NOrec',\
    'vhi.LLT.csv'                   u 2:($3) pt 1 lw 1.5 t 'LLT',\
    'vhi.TMLLazy.csv'               u 2:($3) pt 2 lw 1.5 t 'TMLLazy',\
    'vhi.CGL.csv'                   u 2:($3) pt 9 lw 1.5 t 'CGL'
    
set output 'vlo.eps'
plot \
    'vlo.OrecLazy.csv'              u 2:($3) pt 4 lw 1.5 t 'OrecLazy',   \
    'vlo.OrecEager.csv'             u 2:($3) pt 3 lw 1.5 t 'OrecEager',   \
    'vlo.TML.csv'                   u 2:($3) pt 6 lw 1.5 t 'TML',   \
    'vlo.NOrec.csv'                 u 2:($3) pt 7 lw 1.5 t 'NOrec',\
    'vlo.LLT.csv'                   u 2:($3) pt 1 lw 1.5 t 'LLT',\
    'vlo.TMLLazy.csv'               u 2:($3) pt 2 lw 1.5 t 'TMLLazy',\
    'vlo.CGL.csv'                   u 2:($3) pt 9 lw 1.5 t 'CGL'
    
set output 'lab.eps'
plot \
    'vlo.OrecLazy.csv'              u 2:($3) pt 4 lw 1.5 t 'OrecLazy',   \
    'vlo.OrecEager.csv'             u 2:($3) pt 3 lw 1.5 t 'OrecEager',   \
    'vlo.TML.csv'                   u 2:($3) pt 6 lw 1.5 t 'TML',   \
    'vlo.NOrec.csv'                 u 2:($3) pt 7 lw 1.5 t 'NOrec',\
    'vlo.LLT.csv'                   u 2:($3) pt 1 lw 1.5 t 'LLT',\
    'vlo.TMLLazy.csv'               u 2:($3) pt 2 lw 1.5 t 'TMLLazy',\
    'vlo.CGL.csv'                   u 2:($3) pt 9 lw 1.5 t 'CGL'
    
