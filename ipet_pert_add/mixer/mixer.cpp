/** 
 * This file contains only the core mixing algorithm from the iPET project.
 * This is done so that we can run it inside shadow and analyse the privacy performance.
 * The main advantage is that we get to control time inside shadow and can play large pcap files
 * inside the program relatively quickly.
 */

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string>
#include <arpa/inet.h>
#include <chrono>
#include <vector>
#include <thread>
#include <mutex>
#include <fstream>
#include <netdb.h>

#include "mixer.hpp"

std::mutex mtx;

Device::Device(std::string deviceid, int internal_fd, int external_fd) {
    this->deviceid = deviceid;
    this->internal_fd = internal_fd;
    this->external_fd = external_fd;
}

void Device::load_next_vector(){
    vector_index += 1;
    quantum_index = vector_index * UNIT_VECTOR_LENGTH;
}

std::vector<int> Device::get_next_vector_quantum(){
    std::vector<int> next_component_arr;
    next_component_arr.reserve(6);

    // check if we have reached the end of the vector
    if(quantum_index >= vectors.size()){
        return next_component_arr;
    }

    // first get next line our vector array
    std::string next_component_raw = vectors[quantum_index++];

    // then split by commas
    std::string delim = ",";

    auto start = 0U;
    auto end = next_component_raw.find(delim);
    while (end != std::string::npos)
    {
        std::string value = next_component_raw.substr(start, end - start);
        
        next_component_arr.push_back(std::stoi(value));

        start = end + delim.length();
        end = next_component_raw.find(delim, start);
    }

    // last element
    std::string value = next_component_raw.substr(start, end);
    next_component_arr.push_back(std::stoi(value));

    return next_component_arr;
}

bool Device::get_device_vectors(int num_vectors) {
    // load vectors from file
    std::string fname = "vectors/" + deviceid + ".csv";
    std::ifstream is(fname, std::ifstream::binary);
    if (is) {
        // get length of file:
        is.seekg (0, is.end);
        int length = is.tellg();
        is.seekg (0, is.beg);

        char *buffer = new char[length];

        // read data as a block:
        is.read (buffer, length);
        is.close();

        // split by newline 
        std::string buff_str(buffer, length);
        std::string delim = "\n";

        auto start = 0U;
        auto end = buff_str.find(delim);
        while (end != std::string::npos)
        {
            std::string line = buff_str.substr(start, end - start);

            vectors.push_back(line);

            start = end + delim.length();
            end = buff_str.find(delim, start);
        }
        // last element can be ignored (since it's empty)

        printf("Loaded %d vectors from file %s\n", (int)vectors.size(), fname.c_str());

        delete[] buffer;
        return true;
    }
    else {
        printf("File %s not found!", fname.c_str());
        return false;
    }

}

bool Device::is_device_active(bool set, bool value) {
    // acquire mtx lock
    std::lock_guard<std::mutex> lock(mtx);

    if (set) {
        active_traffic = value;
        return true;
    } else {
        return active_traffic;
    }
}

int make_send_decision(int num_pkts, int to_add_padding, int added_padding) {
    // probability of packet to be added in a tick
    // todo add both inbound and outbound here!
    double packet_per_tick_probability = double(num_pkts) / double(MAX_PACKETS_PER_QUANTUM);

    // send packet based on probability
    if ((double(std::rand()) / double(RAND_MAX)) > packet_per_tick_probability) {
        return 0;
    }

    // check if we have exhausted ct bandwidth for this quantum
    // or if there is anything to add
    if (added_padding >= to_add_padding || to_add_padding == 0) {
        return 0;
    }

    // expected value of packet size
    if (num_pkts == 0) {
        // this is a special case, set it to 4
        num_pkts = 4;
    }
    int expected_val = to_add_padding / num_pkts + 1;
    int ct_packet_size = std::rand() % (2 * expected_val);

    // commenting for now
    // // if we are really low on remainder
    // if (2 * MIN_CT_PACKET_SIZE > to_add_padding - added_padding) {
    //     ct_packet_size = MIN_CT_PACKET_SIZE;
    // }
    // else {
    //     // there is still room
    //     // get random packet size in our range
    //     ct_packet_size = MIN_CT_PACKET_SIZE + (std::rand() % (MAX_CT_PACKET_SIZE - MIN_CT_PACKET_SIZE));

    //     // check if are overshooting quota
    //     if (ct_packet_size + added_padding >= to_add_padding) {
    //         // if so, select size in the remainder range
    //         ct_packet_size = MIN_CT_PACKET_SIZE + (std::rand() % (MAX_CT_PACKET_SIZE - (to_add_padding - added_padding)));
    //     }
    // }

    return ct_packet_size;
}

void Device::cover_traffic_gen() {
    std::srand(std::time(nullptr));

    // nonstop mode to sync with active traffic
    bool nonstop_mode = false;
    bool nonstop_mode_requested = false;
    Device *device = this;

    while (1) { 
        // vector windows are moved here
        device->load_next_vector();

        // check non stop mode was requested (it is turned on in the innermost loop)
        // start reading this code from that point^ 
        if (nonstop_mode_requested) {
            nonstop_mode = true;

            // reset request
            nonstop_mode_requested = false;
        }

        int vector_window_start_tick = 0;
        while (vector_window_start_tick < VECTOR_WINDOW_DURATION_MS) {
            // check if non_stop was requested inside
            if(nonstop_mode_requested) {
                // we did, load a new fresh vector outside
                break;
            }

            // quantum windows are moved here
            // get the next quantum component
            std::vector<int> vec_quantum = device->get_next_vector_quantum();
            if (vec_quantum.size() == 0) {
                // request for new batch of vectors
                if (!device->get_device_vectors(NUM_VECTORS_TO_REQUEST)){
                    // vector not available! notify server and exit
                    unsigned char packet[4];
                    packet[0] = 'E';
                    packet[1] = 'E';
                    packet[2] = 0;
                    packet[3] = 0;

                    write(external_fd, packet, 4);
                    exit(1);
                }
                continue;
            }

            // reset current quantum
            device->current_quantum.added_inbound.padding_size = 0;
            device->current_quantum.added_outbound.padding_size = 0;

            // number of packets and padding to be added this quantum
            device->current_quantum.to_add_inbound.num_packets_ct = vec_quantum[0];
            device->current_quantum.to_add_inbound.padding_size = vec_quantum[1];
            device->current_quantum.to_add_outbound.num_packets_ct = vec_quantum[2];
            device->current_quantum.to_add_outbound.padding_size = vec_quantum[3];

            int quantum_start_tick = 0;
            while (quantum_start_tick < VECTOR_QUANTUM_DURATION_MS) { // main tick loop
                // first check if active_mode is set and we are not already in nonstop mode
                if (device->is_device_active(false, false) && !nonstop_mode) {
                    // device recently started transmitting, enter nonstop mode
                    nonstop_mode_requested = true;
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(CT_TICK_DURATION_MS));

                // total time passed since the beginning of this quantum
                quantum_start_tick += CT_TICK_DURATION_MS;

                // first calculate for inbound
                int ct_packet_size_ib = make_send_decision(device->current_quantum.to_add_inbound.num_packets_ct,
                                    device->current_quantum.to_add_inbound.padding_size,
                                    device->current_quantum.added_inbound.padding_size);

                if (ct_packet_size_ib) {
                    unsigned char packet[4];
                    // indicate cover traffic to server in the first byte
                    packet[0] = 'C';
                    // indicate if it is inbound (I) in the second byte
                    packet[1] = 'I';
                    // and the packet size in the next two bytes
                    // this is done to avoid the overhead of sending the whole packet
                    // thus the packet payload is just 4 bytes
                    // note endianness
                    for (int i = 0; i < 2; i++) {
                        packet[i+2] = (unsigned char)(ct_packet_size_ib >> (8 * i));
                    }

                    // send packet
                    // printf("Sending packet of size %d\n", ct_packet_size_ib);
                    write(external_fd, packet, 4);
                }

                device->current_quantum.added_inbound.padding_size += ct_packet_size_ib;

                // then calculate for outbound
                int ct_packet_size_ob = make_send_decision(device->current_quantum.to_add_outbound.num_packets_ct,
                    device->current_quantum.to_add_outbound.padding_size,
                    device->current_quantum.added_outbound.padding_size);

                if (ct_packet_size_ob) {
                    unsigned char packet[3];
                    // indicate cover traffic to server in the first byte
                    packet[0] = 'C';
                    // indicate if it is outbound (0) in the second byte
                    packet[1] = 'O';
                    // and the packet size in the next two bytes
                    // this is done to avoid the overhead of sending the whole packet
                    // thus the packet payload is just 4 bytes
                    // note endianness
                    for (int i = 0; i < 2; i++) {
                        packet[i+2] = (unsigned char)(ct_packet_size_ob >> (8 * i));
                    }

                    // send packet
                    // printf("Sending packet of size %d\n", ct_packet_size_ob);
                    write(external_fd, packet, 4);
                }

                device->current_quantum.added_outbound.padding_size += ct_packet_size_ob;
            }
            // total time passed since the beginning of this vector window
            vector_window_start_tick += quantum_start_tick;
        }        

        // reset nonstop mode
        nonstop_mode = false;
    }
}

void Device::active_timer() {
    // set the active flag to true
    is_device_active(true, true);

    std::this_thread::sleep_for(std::chrono::milliseconds(VECTOR_WINDOW_DURATION_MS));

    // set active traffic off
    is_device_active(true, false);
}

void Device::relay_thread_loop() {
    unsigned char buff[4];
    int bytesRecv;

    while (1) {
        // read from internal socket
        bytesRecv = read(internal_fd, buff, 4);
        if (bytesRecv == 0) {
            // connection closed, return
            printf("Peer closed connection \n");
            break;
        }

        // set active mode if not set already
        if (!is_device_active(false, false)) {
            // start timer to turn it off after some time
            std::thread(&Device::active_timer, this).detach();
        }

        // write to the external socket
        write(external_fd, buff, bytesRecv);
    }
}

