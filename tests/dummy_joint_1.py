from typing import Dict, Any
from multiverse_client_py import MultiverseClient, MultiverseMetaData

class MultiverseConnector(MultiverseClient):
    def __init__(self, port: str, multiverse_meta_data: MultiverseMetaData) -> None:
        super().__init__(port, multiverse_meta_data)

    def loginfo(self, message: str) -> None:
        print(f"INFO: {message}")

    def logwarn(self, message: str) -> None:
        print(f"WARN: {message}")

    def _run(self) -> None:
        self.loginfo("Start running the client.")
        self._connect_and_start()

    def send_and_receive_meta_data(self) -> None:
        self.loginfo("Sending request meta data: " + str(self.request_meta_data))
        self._communicate(True)
        self.loginfo("Received response meta data: " + str(self.response_meta_data))

    def send_and_receive_data(self) -> None:
        self._communicate(False)

import argparse
import time
import numpy
import yaml

class MultiverseInitializer(MultiverseConnector):
    def __init__(self, data_path: str):
        multiverse_meta_data = MultiverseMetaData(
            world_name="world",
            simulation_name="multiverse_initializer",
            length_unit="m",
            angle_unit="rad",
            mass_unit="kg",
            time_unit="s",
            handedness="rhs",
        )
        super().__init__(port="5252", multiverse_meta_data=multiverse_meta_data)
        self.run()

        data = yaml.load(open(data_path, 'r'), Loader=yaml.FullLoader)
        object_names = list(data.keys())

        self.request_meta_data["send"] = {}
        self.request_meta_data["receive"] = {}
        
        for object_name in object_names:
            self.request_meta_data["send"][object_name] = []
            for attribute_name, attribute_data in data[object_name].items():
                self.request_meta_data["send"][object_name].append(attribute_name)
        self.send_and_receive_meta_data()
        
        response_meta_data = self.response_meta_data
        send_data = []
        for object_name, attributes in response_meta_data["send"].items():
            for attribute_name in attributes.keys():
                send_data += data[object_name][attribute_name]

        self.send_data = [0.0] + send_data
        self.send_and_receive_data()
        self.stop()

class Joint1Connector(MultiverseConnector):
    cmd_position: float = 0.0
    cmd_velocity: float = 0.0
    cmd_torque: float = 0.0
    position: float = 0.0
    velocity: float = 0.0
    torque: float = 0.0
    KD: float = 0.0
    KV: float = 0.0
    KI: float = 0.0
    sim_time: float = 0.0
    world_time: float = 0.0
    motor_time: float = 0.0

    def __init__(self) -> None:
        multiverse_meta_data = MultiverseMetaData(
            world_name="world",
            simulation_name="joint_1_simulation",
            length_unit="m",
            angle_unit="rad",
            mass_unit="kg",
            time_unit="s",
            handedness="rhs"
        )
        super().__init__(port="7575", multiverse_meta_data=multiverse_meta_data)

        def reset_callback() -> None:
            self.cmd_position = 0.0
            self.cmd_velocity = 0.0
            self.cmd_torque = 0.0
            self.position = 0.0
            self.velocity = 0.0
            self.torque = 0.0
            self.KD = 0.0
            self.KV = 0.0
            self.KI = 0.0
            self.start_time = time.time()
            self.sim_time = 0.0
            self.motor_time = 0.0
        self.reset_callback = reset_callback

        def get_everything(sample_size: int):
            return [f"Received get_everything with sample size: {sample_size}"]
        self.api_callbacks = {"get_everything": get_everything}

        def get_everything_response(sample_size: int):
            return [f"{[0, 1, 2, 3]}"]
        self.api_callbacks_response = {"get_everything": get_everything_response}

        self.run()
        self.request_meta_data["send"] = {}
        self.request_meta_data["send"]["joint_1"] = [
            "cmd_joint_rvalue",
            "cmd_joint_angular_velocity",
            "cmd_joint_torque",
            "joint_rvalue",
            "joint_angular_velocity",
            "joint_torque",
            "time",
        ]
        self.request_meta_data["receive"] = {}
        self.request_meta_data["receive"]["KD"] = ["scalar"]
        self.request_meta_data["receive"]["KV"] = ["scalar"]
        self.request_meta_data["receive"]["KI"] = ["scalar"]

        self.send_and_receive_meta_data()
        self.start_time = time.time()

    def communicate(self):
        self.sim_time = time.time() - self.start_time
        self.send_data = [
            self.sim_time,
            self.cmd_velocity,
            self.cmd_position,
            self.cmd_torque,
            self.velocity,
            self.position,
            self.torque,
            self.motor_time,
        ]
        self.send_and_receive_data()
        self.world_time = self.receive_data[0]
        self.KD = self.receive_data[1]
        self.KV = self.receive_data[2]
        self.KI = self.receive_data[3]

import os
import threading

if __name__ == "__main__":
    default_data_dir = os.path.dirname(os.path.abspath(__file__))
    default_data_path = os.path.join(default_data_dir, "init.yaml")

    parser = argparse.ArgumentParser(description=f"Dummy data for joint 1 simulation.")

    # Define arguments
    parser.add_argument("--host", type=str, required=False, default="tcp://127.0.0.1", help="Host IP address")
    parser.add_argument("--data_path", type=str, required=False, default=default_data_path, help="Path to load the initial data")

    # Parse arguments
    args = parser.parse_args()

    MultiverseClient._host = args.host
    MultiverseInitializer(args.data_path)

    joint_1_connector = Joint1Connector()

    stop_event = threading.Event()
    def run_joint1_thread():
        while not stop_event.is_set():
            time_now = time.time()
            joint_1_connector.communicate()
            time_diff = time.time() - time_now
            if time_diff < 0.0001:
                time.sleep(0.0001 - time_diff)
    joint_1_thread = threading.Thread(target=run_joint1_thread)
    joint_1_thread.start()

    try:
        last_time = time.time()
        while joint_1_connector.world_time >= 0.0:
            joint_1_connector.motor_time = time.time() - joint_1_connector.start_time
            joint_1_connector.cmd_position = 0.1 * numpy.sin(joint_1_connector.sim_time)
            joint_1_connector.cmd_velocity = 0.2 * numpy.sin(joint_1_connector.sim_time)
            joint_1_connector.cmd_torque = 0.3 * numpy.sin(joint_1_connector.sim_time)
            joint_1_connector.position = 0.4 * numpy.sin(joint_1_connector.sim_time)
            joint_1_connector.velocity = 0.5 * numpy.sin(joint_1_connector.sim_time)
            joint_1_connector.torque = 0.6 * numpy.sin(joint_1_connector.sim_time)

            KD = joint_1_connector.KD
            KV = joint_1_connector.KV
            KI = joint_1_connector.KI

            if time.time() - last_time > 0.5:
                print(f"KD: {KD:.2f}, KV: {KV:.2f}, KI: {KI:.2f}")
                last_time = time.time()
            time.sleep(0.001)

    except KeyboardInterrupt:
        print("Interrupted by user.")
    finally:
        joint_1_connector.stop()
        stop_event.set()
        joint_1_thread.join()
        print("Joint 1 simulation stopped.")
