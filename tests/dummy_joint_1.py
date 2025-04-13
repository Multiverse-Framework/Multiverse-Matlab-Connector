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

class Joint1Connector(MultiverseConnector):
    cmd_torque: float = 0.0
    position: float = 0.0
    velocity: float = 0.0
    torque: float = 0.0
    KD: float = 0.0
    KV: float = 0.0
    KI: float = 0.0
    sim_time: float = 0.0
    world_time: float = 0.0

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

        self.run()
        self.request_meta_data["send"] = {}
        self.request_meta_data["send"]["joint_1"] = [
            "cmd_joint_torque",
            "joint_rvalue",
            "joint_angular_velocity",
            "joint_torque",
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
            self.cmd_torque,
            self.velocity,
            self.position,
            self.torque,
        ]
        self.send_and_receive_data()
        self.world_time = self.receive_data[0]
        self.KD = self.receive_data[1]
        self.KV = self.receive_data[2]
        self.KI = self.receive_data[3]

import time
import numpy
import matplotlib.pyplot as plt

if __name__ == "__main__":
    joint_1_connector = Joint1Connector()

    while joint_1_connector.world_time >= 0.0:
        joint_1_connector.cmd_torque = 0.5 * numpy.sin(joint_1_connector.sim_time)
        joint_1_connector.position = 0.5 * numpy.sin(joint_1_connector.sim_time)
        joint_1_connector.velocity = 0.5 * numpy.cos(joint_1_connector.sim_time)
        joint_1_connector.torque = 0.5 * numpy.sin(joint_1_connector.sim_time)

        joint_1_connector.communicate()

        KD = joint_1_connector.KD
        KV = joint_1_connector.KV
        KI = joint_1_connector.KI

        print(f"KD: {KD}, KV: {KV}, KI: {KI}")
        
        time.sleep(0.1)

    joint_1_connector.stop()

