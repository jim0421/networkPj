
# helper function
def findMaxWinInRange(range_start, range_stop, times_arr, winsize_arr)
	max = 0
	times_arr.each { |t |
		if ( t >= range_start && t <= range_stop) 
			if ( winsize_arr[t] > max )
				max = winsize_arr[t]
			end	
		end
	}
	return max
end 



if( !File.exists?("problem2-peer.txt") )
	puts "FAILURE: could not find file 'problem2-peer.txt'"
	exit 1
end

times = []
winsize = {}
index = 0
low_times = [] # includes timestamp whenever win-size = 1


IO.foreach("problem2-peer.txt") { | l | 
	arr = l.chop.split(" ")
	if ( arr.length != 3 ) 
		puts "FAILURE: Badly formatted line doesn't have 3 entries: '#{l}'"
		exit 1
	end
	time = arr[1].to_i
	size = arr[2].to_i
	times[index] = time
	winsize[time] = size
#	puts "adding value #{time} => #{size}"
	index = index + 1
	if(size == 1) 
		low_times.push(time)
	end
}

num_lows = low_times.length
if (num_lows != 3) 
	puts "FAILURE: Window size should be changed to 1 exactly 3 times"
	puts "Once at the beginning of each GET, and one after the loss"
	puts "You set window to one #{num_lows} times, listed below"
	low_times.each { |v | puts "t = #{v}" }
	exit 1
end

max_get1_before_loss = findMaxWinInRange(low_times[0], low_times[1], times, winsize)
max_get1_after_loss = findMaxWinInRange(low_times[1], low_times[2], times, winsize)
max_get2 = findMaxWinInRange(low_times[2], times[times.length - 1], times, winsize )

if(max_get1_before_loss > 70 || max_get1_before_loss < 50) 
	puts "FAILURE: expected the max window size before the first loss to be in range 50-70."
	puts "Your max window during this period was #{max_get1_before_loss}"
	exit 1
end

if(max_get1_after_loss > 40 || max_get1_after_loss < 30) 
	puts "FAILURE: expected the 1st connection's max window size after the first loss to be in range 30-40. #{low_times[2]}"  
	puts "Your max window during this period was #{max_get1_before_loss} #{max_get1_after_loss} #{max_get2}"
	exit 1
end

if(max_get2 > 70 || max_get2 < 60) 
	puts "FAILURE: expected the 2nd connection's max window size to be in range 25-30."  
	puts "Your max window during this period was #{max_get2}"
	exit 1
end

puts "Basic sanity checks on window sizes look good"
exit 10

