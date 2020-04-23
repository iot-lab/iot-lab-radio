# -*- coding:utf-8 -*-

# This file is a part of IoT-LAB cli-tools
# Copyright (C) 2015 INRIA (Contact: admin@iot-lab.info)
# Contributor(s) : see AUTHORS file
#
# This software is governed by the CeCILL license under French law
# and abiding by the rules of distribution of free software.  You can  use,
# modify and/ or redistribute the software under the terms of the CeCILL
# license as circulated by CEA, CNRS and INRIA at the following URL
# http://www.cecill.info.
#
# As a counterpart to the access to the source code and  rights to copy,
# modify and redistribute granted by the license, users are provided only
# with a limited warranty  and the software's author,  the holder of the
# economic rights,  and the successive licensors  have only  limited
# liability.
#
# The fact that you are presently reading this means that you have had
# knowledge of the CeCILL license and that you accept its terms.
""" Radio serial aggregation """

from __future__ import print_function
import argparse
import re
import time
import json
import iotlabaggregator.common
from iotlabaggregator.serial import SerialAggregator
import iotlabcli
from iotlabcli.auth import get_user_credentials
from iotlabcli.rest import Api
from iotlabradio.helpers import log_dict, save_radio_logs


CHANNEL_LIST = list(range(11, 27))
POWER_LIST = [-17, -12, -9, -7, -5, -4, -3, -2, -1, 0, 1, 2, 3]
LOGS = log_dict(3, int)
WAIT_CMD = {'show':[], 'clear':[], 'channel':[], 'power':[]}


def opts_parser():
    """ Argument parser object """
    parser = argparse.ArgumentParser()
    iotlabaggregator.common.add_nodes_selection_parser(parser)
    parser.add_argument('--packet-size', type=int,
                        choices=range(16, 117),
                        metavar='{16,...,116}',
                        default=50, help="Packet size (bytes)")
    parser.add_argument('--nb-packet', type=int, default=100,
                        choices=range(1, 1025),
                        metavar='{1,...,1024}',
                        help="Number of packets sent by a node")
    parser.add_argument("--txpower", nargs='+', type=int,
                        default=POWER_LIST, choices=POWER_LIST,
                        help="Radio transmission power list")
    parser.add_argument("--channel", nargs='+', type=int,
                        default=CHANNEL_LIST, choices=CHANNEL_LIST,
                        help="Radio channel list")
    parser.add_argument("--delay", type=int, default=1,
                        choices=range(1, 101),
                        metavar='{1,...,100}',
                        help="Delay between two transmission of packets (ms)")
    parser.add_argument("--timeout", type=int, default=5,
                        help="Wait timeout for logger commands")
    return parser


def wait_for_cmd(cmd, timeout, step=1, nodes=None):
    """ Wait for a command on serial aggregator """
    start_time = time.time()
    while not time.time() > start_time + timeout:
        time.sleep(step)
        if nodes and len(WAIT_CMD[cmd]) == len(nodes):
            WAIT_CMD[cmd] = []
            return
    if nodes:
        # missing nodes in WAIT_CMD
        timeout_nodes = set(nodes).difference(WAIT_CMD[cmd])
        if timeout_nodes:
            print('Timeout {} is reached: {}'.format(cmd,
                                                     timeout_nodes))
        WAIT_CMD[cmd] = []


def handle_log(identifier, line):
    """ Print one line prefixed by id in format: """
    node = re.sub('^node-', '', identifier)  # remove node- from a8 nodes
    node_id = int(node.rsplit('-', 1)[1])
    try:
        json_line = json.loads(line)
        if 'ack' in json_line:
            WAIT_CMD[json_line['ack']].append(node)
        else:
            channel = json_line['channel']
            power = json_line['power']
            # show = recv empty list when node sent packets
            # or when node don't receive packets
            if (('recv' in json_line and json_line['recv'])
                    or ('send' in json_line)):
                log = LOGS[channel][power][node_id]
                # first log for this node
                if log == 0:
                    LOGS[channel][power][node_id] = {'logs': [json_line],
                                                     'node': node}
                else:
                    log['logs'].append(json_line)
                    LOGS[channel][power][node_id] = log
    #pylint: disable=unused-variable
    except ValueError as ex:
        # debug/log lines
        print("%s: %s" % (node, line))


def run_cmd_manager(opts, aggregator, nodes):
    """ Run command manager """
    for channel in opts.channel:
        aggregator.broadcast('channel {}\n'.format(channel))
        wait_for_cmd('channel', opts.timeout, nodes=nodes)
        for txpower in opts.txpower:
            aggregator.broadcast('power {}\n'.format(txpower))
            wait_for_cmd('power', opts.timeout, nodes=nodes)
            for node in nodes:
                node_id = node.rsplit('-', 1)[1]
                cmd = "send {} {} {} {}\n".format(node_id,
                                                  opts.packet_size,
                                                  opts.nb_packet,
                                                  opts.delay)
                aggregator.send_nodes([node], cmd)
                # wait for packets reception: timeout assumption with delay
                # because only a set of nodes will receive packets
                timeout = opts.delay*0.001*opts.nb_packet + opts.timeout
                wait_for_cmd('send', timeout)
                aggregator.broadcast('show\n')
                wait_for_cmd('show', opts.timeout, nodes=nodes)
                aggregator.broadcast('clear\n')
                wait_for_cmd('clear', opts.timeout, nodes=nodes)


def run_radio_logger(exp_id, opts, nodes):
    """ Run radio logger """
    with SerialAggregator(nodes, line_handler=handle_log) as aggregator:
        while True:
            try:
                run_cmd_manager(opts, aggregator, nodes)
                print("Saving radio logs ...")
                save_radio_logs(exp_id, opts, nodes, LOGS)
                break
            except KeyboardInterrupt:
                print("Interrupted by user ...")
                break


def main():
    """ Launch serial aggregator and aggregate serial links
    of all nodes.
    """
    parser = opts_parser()
    opts = parser.parse_args()
    api = Api(*get_user_credentials())
    opts.with_a8 = True
    try:
        nodes = SerialAggregator.select_nodes(opts)
    except RuntimeError as err:
        print(err)
        exit(1)
    if opts.experiment_id:
        exp_id = opts.experiment_id
    else:
        exp_id = iotlabcli.get_current_experiment(api)
    print("Running radio logger ...")
    run_radio_logger(exp_id, opts, nodes)


if __name__ == "__main__":
    main()
