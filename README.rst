IoT-LAB radio characterization tool
===================================

IoT-LAB radio tool provide a basic setup to carry out the radio characterization of a set of IoT-LAB nodes.

License
-------

IoT-LAB radio tool, including all examples, code snippets and attached documentation is covered by the CeCILL v2.1 free
software licence.


Radio characterization 
----------------------

All the nodes of an experiment are in receive mode (RX) at the start. Then each node, in turn, will send broadcast 802.15.4
raw packets with a time delay. Each node receiving the packets will record the good reception information with
radio Received Signal Strength Indication (RSSI) and Link Quality indicator(LQI) values. This packet sending step will be performed
for a given list of radio channels and transmission power.

We can summarize as follows:

::

    for every channel:
        for every power:
            for every node:
                send_packets(nb_packets, packet_size, delay)
                collect_all_receiver_infos
                clear_all_receiver_infos


To perform the different step on the nodes (send packets, collect, clear) we use the serial port on which we send commands. Thanks to the
serial aggregator tool which allows us to aggregate all serial ports of nodes very easily. We also manage error detection when receiving 
packets (eg. bad crc & magic number, ...)


Radio characterization firmware
-------------------------------

It is based on `RIOT OS <https://riot-os.org/>`_ and used the `GNRC network stack <https://riot-os.org/api/group__net__gnrc.html>`_ to send 802.15.4 packets.
You can find the source code `here <https://github.com/iot-lab/iot-lab-radio/blob/master/iotlabradio/riot/radio-characterization/main.c>`_.

When a node sends packets the firmware builds each packet with a fixed header of 16 bytes as follows:

::

    0       7 8     15 16    23 24    31
    +--------+--------+--------+--------+
    | magic number (*)|   CRC value     |
    +--------+--------+--------+--------+
    |  len(node id)   |   node id       |
    +--------+--------+--------+--------+
    |  packet number  |   txpower       |
    +--------+--------+--------+--------+
    |  channel        |   packet size   |
    +--------+--------+--------+--------+
    \                                   \
    /        Each bytes is filled       /  0 or more bytes 
    \         with packet number        \
    +-----------------------------------+

    (*) = 0xADCE

This firmware is originally based on the work of Cedric Adjih:

- `Firmware <https://github.com/adjih/openlab/tree/radio-exp/devel/radio_test>`_ source code
- `Paper <https://www.researchgate.net/publication/304285486_Lessons_Learned_from_Large-scale_Dense_IEEE802154_Connectivity_Traces>`_ published at IEEE CASE 2015 conference


Launch a Setup
--------------

The first step is to launch an experiment on IoT-LAB testbed and flash the firmware on nodes. The following example shows you how to reserve nodes
on Grenoble site that are automatically allocated by the scheduler. If you want specific nodes or topologies you will have to adapt it.
Another important point is that we use the serial aggregator tool to collect the data, so we need to run the radio setup on the IoT-LAB SSH server site.

::

    $ ssh <login>@grenoble.iot-lab.info
    <login>@grenoble:~$ source /opt/riot.source (eg. compilation toolchain)
    <login>@grenoble:~$ git clone --recursive https://github.com/iot-lab/iot-lab-radio
    <login>@grenoble:~$ cd iot-lab-radio
    <login>@grenoble:~$ make -C iotlabradio/riot/radio-characterization (eg. BOARD=iotlab-m3 in the Makefile)
    <login>@grenoble:~$ cp iotlabradio/riot/radio-characterization/bin/iotlab-m3/radio-characterization.elf .
    <login>@grenoble:~$ iotlab-experiment submit -d 60 -l 10,archi=m3:at86rf231+site=grenoble,radio-characterization.elf
    <login>@grenoble:~$ iotlab-experiment wait
    <login>@grenoble:~$ ./iotlab-radio

This setup is launched by default with following parameters:

- all channels: [11..26]
- all txpower: [-17, -12, -9, -7, -5, -4, -3, -2, -1, 0, 0.7, 1.3, 1.8, 2.3, 2.8, 3]
- number of packets: 100
- packet size: 50 bytes
- delay: 1 ms

Of course you can modify this configuration with iotlab-radio command and choose another board when you compile the firmware.

Radio log data
--------------

At the end of each setup we save log files as follows:

::

    logs/<exp_id>/%Y%m%d-%H%M%S/<channel>/<txpower>/<board>-<id>.json

Thus if you launch a setup with m3-1 and m3-2 on the Grenoble site for channel=[11, 12] and txpower=[-4, -3] you will obtain:

::

    .../11/-4/m3-1.json
    .../11/-4/m3-2.json
    .../11/-3/m3-1.json
    .../11/-3/m3-2.json
    .../12/-4/m3-1.json
    .../12/-4/m3-2.json
    ...

In each JSON file you can find a list of all packets sended/received by a node during the radio characterization.

For example when one node send packets we use this log format:

::

    {"nb_error": 0, "node_id": "126", "power": -17, "channel": 11,  "nb_pkt": 100,
     "send": [{"pkt_num": 0, "pkt_res": 1}, {"pkt_num": 1, "pkt_res": 1}, ...]}

- **nb_error**: number of send packets failure
- **node_id**: sender node id
- **power**: transmission power used to send packets
- **channel**: channel used to send packets
- **nb_pkt**: number of packets sent
- **send**:
    - **pkt_num**: packet number
    - **pkt_res**: sending result (1=Success|-1=Failure)

For one node which received the packets we use this log format:

::

    {"nb_magic_error": 0, "nb_crc_error": 0, "nb_error": 0, "nb_pkt": 67, "node_id": "112", "power": -17, "channel": 11,
    "recv": [{"lqi": 255, "pkt_num": 0, "rssi": -91}, { "lqi": 244, "pkt_num": 1, "rssi": -91}, ...]}

- **nb_magic_error**: number of magic number detection errors (eg. packet dropped)
- **nb_crc_error**: number of CRC errors (eg. packet data corruption)
- **nb_error**: number of errors
    - packet payload size < 16 bytes (eg. packet dropped)
    - packet payload size != packet size (*)
    - detection of changes in the values of sender node id, channel, power, packet size.
- **node_id**: sender node id (*)
- **power**: transmission power used to send packets (*)
- **channel**: channel used to send packets (*)
- **nb_pkt**: number of packets received
- **send**:
    - **pkt_num**: packet number (*) or error number (**)
    - **rssi**: Received Signal Strength Indication (RSSI)
    - **lqi**: Link quality indicator (LQI)

(*) extract from packet data received

(**) Code error:

- CRC error = 65345
- packet payload size != packet size = 65346
- sender node id change = 65347
- packet size change = 65348
- channel change = 65349
- power change = 65350


Parse radio log data
---------------------

TODO
