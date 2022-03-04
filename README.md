# iPET: Privacy Enhancing Traffic Perturbations for IoT Communications

iPET is a privacy enhancing traffic perturbation technique that counters ML-based fingerprinting attacks. iPET uses adversarial deep learning, specifically, Generative Adversarial Networks (GANs), to generate these perturbations. Unlike conventional GANs, a key idea of iPET is to deliberately introduce stochasticity in the model.

Our paper describes the workings of iPET in detail. [Link to paper](https://doi.org/10.36227/techrxiv.19235742.v1).

We make our source code available in this repository.

iPET has three components:  
1) **ipet-pert-train**: This directory contains the source code to train the generators that used by the victim. *kanav to add details*
2) **ipet-pert-gen**: Once the generators are trained (i.e., model files saved), the user calls `predict.py` to produce perturbation vector files that instruct the gateway to add appropriate dummy packets.
3) **ipet-pert-add**: A sample gateway simulator that receives packets from a device and adds cover traffic as instructed by the perturbation vector.

## Data required
## Running
### Training Generators

### Producing Perturbations
The trained models will be saved on the disk for each a) device, b) generator version and c) chain. The `predict.py` script loads the saved models for the specific combination of parameters requested by the user.  

The user must ensure that the requirements in `requirements.txt` are installed.
```sh
$ cd ipet-pert-gen
$ python3 -m venv ./venv
$ source venv/bin/activate
$ pip install -r requirements.txt
```

To run the script:
```sh
$ python python predict.py <num_samples> <dev_id> <chain> <generation> > output.csv
```
- `num_samples`: Total number of perturbations vectors required.
- `dev_id`: The id of the device. In our example, we use human friendly IDs that correspond to the IDs used in the training phase.
- `chain`: The training phase may involve creation of multiple chains. This paramater allows us to select a specific one.
- `generation`: The generator stage (i.e. G_0, G_1..). 

The output csv can be used by the gateway to add perturbations. Each row corresponds to timeslot omega and states the quantities that must be added as cover traffic. Specifically, they are: 
`# outgoing packets | total size of outgoing packets | # incoming packets | total size of incoming packets`

### Adding Perturbations
To test the perturbations on real traffic, we provide a simple simulator.  

First, the `pcap-replay` component (heavily derived from [here](https://github.com/shadow/shadow-plugin-extras/tree/master/pcap_replay)), ingests a real pcap file containing IoT packet and _replays_ packets from a particular device to the gateway.

The gateway runs the `mixer` component which accepts traffic from the device, adds cover traffic and forwards it to server.

The `simple-server` component simply accepts the traffic from the gateway and outputs traffic metadata that can be analysed further.

All the components have been configured to run on the [Shadow](https://github.com/shadow/shadow) simulator. Make sure that you have the Shadow installed. Alternatively, ensure that you comment out shadow related cmake statements if you intend to use the executables independently. 

#### pcap-replay
Ensure that you have glib and libpcap installed. The program also makes use of epoll. To build this component:
```sh
$ cd ipet-pert-add/pcap-replay
$ mkdir build
$ cd build
$ cmake .. && make
```

To run:
```sh
./pcap-replay {dev_name} {mode} {gateway_id} 80 {client_ip} {net_addr} {net_mask} 99999 {fname}
```

## Contact
For any queries, please feel free to raise issues or contact the authors.