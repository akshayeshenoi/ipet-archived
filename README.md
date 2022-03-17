# iPET: Privacy Enhancing Traffic Perturbations for IoT Communications

iPET is a privacy enhancing traffic perturbation technique that counters ML-based fingerprinting attacks. iPET uses adversarial deep learning, specifically, Generative Adversarial Networks (GANs), to generate these perturbations. Unlike conventional GANs, a key idea of iPET is to deliberately introduce stochasticity in the model.

Our paper describes the workings of iPET in detail. [Link to paper](https://doi.org/10.36227/techrxiv.19235742.v1).

We make our source code available in this repository.

iPET has three components:  
1) **ipet-pert-train**: This directory contains the source code to train the generators that used by the victim. *kanav to add details*
2) **ipet-pert-gen**: Once the generators are trained (i.e., model files saved), the user calls `predict.py` to produce perturbation vector files that instruct the gateway to add appropriate dummy packets.
3) **ipet-pert-add**: A sample gateway simulator that receives packets from a device and adds cover traffic as instructed by the perturbation vector.

## Data required
Network traces are required to train the iPet generators.

`pcap` packet capture files must be used to generate corresponding `csv` files that contain the specific fields (stated below). They must be placed in the `ipet_pert_train/IoT_Traces` directory. Note that the rows in the `csv` file correspond to packet metadata for a single device communicating with one or more servers on the internet. If the `pcap` file contains traffic from multiple devices, it must must be decomposed for each device into separate `csv` files. The file may follow a naming convention `[device_name].csv`.

The reqiured fields are:
 - `frame.number`	
 - `frame.time_relative`	
 - `ip.src`	
 - `ip.dst`	
 - `ip.proto`	
 - `tcp.len`	
 - `tcp.stream`	
 - `udp.length`	
 - `eth.src`	
 - `eth.dst`	
 - `transport.len`

A sample of the network traces in the expected format has been shared as a tarball in the releases tab.

## Running
### Training Generators

#### Configuration
To allow a user to customise their iPet instance, we expect them to specify the following variables in `constants.py`:
- `total_time`: The total observation time for the time series, in seconds.
- `omega` : The duration of a discrete time-slot in the time series, in seconds.
- `device_name` : Name list of the devices in the network. For e.g. `['device_A','device_B,'device_C']`
- `max_packets_per_omega` : Maximum number of dummy packets allowed to be added in a discrete time-slot 
- `max_payload_per_omega` : Maximum additional payload bytes to be added in a discrete time-slot
- `training_stages` : Number of stages you want to train iPet for

#### Generating Fetaure Vectors
The raw data is converted to numpy feature vectors for the model to training on by running the script:
```sh
$ cd ipet_pert_train
$ python feature_generatiom.py 
```
#### Training Basic Discriminator
We first need to train the Discriminator model that would be used by the iPET model to train the generators, using the following script: 
```sh
$ python train_D0.py 
```
#### Training iPET generators
Finally, the iPET generators can be trained using the following script: 
```sh
$ python iPet_Training.py 
```

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
