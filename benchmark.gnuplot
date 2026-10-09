#!/usr/bin/env gnuplot

set terminal pngcairo size 1400,1000 enhanced font "Sans,11"
set output "benchmark.png"

STD = "benchmark_mthread_std.dat"
TCM = "benchmark_mthread_tcmalloc.dat"

maxcmd(f, col) = sprintf("< awk '!/^#/ && NF { if ($%d > m[$1]) m[$1] = $%d } END { for (k in m) print k, m[k] }' %s | sort -n", col, col, f)

set logscale x 2
set xtics (1, 2, 4, 8, 16, 32)
set xrange [0.8:40]
set xlabel "Number of Threads"
set yrange [0:*]
set grid
set key top left

set multiplot layout 2,2 title "Benchmark: (GNU) malloc vs. (gperftools) tcmalloc (fixed amount of malloc/free per thread)"

set title "Wall-Clock Time per Thread (ms): individual threads + mean"
plot STD using ($1*0.94):($4/1e6) with points pt 7 ps 0.6 lc 1 notitle, \
     TCM using ($1*1.06):($4/1e6) with points pt 7 ps 0.6 lc 2 notitle, \
     STD using 1:($4/1e6) smooth unique with lines lw 2 lc 1 title "(GNU) malloc", \
     TCM using 1:($4/1e6) smooth unique with lines lw 2 lc 2 title "(gperftools) tcmalloc"

set title "Wall-Clock Time (ms)"
plot maxcmd(STD, 4) using 1:($2/1e6) with linespoints lw 2 lc 1 title "(GNU) malloc", \
     maxcmd(TCM, 4) using 1:($2/1e6) with linespoints lw 2 lc 2 title "(gperftools) tcmalloc"

set title "Instructions / Cycle per Thread (mean)"
set key bottom left
plot STD using 1:($2/$3) smooth unique with linespoints lw 2 lc 1 title "(GNU) malloc", \
     TCM using 1:($2/$3) smooth unique with linespoints lw 2 lc 2 title "(gperftools) tcmalloc"

set title "Effective Clock = cycles / (Wall-Clock) time (GHz, mean)"
set key bottom left
plot STD using 1:($3/$4) smooth unique with linespoints lw 2 lc 1 title "(GNU) malloc", \
     TCM using 1:($3/$4) smooth unique with linespoints lw 2 lc 2 title "(gperftools) tcmalloc"

unset multiplot
