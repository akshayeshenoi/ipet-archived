#ifndef MIXER_H
#define MIXER_H

#include <string>
#include <sstream>
#include <vector>


static const int UNIT_VECTOR_LENGTH = 40; // number of quantums in a unit vector
static const int NUM_VECTORS_TO_REQUEST = 30; // vectors to request in one shot
static const int MAX_VECTORS_LEN_BYTES = 1 << 16; // 64K
static const int VECTOR_WINDOW_DURATION_MS = 4000; // 4 second vectors
static const int VECTOR_QUANTUM_DURATION_MS = 100; // .1 second quantums
static const int MAX_PACKETS_PER_QUANTUM = 20;
static const int MIN_CT_PACKET_SIZE = 10;
static const int MAX_CT_PACKET_SIZE = 40; // bytes
// cover traffic generator tick duration
static const int CT_TICK_DURATION_MS = VECTOR_QUANTUM_DURATION_MS / MAX_PACKETS_PER_QUANTUM;


/** 
 * As the mixer receives a packet from the device, it enters into active mode.
 * We load a fresh random vector into the mixer and keep generating cover traffic for the next
 * VECTOR_WINDOW_DURATION_MS milliseconds.
 * Then, if there is no new packet, we enter into inactive mode, and random vectors are chosen to
 * fill in the silence period.
 * 
 * Programmatically, in the relay loop, we set `active_traffic` to true whenever a packet arises, and
 * set a timer to expire after VECTOR_WINDOW_DURATION_MS milliseconds in a separate thread. When
 * it does, we set `active_traffic` to false, and wait for the next packet, repeating the process.
 * In the cover traffic generator (before every tick; innermost loop), we first check the status
 * of the boolean `active_traffic`.
 * If `active_traffic` is false, we simply keep ticking and load new vectors as they are exhausted.
 * If `active_traffic` is true, we enter into `nonstop` mode, load a fresh vector, and generate cover
 * traffic until it finishes, and then we turn off `nonstop` mode.
 * 
 */
class Device {
    /** Hold current quantum state data like cover traffic packets added, noise added, etc. */
    struct CurrentQuantum {
        // outbound packets
        struct OutboundToAdd {
            int num_packets_ct; // number of cover traffic packets
            int padding_size; // amount of padding
        } to_add_outbound;
        struct OutboundAdded {
            int num_packets_ct; // number of cover traffic packets (we may not need this)
            int padding_size; // amount of padding
        } added_outbound;

        // inbound packets
        struct InboundToAdd {
            int num_packets_ct; // number of cover traffic packets
            int padding_size; // amount of padding
        } to_add_inbound;
        struct InboundAdded {
            int num_packets_ct; // number of cover traffic packets (we may not need this)
            int padding_size; // amount of padding
        } added_inbound;
    };

    public:
        std::string deviceid;
        // points to current n-second vector fetched from host
        struct Device::CurrentQuantum current_quantum;
        // checks if we are in active mode, i.e. device just started transmitting packets
        // first param asks function to set
        // if true, second param specifies the value
        bool is_device_active(bool set, bool value);

    private:
        // holds all vectors, concatenated
        std::vector<std::string> vectors;
        // points to the nth vector (must be multiplied by VECTOR_LENGTH)
        int vector_index = -1;
        // corresponding quantum index
        int quantum_index;
        bool active_traffic;
        int internal_fd;
        int external_fd;

    public:
        // ctor
        Device(
            std::string deviceid,
            int internal_fd,
            int external_fd
        );
        // relay thread that relays traffic to the server, and sets active_traffic to true
        // when it should.
        void relay_thread_loop();
        // sets active_traffic and turns off automatically after VECTOR_WINDOW_DURATION_MS milliseconds
        void active_timer();
        // fetch device vectors from host, return false if failed
        bool get_device_vectors(int num_vectors);
        std::vector<int> get_next_vector_quantum();
        void load_next_vector();
        void cover_traffic_gen();
};


#endif // MIXER_H
