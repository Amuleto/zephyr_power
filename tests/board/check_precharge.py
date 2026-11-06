"""Check generated hardware mapping against the schematic and confirmed board differences."""

import argparse
import pickle
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path, help="bringup image directory, not sysbuild root")
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[3]
    sys.path.insert(0, str(workspace / "gd32_zephyr/scripts/dts/python-devicetree/src"))
    with (args.build_dir / "zephyr/edt.pickle").open("rb") as source:
        edt = pickle.load(source)
    user = edt.get_node("/zephyr,user")
    gpio_b = edt.label2node["gpiob"]
    led = edt.get_node("led0").props["gpios"].val[0]
    assert led.controller is gpio_b and led.data == {"pin": 12, "flags": 0}
    relays = user.props["relay-gpios"].val
    assert all(item.controller is gpio_b and item.data["flags"] == 0 for item in relays)
    assert [item.data["pin"] for item in relays] == [6, 5, 4, 3]
    direction = user.props["rs485-de-gpios"].val[0]
    assert direction.controller is gpio_b and direction.data == {"pin": 8, "flags": 0}
    assert direction.data["pin"] != led.data["pin"], "RS485 direction conflicts with LED"
    inputs = user.props["io-channels"].val
    assert [item.data["input"] for item in inputs] == list(range(9))
    assert all(item.controller is edt.label2node["adc0"] for item in inputs)
    for channel in inputs:
        node = channel.controller.children[f"channel@{channel.data['input']:x}"]
        assert node.props["zephyr,resolution"].val == 12
    for name in ("usart0", "usart2", "uart4"):
        node = edt.label2node[name]
        assert node.status == "okay" and node.props["current-speed"].val == 115200
    assert edt.chosen_nodes["zephyr,console"] is edt.label2node["uart4"]
    assert edt.chosen_nodes["zephyr,shell-uart"] is edt.label2node["uart4"]
    print("PASS: PB12 LED, PB8 direction, four relays, nine ADC inputs and three UARTs")


if __name__ == "__main__":
    main()
