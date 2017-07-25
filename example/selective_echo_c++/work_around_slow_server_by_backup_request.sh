comake2 -P
make -sj8
#SelectiveChannel(timeout=100ms, backup=6ms) [
#  Channel[list://0.0.0.0:8004,0.0.0.0:8005,0.0.0.0:8006]          
#  ParallelChannel[
#    Channel[0.0.0.0:8007]
#    Channel[0.0.0.0:8008]
#    Channel[0.0.0.0:8009]]
#  SelectiveChannel[
#    Channel[list://0.0.0.0:8010(always timeout),0.0.0.0:8011,0.0.0.0:8012]
#    Channel[0.0.0.0:8013]
#    Channel[0.0.0.0:8014(unavailable)]]]
( ./echo_server -server_num 10 -sleep_us "0,0,0,0,0,0,1000000,0" -max_ratio 1 -min_ratio 1 -max_concurrency 10 & ) && ./echo_client -backup_ms 6 -dont_fail; pkill echo_server
