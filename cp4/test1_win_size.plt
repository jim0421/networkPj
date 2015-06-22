set xlabel "Time (msec since flow start)"
set ylabel "Window Size (packets)"
set terminal png
set output "test1_win_size.png"
plot "problem2-peer.txt" using 2:3 title 'flow 1'
