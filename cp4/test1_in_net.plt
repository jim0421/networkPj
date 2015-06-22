set xlabel "Time (msecs since first packet in flow)"
set ylabel "Packets in Network"
set terminal png
set output "test1_in_net.png"
plot "test1_in_net.dat" using 2:3 title 'flow 1'
