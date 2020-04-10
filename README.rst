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

.. code-block::
    for every channel:
        for every power:
            for every node:
                send_packets(nb_packets, packet_size, delay)
                collect_all_receiver_infos
                clear_all_receiver_infos

To perform the different step on the nodes (send packets, collect, clear) we use the serial port on which we send commands. Thanks to the
serial aggregator tool which allows us to aggregate all serial ports of nodes very easily. We also manage error detection when receiving 
packets (bad crc, bad magic number, ...)


Radio characterization firmware
-------------------------------

It is based on RIOT OS and used the `GNRC network stack <https://riot-os.org/api/group__net__gnrc.html>`_ to send 802.15.4 packets.
You can find the source code in the *iotlabradio/riot/radio-characterization* directory.

The firmware build packet with a header of 16 bytes as follows:

::

    0       7 8     15 16    23 24    31
    +--------+--------+--------+--------+
    |  magic number   |   CRC           |  
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

This firmware is originally based on the work of Cedric Adjih:

- `Firmware link <https://github.com/adjih/openlab/tree/radio-exp/devel/radio_test>`_
- T. Watteyne, C. Adjih and X. Vilajosana, "Lessons Learned from Large-scale Dense IEEE802.15.4 Connectivity Traces", IEEE CASE 2015


Launch a Setup
--------------

The first step is to launch an experiment on IoT-LAB testbed and flash the firmware on nodes. The following example shows you how to reserve nodes
on Grenoble site that are automatically allocated by the scheduler. If you want specific nodes or topologies you will have to adapt it.
Another important point is that we use the serial aggregator tool to collect the data, so we need to run the radio setup on the IoT-LAB SSH server site.

.. code-block:: bash
    $ ssh <login>@grenoble.iot-lab.info
    $ source /opt/riot.source (eg. compilation toolchain) 
    $ git clone --recursive https://github.com/iot-lab/iot-lab-radio
    $ cd iot-lab-radio
    $ make -C iotlabradio/riot/radio-characterization (eg. BOARD=iotlab-m3 in the Makefile)
    $ cp iotlabradio/riot/radio-characterization/bin/iotlab-m3/radio-characterization.elf .
    $ iotlab-experiment submit -d 60 -l 10,archi=m3:at86rf231+site=grenoble,radio-characterization.elf
    $ iotlab-experiment wait
    $ ./iotlab-radio

The setup by default launch a radio characterization with following parameters:

- all channels: [11..26]
- all txpower: [-17, -12, -9, -7, -5, -4, -3, -2, -1, 0, 0.7, 1.3, 1.8, 2.3, 2.8, 3]
- number of packets: 100
- packet size: 50 bytes
- delay: 1 ms

Of course you can modify this configuration with iotlab-radio command.

Radio log data
---------------

TODO

Parse radio log data
---------------------

TODO
