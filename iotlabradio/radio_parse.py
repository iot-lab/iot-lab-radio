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
""" Radio logs parser """

from __future__ import print_function
import argparse
import os
from iotlabradio.helpers import read_config, read_json, save_parsing_logs
from iotlabradio.helpers import parser_frame, get_unused_rows


def dir_path(path):
    """ Check if logs directory exists """
    if os.path.isdir(path):
        return path
    raise argparse.ArgumentTypeError('{} is not a valid path'.format(path))


def opts_parser():
    """ Argument parser object """
    parser = argparse.ArgumentParser()
    parser.add_argument('-p', '--log-path', type=dir_path,
                        required=True, help="Radio logs path")
    return parser


def _parse_pkt(log, log_name, nb_packet, log_row, logs):
    for pkt in log[log_name]:
        pkt_num = pkt['pkt_num']
        if pkt_num < nb_packet:
            if log_name == 'recv':
                # log['node_id] = tx_node (extract from packet)
                row = log_row + (int(log['node_id']), pkt_num, )
                columns = (pkt['rssi'], pkt['lqi'])
            else:
                row = log_row + (pkt_num,)
                columns = (pkt['pkt_send'])
            logs[log_name].loc[row] = columns
        else:
            print('Packet error: {}'.format(row))


def parser_logs_node(log_path, nb_packet, log_row, logs):
    """ Parse logs node """
    node_logs = read_json(log_path)
    for log in node_logs['logs']:
        if 'recv' in log:
            # log['node_id] = tx_node (extract from packet)
            _parse_pkt(log, 'recv', nb_packet, log_row, logs)
            row = log_row + (int(log['node_id']),)
            logs['error'].loc[row] = (log['nb_generic_error'],
                                      log['nb_magic_error'],
                                      log['nb_crc_error'],
                                      log['nb_control_error'])
        else:
            _parse_pkt(log, 'send', nb_packet, log_row, logs)


def init_parser(config):
    """ Initialize parser data structure (eg. pandas MultiIndex) """
    # use only int values -> speed up the parsing !!!
    nodes = [int(node.rsplit('-', 1)[1]) for node in config['nodes']]
    #                                       rssi  lqi
    #channel power rx_node tx_node pkt_num
    #  11     -3     11      14      0        0    0
    #                                1        0    0
    recv = parser_frame([config['channel'], config['power'], nodes,
                         nodes, range(config['nb_packet'])],
                        ['channel', 'power', 'rx_node', 'tx_node', 'pkt_num'],
                        ['rssi', 'lqi'],
                        len(config['channel'])*len(config['power'])
                        *len(nodes)*len(nodes)*config['nb_packet'])
    # remove lines where rx_node=tx_node
    recv = recv[~recv.index.isin(get_unused_rows(config['channel'],
                                                 config['power'],
                                                 nodes,
                                                 range(config['nb_packet'])))]
    #                               pkt_send
    #channel power tx_node pkt_num
    #  11     -3     11       0        0
    #                         1        0
    send = parser_frame([config['channel'], config['power'], nodes,
                         range(config['nb_packet'])],
                        ['channel', 'power', 'tx_node', 'pkt_num'],
                        ['pkt_send'],
                        len(config['channel'])*len(config['power'])
                        *len(nodes)*config['nb_packet'])
    #                               generic  magic_number  crc   control
    #channel power rx_node tx_node
    #  11     -3     11      14       0             0      0        0
    #                        18       0             0      0        0
    error = parser_frame([config['channel'], config['power'],
                          nodes, nodes],
                         ['channel', 'power', 'rx_node', 'tx_node'],
                         ['generic', 'magic_number', 'crc', 'control'],
                         len(config['channel'])*len(config['power'])
                         *len(nodes)*len(nodes))
    # remove lines where rx_node=tx_node
    error = error[~error.index.isin(get_unused_rows(config['channel'],
                                                    config['power'],
                                                    nodes))]
    return {'recv': recv, 'send': send, 'error': error}


def parser_logs(opts):
    """ Parse radio logs """
    config = read_config(opts.log_path)
    logs = init_parser(config)
    for channel in config['channel']:
        for power in config['power']:
            for node in config['nodes']:
                log_path = os.path.join(opts.log_path, str(channel),
                                        str(power),
                                        '{}.json'.format(node))
                log_row = (channel, power, int(node.rsplit('-', 1)[1]))
                msg = "Parsing logs: channel={}, power={}, node={}"
                print(msg.format(log_row[0], log_row[1], log_row[2]))
                parser_logs_node(log_path, config['nb_packet'], log_row,
                                 logs)
    return logs


def main():
    """ Main parser """
    parser = opts_parser()
    opts = parser.parse_args()
    logs = parser_logs(opts)
    save_parsing_logs(opts.log_path, logs)


if __name__ == "__main__":
    main()
