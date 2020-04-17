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
""" Helpers method """

from __future__ import print_function
import os
import time
import json
from collections import defaultdict


def log_dict(nb_dict, type_dict):
    """ Create nested defaultdict """
    if nb_dict == 1:
        return defaultdict(type_dict)
    return defaultdict(lambda: log_dict(nb_dict-1, type_dict))


def get_results(path, logs, res=None):
    """ Loop over nested defaultdict """
    if res is None:
        res = {}
    for key, value in logs.items():
        if isinstance(value, defaultdict):
            get_results(os.path.join(path, str(key)),
                        value, res=res)
        else:
            log_path = os.path.join(path,
                                    '{}.json'.format(value['node']))
            res[log_path] = value
    return res


def store_radio_logs(exp_id, logs):
    """ Store radio logger values """
    log_path = os.path.join("logs", str(exp_id),
                            time.strftime("%Y%m%d-%H%M%S"))
    res = get_results(log_path, logs)
    for path in res:
        print(path)
        # python2 support due to aggregation-tools
        if not os.path.exists(os.path.dirname(path)):
            os.makedirs(os.path.dirname(path))
        with open(path, 'w') as json_file:
            json.dump(res[path], json_file)
