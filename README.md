# monitoring_win
g++ server_win_fixed2.cpp -o server.exe -lws2_32

g++ client_win_fixed1_1.cpp -o client.exe -lws2_32 -lgdiplus -lole32 -lnetapi32 -liphlpapi -mwindows
