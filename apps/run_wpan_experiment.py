#!/usr/bin/env python3
import subprocess
import json
import re
import sys


def run(cmd):
    """Run a shell command and return stdout."""
    print(f"Running: {cmd}")
    out = subprocess.check_output(cmd, shell=True, text=True)
    return out.strip()


# -------------------------------------------------------------------
# 1. Submit experiment
# -------------------------------------------------------------------
submit_out = run("iotlab-experiment submit -n my_little_test -d 20 -l 2,archi=nrf52840dk:multi+site=saclay")

# JSON parse: submit output is { "id": #### }
exp_id = json.loads(submit_out)["id"]
print("Experiment ID:", exp_id)

# -------------------------------------------------------------------
# 2. Wait until experiment is Running
# -------------------------------------------------------------------
run("iotlab-experiment wait")

# -------------------------------------------------------------------
# 3. Get node list in JSON
# -------------------------------------------------------------------
get_out = run("iotlab-experiment get -n")

nodes = json.loads(get_out)["items"]

if len(nodes) != 2:
    print("Expected 2 nodes, got:", len(nodes))
    sys.exit(1)

# -------------------------------------------------------------------
# 4. Extract proper node IDs
#    Example field:
#       "network_address": "nrf52840dk-10.saclay.iot-lab.info"
#    We extract the number after "nrf52840dk-"
# -------------------------------------------------------------------
node_ids = []
for n in nodes:
    addr = n["network_address"]
    # Extract the board ID from the hostname
    m = re.search(r"nrf52840dk-(\d+)", addr)
    if not m:
        print("Cannot parse network_address:", addr)
        sys.exit(1)
    node_ids.append(m.group(1))

print("Nodes identified:", node_ids)

client_id, server_id = node_ids

# -------------------------------------------------------------------
# 5. Flash both nodes automatically
# -------------------------------------------------------------------
flash_client_cmd = (
    f"iotlab-node --flash wpan_serial.elf -l saclay,nrf52840dk,{client_id}"
)
flash_server_cmd = (
    f"iotlab-node --flash echo_server.elf -l saclay,nrf52840dk,{server_id}"
)

print("Flashing wpan_serial device:", flash_client_cmd)
run(flash_client_cmd)

print("Flashing server:", flash_server_cmd)
run(flash_server_cmd)

print("All done!")

# -------------------------------------------------------------------
# 6. Reset nodes
# -------------------------------------------------------------------

reset_nodes_cmd = "iotlab-node --reset"
run(reset_nodes_cmd)