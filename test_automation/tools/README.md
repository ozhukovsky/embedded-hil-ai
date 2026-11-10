# PC bring-up utilities

These scripts are small serial-port bring-up tools, not a full test automation framework. They require Python 3.9+ and `pyserial`:

```powershell
python -m pip install pyserial
```

## Protocol smoke test

`hil_smoke_test.py` checks a selected DUT or Plant serial endpoint. It opens a COM port, sends an empty STATUS_REQUEST, validates the received frame CRC and STATUS payload/role, reports whether BOOT_REPORT was observed (BOOT_REPORT is optional for a late connection), then displays incoming frames such as HEARTBEAT.

BlackPill/DUT over USB CDC:

```powershell
python test_automation/tools/hil_smoke_test.py --role dut --port COM6
```

BluePill/Plant over FT232RL USART1 (115200 baud, 8-N-1):

```powershell
python test_automation/tools/hil_smoke_test.py --role plant --port COM7
```

Use `--status-timeout` and `--listen-seconds` to adjust the response and observation windows. A STATUS response must arrive for the selected role; a missed BOOT_REPORT alone is not a failure. This utility does not reset or flash a board and does not exercise a complete test run.

## Plant bring-up

`plant_bringup.py` sends a Plant configuration, waits for the Plant's acknowledgement/readiness status, and can observe subsequent protocol traffic. It depends on the framing and CRC helpers in `hil_smoke_test.py` and the same serial prerequisites.

```powershell
python test_automation/tools/plant_bringup.py --help
python test_automation/tools/plant_bringup.py --port COM7 --timeout 5 --listen-seconds 10
```

Supported options are `--port` (required), `--baud` (default 115200), `--timeout` (default 5 seconds), and `--listen-seconds` (default 10 seconds). The utility configures and checks the Plant; **it does not send ARM_TEST or START_TEST**. It does not verify DAC analogue voltage or the complete heater interface electrically.

## Host regression tests

From the repository root:

```powershell
python -m unittest discover -s test_automation/tests -v
```

Host tests validate Python utility behavior and protocol handling; they do not establish physical serial communication or peripheral behavior on a target board.
